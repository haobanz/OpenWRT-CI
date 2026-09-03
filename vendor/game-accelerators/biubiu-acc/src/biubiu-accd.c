#define _GNU_SOURCE

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

#define BIUBIU_ACC_VERSION "0.8.0"
#define DEFAULT_RUNTIME_FILE "/etc/biubiu-acc/runtime.json"
#define DEFAULT_TCP_PORT 18080
#define DEFAULT_UDP_PORT 18081
#define MAX_RUNTIME_SIZE (4U * 1024U * 1024U)
#define MAX_FRAME_SIZE UINT16_MAX
#define MAX_UDP_FLOWS 32U
#define UDP_FLOW_IDLE_SECONDS 120U

#define BOLT_PROTOCOL_VERSION 3U
#define BOLT_DATA_HEADER_LENGTH 11U
#define BOLT_COMMAND_DATA 0x11U
#define BOLT_COMMAND_CONNECT_REQUEST 0x22U
#define BOLT_COMMAND_CONNECT_RESPONSE 0x23U
#define BOLT_COMMAND_ASSOCIATE_REQUEST 0x24U
#define BOLT_COMMAND_ASSOCIATE_RESPONSE 0x25U
#define BOLT_COMMAND_ERROR 0x27U
#define BOLT_STATUS_SUCCESS 0x22U

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

struct bolt_channel {
    char protocol[8];
    char address[64];
    uint16_t port;
    uint32_t session_id;
    bool encrypted;
    struct bytes ticket;
    struct bytes client_parameter;
    struct bytes server_parameter;
    struct bytes strategy;
};

struct runtime_config {
    struct bolt_channel tcp;
    struct bolt_channel udp;
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
    int remote_fd;
    int reply_fd;
    struct sockaddr_in client;
    struct sockaddr_in target;
    uint16_t connection_id;
    time_t last_seen;
};

static volatile sig_atomic_t keep_running = 1;

static void write_be16(unsigned char *output, uint16_t value)
{
    output[0] = (unsigned char)(value >> 8);
    output[1] = (unsigned char)value;
}

static void write_be32(unsigned char *output, uint32_t value)
{
    output[0] = (unsigned char)(value >> 24);
    output[1] = (unsigned char)(value >> 16);
    output[2] = (unsigned char)(value >> 8);
    output[3] = (unsigned char)value;
}

static uint16_t read_be16(const unsigned char *input)
{
    return (uint16_t)(((uint16_t)input[0] << 8) | input[1]);
}

static uint32_t read_be32(const unsigned char *input)
{
    return ((uint32_t)input[0] << 24) | ((uint32_t)input[1] << 16) |
           ((uint32_t)input[2] << 8) | input[3];
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
    bytes_free(&channel->ticket);
    bytes_free(&channel->client_parameter);
    bytes_free(&channel->server_parameter);
    bytes_free(&channel->strategy);
    memset(channel, 0, sizeof(*channel));
}

