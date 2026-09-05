#define _GNU_SOURCE

#include "bbnet_transport.h"
#include "confluence_codec.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <json-c/json.h>
#include <netinet/in.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#ifndef SO_ORIGINAL_DST
#define SO_ORIGINAL_DST 80
#endif
#ifndef IP_TRANSPARENT
#define IP_TRANSPARENT 19
#endif
#ifndef IP_ORIGDSTADDR
#define IP_ORIGDSTADDR 20
#endif
#ifndef IP_RECVORIGDSTADDR
#define IP_RECVORIGDSTADDR IP_ORIGDSTADDR
#endif

#define BIUBIU_ACC_VERSION "0.10.1"
#define DEFAULT_RUNTIME_FILE "/etc/biubiu-acc/runtime.json"
#define DEFAULT_TCP_PORT 18080
#define DEFAULT_UDP_PORT 18081
#define MAX_RUNTIME_SIZE (4U * 1024U * 1024U)
#define MAX_FRAME_SIZE UINT16_MAX
#define MAX_UDP_FLOWS 32U
#define UDP_FLOW_IDLE_SECONDS 120U
#define BBNET_CONNECT_TIMEOUT_MS 10000
#define BBNET_KEEPALIVE_MS 10000
#define CONFLUENCE_LINK_TIMEOUT_MS 10000

#define BOLT_PROTOCOL_VERSION 3U
#define BOLT_DATA_HEADER_LENGTH 11U
#define BOLT_COMMAND_DATA 0x11U
#define BOLT_COMMAND_CONNECT_REQUEST 0x22U
#define BOLT_COMMAND_CONNECT_RESPONSE 0x23U
#define BOLT_COMMAND_ASSOCIATE_REQUEST 0x24U
#define BOLT_COMMAND_ASSOCIATE_RESPONSE 0x25U
#define BOLT_COMMAND_ERROR 0x27U
#define BOLT_STATUS_SUCCESS 0x22U
#define MAX_BOLT_EXTENSION_SIZE UINT8_MAX
#define BOLT_BIND_REQUEST_TYPE 0U
#define BOLT_BIND_TCP_REQUEST_TYPE 1U
#define BOLT_BIND_RESPONSE_TYPE 2U
#define BOLT_BIND_HEADER_LENGTH 21U
#define BOLT_BIND_PAYLOAD_LENGTH 73U
#define BOLT_BIND_FRAME_LENGTH \
    (BOLT_BIND_HEADER_LENGTH + BOLT_BIND_PAYLOAD_LENGTH)
#define BOLT_BIND_STATUS_OFFSET 9U
#define BOLT_BIND_FIXED_PAYLOAD_LENGTH 9U
#define BOLT_BIND_EPT_REQUEST "ept=1"
#define BOLT_BIND_EPT_KEY_PREFIX "ept_key="
#define MAX_OUTBOUND_ID_LENGTH 127U

struct bytes {
    unsigned char *data;
    size_t len;
};

struct bolt_extension {
    uint8_t type;
    const unsigned char *value;
    size_t length;
};

struct bolt_response {
    uint8_t command;
    uint32_t session_id;
    uint16_t connection_id;
    uint8_t status;
    bool has_status;
    const unsigned char *payload;
    size_t payload_length;
};

struct bolt_bind_response {
    bool has_ept_key;
    uint8_t ept_key;
};

struct bolt_channel {
    char protocol[8];
    char address[64];
    uint16_t port;
    char bip_address[64];
    char channel_token[MAX_OUTBOUND_ID_LENGTH + 1];
    uint32_t session_id;
    int64_t expires_at;
    bool encrypted;
    bool native_bolt;
    int datagram_fd;
    bool bound;
    bool ept_enabled;
    uint8_t ept_key;
    struct bytes ticket;
    struct bytes client_parameter;
    struct bytes server_parameter;
    struct bytes strategy;
    struct bbnet_transport *transport;
};

struct bolt_outbound {
    char id[MAX_OUTBOUND_ID_LENGTH + 1];
    char type[16];
    struct bolt_channel *channels;
    size_t channel_count;
};

struct route_network {
    uint32_t network;
    uint32_t mask;
};

struct route_port {
    uint16_t first;
    uint16_t last;
};

struct bolt_route {
    char outbound_id[MAX_OUTBOUND_ID_LENGTH + 1];
    char primary_outbound_id[MAX_OUTBOUND_ID_LENGTH + 1];
    uint8_t protocol;
    struct route_network *networks;
    size_t network_count;
    struct route_port *ports;
    size_t port_count;
};

struct runtime_config {
    struct bolt_channel tcp;
    struct bolt_channel udp;
    struct bolt_outbound *outbounds;
    size_t outbound_count;
    struct bolt_route *routes;
    size_t route_count;
    char default_outbound_id[MAX_OUTBOUND_ID_LENGTH + 1];
    int64_t expires_at;
    bool has_tcp;
    bool has_udp;
};

struct tcp_worker {
    int client_fd;
    const struct runtime_config *runtime;
};

struct udp_context {
    int listener;
    const struct runtime_config *runtime;
};

struct udp_flow {
    bool active;
    int reply_fd;
    struct sockaddr_in client;
    struct sockaddr_in target;
    const struct bolt_channel *channel;
    time_t last_seen;
};

static volatile sig_atomic_t keep_running = 1;
static pthread_mutex_t tcp_workers_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t tcp_workers_idle = PTHREAD_COND_INITIALIZER;
static unsigned int tcp_worker_count;

static void tcp_worker_acquire(void)
{
    (void)pthread_mutex_lock(&tcp_workers_lock);
    tcp_worker_count++;
    (void)pthread_mutex_unlock(&tcp_workers_lock);
}

static void tcp_worker_release(void)
{
    (void)pthread_mutex_lock(&tcp_workers_lock);
    if (tcp_worker_count > 0)
        tcp_worker_count--;
    if (tcp_worker_count == 0)
        (void)pthread_cond_broadcast(&tcp_workers_idle);
    (void)pthread_mutex_unlock(&tcp_workers_lock);
}

static void wait_for_tcp_workers(void)
{
    (void)pthread_mutex_lock(&tcp_workers_lock);
    while (tcp_worker_count > 0)
        (void)pthread_cond_wait(&tcp_workers_idle, &tcp_workers_lock);
    (void)pthread_mutex_unlock(&tcp_workers_lock);
}

static void write_le16(unsigned char *output, uint16_t value)
{
    output[0] = (unsigned char)value;
    output[1] = (unsigned char)(value >> 8);
}

static void write_le32(unsigned char *output, uint32_t value)
{
    output[0] = (unsigned char)value;
    output[1] = (unsigned char)(value >> 8);
    output[2] = (unsigned char)(value >> 16);
    output[3] = (unsigned char)(value >> 24);
}

static uint16_t read_le16(const unsigned char *input)
{
    return (uint16_t)(input[0] | ((uint16_t)input[1] << 8));
}

static uint32_t read_le32(const unsigned char *input)
{
    return input[0] | ((uint32_t)input[1] << 8) |
           ((uint32_t)input[2] << 16) | ((uint32_t)input[3] << 24);
}