static void runtime_free(struct runtime_config *runtime)
{
    if (!runtime)
        return;
    channel_free(&runtime->tcp);
    channel_free(&runtime->udp);
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
    json_object *value = object_member(object, name, json_type_int);

    if (!value || !result)
        return -1;
    *result = json_object_get_int64(value);
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
    if (length > 4096)
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

static int load_channel(json_object *auth, json_object *profile,
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
    struct in_addr ignored;

    if (!protocol)
        protocol = string_member(auth, "proType");
    if (!address)
        address = profile_address;
    if (!address)
        address = string_member(auth, "channelIp");
    if (!protocol || !address || inet_pton(AF_INET, address, &ignored) != 1 ||
        integer_member(auth, "port", &port) != 0 || port < 1 || port > 65535 ||
        (integer_member(auth, "sessionId", &session_id) != 0 &&
         integer_member(auth, "dataChannelSessionId", &session_id) != 0) ||
        session_id < 1 || session_id > UINT32_MAX)
        return -1;
    memset(channel, 0, sizeof(*channel));
    snprintf(channel->protocol, sizeof(channel->protocol), "%s", protocol);
    for (char *cursor = channel->protocol; *cursor; cursor++)
        if (*cursor >= 'a' && *cursor <= 'z')
            *cursor = (char)(*cursor - 'a' + 'A');
    snprintf(channel->address, sizeof(channel->address), "%s", address);
    channel->port = (uint16_t)port;
    channel->session_id = (uint32_t)session_id;
    channel->encrypted = boolean_member(profile, "encryption", false);
    if (copy_optional_bytes(auth, "ticket", "ticketBase64",
                            &channel->ticket) != 0)
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

static int load_runtime(const char *path, struct runtime_config *runtime)
{
    struct bytes contents = {0};
    json_object *root = NULL;
    json_object *channels = NULL;
    size_t index;
    int status = -1;

    memset(runtime, 0, sizeof(*runtime));
    if (read_private_file(path, &contents) != 0)
        return -1;
    root = json_tokener_parse((const char *)contents.data);
    channels = object_member(root, "channels", json_type_array);
    if (!root || !channels || !json_object_array_length(channels)) {
        errno = EINVAL;
        goto out;
    }
    for (index = 0; index < json_object_array_length(channels); index++) {
        json_object *auth = json_object_array_get_idx(channels, index);
        struct bolt_channel *destination;
        const char *protocol = string_member(auth, "protocol");
        json_object *profile = auth;

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
        if (load_channel(auth, profile, destination) != 0)
            goto out;
        destination->encrypted = boolean_member(auth, "encrypted",
                                                  destination->encrypted);
        runtime->has_tcp = runtime->has_tcp || !strcasecmp(protocol, "TCP");
        runtime->has_udp = runtime->has_udp || !strcasecmp(protocol, "UDP");
    }
    if (!runtime->has_tcp && !runtime->has_udp) {
        errno = EINVAL;
        goto out;
    }
    status = 0;

out:
    if (status != 0)
        runtime_free(runtime);
    json_object_put(root);
    bytes_free(&contents);
    return status;
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
    write_be16(output->data + 2, (uint16_t)header_length);
    output->data[4] = command;
    write_be32(output->data + 5, session_id);
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
    write_be16(output->data + 2, (uint16_t)total);
    output->data[4] = BOLT_COMMAND_DATA;
    write_be32(output->data + 5, session_id);
    write_be16(output->data + 9, connection_id);
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
    total_length = read_be16(frame + 2);
    if (header_length < minimum_header || header_length > total_length ||
        total_length != length)
        return -1;
    memset(response, 0, sizeof(*response));
    response->command = frame[4];
    response->session_id = read_be32(frame + 5);
    response->connection_id = read_be16(frame + 9);
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
    struct bolt_extension extension = {6, extension_value, 1};
    struct bolt_response response;
    struct bytes request = {0};
    struct bytes data = {0};
    unsigned char response_frame[13] = {
        BOLT_PROTOCOL_VERSION, 13, 0, 13, BOLT_COMMAND_CONNECT_RESPONSE,
        0x10, 0x20, 0x30, 0x40, 0x00, 0x42, BOLT_STATUS_SUCCESS, 0
    };
    int status = 1;

    if (bolt_encode_request(BOLT_COMMAND_CONNECT_REQUEST, 0x10203040,
                             &extension, 1, &request) != 0 || request.len != 13 ||
        request.data[0] != BOLT_PROTOCOL_VERSION || request.data[1] != 13 ||
        read_be16(request.data + 2) != 13 || request.data[4] != BOLT_COMMAND_CONNECT_REQUEST ||
        read_be32(request.data + 5) != 0x10203040 || request.data[9] != 1 ||
        request.data[10] != 6 || request.data[11] != 1 || request.data[12] != 'x')
        goto out;
    if (bolt_parse_response(response_frame, sizeof(response_frame), &response) != 0 ||
        !bolt_success(&response, BOLT_COMMAND_CONNECT_REQUEST) ||
        response.session_id != 0x10203040 || response.connection_id != 0x42)
        goto out;
    if (bolt_encode_data(0x10203040, 0x42, payload, sizeof(payload), &data) != 0 ||
        data.len != BOLT_DATA_HEADER_LENGTH + sizeof(payload) ||
        bolt_parse_response(data.data, data.len, &response) != 0 ||
        response.command != BOLT_COMMAND_DATA || response.session_id != 0x10203040 ||
        response.connection_id != 0x42 || response.payload_length != sizeof(payload) ||
        memcmp(response.payload, payload, sizeof(payload)) != 0)
        goto out;
    puts("{\"success\":true,\"tests\":[\"bolt-v3-request\",\"bolt-v3-response\",\"bolt-v3-data\"]}");
    status = 0;

out:
    bytes_free(&request);
    bytes_free(&data);
    if (status != 0)
        fputs("biubiu-accd self-test failed\n", stderr);
    return status;
}

static int write_full(int fd, const unsigned char *data, size_t length)
{
    size_t offset = 0;

    while (offset < length) {
        ssize_t written = send(fd, data + offset, length - offset, MSG_NOSIGNAL);

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

static int read_stream_frame(int fd, struct bytes *frame)
{
    unsigned char prefix[4];
    uint16_t total;

    memset(frame, 0, sizeof(*frame));
    if (read_full(fd, prefix, sizeof(prefix)) != 0)
        return -1;
    total = read_be16(prefix + 2);
    if (total < 5)
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

static int set_socket_timeout(int fd, int seconds)
{
    struct timeval timeout = {.tv_sec = seconds, .tv_usec = 0};

    return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0 &&
                   setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0
               ? 0
               : -1;
}

static int connect_channel(const struct bolt_channel *channel, int type)
{
    struct sockaddr_in address = {0};
    int fd;
    int one = 1;

    fd = socket(AF_INET, type, 0);
    if (fd < 0)
        return -1;
    (void)setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
    if (set_socket_timeout(fd, 10) != 0 ||
        inet_pton(AF_INET, channel->address, &address.sin_addr) != 1) {
        close(fd);
        return -1;
    }
    address.sin_family = AF_INET;
    address.sin_port = htons(channel->port);
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static size_t build_extensions(const struct bolt_channel *channel,
                               const struct sockaddr_in *target,
                               struct bolt_extension *extensions,
                               unsigned char endpoint[6],
                               unsigned char encrypted[1])
{
    size_t count = 0;

    memcpy(endpoint, &target->sin_addr, 4);
    memcpy(endpoint + 4, &target->sin_port, 2);
    extensions[count++] = (struct bolt_extension){1, endpoint, 6};
    *encrypted = channel->encrypted ? 1 : 0;
    extensions[count++] = (struct bolt_extension){5, encrypted, 1};
    if (channel->client_parameter.len)
        extensions[count++] = (struct bolt_extension){6,
                                                       channel->client_parameter.data,
                                                       channel->client_parameter.len};
    else if (channel->ticket.len)
        extensions[count++] = (struct bolt_extension){6, channel->ticket.data,
                                                       channel->ticket.len};
    if (channel->server_parameter.len)
        extensions[count++] = (struct bolt_extension){9,
                                                       channel->server_parameter.data,
                                                       channel->server_parameter.len};
    else if (channel->client_parameter.len && channel->ticket.len)
        extensions[count++] = (struct bolt_extension){9, channel->ticket.data,
                                                       channel->ticket.len};
    if (channel->strategy.len)
        extensions[count++] = (struct bolt_extension){10, channel->strategy.data,
                                                       channel->strategy.len};
    return count;
}

static int open_bolt_connection(const struct bolt_channel *channel,
                                const struct sockaddr_in *target, uint8_t command,
                                int socket_type, uint16_t *connection_id)
{
    struct bolt_extension extensions[6];
    unsigned char endpoint[6];
    unsigned char encrypted[1];
    struct bytes request = {0};
    struct bytes response_frame = {0};
    struct bolt_response response;
    int fd;
    int status = -1;

    if (!channel || !target || !connection_id)
        return -1;
    fd = connect_channel(channel, socket_type);
    if (fd < 0)
        return -1;
    if (bolt_encode_request(command, channel->session_id,
                            extensions,
                            build_extensions(channel, target, extensions, endpoint,
                                             encrypted),
                            &request) != 0 || write_full(fd, request.data, request.len) != 0)
        goto out;
    if (socket_type == SOCK_STREAM) {
        if (read_stream_frame(fd, &response_frame) != 0)
            goto out;
    } else {
        response_frame.data = malloc(MAX_FRAME_SIZE);
        if (!response_frame.data)
            goto out;
        response_frame.len = (size_t)recv(fd, response_frame.data, MAX_FRAME_SIZE, 0);
        if ((ssize_t)response_frame.len <= 0) {
            bytes_free(&response_frame);
            goto out;
        }
    }
    if (bolt_parse_response(response_frame.data, response_frame.len, &response) != 0 ||
        !bolt_success(&response, command) ||
        response.session_id != channel->session_id) {
        fprintf(stderr, "Bolt %s handshake rejected\n",
                command == BOLT_COMMAND_CONNECT_REQUEST ? "TCP" : "UDP");
        goto out;
    }
    *connection_id = response.connection_id;
    status = 0;

out:
    bytes_free(&request);
    bytes_free(&response_frame);
    if (status != 0)
        close(fd);
    return status == 0 ? fd : -1;
}

static int get_original_destination(int fd, struct sockaddr_in *destination)
{
    socklen_t length = sizeof(*destination);

    memset(destination, 0, sizeof(*destination));
    if (getsockopt(fd, SOL_IP, SO_ORIGINAL_DST, destination, &length) != 0 ||
        length != sizeof(*destination) || destination->sin_family != AF_INET ||
        destination->sin_port == 0)
        return -1;
    return 0;
}

static void *tcp_worker_main(void *argument)
{
    struct tcp_worker *worker = argument;
    struct sockaddr_in target;
    struct bytes outbound = {0};
    struct bytes inbound = {0};
    struct bolt_response response;
    unsigned char buffer[16384];
    uint16_t connection_id = 0;
    int remote_fd = -1;
    int result;

    if (get_original_destination(worker->client_fd, &target) != 0 ||
        !worker->runtime->has_tcp) {
        fprintf(stderr, "biubiu-accd: TCP original destination unavailable\n");
        goto out;
    }
    remote_fd = open_bolt_connection(&worker->runtime->tcp, &target,
                                     BOLT_COMMAND_CONNECT_REQUEST, SOCK_STREAM,
                                     &connection_id);
    if (remote_fd < 0)
        goto out;
    while (keep_running) {
        struct pollfd fds[2] = {
            {.fd = worker->client_fd, .events = POLLIN},
            {.fd = remote_fd, .events = POLLIN},
        };

        result = poll(fds, 2, 1000);
        if (result < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (result == 0)
            continue;
        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL))
            break;
        if (fds[0].revents & POLLIN) {
            ssize_t count = recv(worker->client_fd, buffer, sizeof(buffer), 0);

            if (count <= 0 || bolt_encode_data(worker->runtime->tcp.session_id,
                                               connection_id, buffer, (size_t)count,
                                               &outbound) != 0 ||
                write_full(remote_fd, outbound.data, outbound.len) != 0)
                break;
            bytes_free(&outbound);
        }
        if (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL))
            break;
        if (fds[1].revents & POLLIN) {
            bytes_free(&inbound);
            if (read_stream_frame(remote_fd, &inbound) != 0 ||
                bolt_parse_response(inbound.data, inbound.len, &response) != 0 ||
                response.session_id != worker->runtime->tcp.session_id)
                break;
            if (response.command != BOLT_COMMAND_DATA ||
                (response.connection_id != connection_id && response.connection_id != 0))
                break;
            if (response.payload_length &&
                write_full(worker->client_fd, response.payload, response.payload_length) != 0)
                break;
        }
    }

out:
    bytes_free(&outbound);
    bytes_free(&inbound);
    if (remote_fd >= 0)
        close(remote_fd);
    close(worker->client_fd);
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
    if (flow->remote_fd >= 0)
        close(flow->remote_fd);
    if (flow->reply_fd >= 0)
        close(flow->reply_fd);
    memset(flow, 0, sizeof(*flow));
    flow->remote_fd = -1;
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
    uint16_t connection_id;
    int reply_fd;
    int remote_fd;

    for (index = 0; index < MAX_UDP_FLOWS; index++)
        if (!flows[index].active)
            break;
    if (index == MAX_UDP_FLOWS)
        return -1;
    remote_fd = open_bolt_connection(&runtime->udp, target,
                                     BOLT_COMMAND_ASSOCIATE_REQUEST, SOCK_DGRAM,
                                     &connection_id);
    if (remote_fd < 0)
        return -1;
    reply_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (reply_fd < 0) {
        close(remote_fd);
        return -1;
    }
    {
        int one = 1;

        if (setsockopt(reply_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) != 0 ||
            setsockopt(reply_fd, SOL_IP, IP_TRANSPARENT, &one, sizeof(one)) != 0 ||
            bind(reply_fd, (const struct sockaddr *)target, sizeof(*target)) != 0) {
            close(reply_fd);
            close(remote_fd);
            return -1;
        }
    }
    flows[index].active = true;
    flows[index].remote_fd = remote_fd;
    flows[index].reply_fd = reply_fd;
    flows[index].client = *client;
    flows[index].target = *target;
    flows[index].connection_id = connection_id;
    flows[index].last_seen = time(NULL);
    return (int)index;
}

static void *udp_loop_main(void *argument)
{
    struct udp_context *context = argument;
    struct udp_flow flows[MAX_UDP_FLOWS];
    unsigned char buffer[MAX_FRAME_SIZE];
    unsigned char control[CMSG_SPACE(sizeof(struct sockaddr_in))];
    size_t index;

    memset(flows, 0, sizeof(flows));
    for (index = 0; index < MAX_UDP_FLOWS; index++) {
        flows[index].remote_fd = -1;
        flows[index].reply_fd = -1;
    }
    while (keep_running) {
        struct pollfd pollfds[MAX_UDP_FLOWS + 1];
        int flow_indexes[MAX_UDP_FLOWS + 1];
        size_t count = 1;
        int result;

        pollfds[0] = (struct pollfd){.fd = context->listener, .events = POLLIN};
        flow_indexes[0] = -1;
        for (index = 0; index < MAX_UDP_FLOWS; index++) {
            if (!flows[index].active)
                continue;
            pollfds[count] = (struct pollfd){.fd = flows[index].remote_fd,
                                             .events = POLLIN};
            flow_indexes[count++] = (int)index;
        }
        result = poll(pollfds, count, 1000);
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
            struct bytes outbound = {0};

            if (received <= 0 || parse_original_destination_message(&message, &target) != 0)
                continue;
            flow_index = find_udp_flow(flows, &client, &target);
            if (flow_index < 0)
                flow_index = allocate_udp_flow(flows, context->runtime, &client, &target);
            if (flow_index < 0)
                continue;
            flows[flow_index].last_seen = time(NULL);
            if (bolt_encode_data(context->runtime->udp.session_id,
                                 flows[flow_index].connection_id, buffer,
                                 (size_t)received, &outbound) == 0) {
                (void)send(flows[flow_index].remote_fd, outbound.data, outbound.len,
                           MSG_NOSIGNAL);
            }
            bytes_free(&outbound);
        }
        for (index = 1; index < count; index++) {
            struct bolt_response response;
            ssize_t received;
            int flow_index = flow_indexes[index];

            if (!(pollfds[index].revents & POLLIN))
                continue;
            received = recv(flows[flow_index].remote_fd, buffer, sizeof(buffer), 0);
            if (received <= 0 ||
                bolt_parse_response(buffer, (size_t)received, &response) != 0 ||
                response.command != BOLT_COMMAND_DATA ||
                response.session_id != context->runtime->udp.session_id ||
                (response.connection_id != flows[flow_index].connection_id &&
                 response.connection_id != 0)) {
                close_udp_flow(&flows[flow_index]);
                continue;
            }
            if (response.payload_length)
                (void)sendto(flows[flow_index].reply_fd, response.payload,
                             response.payload_length, 0,
                             (struct sockaddr *)&flows[flow_index].client,
                             sizeof(flows[flow_index].client));
            flows[flow_index].last_seen = time(NULL);
        }
    }
    for (index = 0; index < MAX_UDP_FLOWS; index++)
        close_udp_flow(&flows[index]);
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
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);
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
            if (pthread_create(&worker_thread, NULL, tcp_worker_main, worker) != 0) {
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
    runtime_free(&runtime);
    return 0;
}