static int bolt_tick_count(uint32_t *output)
{
    struct timespec now;
    uint64_t milliseconds;

    if (!output || clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return -1;
    milliseconds = (uint64_t)now.tv_sec * 1000U +
                   (uint64_t)now.tv_nsec / 1000000U;
    *output = (uint32_t)milliseconds;
    return 0;
}

static void bytes_free(struct bytes *value)
{
    if (!value || !value->data)
        return;
    OPENSSL_cleanse(value->data, value->len);
    free(value->data);
    value->data = NULL;
    value->len = 0;
}

static void channel_free(struct bolt_channel *channel)
{
    if (!channel)
        return;
    if (channel->native_bolt && channel->datagram_fd >= 0)
        close(channel->datagram_fd);
    bbnet_transport_destroy(channel->transport);
    channel->transport = NULL;
    bytes_free(&channel->ticket);
    bytes_free(&channel->client_parameter);
    bytes_free(&channel->server_parameter);
    bytes_free(&channel->strategy);
    memset(channel, 0, sizeof(*channel));
}

static void runtime_free(struct runtime_config *runtime)
{
    size_t index;

    if (!runtime)
        return;
    channel_free(&runtime->tcp);
    channel_free(&runtime->udp);
    for (index = 0; index < runtime->outbound_count; index++) {
        size_t channel_index;

        for (channel_index = 0;
             channel_index < runtime->outbounds[index].channel_count;
             channel_index++)
            channel_free(&runtime->outbounds[index].channels[channel_index]);
        free(runtime->outbounds[index].channels);
    }
    for (index = 0; index < runtime->route_count; index++) {
        free(runtime->routes[index].networks);
        free(runtime->routes[index].ports);
    }
    free(runtime->outbounds);
    free(runtime->routes);
    memset(runtime, 0, sizeof(*runtime));
}

static json_object *object_member(json_object *object, const char *name,
                                  enum json_type type)
{
    json_object *value = NULL;

    if (!object || !json_object_is_type(object, json_type_object) ||
        !json_object_object_get_ex(object, name, &value) ||
        !json_object_is_type(value, type))
        return NULL;
    return value;
}

static const char *string_member(json_object *object, const char *name)
{
    json_object *value = NULL;

    if (!object || !json_object_is_type(object, json_type_object) ||
        !json_object_object_get_ex(object, name, &value) ||
        !json_object_is_type(value, json_type_string))
        return NULL;
    return json_object_get_string(value);
}

static bool boolean_member(json_object *object, const char *name, bool fallback)
{
    json_object *value = object_member(object, name, json_type_boolean);

    return value ? json_object_get_boolean(value) : fallback;
}

static int integer_member(json_object *object, const char *name, int64_t *result)
{
    json_object *value = NULL;
    const char *text;
    char *end = NULL;
    long long parsed;

    if (!object || !json_object_is_type(object, json_type_object) || !result ||
        !json_object_object_get_ex(object, name, &value))
        return -1;
    if (json_object_is_type(value, json_type_int)) {
        *result = json_object_get_int64(value);
        return 0;
    }
    if (!json_object_is_type(value, json_type_string))
        return -1;
    text = json_object_get_string(value);
    if (!text || !text[0] || strlen(text) > 20)
        return -1;
    for (size_t index = 0; text[index]; index++)
        if (text[index] < '0' || text[index] > '9')
            return -1;
    errno = 0;
    parsed = strtoll(text, &end, 10);
    if (errno != 0 || !end || *end)
        return -1;
    *result = (int64_t)parsed;
    return 0;
}

static int parse_expiry_value(json_object *value, int64_t *result)
{
    const char *text;
    unsigned long long parsed;
    char *end = NULL;
    size_t index;

    if (!value || !result)
        return -1;
    if (json_object_is_type(value, json_type_int)) {
        int64_t integer = json_object_get_int64(value);

        if (integer < 0)
            return -1;
        parsed = (unsigned long long)integer;
    } else if (json_object_is_type(value, json_type_string)) {
        text = json_object_get_string(value);
        if (!text || !text[0] || strlen(text) > 20)
            return -1;
        for (index = 0; text[index]; index++)
            if (text[index] < '0' || text[index] > '9')
                return -1;
        errno = 0;
        parsed = strtoull(text, &end, 10);
        if (errno == ERANGE || !end || *end || parsed > (uint64_t)INT64_MAX)
            return -1;
    } else {
        return -1;
    }
    if (parsed > 100000000000ULL)
        parsed /= 1000ULL;
    if (parsed > 4102444800ULL)
        return -1;
    *result = (int64_t)parsed;
    return 0;
}

static int load_optional_expiry(json_object *object, const char *name,
                                int64_t *result)
{
    json_object *value = NULL;

    if (!result)
        return -1;
    *result = 0;
    if (!object || !json_object_is_type(object, json_type_object) ||
        !json_object_object_get_ex(object, name, &value))
        return 1;
    if (parse_expiry_value(value, result) != 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static int read_private_file(const char *path, struct bytes *contents)
{
    struct stat info;
    size_t offset = 0;
    int fd = -1;
    int status = -1;

    memset(contents, 0, sizeof(*contents));
    fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return -1;
    if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) ||
        info.st_uid != geteuid() || (info.st_mode & (S_IRWXG | S_IRWXO)) != 0 ||
        info.st_size <= 0 || (uintmax_t)info.st_size > MAX_RUNTIME_SIZE) {
        errno = EACCES;
        goto out;
    }
    contents->data = malloc((size_t)info.st_size + 1);
    if (!contents->data)
        goto out;
    while (offset < (size_t)info.st_size) {
        ssize_t count = read(fd, contents->data + offset,
                             (size_t)info.st_size - offset);

        if (count < 0) {
            if (errno == EINTR)
                continue;
            goto out;
        }
        if (count == 0)
            goto out;
        offset += (size_t)count;
    }
    contents->len = offset;
    contents->data[offset] = '\0';
    status = 0;

out:
    if (status != 0)
        bytes_free(contents);
    close(fd);
    return status;
}

static int decode_base64(const char *value, struct bytes *output)
{
    size_t length;
    size_t padding = 0;
    int decoded;

    memset(output, 0, sizeof(*output));
    if (!value || !value[0])
        return -1;
    length = strlen(value);
    if (length > INT32_MAX || length % 4 != 0)
        return -1;
    output->data = malloc((length / 4) * 3 + 1);
    if (!output->data)
        return -1;
    decoded = EVP_DecodeBlock(output->data, (const unsigned char *)value,
                              (int)length);
    if (decoded < 0) {
        bytes_free(output);
        return -1;
    }
    if (length && value[length - 1] == '=')
        padding++;
    if (length > 1 && value[length - 2] == '=')
        padding++;
    if ((size_t)decoded < padding) {
        bytes_free(output);
        return -1;
    }
    output->len = (size_t)decoded - padding;
    output->data[output->len] = '\0';
    return 0;
}

static int copy_text_bytes(const char *value, struct bytes *output)
{
    size_t length;

    memset(output, 0, sizeof(*output));
    if (!value)
        return 0;
    length = strlen(value);
    if (length > MAX_BOLT_EXTENSION_SIZE)
        return -1;
    if (!length)
        return 0;
    output->data = malloc(length);
    if (!output->data)
        return -1;
    memcpy(output->data, value, length);
    output->len = length;
    return 0;
}

static int copy_optional_bytes(json_object *object, const char *text_name,
                               const char *base64_name, struct bytes *output)
{
    const char *encoded = string_member(object, base64_name);
    const char *text = string_member(object, text_name);

    if (encoded && encoded[0])
        return decode_base64(encoded, output);
    return copy_text_bytes(text, output);
}

static int load_optional_endpoint(json_object *object, const char *address_name,
                                  char *address, size_t address_size)
{
    const char *value = string_member(object, address_name);

    if (!value || !value[0])
        return 0;
    if (strlen(value) >= address_size ||
        inet_pton(AF_INET, value, &(struct in_addr){0}) != 1)
        return -1;
    snprintf(address, address_size, "%s", value);
    return 0;
}

static int load_channel(json_object *auth, json_object *profile,
                        const char *channel_token,
                        struct bolt_channel *channel)
{
    const char *protocol = string_member(auth, "protocol");
    const char *address = string_member(auth, "channelIp");
    const char *profile_address = string_member(auth, "ip");
    const char *client_parameter;
    const char *server_parameter;
    const char *strategy;
    int64_t port;
    int64_t session_id;
    int64_t expires_at = 0;
    int expiry_status;
    struct in_addr ignored;
    size_t channel_token_length;

    if (!protocol)
        protocol = string_member(auth, "proType");
    if (!address)
        address = profile_address;
    if (!address)
        address = string_member(auth, "channelIp");
    if (!protocol || !address || !channel_token || !channel_token[0] ||
        (channel_token_length = strlen(channel_token)) > MAX_OUTBOUND_ID_LENGTH ||
        channel_token_length >
            BOLT_BIND_PAYLOAD_LENGTH - BOLT_BIND_FIXED_PAYLOAD_LENGTH ||
        inet_pton(AF_INET, address, &ignored) != 1 ||
        integer_member(auth, "port", &port) != 0 || port < 1 || port > 65535 ||
        (integer_member(auth, "sessionId", &session_id) != 0 &&
         integer_member(auth, "dataChannelSessionId", &session_id) != 0) ||
        session_id < 1 || session_id > UINT32_MAX)
        return -1;
    expiry_status = load_optional_expiry(auth, "expiresAt", &expires_at);
    if (expiry_status == 1)
        expiry_status = load_optional_expiry(auth, "expireTime", &expires_at);
    if (expiry_status < 0)
        return -1;
    memset(channel, 0, sizeof(*channel));
    channel->datagram_fd = -1;
    snprintf(channel->protocol, sizeof(channel->protocol), "%s", protocol);
    {
        const char *transport = string_member(profile, "transport");

        if (transport && strcmp(transport, "bolt") && strcmp(transport, "bbnet"))
            return -1;
        channel->native_bolt = transport && !strcmp(transport, "bolt");
    }
    for (char *cursor = channel->protocol; *cursor; cursor++)
        if (*cursor >= 'a' && *cursor <= 'z')
            *cursor = (char)(*cursor - 'a' + 'A');
    snprintf(channel->address, sizeof(channel->address), "%s", address);
    snprintf(channel->channel_token, sizeof(channel->channel_token), "%s",
             channel_token);
    channel->port = (uint16_t)port;
    channel->expires_at = expires_at;
    if (load_optional_endpoint(profile, "bip", channel->bip_address,
                               sizeof(channel->bip_address)) != 0)
        goto fail;
    channel->session_id = (uint32_t)session_id;
    channel->encrypted = boolean_member(profile, "encrypted",
                                        boolean_member(profile, "encryption", false));
    if (copy_optional_bytes(auth, "ticket", "ticketBase64",
                            &channel->ticket) != 0 || !channel->ticket.len ||
        channel->ticket.len >
            BOLT_BIND_PAYLOAD_LENGTH - BOLT_BIND_FIXED_PAYLOAD_LENGTH -
                channel_token_length)
        goto fail;
    client_parameter = string_member(profile, "clientParameter");
    server_parameter = string_member(profile, "serverParameter");
    strategy = string_member(profile, "strategy");
    if (copy_text_bytes(client_parameter, &channel->client_parameter) != 0 ||
        copy_text_bytes(server_parameter, &channel->server_parameter) != 0 ||
        copy_text_bytes(strategy, &channel->strategy) != 0)
        goto fail;
    return 0;

fail:
    channel_free(channel);
    return -1;
}

static void update_runtime_expiry(struct runtime_config *runtime,
                                  const struct bolt_channel *channel)
{
    if (channel->expires_at > 0 &&
        (runtime->expires_at == 0 ||
         channel->expires_at < runtime->expires_at))
        runtime->expires_at = channel->expires_at;
}

static int load_legacy_channels(json_object *root,
                                struct runtime_config *runtime)
{
    json_object *channels = object_member(root, "channels", json_type_array);
    size_t index;

    if (!channels || !json_object_array_length(channels))
        return -1;
    for (index = 0; index < json_object_array_length(channels); index++) {
        json_object *auth = json_object_array_get_idx(channels, index);
        struct bolt_channel *destination;
        const char *protocol = string_member(auth, "protocol");

        if (!protocol)
            continue;
        if (!strcasecmp(protocol, "TCP")) {
            if (runtime->has_tcp)
                continue;
            destination = &runtime->tcp;
        } else if (!strcasecmp(protocol, "UDP")) {
            if (runtime->has_udp)
                continue;
            destination = &runtime->udp;
        } else {
            continue;
        }
        if (load_channel(auth, auth, string_member(auth, "channelToken"),
                         destination) != 0)
            return -1;
        destination->encrypted = boolean_member(auth, "encrypted",
                                                 destination->encrypted);
        update_runtime_expiry(runtime, destination);
        runtime->has_tcp = runtime->has_tcp || !strcasecmp(protocol, "TCP");
        runtime->has_udp = runtime->has_udp || !strcasecmp(protocol, "UDP");
    }
    return runtime->has_tcp || runtime->has_udp ? 0 : -1;
}

static int load_outbounds(json_object *root, struct runtime_config *runtime)
{
    json_object *outbounds = object_member(root, "outbounds", json_type_array);
    size_t index;

    if (!outbounds || !json_object_array_length(outbounds))
        return -1;
    runtime->outbound_count = json_object_array_length(outbounds);
    runtime->outbounds = calloc(runtime->outbound_count,
                                sizeof(*runtime->outbounds));
    if (!runtime->outbounds)
        return -1;
    for (index = 0; index < runtime->outbound_count; index++) {
        json_object *source = json_object_array_get_idx(outbounds, index);
        json_object *channels = object_member(source, "channels", json_type_array);
        struct bolt_outbound *outbound = &runtime->outbounds[index];
        const char *id = string_member(source, "id");
        const char *type = string_member(source, "type");
        size_t channel_index;

        if (!id || !id[0] || strlen(id) > MAX_OUTBOUND_ID_LENGTH ||
            !type || !type[0] || strlen(type) >= sizeof(outbound->type) ||
            !channels || !json_object_array_length(channels))
            return -1;
        snprintf(outbound->id, sizeof(outbound->id), "%s", id);
        snprintf(outbound->type, sizeof(outbound->type), "%s", type);
        outbound->channel_count = json_object_array_length(channels);
        outbound->channels = calloc(outbound->channel_count,
                                    sizeof(*outbound->channels));
        if (!outbound->channels)
            return -1;
        for (channel_index = 0; channel_index < outbound->channel_count;
             channel_index++) {
            json_object *channel = json_object_array_get_idx(channels,
                                                              channel_index);
            const char *protocol = string_member(channel, "protocol");

            if (load_channel(channel, channel, string_member(root, "signalSessionId"),
                             &outbound->channels[channel_index]) != 0)
                return -1;
            update_runtime_expiry(runtime,
                                  &outbound->channels[channel_index]);
            if (protocol && !strcasecmp(protocol, "TCP"))
                runtime->has_tcp = true;
            else if (protocol && !strcasecmp(protocol, "UDP"))
                runtime->has_udp = true;
        }
    }
    return runtime->has_tcp || runtime->has_udp ? 0 : -1;
}

static int parse_route_network(const char *text, struct route_network *network)
{
    char address[INET_ADDRSTRLEN];
    const char *slash;
    char *end = NULL;
    unsigned long prefix = 32;
    struct in_addr parsed;
    size_t length;
    uint32_t mask;

    if (!text || !network || !text[0])
        return -1;
    slash = strchr(text, '/');
    length = slash ? (size_t)(slash - text) : strlen(text);
    if (!length || length >= sizeof(address))
        return -1;
    memcpy(address, text, length);
    address[length] = '\0';
    if (inet_pton(AF_INET, address, &parsed) != 1)
        return -1;
    if (slash) {
        errno = 0;
        prefix = strtoul(slash + 1, &end, 10);
        if (errno == ERANGE || !end || *end || prefix > 32)
            return -1;
    }
    mask = prefix == 0 ? 0 : UINT32_MAX << (32U - prefix);
    network->mask = htonl(mask);
    network->network = parsed.s_addr & network->mask;
    return 0;
}

static int parse_route_port(const char *text, struct route_port *port)
{
    char input[32];
    char *separator;
    char *end = NULL;
    unsigned long first;
    unsigned long last;

    if (!text || !port || !text[0] || strlen(text) >= sizeof(input))
        return -1;
    snprintf(input, sizeof(input), "%s", text);
    separator = strchr(input, '-');
    if (separator)
        *separator++ = '\0';
    errno = 0;
    first = strtoul(input, &end, 10);
    if (errno == ERANGE || !end || *end || first > 65535)
        return -1;
    if (separator) {
        errno = 0;
        last = strtoul(separator, &end, 10);
        if (errno == ERANGE || !end || *end || last > 65535)
            return -1;
    } else {
        last = first;
    }
    if (first > last) {
        unsigned long temporary = first;

        first = last;
        last = temporary;
    }
    port->first = (uint16_t)first;
    port->last = (uint16_t)last;
    return 0;
}

static int load_routes(json_object *root, struct runtime_config *runtime)
{
    json_object *routes = object_member(root, "routes", json_type_array);
    size_t index;

    if (!routes)
        return -1;
    runtime->route_count = json_object_array_length(routes);
    if (!runtime->route_count)
        return 0;
    runtime->routes = calloc(runtime->route_count, sizeof(*runtime->routes));
    if (!runtime->routes)
        return -1;
    for (index = 0; index < runtime->route_count; index++) {
        json_object *source = json_object_array_get_idx(routes, index);
        json_object *networks = object_member(source, "cidrs", json_type_array);
        json_object *ports = object_member(source, "ports", json_type_array);
        struct bolt_route *route = &runtime->routes[index];
        const char *outbound_id = string_member(source, "outboundId");
        const char *primary_id = string_member(source, "primaryOutboundId");
        int64_t protocol;
        size_t item_index;

        if (!outbound_id || !outbound_id[0] ||
            strlen(outbound_id) > MAX_OUTBOUND_ID_LENGTH ||
            !primary_id || !primary_id[0] ||
            strlen(primary_id) > MAX_OUTBOUND_ID_LENGTH ||
            integer_member(source, "protocol", &protocol) != 0 ||
            (protocol != 0 && protocol != IPPROTO_TCP &&
             protocol != IPPROTO_UDP) || !networks)
            return -1;
        snprintf(route->outbound_id, sizeof(route->outbound_id), "%s",
                 outbound_id);
        snprintf(route->primary_outbound_id,
                 sizeof(route->primary_outbound_id), "%s", primary_id);
        route->protocol = (uint8_t)protocol;
        route->network_count = json_object_array_length(networks);
        if (!route->network_count)
            return -1;
        route->networks = calloc(route->network_count,
                                  sizeof(*route->networks));
        if (!route->networks)
            return -1;
        for (item_index = 0; item_index < route->network_count; item_index++) {
            json_object *value = json_object_array_get_idx(networks, item_index);

            if (!json_object_is_type(value, json_type_string) ||
                parse_route_network(json_object_get_string(value),
                                    &route->networks[item_index]) != 0)
                return -1;
        }
        if (ports) {
            route->port_count = json_object_array_length(ports);
            route->ports = calloc(route->port_count, sizeof(*route->ports));
            if (route->port_count && !route->ports)
                return -1;
            for (item_index = 0; item_index < route->port_count; item_index++) {
                json_object *value = json_object_array_get_idx(ports, item_index);

                if (!json_object_is_type(value, json_type_string) ||
                    parse_route_port(json_object_get_string(value),
                                     &route->ports[item_index]) != 0)
                    return -1;
            }
        }
    }
    return 0;
}

static int load_runtime(const char *path, struct runtime_config *runtime)
{
    struct bytes contents = {0};
    json_object *root = NULL;
    json_object *schema_version = NULL;
    const char *signal_session_id;
    const char *cursor;
    int64_t schema;
    int status = -1;

    memset(runtime, 0, sizeof(*runtime));
    if (read_private_file(path, &contents) != 0)
        return -1;
    root = json_tokener_parse((const char *)contents.data);
    signal_session_id = string_member(root, "signalSessionId");
    if (!root || !json_object_is_type(root, json_type_object) ||
        !json_object_object_get_ex(root, "schemaVersion", &schema_version) ||
        !json_object_is_type(schema_version, json_type_int) ||
        ((schema = json_object_get_int64(schema_version)) != 1 && schema != 2) ||
        !signal_session_id ||
        !signal_session_id[0] || strlen(signal_session_id) > 255)
        goto invalid;
    for (cursor = signal_session_id; *cursor; cursor++) {
        if ((unsigned char)*cursor <= 0x20U || (unsigned char)*cursor >= 0x7fU)
            goto invalid;
    }
    if (schema == 1) {
        if (load_legacy_channels(root, runtime) != 0)
            goto invalid;
    } else {
        const char *default_outbound_id = string_member(root,
                                                         "defaultOutboundId");

        if (!default_outbound_id || !default_outbound_id[0] ||
            strlen(default_outbound_id) > MAX_OUTBOUND_ID_LENGTH ||
            load_outbounds(root, runtime) != 0 ||
            load_routes(root, runtime) != 0)
            goto invalid;
        snprintf(runtime->default_outbound_id,
                 sizeof(runtime->default_outbound_id), "%s",
                 default_outbound_id);
    }
    if (!runtime->has_tcp && !runtime->has_udp)
        goto invalid;
    status = 0;
    goto out;

invalid:
    errno = EINVAL;

out:
    if (status != 0)
        runtime_free(runtime);
    json_object_put(root);
    bytes_free(&contents);
    return status;
}

static bool runtime_is_expired(const struct runtime_config *runtime, time_t now)
{
    return runtime && runtime->expires_at > 0 && now != (time_t)-1 &&
           (int64_t)now >= runtime->expires_at;
}

static int bolt_encode_request(uint8_t command, uint32_t session_id,
                               const struct bolt_extension *extensions,
                               size_t extension_count, struct bytes *output)
{
    size_t header_length = 10;
    size_t cursor;
    size_t index;

    if (!output ||
        (command != BOLT_COMMAND_CONNECT_REQUEST &&
         command != BOLT_COMMAND_ASSOCIATE_REQUEST) ||
        extension_count > UINT8_MAX || (extension_count && !extensions))
        return -1;
    memset(output, 0, sizeof(*output));
    for (index = 0; index < extension_count; index++) {
        if (extensions[index].length > UINT8_MAX ||
            (extensions[index].length && !extensions[index].value) ||
            header_length + 2U + extensions[index].length > UINT8_MAX)
            return -1;
        header_length += 2U + extensions[index].length;
    }
    output->data = malloc(header_length);
    if (!output->data)
        return -1;
    output->len = header_length;
    output->data[0] = BOLT_PROTOCOL_VERSION;
    output->data[1] = (unsigned char)header_length;
    write_le16(output->data + 2, (uint16_t)header_length);
    output->data[4] = command;
    write_le32(output->data + 5, session_id);
    output->data[9] = (unsigned char)extension_count;
    cursor = 10;
    for (index = 0; index < extension_count; index++) {
        output->data[cursor++] = extensions[index].type;
        output->data[cursor++] = (unsigned char)extensions[index].length;
        if (extensions[index].length) {
            memcpy(output->data + cursor, extensions[index].value,
                   extensions[index].length);
            cursor += extensions[index].length;
        }
    }
    return 0;
}

static int bolt_encode_bind_at_tick(const struct bolt_channel *channel,
                                    uint32_t tick_count,
                                    struct bytes *output)
{
    const bool tcp = channel && !strcasecmp(channel->protocol, "TCP");
    const size_t extension_length = channel && (tcp || channel->ept_enabled)
                                        ? sizeof(BOLT_BIND_EPT_REQUEST) - 1U
                                        : 0U;
    const size_t header_length = BOLT_BIND_HEADER_LENGTH + extension_length;
    const size_t frame_length = header_length + BOLT_BIND_PAYLOAD_LENGTH;
    size_t channel_token_length;
    size_t cursor;

    if (!channel || !output || !channel->channel_token[0] ||
        !channel->ticket.data || !channel->ticket.len ||
        !channel->session_id)
        return -1;
    channel_token_length = strlen(channel->channel_token);
    if (channel_token_length >
            BOLT_BIND_PAYLOAD_LENGTH - BOLT_BIND_FIXED_PAYLOAD_LENGTH ||
        channel->ticket.len >
            BOLT_BIND_PAYLOAD_LENGTH - BOLT_BIND_FIXED_PAYLOAD_LENGTH -
                channel_token_length ||
        frame_length > UINT16_MAX || header_length > UINT8_MAX)
        return -1;
    memset(output, 0, sizeof(*output));
    output->data = calloc(1, frame_length);
    if (!output->data)
        return -1;
    output->len = frame_length;
    output->data[0] = tcp ? BOLT_BIND_TCP_REQUEST_TYPE : BOLT_BIND_REQUEST_TYPE;
    output->data[1] = (unsigned char)header_length;
    write_le16(output->data + 2, (uint16_t)frame_length);
    if (extension_length)
        memcpy(output->data + BOLT_BIND_HEADER_LENGTH,
               BOLT_BIND_EPT_REQUEST, extension_length);
    cursor = header_length;
    output->data[cursor++] = 1U;
    write_le32(output->data + cursor, tick_count);
    cursor += 4;
    memcpy(output->data + cursor, channel->channel_token,
           channel_token_length);
    cursor += channel_token_length;
    write_le32(output->data + cursor, channel->session_id);
    cursor += 4;
    memcpy(output->data + cursor, channel->ticket.data, channel->ticket.len);
    return 0;
}

static int bolt_encode_bind(const struct bolt_channel *channel,
                            struct bytes *output)
{
    uint32_t tick_count;

    if (bolt_tick_count(&tick_count) != 0)
        return -1;
    return bolt_encode_bind_at_tick(channel, tick_count, output);
}

static bool bind_ept_key(const unsigned char *extension, size_t length,
                         uint8_t *key)
{
    static const unsigned char prefix[] = BOLT_BIND_EPT_KEY_PREFIX;
    const size_t prefix_length = sizeof(prefix) - 1U;
    size_t index;

    if (!extension || !key || length <= prefix_length)
        return false;
    for (index = 0; index + prefix_length < length; index++) {
        if (memcmp(extension + index, prefix, prefix_length) == 0) {
            *key = extension[index + prefix_length];
            return true;
        }
    }
    return false;
}

static int bolt_parse_bind_response(const unsigned char *frame, size_t length,
                                    struct bolt_bind_response *response)
{
    size_t header_length;

    if (!frame || length < BOLT_BIND_HEADER_LENGTH ||
        (frame[0] != BOLT_BIND_REQUEST_TYPE && frame[0] != BOLT_BIND_TCP_REQUEST_TYPE) ||
        frame[1] < BOLT_BIND_HEADER_LENGTH ||
        read_le16(frame + 2) != length)
        return -1;
    header_length = frame[1];
    if (header_length > length || length - header_length < 13 ||
        frame[header_length] != BOLT_BIND_RESPONSE_TYPE ||
        read_le32(frame + header_length + BOLT_BIND_STATUS_OFFSET) != 1U)
        return -1;
    if (response) {
        memset(response, 0, sizeof(*response));
        response->has_ept_key = bind_ept_key(
            frame + BOLT_BIND_HEADER_LENGTH,
            header_length - BOLT_BIND_HEADER_LENGTH, &response->ept_key);
    }
    return 0;
}

static bool bolt_bind_success(const unsigned char *frame, size_t length)
{
    return bolt_parse_bind_response(frame, length, NULL) == 0;
}

static int bolt_encode_data(uint32_t session_id, uint16_t connection_id,
                            const unsigned char *payload, size_t payload_length,
                            struct bytes *output)
{
    size_t total = BOLT_DATA_HEADER_LENGTH + payload_length;

    if (!output || (payload_length && !payload) || total > MAX_FRAME_SIZE)
        return -1;
    memset(output, 0, sizeof(*output));
    output->data = malloc(total);
    if (!output->data)
        return -1;
    output->len = total;
    output->data[0] = BOLT_PROTOCOL_VERSION;
    output->data[1] = BOLT_DATA_HEADER_LENGTH;
    write_le16(output->data + 2, (uint16_t)total);
    output->data[4] = BOLT_COMMAND_DATA;
    write_le32(output->data + 5, session_id);
    write_le16(output->data + 9, connection_id);
    if (payload_length)
        memcpy(output->data + BOLT_DATA_HEADER_LENGTH, payload, payload_length);
    return 0;
}

static int bolt_parse_response(const unsigned char *frame, size_t length,
                               struct bolt_response *response)
{
    size_t header_length;
    size_t total_length;
    size_t cursor;
    size_t index;
    uint8_t extension_count;
    size_t minimum_header;

    if (!frame || !response || length < 5 ||
        (frame[0] & 0x0fU) != BOLT_PROTOCOL_VERSION)
        return -1;
    minimum_header = frame[4] == BOLT_COMMAND_DATA ? BOLT_DATA_HEADER_LENGTH : 12;
    if (length < minimum_header)
        return -1;
    header_length = frame[1];
    total_length = read_le16(frame + 2);
    if (header_length < minimum_header || header_length > total_length ||
        total_length != length)
        return -1;
    memset(response, 0, sizeof(*response));
    response->command = frame[4];
    response->session_id = read_le32(frame + 5);
    response->connection_id = read_le16(frame + 9);
    if (response->command == BOLT_COMMAND_DATA) {
        if (header_length != BOLT_DATA_HEADER_LENGTH)
            return -1;
        response->payload = frame + header_length;
        response->payload_length = total_length - header_length;
        return 0;
    }
    cursor = 11;
    if (response->command == BOLT_COMMAND_CONNECT_RESPONSE ||
        response->command == BOLT_COMMAND_ASSOCIATE_RESPONSE ||
        response->command == BOLT_COMMAND_ERROR) {
        response->status = frame[cursor++];
        response->has_status = true;
    }
    if (cursor >= header_length)
        return -1;
    extension_count = frame[cursor++];
    for (index = 0; index < extension_count; index++) {
        size_t extension_length;

        if (cursor + 2 > header_length)
            return -1;
        extension_length = frame[cursor + 1];
        cursor += 2;
        if (extension_length > header_length - cursor)
            return -1;
        cursor += extension_length;
    }
    if (cursor != header_length)
        return -1;
    response->payload = frame + header_length;
    response->payload_length = total_length - header_length;
    return 0;
}

static bool bolt_success(const struct bolt_response *response, uint8_t request)
{
    uint8_t expected;

    if (!response || !response->has_status || response->status != BOLT_STATUS_SUCCESS ||
        response->connection_id == 0)
        return false;
    expected = request == BOLT_COMMAND_CONNECT_REQUEST
                   ? BOLT_COMMAND_CONNECT_RESPONSE
                   : BOLT_COMMAND_ASSOCIATE_RESPONSE;
    return response->command == expected;
}

static int run_self_test(void)
{
    static const unsigned char extension_value[] = {'x'};
    static const unsigned char payload[] = {'o', 'k'};
    char oversized_text[MAX_BOLT_EXTENSION_SIZE + 2];
    struct bolt_extension extension = {6, extension_value, 1};
    struct bolt_response response;
    struct bytes oversized = {0};
    struct bytes request = {0};
    struct bytes bind = {0};
    struct bytes data = {0};
    struct bolt_bind_response bind_result;
    struct bolt_channel bind_channel = {
        .protocol = "TCP",
        .channel_token = "0123456789abcdef0123456789abcdef",
        .session_id = 0x01020304,
    };
    static const unsigned char bind_ticket[] = "0123456789abcdef";
    const size_t bind_header_length = 26;
    unsigned char bind_response[BOLT_BIND_HEADER_LENGTH + 13U] = {
        [0] = BOLT_BIND_TCP_REQUEST_TYPE, [1] = BOLT_BIND_HEADER_LENGTH,
        [2] = BOLT_BIND_HEADER_LENGTH + 13U,
        [BOLT_BIND_HEADER_LENGTH] = BOLT_BIND_RESPONSE_TYPE,
        [BOLT_BIND_HEADER_LENGTH + 9U] = 1
    };
    unsigned char bind_ept_response[BOLT_BIND_HEADER_LENGTH + 9U + 13U] = {
        [0] = BOLT_BIND_TCP_REQUEST_TYPE, [1] = BOLT_BIND_HEADER_LENGTH + 9U,
        [2] = BOLT_BIND_HEADER_LENGTH + 9U + 13U,
        [21] = 'e', 'p', 't', '_', 'k', 'e', 'y', '=', 'K',
        [30] = BOLT_BIND_RESPONSE_TYPE, [39] = 1
    };
    unsigned char response_frame[13] = {
        BOLT_PROTOCOL_VERSION, 13, 13, 0, BOLT_COMMAND_CONNECT_RESPONSE,
        0x40, 0x30, 0x20, 0x10, 0x42, 0x00, BOLT_STATUS_SUCCESS, 0
    };
    int status = 1;

    memset(oversized_text, 'A', sizeof(oversized_text) - 1);
    oversized_text[sizeof(oversized_text) - 1] = '\0';
    bind_channel.ticket.data = (unsigned char *)bind_ticket;
    bind_channel.ticket.len = sizeof(bind_ticket) - 1;
    if (copy_text_bytes(oversized_text, &oversized) == 0)
        goto out;
    if (bolt_encode_request(BOLT_COMMAND_CONNECT_REQUEST, 0x10203040,
                             &extension, 1, &request) != 0 || request.len != 13 ||
        request.data[0] != BOLT_PROTOCOL_VERSION || request.data[1] != 13 ||
        read_le16(request.data + 2) != 13 || request.data[4] != BOLT_COMMAND_CONNECT_REQUEST ||
        read_le32(request.data + 5) != 0x10203040 || request.data[9] != 1 ||
        request.data[10] != 6 || request.data[11] != 1 || request.data[12] != 'x')
        goto out;
    if (bolt_parse_response(response_frame, sizeof(response_frame), &response) != 0 ||
        !bolt_success(&response, BOLT_COMMAND_CONNECT_REQUEST) ||
        response.session_id != 0x10203040 || response.connection_id != 0x42)
        goto out;
    if (bolt_encode_bind_at_tick(&bind_channel, 0x12345678U, &bind) != 0 ||
        bind.len != 99 ||
        bind.data[0] != BOLT_BIND_TCP_REQUEST_TYPE ||
        bind.data[1] != bind_header_length ||
        read_le16(bind.data + 2) != 99 ||
        memcmp(bind.data + BOLT_BIND_HEADER_LENGTH, "ept=1", 5) != 0 ||
        bind.data[bind_header_length] != 1U ||
        read_le32(bind.data + bind_header_length + 1U) != 0x12345678U ||
        memcmp(bind.data + bind_header_length + 5U,
               bind_channel.channel_token,
               strlen(bind_channel.channel_token)) != 0 ||
        read_le32(bind.data + bind_header_length + 5U +
                  strlen(bind_channel.channel_token)) != 0x01020304U ||
        memcmp(bind.data + bind_header_length + 9U +
                   strlen(bind_channel.channel_token),
               bind_ticket, sizeof(bind_ticket) - 1U) != 0 ||
        !bolt_bind_success(bind_response, sizeof(bind_response)) ||
        bolt_parse_bind_response(bind_ept_response,
                                 sizeof(bind_ept_response), &bind_result) != 0 ||
        !bind_result.has_ept_key || bind_result.ept_key != 'K')
        goto out;
    if (bolt_encode_data(0x10203040, 0x42, payload, sizeof(payload), &data) != 0 ||
        data.len != BOLT_DATA_HEADER_LENGTH + sizeof(payload) ||
        bolt_parse_response(data.data, data.len, &response) != 0 ||
        response.command != BOLT_COMMAND_DATA || response.session_id != 0x10203040 ||
        response.connection_id != 0x42 || response.payload_length != sizeof(payload) ||
        memcmp(response.payload, payload, sizeof(payload)) != 0)
        goto out;
    puts("{\"success\":true,\"tests\":[\"bolt-v2-bind\",\"bolt-v3-request\",\"bolt-v3-response\",\"bolt-v3-data\"]}");
    status = 0;

out:
    bytes_free(&oversized);
    bytes_free(&request);
    bytes_free(&bind);
    bytes_free(&data);
    if (status != 0)
        fputs("biubiu-accd self-test failed\n", stderr);
    return status;
}

static int write_full(int fd, const unsigned char *data, size_t length)
{
    size_t offset = 0;

    while (offset < length) {
        ssize_t written = write(fd, data + offset, length - offset);

        if (written < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (written == 0)
            return -1;
        offset += (size_t)written;
    }
    return 0;
}

static int read_full(int fd, unsigned char *data, size_t length)
{
    size_t offset = 0;

    while (offset < length) {
        ssize_t count = recv(fd, data + offset, length - offset, 0);

        if (count < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (count == 0)
            return -1;
        offset += (size_t)count;
    }
    return 0;
}

static int set_socket_timeout(int fd, int seconds)
{
    struct timeval timeout = {.tv_sec = seconds, .tv_usec = 0};

    return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0 &&
                   setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0
               ? 0
               : -1;
}

static int connect_channel(const char *address_text, uint16_t port, int type)
{
    struct sockaddr_in address = {0};
    int fd;
    int one = 1;

    fd = socket(AF_INET, type, 0);
    if (fd < 0)
        return -1;
    (void)setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
    if (set_socket_timeout(fd, 10) != 0 ||
        inet_pton(AF_INET, address_text, &address.sin_addr) != 1) {
        close(fd);
        return -1;
    }
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int read_bind_stream_frame(int fd, uint8_t expected_type,
                                  struct bytes *frame)
{
    unsigned char prefix[4];
    size_t total;

    memset(frame, 0, sizeof(*frame));
    if (read_full(fd, prefix, sizeof(prefix)) != 0 ||
        prefix[0] != expected_type)
        return -1;
    total = read_le16(prefix + 2);
    if (total < BOLT_BIND_HEADER_LENGTH || total > MAX_FRAME_SIZE)
        return -1;
    frame->data = malloc(total);
    if (!frame->data)
        return -1;
    frame->len = total;
    memcpy(frame->data, prefix, sizeof(prefix));
    if (read_full(fd, frame->data + sizeof(prefix), total - sizeof(prefix)) != 0) {
        bytes_free(frame);
        return -1;
    }
    return 0;
}

static int bind_channel_endpoint(struct bolt_channel *channel,
                                 const char *address)
{
    struct bytes request = {0};
    struct bytes response = {0};
    struct bolt_bind_response bind_result;
    int socket_type;
    int fd = -1;
    int status = -1;

    if (!channel || !address || !address[0])
        return -1;
    if (!strcasecmp(channel->protocol, "TCP"))
        socket_type = SOCK_STREAM;
    else if (!strcasecmp(channel->protocol, "UDP"))
        socket_type = SOCK_DGRAM;
    else
        return -1;
    if (bolt_encode_bind(channel, &request) != 0)
        goto out;
    fd = connect_channel(address, channel->port, socket_type);
    if (fd < 0 || write_full(fd, request.data, request.len) != 0)
        goto out;
    if (socket_type == SOCK_STREAM) {
        if (read_bind_stream_frame(fd, BOLT_BIND_TCP_REQUEST_TYPE, &response) != 0)
            goto out;
    } else {
        ssize_t received;

        response.data = malloc(MAX_FRAME_SIZE);
        if (!response.data)
            goto out;
        received = recv(fd, response.data, MAX_FRAME_SIZE, 0);
        if (received <= 0)
            goto out;
        response.len = (size_t)received;
    }
    if (bolt_parse_bind_response(response.data, response.len, &bind_result) != 0)
        goto out;
    channel->ept_enabled = bind_result.has_ept_key;
    channel->ept_key = bind_result.ept_key;
    status = 0;

out:
    if (fd >= 0)
        close(fd);
    bytes_free(&request);
    bytes_free(&response);
    return status;
}

static int bind_channel(struct bolt_channel *channel)
{
    struct bbnet_transport_config transport_config = {0};
    char addresses[2][64];
    size_t count = 1;
    size_t index;

    if (!channel)
        return -1;
    snprintf(addresses[0], sizeof(addresses[0]), "%s", channel->address);
    if (channel->bip_address[0]) {
        snprintf(addresses[count], sizeof(addresses[count]), "%s",
                 channel->bip_address);
        count++;
    }
    for (index = 0; index < count; index++) {
        if (bind_channel_endpoint(channel, addresses[index]) != 0)
            continue;
        memcpy(channel->address, addresses[index], sizeof(channel->address));
        channel->address[sizeof(channel->address) - 1U] = '\0';
        if (channel->native_bolt) {
            if (!strcasecmp(channel->protocol, "UDP")) {
                channel->datagram_fd = connect_channel(channel->address,
                                                        channel->port, SOCK_DGRAM);
                if (channel->datagram_fd < 0)
                    continue;
            }
            channel->bound = true;
            return 0;
        }
        transport_config.address = channel->address;
        transport_config.port = channel->port;
        transport_config.server_parameter = channel->server_parameter.data;
        transport_config.server_parameter_length = channel->server_parameter.len;
        transport_config.client_parameter = channel->client_parameter.data;
        transport_config.client_parameter_length = channel->client_parameter.len;
        transport_config.keepalive_ms = BBNET_KEEPALIVE_MS;
        transport_config.connect_timeout_ms = BBNET_CONNECT_TIMEOUT_MS;
        if (!strcasecmp(channel->protocol, "TCP")) {
            transport_config.mode = BBNET_TRANSPORT_CONFLUENCE;
            transport_config.application_protocol = BBNET_APPLICATION_KCP;
        } else {
            transport_config.mode = BBNET_TRANSPORT_DATAGRAM;
            transport_config.application_protocol = BBNET_APPLICATION_NACK;
        }
        channel->transport = bbnet_transport_start(&transport_config);
        if (!channel->transport)
            continue;
        channel->bound = true;
        return 0;
    }
    channel->bound = false;
    memcpy(channel->address, addresses[0], sizeof(channel->address));
    channel->address[sizeof(channel->address) - 1U] = '\0';
    return -1;
}

static int bind_runtime_channels(struct runtime_config *runtime)
{
    size_t usable_tcp = 0;
    size_t usable_udp = 0;
    size_t failed = 0;
    size_t index;

    if (!runtime)
        return -1;
    if (!runtime->outbound_count) {
        if (runtime->has_tcp && bind_channel(&runtime->tcp) == 0)
            usable_tcp++;
        if (runtime->has_udp && bind_channel(&runtime->udp) == 0)
            usable_udp++;
    } else {
        for (index = 0; index < runtime->outbound_count; index++) {
            struct bolt_outbound *outbound = &runtime->outbounds[index];
            size_t channel_index;

            for (channel_index = 0; channel_index < outbound->channel_count;
                 channel_index++) {
                struct bolt_channel *channel = &outbound->channels[channel_index];

                if (strcasecmp(channel->protocol, "TCP") &&
                    strcasecmp(channel->protocol, "UDP"))
                    continue;
                if (bind_channel(channel) != 0) {
                    failed++;
                    continue;
                }
                if (!strcasecmp(channel->protocol, "TCP"))
                    usable_tcp++;
                else
                    usable_udp++;
            }
        }
    }
    runtime->has_tcp = usable_tcp > 0;
    runtime->has_udp = usable_udp > 0;
    fprintf(stderr,
            "biubiu-accd: authorized data channels ready "
            "(TCP=%zu UDP=%zu failed=%zu)\n",
            usable_tcp, usable_udp, failed);
    return usable_tcp || usable_udp ? 0 : -1;
}

static size_t build_extensions(const struct bolt_channel *channel,
                               const struct sockaddr_in *source,
                               const struct sockaddr_in *target,
                               struct bolt_extension *extensions,
                               unsigned char source_endpoint[6],
                               unsigned char target_endpoint[6])
{
    size_t count = 0;

    (void)channel;
    memcpy(source_endpoint, &source->sin_addr, 4);
    memcpy(source_endpoint + 4, &source->sin_port, 2);
    extensions[count++] = (struct bolt_extension){1, source_endpoint, 6};
    memcpy(target_endpoint, &target->sin_addr, 4);
    memcpy(target_endpoint + 4, &target->sin_port, 2);
    extensions[count++] = (struct bolt_extension){2, target_endpoint, 6};
    return count;
}

static bool route_matches(const struct bolt_route *route,
                          const struct sockaddr_in *target, uint8_t protocol)
{
    uint16_t port;
    size_t index;
    bool network_match = false;
    bool port_match = false;

    if (!route || !target ||
        (route->protocol != 0 && route->protocol != protocol))
        return false;
    for (index = 0; index < route->network_count; index++) {
        if ((target->sin_addr.s_addr & route->networks[index].mask) ==
            route->networks[index].network) {
            network_match = true;
            break;
        }
    }
    if (!network_match)
        return false;
    if (!route->port_count)
        return true;
    port = ntohs(target->sin_port);
    for (index = 0; index < route->port_count; index++) {
        if (port >= route->ports[index].first &&
            port <= route->ports[index].last) {
            port_match = true;
            break;
        }
    }
    return port_match;
}

static const struct bolt_channel *outbound_channel(
    const struct runtime_config *runtime, const char *outbound_id,
    const char *protocol, bool spare_only)
{
    size_t index;

    if (!runtime || !outbound_id || !outbound_id[0] || !protocol)
        return NULL;
    for (index = 0; index < runtime->outbound_count; index++) {
        const struct bolt_outbound *outbound = &runtime->outbounds[index];
        bool is_spare = !strcasecmp(outbound->type, "spare");
        size_t channel_index;

        if (strcmp(outbound->id, outbound_id) || is_spare != spare_only)
            continue;
        for (channel_index = 0; channel_index < outbound->channel_count;
             channel_index++) {
            const struct bolt_channel *channel =
                &outbound->channels[channel_index];

            if (channel->bound && !strcasecmp(channel->protocol, protocol))
                return channel;
        }
    }
    return NULL;
}

static const struct bolt_channel *select_channel(
    const struct runtime_config *runtime, const struct sockaddr_in *target,
    uint8_t protocol)
{
    const char *protocol_name = protocol == IPPROTO_TCP ? "TCP" : "UDP";
    const char *selected_id = runtime->default_outbound_id;
    const char *primary_id = runtime->default_outbound_id;
    const struct bolt_channel *channel;
    size_t index;

    if (!runtime->outbound_count)
        return protocol == IPPROTO_TCP
                   ? (runtime->tcp.bound ? &runtime->tcp : NULL)
                   : (runtime->udp.bound ? &runtime->udp : NULL);
    for (index = 0; index < runtime->route_count; index++) {
        if (!route_matches(&runtime->routes[index], target, protocol))
            continue;
        selected_id = runtime->routes[index].outbound_id;
        primary_id = runtime->routes[index].primary_outbound_id;
        break;
    }
    channel = outbound_channel(runtime, selected_id, protocol_name, false);
    if (!channel && strcmp(selected_id, primary_id))
        channel = outbound_channel(runtime, primary_id, protocol_name, false);
    if (!channel)
        channel = outbound_channel(runtime, primary_id, protocol_name, true);
    if (!channel && strcmp(primary_id, runtime->default_outbound_id))
        channel = outbound_channel(runtime, runtime->default_outbound_id,
                                   protocol_name, false);
    if (!channel)
        channel = outbound_channel(runtime, runtime->default_outbound_id,
                                   protocol_name, true);
    return channel;
}

static ssize_t receive_link_packet(int fd, void *buffer, size_t capacity,
                                   int timeout_ms)
{
    struct pollfd descriptor = {.fd = fd, .events = POLLIN};
    int result;

    do {
        result = poll(&descriptor, 1, timeout_ms);
    } while (result < 0 && errno == EINTR);
    if (result <= 0 || !(descriptor.revents & POLLIN)) {
        if (result == 0)
            errno = ETIMEDOUT;
        else if (result > 0)
            errno = ECONNRESET;
        return -1;
    }
    return recv(fd, buffer, capacity, 0);
}

static int open_native_bolt_tcp(const struct bolt_channel *channel,
                                const struct sockaddr_in *target,
                                int *receive_fd)
{
    unsigned char request[22] = {1, 21, 22};
    struct bytes response = {0};
    int fd = -1;
    int status = -1;
    size_t header_length;

    if (!channel || !channel->bound || !target || target->sin_family != AF_INET)
        return -1;
    memcpy(request + 5, &target->sin_addr, 4);
    memcpy(request + 9, &target->sin_port, 2);
    write_le32(request + 17, channel->session_id);
    request[21] = 0x20;
    fd = connect_channel(channel->address, channel->port, SOCK_STREAM);
    if (fd < 0 || write_full(fd, request, sizeof(request)) != 0 ||
        read_bind_stream_frame(fd, 1, &response) != 0)
        goto out;
    header_length = response.data[1];
    if (header_length < BOLT_BIND_HEADER_LENGTH || header_length > response.len ||
        response.len - header_length != 2 || response.data[header_length] != 0x21 ||
        response.data[header_length + 1] != 0x22) {
        errno = ECONNREFUSED;
        goto out;
    }
    *receive_fd = fd;
    fd = -1;
    status = 0;
out:
    if (fd >= 0)
        close(fd);
    bytes_free(&response);
    return status;
}

static void native_bolt_tcp_transform(const struct bolt_channel *channel,
                                      unsigned char *data, size_t length)
{
    size_t index;

    if (!channel->native_bolt || !channel->ept_enabled)
        return;
    for (index = 0; index < length; index++)
        data[index] ^= channel->ept_key;
}

static int open_bolt_tcp_link(const struct bolt_channel *channel,
                              const struct sockaddr_in *source,
                              const struct sockaddr_in *target,
                              uint32_t *link_id, int *receive_fd,
                              struct bytes *initial_payload)
{
    struct bolt_extension extensions[8];
    unsigned char source_endpoint[6];
    unsigned char target_endpoint[6];
    unsigned char *response_frame = NULL;
    struct bytes request = {0};
    struct bolt_response response;
    ssize_t response_length;
    int fd = -1;
    int status = -1;

    if (!channel || !channel->bound || !source ||
        !target || !link_id || !receive_fd || !initial_payload)
        return -1;
    memset(initial_payload, 0, sizeof(*initial_payload));
    if (channel->native_bolt)
        return open_native_bolt_tcp(channel, target, receive_fd);
    if (!channel->transport)
        return -1;
    if (bolt_encode_request(
            BOLT_COMMAND_CONNECT_REQUEST, channel->session_id, extensions,
            build_extensions(channel, source, target, extensions,
                             source_endpoint, target_endpoint),
            &request) != 0 ||
        bbnet_transport_open_link(channel->transport, link_id, &fd,
                                  CONFLUENCE_LINK_TIMEOUT_MS) != 0)
        goto out;
    if (bbnet_transport_send_link(channel->transport, *link_id, request.data,
                                  request.len) != 0)
        goto out;
    response_frame = malloc(MAX_FRAME_SIZE);
    if (!response_frame)
        goto out;
    response_length = receive_link_packet(fd, response_frame, MAX_FRAME_SIZE,
                                          CONFLUENCE_LINK_TIMEOUT_MS);
    if (response_length <= 0 ||
        bolt_parse_response(response_frame, (size_t)response_length,
                            &response) != 0 ||
        !bolt_success(&response, BOLT_COMMAND_CONNECT_REQUEST) ||
        response.session_id != channel->session_id)
        goto out;
    if (response.payload_length) {
        initial_payload->data = malloc(response.payload_length);
        if (!initial_payload->data)
            goto out;
        memcpy(initial_payload->data, response.payload, response.payload_length);
        initial_payload->len = response.payload_length;
    }
    *receive_fd = fd;
    fd = -1;
    status = 0;

out:
    if (status != 0 && fd >= 0) {
        bbnet_transport_close_link(channel->transport, *link_id);
        close(fd);
    }
    if (status != 0)
        bytes_free(initial_payload);
    bytes_free(&request);
    free(response_frame);
    return status;
}

static int forward_tcp_stream(int client_fd, const struct bolt_channel *channel,
                              const struct sockaddr_in *source,
                              const struct sockaddr_in *target)
{
    struct bytes initial_payload = {0};
    unsigned char *buffer = NULL;
    uint32_t link_id = 0;
    int remote_fd = -1;
    int status = -1;
    bool client_read_open = true;

    buffer = malloc(MAX_FRAME_SIZE - CONFLUENCE_DATA_HEADER_SIZE);
    if (!buffer)
        return -1;
    if (open_bolt_tcp_link(channel, source, target, &link_id, &remote_fd,
                           &initial_payload) != 0)
        goto out;
    status = 0;
    if (initial_payload.len &&
        write_full(client_fd, initial_payload.data, initial_payload.len) != 0) {
        status = -1;
        goto out;
    }
    while (keep_running) {
        struct pollfd fds[2] = {
            {.fd = client_read_open ? client_fd : -1, .events = POLLIN},
            {.fd = remote_fd, .events = POLLIN},
        };
        int result = poll(fds, 2, 1000);

        if (result < 0) {
            if (errno == EINTR)
                continue;
            status = -1;
            break;
        }
        if (result == 0)
            continue;
        if (fds[0].revents & POLLIN) {
            ssize_t count = recv(client_fd, buffer,
                                 MAX_FRAME_SIZE -
                                     CONFLUENCE_DATA_HEADER_SIZE,
                                 0);

            if (count > 0) {
                int sent;

                native_bolt_tcp_transform(channel, buffer, (size_t)count);
                sent = channel->native_bolt ?
                    write_full(remote_fd, buffer, (size_t)count) :
                    bbnet_transport_send_link(channel->transport, link_id,
                                               buffer, (size_t)count);
                if (sent != 0) {
                    status = -1;
                    break;
                }
            } else if (count == 0) {
                if (channel->native_bolt) {
                    client_read_open = false;
                    (void)shutdown(remote_fd, SHUT_WR);
                    continue;
                }
                break;
            } else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
                status = -1;
                break;
            }
        }
        if (fds[0].revents & (POLLERR | POLLNVAL)) {
            status = -1;
            break;
        }
        if ((fds[0].revents & POLLHUP) &&
            !(fds[0].revents & POLLIN)) {
            break;
        }
        if (fds[1].revents & POLLIN) {
            ssize_t count = recv(remote_fd, buffer,
                                 MAX_FRAME_SIZE -
                                     CONFLUENCE_DATA_HEADER_SIZE,
                                 0);

            if (count > 0) {
                native_bolt_tcp_transform(channel, buffer, (size_t)count);
                if (write_full(client_fd, buffer, (size_t)count) == 0)
                    continue;
                status = -1;
            } else if (count < 0 &&
                       (errno == EINTR || errno == EAGAIN ||
                        errno == EWOULDBLOCK))
                continue;
            break;
        }
        if (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            (void)shutdown(client_fd, SHUT_WR);
            break;
        }
    }
out:
    free(buffer);
    bytes_free(&initial_payload);
    if (remote_fd >= 0) {
        if (!channel->native_bolt)
            bbnet_transport_close_link(channel->transport, link_id);
        close(remote_fd);
    }
    return status;
}

static int get_original_destination(int fd, struct sockaddr_in *destination)
{
    socklen_t length = sizeof(*destination);

    memset(destination, 0, sizeof(*destination));
    if (getsockopt(fd, SOL_IP, SO_ORIGINAL_DST, destination, &length) == 0 &&
        length == sizeof(*destination) && destination->sin_family == AF_INET &&
        destination->sin_addr.s_addr != htonl(INADDR_ANY) &&
        destination->sin_port != 0)
        return 0;

    /* nft TPROXY preserves the destination on the socket itself; unlike a
     * REDIRECT rule it may not populate SO_ORIGINAL_DST. */
    memset(destination, 0, sizeof(*destination));
    length = sizeof(*destination);
    if (getsockname(fd, (struct sockaddr *)destination, &length) != 0 ||
        length != sizeof(*destination) || destination->sin_family != AF_INET ||
        destination->sin_addr.s_addr == htonl(INADDR_ANY) ||
        destination->sin_port == 0)
        return -1;
    return 0;
}

static void *tcp_worker_main(void *argument)
{
    struct tcp_worker *worker = argument;
    const struct bolt_channel *channel;
    struct sockaddr_in source;
    struct sockaddr_in target;
    socklen_t source_length = sizeof(source);

    if (get_original_destination(worker->client_fd, &target) != 0 ||
        getpeername(worker->client_fd, (struct sockaddr *)&source,
                    &source_length) != 0 || source.sin_family != AF_INET ||
        !(channel = select_channel(worker->runtime, &target, IPPROTO_TCP))) {
        fprintf(stderr, "biubiu-accd: TCP original destination unavailable\n");
        goto out;
    }
    (void)forward_tcp_stream(worker->client_fd, channel, &source, &target);

out:
    close(worker->client_fd);
    tcp_worker_release();
    free(worker);
    return NULL;
}

static int make_listener(int type, uint16_t port)
{
    struct sockaddr_in address = {0};
    int one = 1;
    int fd;

    fd = socket(AF_INET, type, 0);
    if (fd < 0)
        return -1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) != 0 ||
        setsockopt(fd, SOL_IP, IP_TRANSPARENT, &one, sizeof(one)) != 0) {
        close(fd);
        return -1;
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        close(fd);
        return -1;
    }
    if (type == SOCK_STREAM && listen(fd, 64) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int parse_original_destination_message(struct msghdr *message,
                                              struct sockaddr_in *destination)
{
    struct cmsghdr *control;

    memset(destination, 0, sizeof(*destination));
    for (control = CMSG_FIRSTHDR(message); control;
         control = CMSG_NXTHDR(message, control)) {
        if (control->cmsg_level == IPPROTO_IP &&
            control->cmsg_type == IP_ORIGDSTADDR &&
            control->cmsg_len >= CMSG_LEN(sizeof(*destination))) {
            memcpy(destination, CMSG_DATA(control), sizeof(*destination));
            break;
        }
    }
    return destination->sin_family == AF_INET && destination->sin_port != 0 ? 0 : -1;
}

static bool same_endpoint(const struct sockaddr_in *left,
                          const struct sockaddr_in *right)
{
    return left->sin_addr.s_addr == right->sin_addr.s_addr &&
           left->sin_port == right->sin_port;
}

static void close_udp_flow(struct udp_flow *flow)
{
    if (!flow)
        return;
    if (flow->reply_fd >= 0)
        close(flow->reply_fd);
    memset(flow, 0, sizeof(*flow));
    flow->reply_fd = -1;
}

static int find_udp_flow(struct udp_flow *flows, const struct sockaddr_in *client,
                         const struct sockaddr_in *target)
{
    size_t index;

    for (index = 0; index < MAX_UDP_FLOWS; index++)
        if (flows[index].active && same_endpoint(&flows[index].client, client) &&
            same_endpoint(&flows[index].target, target))
            return (int)index;
    return -1;
}

static int allocate_udp_flow(struct udp_flow *flows, const struct runtime_config *runtime,
                             const struct sockaddr_in *client,
                             const struct sockaddr_in *target)
{
    size_t index;
    int reply_fd;
    const struct bolt_channel *channel;

    for (index = 0; index < MAX_UDP_FLOWS; index++)
        if (!flows[index].active)
            break;
    if (index == MAX_UDP_FLOWS)
        return -1;
    channel = select_channel(runtime, target, IPPROTO_UDP);
    if (!channel || !channel->transport)
        return -1;
    reply_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (reply_fd < 0)
        return -1;
    {
        int one = 1;

        if (setsockopt(reply_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) != 0 ||
            setsockopt(reply_fd, SOL_IP, IP_TRANSPARENT, &one, sizeof(one)) != 0 ||
            bind(reply_fd, (const struct sockaddr *)target, sizeof(*target)) != 0 ||
            connect(reply_fd, (const struct sockaddr *)client,
                    sizeof(*client)) != 0) {
            close(reply_fd);
            return -1;
        }
    }
    flows[index].active = true;
    flows[index].reply_fd = reply_fd;
    flows[index].client = *client;
    flows[index].target = *target;
    flows[index].channel = channel;
    flows[index].last_seen = time(NULL);
    return (int)index;
}

static int send_udp_flow_payload(const struct bolt_channel *channel,
                                 const struct udp_flow *flow,
                                 const unsigned char *payload, size_t payload_length)
{
    unsigned char *frame;
    size_t frame_length;
    int status;

    if (!channel || !flow ||
        (channel->native_bolt ? (!channel->bound || channel->datagram_fd < 0) :
                                !channel->transport) ||
        payload_length > MAX_FRAME_SIZE - BIUBIU_UDP_TUNNEL_HEADER_SIZE)
        return -1;
    frame = malloc(BIUBIU_UDP_TUNNEL_HEADER_SIZE + payload_length);
    if (!frame)
        return -1;
    if (biubiu_udp_tunnel_encode(
            IPPROTO_UDP,
            channel->native_bolt ? &flow->target : &flow->client,
            channel->native_bolt ? &flow->client : &flow->target,
            channel->session_id,
            payload, payload_length, frame,
            BIUBIU_UDP_TUNNEL_HEADER_SIZE + payload_length,
            &frame_length) != 0) {
        free(frame);
        return -1;
    }
    if (channel->native_bolt)
        status = send(channel->datagram_fd, frame, frame_length, 0) ==
                     (ssize_t)frame_length ? 0 : -1;
    else
        status = bbnet_transport_send_datagram(channel->transport, frame,
                                               frame_length);
    free(frame);
    return status;
}

static int find_udp_response_flow(struct udp_flow *flows,
                                  const struct bolt_channel *channel,
                                  const struct sockaddr_in *source,
                                  const struct sockaddr_in *target)
{
    size_t index;

    for (index = 0; index < MAX_UDP_FLOWS; index++) {
        if (!flows[index].active || flows[index].channel != channel)
            continue;
        if (same_endpoint(&flows[index].target, source) &&
            same_endpoint(&flows[index].client, target))
            return (int)index;
    }
    return -1;
}

static int inject_udp_tunnel_payload(struct udp_flow *flows,
                                     const struct bolt_channel *channel,
                                     const unsigned char *data, size_t length)
{
    struct biubiu_udp_tunnel_frame frame;
    struct udp_flow *flow;
    ssize_t sent;
    int flow_index;

    if (!flows || !channel ||
        biubiu_udp_tunnel_parse(data, length, &frame) != 0 ||
        frame.protocol != IPPROTO_UDP ||
        frame.route_context != channel->session_id)
        return -1;
    flow_index = find_udp_response_flow(flows, channel, &frame.source,
                                        &frame.target);
    if (flow_index < 0)
        return -1;
    flow = &flows[flow_index];
    if (!frame.payload_length)
        return 0;
    sent = write(flow->reply_fd, frame.payload, frame.payload_length);
    if (sent < 0 || (size_t)sent != frame.payload_length)
        return -1;
    flow->last_seen = time(NULL);
    return 0;
}

static size_t collect_udp_channels(const struct runtime_config *runtime,
                                   const struct bolt_channel **channels,
                                   size_t capacity)
{
    size_t count = 0;
    size_t index;

    if (!runtime)
        return 0;
    if (!runtime->outbound_count) {
        if (runtime->udp.bound &&
            (runtime->udp.native_bolt ? runtime->udp.datagram_fd >= 0 :
                                       runtime->udp.transport != NULL)) {
            if (channels && count < capacity)
                channels[count] = &runtime->udp;
            count++;
        }
        return count;
    }
    for (index = 0; index < runtime->outbound_count; index++) {
        const struct bolt_outbound *outbound = &runtime->outbounds[index];
        size_t channel_index;

        for (channel_index = 0; channel_index < outbound->channel_count;
             channel_index++) {
            const struct bolt_channel *channel =
                &outbound->channels[channel_index];

            if (!channel->bound ||
                (channel->native_bolt ? channel->datagram_fd < 0 :
                                       channel->transport == NULL) ||
                strcasecmp(channel->protocol, "UDP"))
                continue;
            if (channels && count < capacity)
                channels[count] = channel;
            count++;
        }
    }
    return count;
}

static void *udp_loop_main(void *argument)
{
    struct udp_context *context = argument;
    struct udp_flow flows[MAX_UDP_FLOWS];
    const struct bolt_channel **channels = NULL;
    struct pollfd *pollfds = NULL;
    unsigned char buffer[MAX_FRAME_SIZE];
    unsigned char control[CMSG_SPACE(sizeof(struct sockaddr_in))];
    size_t channel_count;
    size_t index;

    memset(flows, 0, sizeof(flows));
    for (index = 0; index < MAX_UDP_FLOWS; index++)
        flows[index].reply_fd = -1;
    channel_count = collect_udp_channels(context->runtime, NULL, 0);
    channels = calloc(channel_count, sizeof(*channels));
    pollfds = calloc(channel_count + 1U, sizeof(*pollfds));
    if (!channel_count || !channels || !pollfds ||
        collect_udp_channels(context->runtime, channels, channel_count) !=
            channel_count)
        goto out;
    pollfds[0] = (struct pollfd){.fd = context->listener, .events = POLLIN};
    for (index = 0; index < channel_count; index++)
        pollfds[index + 1U] =
            (struct pollfd){.fd = channels[index]->native_bolt ?
                                channels[index]->datagram_fd :
                                bbnet_transport_datagram_fd(channels[index]->transport),
                            .events = POLLIN};
    while (keep_running) {
        int result;

        result = poll(pollfds, channel_count + 1U, 1000);
        if (result < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        for (index = 0; index < MAX_UDP_FLOWS; index++)
            if (flows[index].active &&
                time(NULL) - flows[index].last_seen > UDP_FLOW_IDLE_SECONDS)
                close_udp_flow(&flows[index]);
        if (pollfds[0].revents & POLLIN) {
            struct sockaddr_in client;
            struct sockaddr_in target;
            struct iovec vector = {.iov_base = buffer, .iov_len = sizeof(buffer)};
            struct msghdr message = {
                .msg_name = &client,
                .msg_namelen = sizeof(client),
                .msg_iov = &vector,
                .msg_iovlen = 1,
                .msg_control = control,
                .msg_controllen = sizeof(control),
            };
            ssize_t received = recvmsg(context->listener, &message, 0);
            int flow_index;

            if (received <= 0 || parse_original_destination_message(&message, &target) != 0)
                continue;
            flow_index = find_udp_flow(flows, &client, &target);
            if (flow_index < 0)
                flow_index = allocate_udp_flow(flows, context->runtime, &client, &target);
            if (flow_index < 0)
                continue;
            flows[flow_index].last_seen = time(NULL);
            if (send_udp_flow_payload(flows[flow_index].channel, &flows[flow_index],
                                      buffer, (size_t)received) != 0)
                close_udp_flow(&flows[flow_index]);
        }
        for (index = 0; index < channel_count; index++) {
            struct pollfd *descriptor = &pollfds[index + 1U];
            ssize_t received;

            if (descriptor->revents & (POLLERR | POLLHUP | POLLNVAL)) {
                size_t flow_index;

                for (flow_index = 0; flow_index < MAX_UDP_FLOWS;
                     flow_index++)
                    if (flows[flow_index].active &&
                        flows[flow_index].channel == channels[index])
                        close_udp_flow(&flows[flow_index]);
                continue;
            }
            if (!(descriptor->revents & POLLIN))
                continue;
            received = recv(descriptor->fd, buffer, sizeof(buffer), 0);
            if (received <= 0)
                continue;
            (void)inject_udp_tunnel_payload(flows, channels[index], buffer,
                                            (size_t)received);
        }
    }
out:
    for (index = 0; index < MAX_UDP_FLOWS; index++)
        close_udp_flow(&flows[index]);
    free(pollfds);
    free(channels);
    close(context->listener);
    free(context);
    return NULL;
}

static void signal_handler(int signal_number)
{
    (void)signal_number;
    keep_running = 0;
}

static void usage(FILE *stream)
{
    fprintf(stream,
            "Usage: biubiu-accd [--runtime PATH] [--tcp-port PORT] [--udp-port PORT]\n"
            "       biubiu-accd --self-test\n"
            "       biubiu-accd --version\n");
}

int main(int argc, char **argv)
{
    const char *runtime_path = DEFAULT_RUNTIME_FILE;
    uint64_t tcp_port = DEFAULT_TCP_PORT;
    uint64_t udp_port = DEFAULT_UDP_PORT;
    struct runtime_config runtime;
    pthread_t udp_thread;
    pthread_t worker_thread;
    bool udp_started = false;
    int tcp_listener = -1;
    int udp_listener = -1;
    int index;

    for (index = 1; index < argc; index++) {
        if (!strcmp(argv[index], "--self-test") && index + 1 == argc)
            return run_self_test();
        if (!strcmp(argv[index], "--version") && index + 1 == argc) {
            puts("biubiu-accd " BIUBIU_ACC_VERSION);
            return 0;
        }
        if (!strcmp(argv[index], "--runtime") && index + 1 < argc) {
            runtime_path = argv[++index];
        } else if (!strcmp(argv[index], "--tcp-port") && index + 1 < argc) {
            tcp_port = strtoull(argv[++index], NULL, 10);
        } else if (!strcmp(argv[index], "--udp-port") && index + 1 < argc) {
            udp_port = strtoull(argv[++index], NULL, 10);
        } else {
            usage(stderr);
            return 2;
        }
    }
    if (tcp_port == 0 || tcp_port > 65535 || udp_port == 0 || udp_port > 65535 ||
        load_runtime(runtime_path, &runtime) != 0) {
        fprintf(stderr, "biubiu-accd: invalid or unavailable runtime file: %s\n",
                strerror(errno));
        return 1;
    }
    if (runtime_is_expired(&runtime, time(NULL))) {
        errno = ETIMEDOUT;
        fprintf(stderr, "biubiu-accd: runtime authorization has expired: %s\n",
                strerror(errno));
        runtime_free(&runtime);
        return 1;
    }
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);
    if (bind_runtime_channels(&runtime) != 0) {
        fputs("biubiu-accd: no authorized data channel accepted the bind\n",
              stderr);
        runtime_free(&runtime);
        return 1;
    }
    if (runtime.has_tcp) {
        tcp_listener = make_listener(SOCK_STREAM, (uint16_t)tcp_port);
        if (tcp_listener < 0) {
            fprintf(stderr, "biubiu-accd: unable to bind TCP transparent listener: %s\n",
                    strerror(errno));
            runtime_free(&runtime);
            return 1;
        }
    }
    if (runtime.has_udp) {
        int one = 1;

        udp_listener = make_listener(SOCK_DGRAM, (uint16_t)udp_port);
        if (udp_listener < 0 ||
            setsockopt(udp_listener, IPPROTO_IP, IP_RECVORIGDSTADDR, &one,
                       sizeof(one)) != 0) {
            fprintf(stderr, "biubiu-accd: unable to bind UDP transparent listener: %s\n",
                    strerror(errno));
            if (udp_listener >= 0)
                close(udp_listener);
            if (tcp_listener >= 0)
                close(tcp_listener);
            runtime_free(&runtime);
            return 1;
        }
        {
            struct udp_context *context = calloc(1, sizeof(*context));
            if (!context) {
                close(udp_listener);
                if (tcp_listener >= 0)
                    close(tcp_listener);
                runtime_free(&runtime);
                return 1;
            }
            context->listener = udp_listener;
            context->runtime = &runtime;
            if (pthread_create(&udp_thread, NULL, udp_loop_main, context) != 0) {
                free(context);
                close(udp_listener);
                if (tcp_listener >= 0)
                    close(tcp_listener);
                runtime_free(&runtime);
                return 1;
            }
            udp_started = true;
        }
    }
    fprintf(stderr, "biubiu-accd: ready (TCP=%s UDP=%s)\n",
            runtime.has_tcp ? "enabled" : "disabled",
            runtime.has_udp ? "enabled" : "disabled");
    while (keep_running) {
        struct pollfd listener_poll;
        int poll_result;

        if (tcp_listener < 0) {
            sleep(1);
            continue;
        }
        listener_poll = (struct pollfd){.fd = tcp_listener, .events = POLLIN};
        poll_result = poll(&listener_poll, 1, 1000);
        if (poll_result < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (poll_result == 0 || !(listener_poll.revents & POLLIN))
            continue;
        {
            int client_fd = accept(tcp_listener, NULL, NULL);
            struct tcp_worker *worker;

            if (client_fd < 0) {
                if (errno == EINTR)
                    continue;
                break;
            }
            worker = calloc(1, sizeof(*worker));
            if (!worker) {
                close(client_fd);
                continue;
            }
            worker->client_fd = client_fd;
            worker->runtime = &runtime;
            tcp_worker_acquire();
            if (pthread_create(&worker_thread, NULL, tcp_worker_main, worker) != 0) {
                tcp_worker_release();
                close(client_fd);
                free(worker);
                continue;
            }
            pthread_detach(worker_thread);
        }
    }
    keep_running = 0;
    if (tcp_listener >= 0)
        close(tcp_listener);
    if (udp_started)
        pthread_join(udp_thread, NULL);
    wait_for_tcp_workers();
    runtime_free(&runtime);
    return 0;
}
