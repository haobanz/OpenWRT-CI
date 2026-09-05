#define _GNU_SOURCE

#include <arpa/inet.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define BIUBIU_ACC_VERSION "0.10.1"
#define LOGIN_ORIGIN "https://member-login.biubiu001.com/"
#define ACCELERATION_HOST "gtm-main.biubiu001.com"
#define ACCELERATION_ORIGIN "https://" ACCELERATION_HOST
#define PC_ACCELERATION_HOST "sz-maga.biubiu001.com"
#define PC_ACCELERATION_ORIGIN "https://" PC_ACCELERATION_HOST
#define PC_SIGNAL_HOST "signal-sp.biubiu001.com"
/* The native heartbeat/signal transport has its own plaintext origin. */
#define PC_SIGNAL_ORIGIN "http://" PC_SIGNAL_HOST
#define MAGA_APP_ID "25344054"
#define MAGA_PACKAGE_NAME "com.njh.biubiu"
#define MAGA_APP_VERSION "5.4.1"
#define MAGA_APP_VERSION_CODE "50401"
#define MAGA_USER_AGENT "MASO-ADAT-SDK/2.1.0"
#define PC_APP_ID "biubiu"
#define PC_APP_NAME "biubiu"
/* Match the native fallback's classic Bolt capability contract. Newer
 * versions select BoltNext UDP endpoints with a different wire protocol. */
#define PC_APP_VERSION "1.0.0.0"
#define PC_APP_VERSION_CODE "1000000"
#define PC_BUILD_ID ""
#define PC_CHANNEL "BPC_1"
#define PC_OS_VERSION "10.0.19045"
#define SIGNAL_ENGINE_VERSION "1.0.0.0"
#define PC_HTTPDNS_ACCOUNT_ID "108842"
#define PC_HTTPDNS_SIGNATURE_LIFETIME 72000LL
#define PC_HTTPDNS_MAX_IPV4 16U
#define PC_HTTPDNS_MAX_RESPONSE_SIZE (64U * 1024U)
#define NATIVE_SEED_KEY_VERSION 1
#define SIGNAL_AES_KEY "pacqlWmtikF4ppge"
#define SIGNAL_AES_IV "qws871Bz23opl9x8"
#define MAX_RESPONSE_SIZE (4U * 1024U * 1024U)
#define MAX_STATE_SIZE (1024U * 1024U)
#define MAX_COOKIE_HEADER_SIZE 8192U
#ifndef DEFAULT_DEVICE_ID_FILE
#define DEFAULT_DEVICE_ID_FILE "/etc/biubiu-acc/device-id"
#endif
#ifndef DEFAULT_PC_DEVICE_ID_FILE
#define DEFAULT_PC_DEVICE_ID_FILE "/etc/biubiu-acc/pc-utdid"
#endif
#ifndef DEFAULT_SESSION_FILE
#define DEFAULT_SESSION_FILE "/etc/biubiu-acc/session.json"
#endif
#ifndef DEFAULT_ACCELERATION_KEY_FILE
#define DEFAULT_ACCELERATION_KEY_FILE "/etc/biubiu-acc/acceleration-key.json"
#endif
#ifndef DEFAULT_GAME_LIST_FILE
#define DEFAULT_GAME_LIST_FILE "/etc/biubiu-acc/game-list.json"
#endif
#ifndef DEFAULT_PC_GAME_LIST_FILE
#define DEFAULT_PC_GAME_LIST_FILE "/etc/biubiu-acc/pc-game-list.json"
#endif
#ifndef DEFAULT_PC_GAME_PROFILE_FILE
#define DEFAULT_PC_GAME_PROFILE_FILE "/etc/biubiu-acc/pc-game-profile.json"
#endif
#ifndef DEFAULT_PC_GAME_MAP_FILE
#define DEFAULT_PC_GAME_MAP_FILE "/etc/biubiu-acc/pc-game-map.json"
#endif
#ifndef DEFAULT_PC_ENTITLEMENT_FILE
#define DEFAULT_PC_ENTITLEMENT_FILE "/etc/biubiu-acc/pc-entitlement.json"
#endif
#ifndef DEFAULT_PC_PROFILE_FILE
#define DEFAULT_PC_PROFILE_FILE "/etc/biubiu-acc/pc-profile.json"
#endif
#ifndef DEFAULT_PC_AUTHORIZATION_FILE
#define DEFAULT_PC_AUTHORIZATION_FILE "/etc/biubiu-acc/pc-authorization.json"
#endif
#ifndef DEFAULT_PC_CHANNEL_TICKET_FILE
#define DEFAULT_PC_CHANNEL_TICKET_FILE "/etc/biubiu-acc/pc-channel-ticket.json"
#endif
#ifndef DEFAULT_PC_RUNTIME_FILE
#define DEFAULT_PC_RUNTIME_FILE "/etc/biubiu-acc/pc-runtime.json"
#endif
#ifndef DEFAULT_PC_CONTEXT_FILE
#define DEFAULT_PC_CONTEXT_FILE "/etc/biubiu-acc/pc-context.json"
#endif
#ifndef DEFAULT_PC_USER_FILE
#define DEFAULT_PC_USER_FILE "/etc/biubiu-acc/pc-user.json"
#endif
#ifndef DEFAULT_SERVICE_CONFIG_FILE
#define DEFAULT_SERVICE_CONFIG_FILE "/etc/biubiu-acc/service-config.json"
#endif
#ifndef DEFAULT_ENTITLEMENT_FILE
#define DEFAULT_ENTITLEMENT_FILE "/etc/biubiu-acc/entitlement.json"
#endif
#ifndef DEFAULT_PROFILE_FILE
#define DEFAULT_PROFILE_FILE "/etc/biubiu-acc/profile.json"
#endif
#ifndef DEFAULT_AUTHORIZATION_FILE
#define DEFAULT_AUTHORIZATION_FILE "/etc/biubiu-acc/authorization.json"
#endif
#ifndef DEFAULT_CHANNEL_TICKET_FILE
#define DEFAULT_CHANNEL_TICKET_FILE "/etc/biubiu-acc/channel-ticket.json"
#endif
#ifndef DEFAULT_RUNTIME_FILE
#define DEFAULT_RUNTIME_FILE "/etc/biubiu-acc/runtime.json"
#endif

#define BOLT_PROTOCOL_VERSION 3U
#define BOLT_DATA_HEADER_LENGTH 11U
#define BOLT_COMMAND_DATA 0x11U
#define BOLT_COMMAND_CONNECT_REQUEST 0x22U
#define BOLT_COMMAND_CONNECT_RESPONSE 0x23U
#define BOLT_COMMAND_ASSOCIATE_REQUEST 0x24U
#define BOLT_COMMAND_ASSOCIATE_RESPONSE 0x25U
#define BOLT_COMMAND_ERROR 0x27U
#define BOLT_STATUS_SUCCESS 0x22U

#define GAME_LIST_ENDPOINT \
    "/api/ping-server.game.ns.gameListV2?df=adat&ver=1.0.0"
#define PC_GAME_LIST_ENDPOINT \
    "/api/ping-server.game.pc.gameList?ver=1.0.1"
#define PC_GAME_SEARCH_ENDPOINT \
    "/api/ping-feed.search.game.pc?ver=1.0.1"
#define PC_GAME_PROFILE_ENDPOINT \
    "/api/ping-server.game.pc.getGameProfile?ver=1.0.0"
#define PC_GAME_MAP_ENDPOINT \
    "/api/ping-server.game.pc.map?ver=1.0.0"
#define PC_USER_INFO_ENDPOINT \
    "/api/ping-account.user.base.getPcUserInfoById?ver=1.0.0"
#define PC_CHECK_SPEEDUP_ENDPOINT \
    "/api/ping-server.biuvpn.game.checkSpeedup?ver=1.0.0"
#define PC_SPEEDUP_CONFIG_ENDPOINT \
    "/api/ping-server.biuvpn.game.getPCSpeedupConfig?ver=1.0.0"
#define SERVICE_CONFIG_ENDPOINT \
    "/api/ping-server.config.base.list?ver=1.0.0"
#define SEARCH_GAME_ENDPOINT \
    "/api/ping-server.game.ns.searchGame?df=adat&ver=1.0.0"
#define CHECK_SPEEDUP_ENDPOINT \
    "/api/ping-server.biuvpn.game.checkSpeedup?df=adat&ver=1.0.0"
#define SPEEDUP_CONFIG_ENDPOINT \
    "/api/ping-server.biuvpn.game.getSpeedupConfig?df=adat&ver=1.0.1"
#define SIGNAL_LOGIN_ENDPOINT \
    "/api/ping-signal.open.login.loginV2?ver=1.0.0&df=adat"
#define CHANNEL_TICKET_ENDPOINT \
    "/api/ping-signal.open.auth.getChannelStV2?ver=1.0.0&df=adat"

static const char public_key_pem[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDCjYYIy9Are9QPRDOVug4e6Fdz\n"
    "8HK2HGyajKR4N8Wb/bB9gwXnieXqj4Mya0nLd6nBcBPN6qUJ0R7p5Cv6aPqQsc7\n"
    "pWfAxPr41GvcOlGixLtpLHLUH9m0093YEBhu4F7pKu0TZTQIPZINWUa1SLjQD/bc\n"
    "BlcaQyWbk6qJhSJFYkwIDAQAB\n"
    "-----END PUBLIC KEY-----\n";

static const char native_seed_public_key_der_b64[] =
    "MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQC+vuIy8iKq1ofkA9o+xVW/oGti"
    "m02dONq7XVyP7dn1J4ymiu7ueKNEG1OKMEmy50pN4hb81SUWHBiGgfdw9aZYxOV/"
    "Vng/HUwXTBZXsg5ILjlaqekKLAWn3ry3lnmiiEfy47XmN1u8VCqAhQ29b2jaDqMpD"
    "PJceNeDcvWAJ9TXQQIDAQAB";

struct bytes {
    unsigned char *data;
    size_t len;
};

struct response_buffer {
    char *data;
    size_t len;
    bool too_large;
};

struct pc_httpdns_ipv4_list {
    char values[PC_HTTPDNS_MAX_IPV4][INET_ADDRSTRLEN];
    size_t count;
};

static const char *const pc_httpdns_bootstrap_ips[] = {
    "203.107.1.1",
    "203.107.1.33",
    "203.107.1.34",
    "203.107.1.35",
};

/* Public protocol material embedded by the official Windows resolver. */
static const char pc_httpdns_signing_key[] =
    "cf595473e698e2fef63dfb80cec5748d";

struct adat_session_keys {
    unsigned char key[16];
    unsigned char iv[16];
};

struct acceleration_public_key {
    int version;
    EVP_PKEY *key;
    char *der_b64;
};

struct bolt_extension {
    uint8_t type;
    const unsigned char *value;
    size_t length;
};

struct bolt_response {
    uint8_t command;
    uint8_t flags;
    uint32_t session_id;
    uint16_t connection_id;
    uint8_t status;
    bool has_status;
    const unsigned char *payload;
    size_t payload_length;
};

struct pc_scout_endpoint {
    const char *id;
    struct sockaddr_in address;
    int *samples;
    size_t sample_count;
};

struct pc_scout_config {
    uint16_t port;
    unsigned int detect_rounds;
    unsigned int loss_threshold_ms;
    unsigned int round_sleep_ms;
    unsigned int batch_count;
    unsigned int batch_sleep_ms;
    unsigned int discard_head_rounds;
};

struct pc_scout_probe {
    struct pc_scout_endpoint *endpoint;
    uint32_t token;
    unsigned int round;
    int64_t sent_at;
    bool sent;
    bool received;
};

enum acceleration_client_kind {
    ACCELERATION_CLIENT_MOBILE = 0,
    ACCELERATION_CLIENT_WINDOWS = 1,
};

static char *new_request_id(void);
static char *new_uuid(void);
static char *new_native_trace_id(void);
static char *new_native_app_session(void);
static char *new_native_speedup_session(void);
static char *new_native_utdid(void);
static bool valid_native_utdid(const char *value);
static int base64_decode(const char *input, struct bytes *output);
static json_object *build_maga_client(const char *device_id);
static json_object *build_acceleration_client(const char *device_id,
                                              const char *biubiu_id,
                                              const char *service_ticket,
                                              enum acceleration_client_kind kind);
static json_object *build_acceleration_client_header(json_object *client,
                                                     enum acceleration_client_kind kind);
static int apply_pc_acceleration_context(json_object *client);
static int load_pc_signal_context(char **acc_pod_id, uint64_t *server_id);
static int load_pc_biubiu_id(const char *path, char **result);
static int load_private_json(const char *path, json_object **result);
static void remove_private_file_if_safe(const char *path);
static size_t response_write(void *contents, size_t size, size_t count,
                             void *userdata);
static void cleanse_json_value(json_object *value);
static bool acceleration_response_success(json_object *response);

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

static bool bolt_request_command(uint8_t command)
{
    return command == BOLT_COMMAND_CONNECT_REQUEST ||
           command == BOLT_COMMAND_ASSOCIATE_REQUEST;
}

static bool bolt_status_command(uint8_t command)
{
    return command == BOLT_COMMAND_CONNECT_RESPONSE ||
           command == BOLT_COMMAND_ASSOCIATE_RESPONSE ||
           command == BOLT_COMMAND_ERROR;
}

static int bolt_encode_request(uint8_t command, uint32_t session_id,
                               const struct bolt_extension *extensions,
                               size_t extension_count,
                               const unsigned char *payload,
                               size_t payload_length, struct bytes *output)
{
    size_t header_length = 10;
    size_t total_length;
    size_t cursor;
    size_t i;

    if (!output || !bolt_request_command(command) || extension_count > UINT8_MAX ||
        (extension_count && !extensions) || (payload_length && !payload))
        return -1;
    memset(output, 0, sizeof(*output));
    for (i = 0; i < extension_count; i++) {
        if (extensions[i].length > UINT8_MAX ||
            (extensions[i].length && !extensions[i].value) ||
            header_length + 2U + extensions[i].length > UINT8_MAX)
            return -1;
        header_length += 2U + extensions[i].length;
    }
    if (payload_length > UINT16_MAX - header_length)
        return -1;
    total_length = header_length + payload_length;
    output->data = malloc(total_length);
    if (!output->data)
        return -1;
    output->len = total_length;
    output->data[0] = BOLT_PROTOCOL_VERSION;
    output->data[1] = (unsigned char)header_length;
    write_le16(output->data + 2, (uint16_t)total_length);
    output->data[4] = command;
    write_le32(output->data + 5, session_id);
    output->data[9] = (unsigned char)extension_count;
    cursor = 10;
    for (i = 0; i < extension_count; i++) {
        output->data[cursor++] = extensions[i].type;
        output->data[cursor++] = (unsigned char)extensions[i].length;
        if (extensions[i].length) {
            memcpy(output->data + cursor, extensions[i].value,
                   extensions[i].length);
            cursor += extensions[i].length;
        }
    }
    if (payload_length)
        memcpy(output->data + header_length, payload, payload_length);
    return 0;
}

static int bolt_encode_data(uint32_t session_id, uint16_t connection_id,
                            const unsigned char *payload, size_t payload_length,
                            struct bytes *output)
{
    size_t total_length = BOLT_DATA_HEADER_LENGTH + payload_length;

    if (!output || (payload_length && !payload) ||
        payload_length > UINT16_MAX - BOLT_DATA_HEADER_LENGTH)
        return -1;
    memset(output, 0, sizeof(*output));
    output->data = malloc(total_length);
    if (!output->data)
        return -1;
    output->len = total_length;
    output->data[0] = BOLT_PROTOCOL_VERSION;
    output->data[1] = BOLT_DATA_HEADER_LENGTH;
    write_le16(output->data + 2, (uint16_t)total_length);
    output->data[4] = BOLT_COMMAND_DATA;
    write_le32(output->data + 5, session_id);
    write_le16(output->data + 9, connection_id);
    if (payload_length)
        memcpy(output->data + BOLT_DATA_HEADER_LENGTH, payload, payload_length);
    return 0;
}

static int bolt_parse_response(const unsigned char *frame, size_t frame_length,
                               struct bolt_response *response)
{
    size_t minimum_header;
    size_t header_length;
    size_t total_length;
    size_t cursor;
    size_t i;
    uint8_t extension_count;

    if (!frame || !response || frame_length < 5 ||
        (frame[0] & 0x0fU) != BOLT_PROTOCOL_VERSION)
        return -1;
    memset(response, 0, sizeof(*response));
    minimum_header = frame[4] == BOLT_COMMAND_DATA ? BOLT_DATA_HEADER_LENGTH : 12U;
    if (frame_length < minimum_header)
        return -1;
    header_length = frame[1];
    total_length = read_le16(frame + 2);
    if (header_length < minimum_header || header_length > total_length ||
        total_length != frame_length)
        return -1;
    response->flags = frame[0] >> 4;
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
    if (bolt_status_command(response->command)) {
        if (cursor >= header_length)
            return -1;
        response->status = frame[cursor++];
        response->has_status = true;
    }
    if (cursor >= header_length)
        return -1;
    extension_count = frame[cursor++];
    for (i = 0; i < extension_count; i++) {
        size_t extension_length;

        if (cursor + 2U > header_length)
            return -1;
        extension_length = frame[cursor + 1];
        cursor += 2U;
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

static bool bolt_response_successful_for(const struct bolt_response *response,
                                         uint8_t request_command)
{
    uint8_t expected;

    if (!response || !bolt_request_command(request_command))
        return false;
    expected = request_command == BOLT_COMMAND_CONNECT_REQUEST
                   ? BOLT_COMMAND_CONNECT_RESPONSE
                   : BOLT_COMMAND_ASSOCIATE_RESPONSE;
    return response->command == expected && response->has_status &&
           response->status == BOLT_STATUS_SUCCESS && response->connection_id != 0;
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

static char *base64_encode(const unsigned char *input, size_t input_len)
{
    size_t output_len;
    char *output;

    if (input_len > (size_t)INT32_MAX)
        return NULL;
    output_len = 4 * ((input_len + 2) / 3);
    output = calloc(output_len + 1, 1);
    if (!output)
        return NULL;
    if (EVP_EncodeBlock((unsigned char *)output, input, (int)input_len) < 0) {
        free(output);
        return NULL;
    }
    return output;
}

static void write_be32(unsigned char *output, uint32_t value)
{
    output[0] = (unsigned char)(value >> 24);
    output[1] = (unsigned char)(value >> 16);
    output[2] = (unsigned char)(value >> 8);
    output[3] = (unsigned char)value;
}

static uint32_t read_be32(const unsigned char *input)
{
    return ((uint32_t)input[0] << 24) | ((uint32_t)input[1] << 16) |
           ((uint32_t)input[2] << 8) | (uint32_t)input[3];
}

static uint32_t native_java_hash(const unsigned char *value, size_t length)
{
    uint32_t hash = 0;
    size_t index;

    for (index = 0; index < length; index++)
        hash = hash * 31U + (uint32_t)(int32_t)(int8_t)value[index];
    return hash;
}

static uint32_t native_utdid_fingerprint_hash(void)
{
    static const char *const paths[] = {
        "/sys/class/net/br-lan/address",
        "/sys/class/net/eth0/address",
        "/sys/class/net/wan/address",
        "/sys/class/net/lan1/address",
    };
    static const unsigned char rc4_key[] =
        "QrMgt8GGYI6T52ZY5AnhtxkLzb8egpFn3j5JELI8H6wtACbUnZ5cc3aYTsTR"
        "bmkAkRJeYbtx92LPBWm7nBO9UIl7y5i5MQNmUZNf5QENurR5tGyo7yJ2G0MB"
        "jWvy6iAtlAbacKP0SwOUeUWx5dsBdyhxa7Id1APtybSdDgicBDuNjI0mlZFUzZ"
        "SS9dmN8lBD0WTVOMz0pRZbR3cysomRXOO1ghqjJdTcyDIxzpNAEszN8RMGjrzy"
        "U7Hjbmwi6YNK";
    unsigned char state[256];
    unsigned char fingerprint[12];
    char line[64];
    size_t path_index;
    size_t index;
    size_t length = 0;
    unsigned int first = 0;
    unsigned int second = 0;

    for (path_index = 0;
         path_index < sizeof(paths) / sizeof(paths[0]) && length != 12;
         path_index++) {
        FILE *file = fopen(paths[path_index], "r");

        if (!file)
            continue;
        if (fgets(line, sizeof(line), file)) {
            length = 0;
            for (index = 0; line[index] && length < sizeof(fingerprint); index++) {
                if (isxdigit((unsigned char)line[index]))
                    fingerprint[length++] =
                        (unsigned char)tolower((unsigned char)line[index]);
                else if (line[index] != ':' && line[index] != '-' &&
                         line[index] != '\r' && line[index] != '\n') {
                    length = 0;
                    break;
                }
            }
        }
        fclose(file);
    }
    if (length != sizeof(fingerprint))
        return 0;

    for (index = 0; index < sizeof(state); index++)
        state[index] = (unsigned char)index;
    for (index = 0; index < sizeof(state); index++) {
        unsigned char swap;

        second = (second + state[index] +
                  rc4_key[index % (sizeof(rc4_key) - 1)]) & 0xffU;
        swap = state[index];
        state[index] = state[second];
        state[second] = swap;
    }
    second = 0;
    for (index = 0; index < sizeof(fingerprint); index++) {
        unsigned char swap;

        first = (first + 1U) & 0xffU;
        second = (second + state[first]) & 0xffU;
        swap = state[first];
        state[first] = state[second];
        state[second] = swap;
        fingerprint[index] ^=
            state[(state[first] + state[second]) & 0xffU];
    }
    second = native_java_hash(fingerprint, sizeof(fingerprint));
    OPENSSL_cleanse(state, sizeof(state));
    OPENSSL_cleanse(fingerprint, sizeof(fingerprint));
    OPENSSL_cleanse(line, sizeof(line));
    return second;
}

static int native_utdid_checksum(const unsigned char raw[18], uint32_t *result)
{
    static const unsigned char hmac_key[] =
        "d6fc3a4a06adbde89223bvefedc24fecde188aaa9161";
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_length = 0;
    char *encoded = NULL;
    int status = -1;

    if (!raw || !result ||
        !HMAC(EVP_sha1(), hmac_key, (int)(sizeof(hmac_key) - 1), raw, 14,
              digest, &digest_length) || digest_length != 20)
        goto out;
    encoded = base64_encode(digest, digest_length);
    if (!encoded)
        goto out;
    *result = native_java_hash((const unsigned char *)encoded, strlen(encoded));
    status = 0;

out:
    OPENSSL_cleanse(digest, sizeof(digest));
    if (encoded)
        OPENSSL_cleanse(encoded, strlen(encoded));
    free(encoded);
    return status;
}

static bool valid_native_utdid(const char *value)
{
    struct bytes decoded = {0};
    uint32_t checksum = 0;
    bool valid = false;

    if (!value || strlen(value) != 24 || base64_decode(value, &decoded) != 0)
        return false;
    valid = decoded.len == 18 && decoded.data[8] == 3 && decoded.data[9] == 0 &&
            native_utdid_checksum(decoded.data, &checksum) == 0 &&
            read_be32(decoded.data + 14) == checksum;
    bytes_free(&decoded);
    return valid;
}

static char *new_native_utdid(void)
{
    unsigned char raw[18] = {0};
    uint32_t checksum;
    time_t now = time(NULL);
    char *encoded = NULL;

    if (now <= 0 || (uint64_t)now > UINT32_MAX ||
        RAND_bytes(raw + 4, 4) != 1)
        goto out;
    write_be32(raw, (uint32_t)now);
    raw[8] = 3;
    raw[9] = 0;
    write_be32(raw + 10, native_utdid_fingerprint_hash());
    if (native_utdid_checksum(raw, &checksum) != 0)
        goto out;
    write_be32(raw + 14, checksum);
    encoded = base64_encode(raw, sizeof(raw));
    if (!valid_native_utdid(encoded)) {
        free(encoded);
        encoded = NULL;
    }

out:
    OPENSSL_cleanse(raw, sizeof(raw));
    return encoded;
}

static int base64_decode(const char *input, struct bytes *output)
{
    size_t input_len;
    size_t padding = 0;
    int decoded_len;

    memset(output, 0, sizeof(*output));
    input_len = strlen(input);
    if (input_len == 0 || input_len % 4 != 0 || input_len > (size_t)INT32_MAX)
        return -1;
    output->data = malloc((input_len / 4) * 3 + 1);
    if (!output->data)
        return -1;
    decoded_len = EVP_DecodeBlock(output->data, (const unsigned char *)input,
                                  (int)input_len);
    if (decoded_len < 0) {
        bytes_free(output);
        return -1;
    }
    if (input_len >= 1 && input[input_len - 1] == '=')
        padding++;
    if (input_len >= 2 && input[input_len - 2] == '=')
        padding++;
    output->len = (size_t)decoded_len - padding;
    output->data[output->len] = '\0';
    return 0;
}

static void acceleration_public_key_free(struct acceleration_public_key *value)
{
    if (!value)
        return;
    EVP_PKEY_free(value->key);
    free(value->der_b64);
    memset(value, 0, sizeof(*value));
}

static int decode_acceleration_public_key(const char *encoded, EVP_PKEY **result)
{
    struct bytes der = {0};
    const unsigned char *cursor;
    EVP_PKEY *key = NULL;
    int status = -1;

    if (!encoded || !result)
        return -1;
    *result = NULL;
    if (base64_decode(encoded, &der) != 0 || !der.len)
        goto out;
    cursor = der.data;
    key = d2i_PUBKEY(NULL, &cursor, (long)der.len);
    if (!key || cursor != der.data + der.len ||
        EVP_PKEY_base_id(key) != EVP_PKEY_RSA || EVP_PKEY_bits(key) < 1024 ||
        EVP_PKEY_bits(key) > 8192)
        goto out;
    *result = key;
    key = NULL;
    status = 0;

out:
    EVP_PKEY_free(key);
    bytes_free(&der);
    return status;
}

static int parse_key_version(const char *value, size_t length, int *version)
{
    uint64_t parsed = 0;
    size_t i;

    if (!value || !length || length > 10 || !version)
        return -1;
    for (i = 0; i < length; i++) {
        if (value[i] < '0' || value[i] > '9')
            return -1;
        parsed = parsed * 10U + (unsigned int)(value[i] - '0');
        if (parsed > INT32_MAX)
            return -1;
    }
    if (!parsed)
        return -1;
    *version = (int)parsed;
    return 0;
}

static int parse_security_key_value(const char *value,
                                    struct acceleration_public_key *result)
{
    const char *separator;
    const char *encoded;
    EVP_PKEY *key = NULL;
    char *encoded_copy = NULL;
    int version;

    if (!value || !result)
        return -1;
    memset(result, 0, sizeof(*result));
    separator = strchr(value, '|');
    if (!separator || strchr(separator + 1, '|') ||
        parse_key_version(value, (size_t)(separator - value), &version) != 0)
        return -1;
    encoded = separator + 1;
    if (!encoded[0] || decode_acceleration_public_key(encoded, &key) != 0)
        return -1;
    encoded_copy = strdup(encoded);
    if (!encoded_copy) {
        EVP_PKEY_free(key);
        return -1;
    }
    result->version = version;
    result->key = key;
    result->der_b64 = encoded_copy;
    return 0;
}

static int random_session_key(unsigned char key[16])
{
    static const unsigned char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    unsigned char random_data[16];
    size_t i;

    if (RAND_bytes(random_data, sizeof(random_data)) != 1)
        return -1;
    for (i = 0; i < sizeof(random_data); i++)
        key[i] = alphabet[random_data[i] % (sizeof(alphabet) - 1)];
    OPENSSL_cleanse(random_data, sizeof(random_data));
    return 0;
}

static int aes_encrypt(const unsigned char key[16], const unsigned char *plaintext,
                       size_t plaintext_len, struct bytes *ciphertext)
{
    EVP_CIPHER_CTX *ctx = NULL;
    unsigned char *padded = NULL;
    size_t pad_count;
    size_t padded_len;
    int output_len = 0;
    int final_len = 0;
    int result = -1;

    memset(ciphertext, 0, sizeof(*ciphertext));
    pad_count = 16 - ((plaintext_len + 1) % 16);
    padded_len = plaintext_len + 1 + pad_count;
    if (padded_len > (size_t)INT32_MAX)
        return -1;
    padded = malloc(padded_len);
    ciphertext->data = malloc(padded_len + EVP_MAX_BLOCK_LENGTH);
    if (!padded || !ciphertext->data)
        goto out;
    memcpy(padded, plaintext, plaintext_len);
    padded[plaintext_len] = 0x0a;
    memset(padded + plaintext_len + 1, (int)pad_count, pad_count);

    ctx = EVP_CIPHER_CTX_new();
    if (!ctx || EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, key) != 1 ||
        EVP_CIPHER_CTX_set_padding(ctx, 0) != 1 ||
        EVP_EncryptUpdate(ctx, ciphertext->data, &output_len, padded,
                          (int)padded_len) != 1 ||
        EVP_EncryptFinal_ex(ctx, ciphertext->data + output_len, &final_len) != 1)
        goto out;
    ciphertext->len = (size_t)(output_len + final_len);
    result = 0;

out:
    if (result != 0)
        bytes_free(ciphertext);
    if (padded) {
        OPENSSL_cleanse(padded, padded_len);
        free(padded);
    }
    EVP_CIPHER_CTX_free(ctx);
    return result;
}

static int aes_decrypt(const unsigned char key[16], const unsigned char *ciphertext,
                       size_t ciphertext_len, struct bytes *plaintext)
{
    EVP_CIPHER_CTX *ctx = NULL;
    int output_len = 0;
    int final_len = 0;
    size_t pad_count;
    size_t i;
    int result = -1;

    memset(plaintext, 0, sizeof(*plaintext));
    if (!ciphertext_len || ciphertext_len % 16 || ciphertext_len > (size_t)INT32_MAX)
        return -1;
    plaintext->data = malloc(ciphertext_len + 1);
    if (!plaintext->data)
        return -1;
    ctx = EVP_CIPHER_CTX_new();
    if (!ctx || EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, key) != 1 ||
        EVP_CIPHER_CTX_set_padding(ctx, 0) != 1 ||
        EVP_DecryptUpdate(ctx, plaintext->data, &output_len, ciphertext,
                          (int)ciphertext_len) != 1 ||
        EVP_DecryptFinal_ex(ctx, plaintext->data + output_len, &final_len) != 1)
        goto out;
    plaintext->len = (size_t)(output_len + final_len);
    if (!plaintext->len)
        goto out;

    pad_count = plaintext->data[plaintext->len - 1];
    if (pad_count < 1 || pad_count > 16 || plaintext->len <= pad_count)
        goto out;
    for (i = 0; i < pad_count; i++) {
        if (plaintext->data[plaintext->len - 1 - i] != pad_count)
            goto out;
    }
    plaintext->len -= pad_count;
    if (plaintext->len && plaintext->data[plaintext->len - 1] == 0x0a)
        plaintext->len--;
    plaintext->data[plaintext->len] = '\0';
    result = 0;

out:
    if (result != 0)
        bytes_free(plaintext);
    EVP_CIPHER_CTX_free(ctx);
    return result;
}

static int adat_random_session_keys(struct adat_session_keys *keys)
{
    unsigned char random_byte;
    size_t i;

    if (!keys)
        return -1;
    for (i = 0; i < sizeof(keys->key); i++) {
        if (RAND_bytes(&random_byte, 1) != 1)
            goto fail;
        keys->key[i] = (unsigned char)('A' + random_byte % 25);
    }
    for (i = 0; i < sizeof(keys->iv); i++) {
        if (RAND_bytes(&random_byte, 1) != 1)
            goto fail;
        keys->iv[i] = (unsigned char)('A' + random_byte % 25);
    }
    return 0;

fail:
    OPENSSL_cleanse(&random_byte, sizeof(random_byte));
    OPENSSL_cleanse(keys, sizeof(*keys));
    return -1;
}

static int adat_aes_encrypt(const struct adat_session_keys *keys,
                            const unsigned char *plaintext, size_t plaintext_len,
                            struct bytes *ciphertext)
{
    EVP_CIPHER_CTX *ctx = NULL;
    int output_len = 0;
    int final_len = 0;
    int result = -1;

    memset(ciphertext, 0, sizeof(*ciphertext));
    if (plaintext_len > (size_t)INT32_MAX)
        return -1;
    ciphertext->data = malloc(plaintext_len + EVP_MAX_BLOCK_LENGTH);
    if (!ciphertext->data)
        return -1;
    ctx = EVP_CIPHER_CTX_new();
    if (!ctx ||
        EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, keys->key, keys->iv) != 1 ||
        EVP_EncryptUpdate(ctx, ciphertext->data, &output_len, plaintext,
                          (int)plaintext_len) != 1 ||
        EVP_EncryptFinal_ex(ctx, ciphertext->data + output_len, &final_len) != 1)
        goto out;
    ciphertext->len = (size_t)(output_len + final_len);
    result = 0;

out:
    if (result != 0)
        bytes_free(ciphertext);
    EVP_CIPHER_CTX_free(ctx);
    return result;
}

static int adat_aes_decrypt(const struct adat_session_keys *keys,
                            const unsigned char *ciphertext, size_t ciphertext_len,
                            struct bytes *plaintext)
{
    EVP_CIPHER_CTX *ctx = NULL;
    int output_len = 0;
    int final_len = 0;
    int result = -1;

    memset(plaintext, 0, sizeof(*plaintext));
    if (!ciphertext_len || ciphertext_len % 16 ||
        ciphertext_len > (size_t)INT32_MAX)
        return -1;
    plaintext->data = malloc(ciphertext_len + 1);
    if (!plaintext->data)
        return -1;
    ctx = EVP_CIPHER_CTX_new();
    if (!ctx ||
        EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, keys->key, keys->iv) != 1 ||
        EVP_DecryptUpdate(ctx, plaintext->data, &output_len, ciphertext,
                          (int)ciphertext_len) != 1 ||
        EVP_DecryptFinal_ex(ctx, plaintext->data + output_len, &final_len) != 1)
        goto out;
    plaintext->len = (size_t)(output_len + final_len);
    plaintext->data[plaintext->len] = '\0';
    result = 0;

out:
    if (result != 0)
        bytes_free(plaintext);
    EVP_CIPHER_CTX_free(ctx);
    return result;
}

static int rsa_encrypt_value(EVP_PKEY *public_key, const unsigned char *input,
                             size_t input_len, struct bytes *encrypted)
{
    EVP_PKEY_CTX *ctx = NULL;
    size_t encrypted_len = 0;
    int result = -1;

    memset(encrypted, 0, sizeof(*encrypted));
    ctx = EVP_PKEY_CTX_new(public_key, NULL);
    if (!ctx || EVP_PKEY_encrypt_init(ctx) <= 0 ||
        EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PADDING) <= 0 ||
        EVP_PKEY_encrypt(ctx, NULL, &encrypted_len, input, input_len) <= 0)
        goto out;
    encrypted->data = malloc(encrypted_len);
    if (!encrypted->data)
        goto out;
    if (EVP_PKEY_encrypt(ctx, encrypted->data, &encrypted_len, input, input_len) <= 0)
        goto out;
    encrypted->len = encrypted_len;
    result = 0;

out:
    if (result != 0)
        bytes_free(encrypted);
    EVP_PKEY_CTX_free(ctx);
    return result;
}

static int load_native_seed_acceleration_key(
    struct acceleration_public_key *result)
{
    EVP_PKEY *key = NULL;
    char *encoded = NULL;

    if (!result)
        return -1;
    memset(result, 0, sizeof(*result));
    if (decode_acceleration_public_key(native_seed_public_key_der_b64, &key) != 0 ||
        !(encoded = strdup(native_seed_public_key_der_b64))) {
        EVP_PKEY_free(key);
        return -1;
    }
    result->version = NATIVE_SEED_KEY_VERSION;
    result->key = key;
    result->der_b64 = encoded;
    return 0;
}

static int rsa_decrypt_value(EVP_PKEY *private_key, const unsigned char *input,
                             size_t input_len, struct bytes *decrypted)
{
    EVP_PKEY_CTX *ctx = NULL;
    size_t decrypted_len = 0;
    int result = -1;

    memset(decrypted, 0, sizeof(*decrypted));
    ctx = EVP_PKEY_CTX_new(private_key, NULL);
    if (!ctx || EVP_PKEY_decrypt_init(ctx) <= 0 ||
        EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PADDING) <= 0 ||
        EVP_PKEY_decrypt(ctx, NULL, &decrypted_len, input, input_len) <= 0)
        goto out;
    decrypted->data = malloc(decrypted_len + 1);
    if (!decrypted->data)
        goto out;
    if (EVP_PKEY_decrypt(ctx, decrypted->data, &decrypted_len, input, input_len) <= 0)
        goto out;
    decrypted->len = decrypted_len;
    decrypted->data[decrypted->len] = '\0';
    result = 0;

out:
    if (result != 0)
        bytes_free(decrypted);
    EVP_PKEY_CTX_free(ctx);
    return result;
}

static json_object *build_adat_envelope(json_object *payload, int key_version,
                                        EVP_PKEY *public_key,
                                        struct adat_session_keys *keys)
{
    const char *payload_json;
    struct bytes ciphertext = {0};
    struct bytes encrypted_key = {0};
    struct bytes encrypted_iv = {0};
    char *ciphertext_b64 = NULL;
    char *encrypted_key_b64 = NULL;
    char *encrypted_iv_b64 = NULL;
    json_object *envelope = NULL;

    if (!payload || key_version < 0 || !public_key || !keys ||
        adat_random_session_keys(keys) != 0)
        goto out;
    payload_json = json_object_to_json_string_ext(payload, JSON_C_TO_STRING_PLAIN);
    if (adat_aes_encrypt(keys, (const unsigned char *)payload_json,
                         strlen(payload_json), &ciphertext) != 0 ||
        rsa_encrypt_value(public_key, keys->key, sizeof(keys->key),
                          &encrypted_key) != 0 ||
        rsa_encrypt_value(public_key, keys->iv, sizeof(keys->iv),
                          &encrypted_iv) != 0)
        goto out;
    ciphertext_b64 = base64_encode(ciphertext.data, ciphertext.len);
    encrypted_key_b64 = base64_encode(encrypted_key.data, encrypted_key.len);
    encrypted_iv_b64 = base64_encode(encrypted_iv.data, encrypted_iv.len);
    if (!ciphertext_b64 || !encrypted_key_b64 || !encrypted_iv_b64)
        goto out;

    envelope = json_object_new_object();
    if (!envelope)
        goto out;
    json_object_object_add(envelope, "v", json_object_new_int(key_version));
    json_object_object_add(envelope, "k", json_object_new_string(encrypted_key_b64));
    json_object_object_add(envelope, "i", json_object_new_string(encrypted_iv_b64));
    json_object_object_add(envelope, "d", json_object_new_string(ciphertext_b64));

out:
    free(ciphertext_b64);
    free(encrypted_key_b64);
    free(encrypted_iv_b64);
    bytes_free(&ciphertext);
    bytes_free(&encrypted_key);
    bytes_free(&encrypted_iv);
    if (!envelope)
        OPENSSL_cleanse(keys, sizeof(*keys));
    return envelope;
}

static bool adat_outer_code(json_object *outer, int *code)
{
    json_object *value = NULL;

    if (!outer || !code ||
        !json_object_object_get_ex(outer, "c", &value) ||
        !json_object_is_type(value, json_type_int))
        return false;
    *code = json_object_get_int(value);
    return true;
}

static int decrypt_adat_response(json_object *outer,
                                 const struct adat_session_keys *keys,
                                 json_object **result)
{
    json_object *encoded_value = NULL;
    const char *encoded;
    struct bytes ciphertext = {0};
    struct bytes plaintext = {0};
    json_object *parsed = NULL;
    int code;
    bool key_rotation;
    int status = -1;

    if (!outer || !keys || !result)
        return -1;
    *result = NULL;
    key_rotation = adat_outer_code(outer, &code) && code == 2;
    if (!json_object_object_get_ex(outer, "d", &encoded_value) ||
        !json_object_is_type(encoded_value, json_type_string))
        return -1;
    encoded = json_object_get_string(encoded_value);
    if (base64_decode(encoded, &ciphertext) != 0 ||
        adat_aes_decrypt(keys, ciphertext.data, ciphertext.len, &plaintext) != 0)
        goto out;
    parsed = json_tokener_parse((const char *)plaintext.data);
    if (!parsed)
        goto out;
    *result = parsed;
    parsed = NULL;
    status = key_rotation ? 2 : 0;

out:
    json_object_put(parsed);
    bytes_free(&ciphertext);
    bytes_free(&plaintext);
    return status;
}

static int decrypt_signal_response(json_object *outer, json_object **result)
{
    json_object *encoded_value = NULL;
    const char *encoded;
    struct adat_session_keys keys = {{0}, {0}};
    struct bytes ciphertext = {0};
    struct bytes plaintext = {0};
    json_object *parsed = NULL;
    int status = -1;

    if (!outer || !result)
        return -1;
    *result = NULL;
    if (!json_object_object_get_ex(outer, "data", &encoded_value) ||
        !json_object_is_type(encoded_value, json_type_string)) {
        *result = json_object_get(outer);
        return 0;
    }
    encoded = json_object_get_string(encoded_value);
    memcpy(keys.key, SIGNAL_AES_KEY, sizeof(keys.key));
    memcpy(keys.iv, SIGNAL_AES_IV, sizeof(keys.iv));
    if (base64_decode(encoded, &ciphertext) != 0 ||
        adat_aes_decrypt(&keys, ciphertext.data, ciphertext.len, &plaintext) != 0)
        goto out;
    parsed = json_tokener_parse((const char *)plaintext.data);
    if (!parsed)
        goto out;
    json_object_object_add(outer, "data", parsed);
    parsed = NULL;
    *result = json_object_get(outer);
    status = 0;

out:
    OPENSSL_cleanse(&keys, sizeof(keys));
    json_object_put(parsed);
    bytes_free(&ciphertext);
    bytes_free(&plaintext);
    return status;
}

static int rsa_encrypt_key(const unsigned char key[16], struct bytes *encrypted)
{
    BIO *bio = NULL;
    EVP_PKEY *public_key = NULL;
    EVP_PKEY_CTX *ctx = NULL;
    size_t encrypted_len = 0;
    int result = -1;

    memset(encrypted, 0, sizeof(*encrypted));
    bio = BIO_new_mem_buf(public_key_pem, -1);
    if (!bio)
        goto out;
    public_key = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
    if (!public_key)
        goto out;
    ctx = EVP_PKEY_CTX_new(public_key, NULL);
    if (!ctx || EVP_PKEY_encrypt_init(ctx) <= 0 ||
        EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PADDING) <= 0 ||
        EVP_PKEY_encrypt(ctx, NULL, &encrypted_len, key, 16) <= 0)
        goto out;
    encrypted->data = malloc(encrypted_len);
    if (!encrypted->data)
        goto out;
    if (EVP_PKEY_encrypt(ctx, encrypted->data, &encrypted_len, key, 16) <= 0)
        goto out;
    encrypted->len = encrypted_len;
    result = 0;

out:
    if (result != 0)
        bytes_free(encrypted);
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(public_key);
    BIO_free(bio);
    return result;
}

static json_object *build_envelope(json_object *payload, unsigned char key[16])
{
    const char *payload_json;
    struct bytes ciphertext = {0};
    struct bytes encrypted_key = {0};
    char *ciphertext_b64 = NULL;
    char *encrypted_key_b64 = NULL;
    json_object *envelope = NULL;

    if (random_session_key(key) != 0)
        goto out;
    payload_json = json_object_to_json_string_ext(payload, JSON_C_TO_STRING_PLAIN);
    if (aes_encrypt(key, (const unsigned char *)payload_json, strlen(payload_json),
                    &ciphertext) != 0 ||
        rsa_encrypt_key(key, &encrypted_key) != 0)
        goto out;
    ciphertext_b64 = base64_encode(ciphertext.data, ciphertext.len);
    encrypted_key_b64 = base64_encode(encrypted_key.data, encrypted_key.len);
    if (!ciphertext_b64 || !encrypted_key_b64)
        goto out;

    envelope = json_object_new_object();
    if (!envelope)
        goto out;
    json_object_object_add(envelope, "k", json_object_new_string(encrypted_key_b64));
    json_object_object_add(envelope, "v", json_object_new_int(1));
    json_object_object_add(envelope, "d", json_object_new_string(ciphertext_b64));
    json_object_object_add(envelope, "i", json_object_new_string(encrypted_key_b64));

out:
    free(ciphertext_b64);
    free(encrypted_key_b64);
    bytes_free(&ciphertext);
    bytes_free(&encrypted_key);
    return envelope;
}

static char *new_uuid(void)
{
    unsigned char random_data[16];
    char *value;

    if (RAND_bytes(random_data, sizeof(random_data)) != 1)
        return NULL;
    random_data[6] = (random_data[6] & 0x0f) | 0x40;
    random_data[8] = (random_data[8] & 0x3f) | 0x80;
    value = malloc(37);
    if (!value)
        return NULL;
    snprintf(value, 37,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
             "%02x%02x%02x%02x%02x%02x",
             random_data[0], random_data[1], random_data[2], random_data[3],
             random_data[4], random_data[5], random_data[6], random_data[7],
             random_data[8], random_data[9], random_data[10], random_data[11],
             random_data[12], random_data[13], random_data[14], random_data[15]);
    OPENSSL_cleanse(random_data, sizeof(random_data));
    return value;
}

static bool valid_uuid(const char *value)
{
    size_t i;

    if (!value || strlen(value) != 36)
        return false;
    for (i = 0; i < 36; i++) {
        bool separator = i == 8 || i == 13 || i == 18 || i == 23;
        bool hexadecimal = (value[i] >= '0' && value[i] <= '9') ||
                           (value[i] >= 'a' && value[i] <= 'f') ||
                           (value[i] >= 'A' && value[i] <= 'F');

        if ((separator && value[i] != '-') || (!separator && !hexadecimal))
            return false;
    }
    return true;
}

static int write_all(int fd, const unsigned char *data, size_t length)
{
    size_t written = 0;

    while (written < length) {
        ssize_t count = write(fd, data + written, length - written);

        if (count < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (count == 0)
            return -1;
        written += (size_t)count;
    }
    return 0;
}

static char *parent_directory(const char *path)
{
    const char *slash;
    size_t length;
    char *parent;

    if (!path || path[0] != '/')
        return NULL;
    slash = strrchr(path, '/');
    if (!slash || slash[1] == '\0')
        return NULL;
    length = slash == path ? 1 : (size_t)(slash - path);
    parent = malloc(length + 1);
    if (!parent)
        return NULL;
    memcpy(parent, path, length);
    parent[length] = '\0';
    return parent;
}

static int atomic_private_write(const char *path, const unsigned char *data,
                                size_t length)
{
    char *parent = NULL;
    char *temporary = NULL;
    char *suffix = NULL;
    int fd = -1;
    int directory_fd = -1;
    int status = -1;

    if (!path || !data || length > MAX_STATE_SIZE)
        return -1;
    parent = parent_directory(path);
    suffix = new_uuid();
    if (!parent || !suffix || asprintf(&temporary, "%s.tmp.%s", path, suffix) < 0)
        goto out;
    fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
              S_IRUSR | S_IWUSR);
    if (fd < 0 || fchmod(fd, S_IRUSR | S_IWUSR) != 0 ||
        write_all(fd, data, length) != 0 || fsync(fd) != 0)
        goto out;
    if (close(fd) != 0) {
        fd = -1;
        goto out;
    }
    fd = -1;
    if (rename(temporary, path) != 0)
        goto out;
    directory_fd = open(parent, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory_fd >= 0)
        (void)fsync(directory_fd);
    status = 0;

out:
    if (fd >= 0)
        close(fd);
    if (status != 0 && temporary)
        unlink(temporary);
    if (directory_fd >= 0)
        close(directory_fd);
    free(parent);
    free(temporary);
    free(suffix);
    return status;
}

static int read_private_file(const char *path, struct bytes *contents)
{
    struct stat info;
    size_t offset = 0;
    int fd = -1;
    int status = -1;

    memset(contents, 0, sizeof(*contents));
    if (!path || path[0] != '/') {
        errno = EINVAL;
        return -1;
    }
    fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return -1;
    if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) ||
        info.st_uid != geteuid() || (info.st_mode & (S_IRWXG | S_IRWXO)) != 0 ||
        info.st_size <= 0 || (uintmax_t)info.st_size > MAX_STATE_SIZE) {
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
            break;
        offset += (size_t)count;
        contents->len = offset;
    }
    if (offset != (size_t)info.st_size) {
        errno = EIO;
        goto out;
    }
    contents->data[offset] = '\0';
    status = 0;

out:
    if (status != 0)
        bytes_free(contents);
    close(fd);
    return status;
}

static int resolve_device_identity(const char *explicit_id, const char *path,
                                   char **device_id)
{
    struct bytes stored = {0};
    char *generated = NULL;
    int status = -1;

    *device_id = NULL;
    if (explicit_id) {
        if (!valid_uuid(explicit_id)) {
            errno = EINVAL;
            return -1;
        }
        *device_id = strdup(explicit_id);
        return *device_id ? 0 : -1;
    }
    if (read_private_file(path, &stored) == 0) {
        while (stored.len && (stored.data[stored.len - 1] == '\n' ||
                              stored.data[stored.len - 1] == '\r'))
            stored.data[--stored.len] = '\0';
        if (!valid_uuid((const char *)stored.data)) {
            errno = EINVAL;
            goto out;
        }
        *device_id = strdup((const char *)stored.data);
        status = *device_id ? 0 : -1;
        goto out;
    }
    if (errno != ENOENT)
        goto out;
    generated = new_uuid();
    if (!generated || atomic_private_write(path, (const unsigned char *)generated,
                                            strlen(generated)) != 0)
        goto out;
    *device_id = generated;
    generated = NULL;
    status = 0;

out:
    free(generated);
    bytes_free(&stored);
    return status;
}

static int resolve_pc_device_identity(const char *path, char **device_id)
{
    struct bytes stored = {0};
    char *generated = NULL;
    int status = -1;

    if (!path || !device_id) {
        errno = EINVAL;
        return -1;
    }
    *device_id = NULL;
    if (read_private_file(path, &stored) == 0) {
        while (stored.len && (stored.data[stored.len - 1] == '\n' ||
                              stored.data[stored.len - 1] == '\r'))
            stored.data[--stored.len] = '\0';
        if (!valid_native_utdid((const char *)stored.data)) {
            errno = EINVAL;
            goto out;
        }
        *device_id = strdup((const char *)stored.data);
        status = *device_id ? 0 : -1;
        goto out;
    }
    if (errno != ENOENT)
        goto out;
    generated = new_native_utdid();
    if (!generated || atomic_private_write(path, (const unsigned char *)generated,
                                            strlen(generated)) != 0)
        goto out;
    *device_id = generated;
    generated = NULL;
    status = 0;

out:
    if (generated) {
        OPENSSL_cleanse(generated, strlen(generated));
        free(generated);
    }
    bytes_free(&stored);
    return status;
}

static bool decimal_string(const char *value, size_t minimum, size_t maximum)
{
    size_t length;
    size_t i;

    if (!value)
        return false;
    length = strlen(value);
    if (length < minimum || length > maximum)
        return false;
    for (i = 0; i < length; i++) {
        if (value[i] < '0' || value[i] > '9')
            return false;
    }
    return true;
}

static bool native_biubiu_id(const char *value, int32_t *result)
{
    char *end = NULL;
    unsigned long parsed;

    if (!result)
        return false;
    if (!value || !value[0]) {
        *result = 0;
        return true;
    }
    errno = 0;
    parsed = strtoul(value, &end, 10);
    if (errno == ERANGE || !end || *end || parsed > INT32_MAX)
        return false;
    *result = (int32_t)parsed;
    return true;
}

static void cleanse_json_string(json_object *object, const char *name)
{
    json_object *value = NULL;
    const char *text;

    if (!object || !json_object_object_get_ex(object, name, &value) ||
        !json_object_is_type(value, json_type_string))
        return;
    text = json_object_get_string(value);
    if (text)
        OPENSSL_cleanse((void *)text, strlen(text));
}

static bool response_is_success(json_object *response)
{
    json_object *code = NULL;

    if (!response || !json_object_object_get_ex(response, "code", &code) ||
        !json_object_is_type(code, json_type_string))
        return false;
    return strcmp(json_object_get_string(code), "SUCCESS") == 0;
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
    json_object *value = object_member(object, name, json_type_string);

    return value ? json_object_get_string(value) : NULL;
}

static int parse_native_key_rotation(json_object *response,
                                     struct acceleration_public_key *result)
{
    json_object *version;
    const char *encoded;
    int64_t version_number;
    EVP_PKEY *key = NULL;
    char *encoded_copy = NULL;

    if (!response || !result)
        return -1;
    memset(result, 0, sizeof(*result));
    version = object_member(response, "v", json_type_int);
    encoded = string_member(response, "rsaPublicKey");
    if (!version || !encoded || !encoded[0])
        return -1;
    version_number = json_object_get_int64(version);
    if (version_number < 1 || version_number > INT32_MAX ||
        decode_acceleration_public_key(encoded, &key) != 0)
        return -1;
    encoded_copy = strdup(encoded);
    if (!encoded_copy) {
        EVP_PKEY_free(key);
        return -1;
    }
    result->version = (int)version_number;
    result->key = key;
    result->der_b64 = encoded_copy;
    return 0;
}

static int store_acceleration_key(const char *path,
                                  const struct acceleration_public_key *value)
{
    json_object *record = NULL;
    const char *serialized;
    int status = -1;

    if (!path || !value || value->version < 1 || !value->key || !value->der_b64)
        return -1;
    record = json_object_new_object();
    if (!record)
        return -1;
    json_object_object_add(record, "schemaVersion", json_object_new_int(1));
    json_object_object_add(record, "keyVersion",
                           json_object_new_int(value->version));
    json_object_object_add(record, "publicKeyDer",
                           json_object_new_string(value->der_b64));
    serialized = json_object_to_json_string_ext(record, JSON_C_TO_STRING_PRETTY);
    if (serialized)
        status = atomic_private_write(path, (const unsigned char *)serialized,
                                      strlen(serialized));
    json_object_put(record);
    return status;
}

static int load_acceleration_key(const char *path,
                                 struct acceleration_public_key *result)
{
    struct bytes contents = {0};
    json_object *record = NULL;
    json_object *schema = NULL;
    json_object *version = NULL;
    const char *encoded;
    int64_t version_number;
    int status = -1;

    if (!result)
        return -1;
    memset(result, 0, sizeof(*result));
    if (read_private_file(path, &contents) != 0)
        return -1;
    record = json_tokener_parse((const char *)contents.data);
    if (!record || !json_object_is_type(record, json_type_object) ||
        !json_object_object_get_ex(record, "schemaVersion", &schema) ||
        !json_object_is_type(schema, json_type_int) ||
        json_object_get_int(schema) != 1 ||
        !json_object_object_get_ex(record, "keyVersion", &version) ||
        !json_object_is_type(version, json_type_int)) {
        errno = EINVAL;
        goto out;
    }
    version_number = json_object_get_int64(version);
    encoded = string_member(record, "publicKeyDer");
    if (version_number < 1 || version_number > INT32_MAX || !encoded ||
        decode_acceleration_public_key(encoded, &result->key) != 0) {
        errno = EINVAL;
        goto out;
    }
    result->der_b64 = strdup(encoded);
    if (!result->der_b64)
        goto out;
    result->version = (int)version_number;
    status = 0;

out:
    if (status != 0)
        acceleration_public_key_free(result);
    json_object_put(record);
    bytes_free(&contents);
    return status;
}

static int load_acceleration_key_or_seed(
    const char *path, struct acceleration_public_key *result)
{
    if (load_acceleration_key(path, result) == 0)
        return 0;
    if (errno != ENOENT)
        return -1;
    if (load_native_seed_acceleration_key(result) != 0) {
        errno = EINVAL;
        return -1;
    }
    if (store_acceleration_key(path, result) != 0) {
        acceleration_public_key_free(result);
        return -1;
    }
    return 0;
}

static int acceleration_key_fingerprint(const char *encoded, char output[65])
{
    struct bytes der = {0};
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_length = 0;
    size_t i;
    int status = -1;

    if (!encoded || !output || base64_decode(encoded, &der) != 0 ||
        EVP_Digest(der.data, der.len, digest, &digest_length, EVP_sha256(), NULL) !=
            1 ||
        digest_length != 32)
        goto out;
    for (i = 0; i < digest_length; i++)
        snprintf(output + i * 2, 3, "%02x", digest[i]);
    output[64] = '\0';
    status = 0;

out:
    OPENSSL_cleanse(digest, sizeof(digest));
    bytes_free(&der);
    return status;
}

static int print_acceleration_key_summary(bool cached,
                                          const struct acceleration_public_key *key)
{
    json_object *summary = NULL;
    char fingerprint[65] = {0};
    int status = 1;

    if (cached && (!key || !key->key || !key->der_b64 ||
                   acceleration_key_fingerprint(key->der_b64, fingerprint) != 0))
        return 1;
    summary = json_object_new_object();
    if (!summary)
        return 1;
    json_object_object_add(summary, "cached", json_object_new_boolean(cached));
    if (cached) {
        json_object_object_add(summary, "keyVersion",
                               json_object_new_int(key->version));
        json_object_object_add(summary, "rsaBits",
                               json_object_new_int(EVP_PKEY_bits(key->key)));
        json_object_object_add(summary, "fingerprintSha256",
                               json_object_new_string(fingerprint));
    }
    puts(json_object_to_json_string_ext(summary, JSON_C_TO_STRING_PLAIN));
    status = 0;
    json_object_put(summary);
    OPENSSL_cleanse(fingerprint, sizeof(fingerprint));
    return status;
}

static int print_acceleration_key_status(const char *path)
{
    struct acceleration_public_key key = {0};
    int status;

    if (load_acceleration_key(path, &key) == 0) {
        status = print_acceleration_key_summary(true, &key);
    } else if (errno == ENOENT) {
        status = print_acceleration_key_summary(false, NULL);
    } else {
        fprintf(stderr, "unable to read acceleration key cache: %s\n",
                strerror(errno));
        status = 1;
    }
    acceleration_public_key_free(&key);
    return status;
}

static int import_acceleration_key(const char *source_path, const char *cache_path)
{
    struct bytes source = {0};
    struct acceleration_public_key key = {0};
    int status = 1;

    if (read_private_file(source_path, &source) != 0) {
        fprintf(stderr, "unable to read private acceleration key input: %s\n",
                strerror(errno));
        goto out;
    }
    while (source.len && (source.data[source.len - 1] == '\n' ||
                          source.data[source.len - 1] == '\r'))
        source.data[--source.len] = '\0';
    if (!source.len || parse_security_key_value((const char *)source.data, &key) !=
                           0) {
        fputs("invalid acceleration key; expected VERSION|BASE64_DER\n", stderr);
        goto out;
    }
    if (store_acceleration_key(cache_path, &key) != 0) {
        fprintf(stderr, "unable to store acceleration key cache: %s\n",
                strerror(errno));
        goto out;
    }
    status = print_acceleration_key_summary(true, &key);

out:
    acceleration_public_key_free(&key);
    bytes_free(&source);
    return status;
}

static int fetch_acceleration_key(const char *cache_path)
{
    struct acceleration_public_key key = {0};
    int status = 1;

    if (load_native_seed_acceleration_key(&key) != 0) {
        fputs("unable to load the official native acceleration seed key\n", stderr);
        goto out;
    }
    if (store_acceleration_key(cache_path, &key) != 0) {
        fprintf(stderr, "unable to store acceleration key cache: %s\n",
                strerror(errno));
        goto out;
    }
    status = print_acceleration_key_summary(true, &key);

out:
    acceleration_public_key_free(&key);
    return status;
}

static json_object *session_info_from_record(json_object *record)
{
    json_object *login = object_member(record, "login", json_type_object);
    json_object *data = object_member(login, "data", json_type_object);

    return object_member(data, "sessionInfo", json_type_object);
}

static int session_components(json_object *record, const char **device_id,
                              const char **method, const char **session_id,
                              const char **refresh_token, size_t *cookie_count)
{
    json_object *local = object_member(record, "local", json_type_object);
    json_object *session_info = session_info_from_record(record);
    json_object *cookies;
    const char *stored_device_id;

    if (!local || !session_info)
        return -1;
    stored_device_id = string_member(local, "deviceId");
    if (!valid_uuid(stored_device_id))
        return -1;
    if (device_id)
        *device_id = stored_device_id;
    if (method) {
        const char *stored_method = string_member(local, "method");

        *method = stored_method && stored_method[0] ? stored_method : "unknown";
    }
    if (session_id)
        *session_id = string_member(session_info, "sessionId");
    if (refresh_token)
        *refresh_token = string_member(session_info, "refreshToken");
    if (cookie_count) {
        cookies = object_member(session_info, "cookies", json_type_array);
        *cookie_count = cookies ? json_object_array_length(cookies) : 0;
    }
    return 0;
}

static const char *json_scalar_string(json_object *object, const char *name)
{
    json_object *value = NULL;

    if (!object || !json_object_object_get_ex(object, name, &value) ||
        (json_object_get_type(value) != json_type_string &&
         json_object_get_type(value) != json_type_int))
        return NULL;
    return json_object_get_string(value);
}

static int session_acceleration_identity(json_object *record,
                                         const char **uid,
                                         const char **service_ticket)
{
    json_object *login = object_member(record, "login", json_type_object);
    json_object *data = object_member(login, "data", json_type_object);
    json_object *user = object_member(data, "userBasicInfo", json_type_object);
    json_object *session_info = session_info_from_record(record);
    const char *stored_uid;
    const char *stored_ticket;

    if (!user || !session_info)
        return -1;
    stored_uid = json_scalar_string(user, "localId");
    stored_ticket = string_member(session_info, "sessionId");
    if (!decimal_string(stored_uid, 1, 24) || stored_uid[0] == '0' ||
        !stored_ticket || !stored_ticket[0])
        return -1;
    if (uid)
        *uid = stored_uid;
    if (service_ticket)
        *service_ticket = stored_ticket;
    return 0;
}

static void cleanse_json_value(json_object *value)
{
    size_t i;

    if (!value)
        return;
    if (json_object_is_type(value, json_type_string)) {
        const char *text = json_object_get_string(value);

        if (text)
            OPENSSL_cleanse((void *)text, strlen(text));
        return;
    }
    if (json_object_is_type(value, json_type_array)) {
        for (i = 0; i < json_object_array_length(value); i++)
            cleanse_json_value(json_object_array_get_idx(value, i));
        return;
    }
    if (json_object_is_type(value, json_type_object)) {
        json_object_object_foreach(value, key, child)
        {
            (void)key;
            cleanse_json_value(child);
        }
    }
}

static int load_session_record(const char *path, json_object **record)
{
    struct bytes contents = {0};
    json_object *parsed = NULL;
    int status = -1;

    *record = NULL;
    if (read_private_file(path, &contents) != 0)
        return -1;
    parsed = json_tokener_parse((const char *)contents.data);
    if (!parsed || !json_object_is_type(parsed, json_type_object) ||
        session_components(parsed, NULL, NULL, NULL, NULL, NULL) != 0) {
        errno = EINVAL;
        goto out;
    }
    *record = parsed;
    parsed = NULL;
    status = 0;

out:
    cleanse_json_value(parsed);
    json_object_put(parsed);
    bytes_free(&contents);
    return status;
}

static bool cookie_name_valid(const char *value)
{
    const unsigned char *cursor;

    if (!value || !value[0])
        return false;
    for (cursor = (const unsigned char *)value; *cursor; cursor++) {
        if (!((*cursor >= 'a' && *cursor <= 'z') ||
              (*cursor >= 'A' && *cursor <= 'Z') ||
              (*cursor >= '0' && *cursor <= '9') || *cursor == '!' ||
              *cursor == '#' || *cursor == '$' || *cursor == '%' ||
              *cursor == '&' || *cursor == '\'' || *cursor == '*' ||
              *cursor == '+' || *cursor == '-' || *cursor == '.' ||
              *cursor == '^' || *cursor == '_' || *cursor == '`' ||
              *cursor == '|' || *cursor == '~'))
            return false;
    }
    return true;
}

static bool cookie_value_valid(const char *value)
{
    const unsigned char *cursor;

    if (!value)
        return false;
    for (cursor = (const unsigned char *)value; *cursor; cursor++) {
        if (*cursor <= 0x20U || *cursor >= 0x7fU || *cursor == '"' ||
            *cursor == ',' || *cursor == ';' || *cursor == '\\')
            return false;
    }
    return true;
}

static bool domain_scope_matches_host(const char *scope, const char *host)
{
    const char *start;
    const char *separator;
    const char *end;
    size_t scope_length;
    size_t host_length;

    if (!scope || !host || !scope[0] || !host[0])
        return false;
    start = scope;
    separator = strstr(start, "://");
    if (separator)
        start = separator + 3;
    if (start[0] == '*' && start[1] == '.')
        start += 2;
    while (*start == '.')
        start++;
    end = start + strcspn(start, "/?#");
    separator = memchr(start, ':', (size_t)(end - start));
    if (separator)
        end = separator;
    if (memchr(start, '@', (size_t)(end - start)))
        return false;
    while (end > start && end[-1] == '.')
        end--;
    scope_length = (size_t)(end - start);
    host_length = strlen(host);
    if (!scope_length || scope_length > host_length || scope_length > 253)
        return false;
    if (strncasecmp(host + host_length - scope_length, start, scope_length) != 0)
        return false;
    return scope_length == host_length ||
           host[host_length - scope_length - 1] == '.';
}

static bool session_allows_cookie_host(json_object *record, const char *host)
{
    json_object *session_info;
    json_object *domains;
    size_t index;

    if (!host || strcmp(host, ACCELERATION_HOST) != 0)
        return false;
    session_info = session_info_from_record(record);
    domains = object_member(session_info, "domains", json_type_array);
    if (!domains || !json_object_array_length(domains))
        return true;
    for (index = 0; index < json_object_array_length(domains); index++) {
        json_object *domain = json_object_array_get_idx(domains, index);

        if (json_object_is_type(domain, json_type_string) &&
            domain_scope_matches_host(json_object_get_string(domain), host))
            return true;
    }
    return false;
}

static int session_cookie_header(json_object *record, const char *host,
                                 char **header)
{
    static const char prefix[] = "Cookie: ";
    json_object *session_info;
    json_object *cookies;
    char *value = NULL;
    size_t length = sizeof(prefix) - 1;
    size_t accepted = 0;
    size_t index;

    if (!record || !header || !session_allows_cookie_host(record, host)) {
        if (header)
            *header = NULL;
        return 0;
    }
    *header = NULL;
    session_info = session_info_from_record(record);
    cookies = object_member(session_info, "cookies", json_type_array);
    if (!cookies || !json_object_array_length(cookies))
        return 0;
    value = malloc(length + 1);
    if (!value)
        return -1;
    memcpy(value, prefix, sizeof(prefix));
    for (index = 0; index < json_object_array_length(cookies); index++) {
        json_object *cookie = json_object_array_get_idx(cookies, index);
        const char *name = string_member(cookie, "keyName");
        const char *cookie_value = string_member(cookie, "value");
        size_t name_length;
        size_t cookie_value_length;
        size_t separator_length = accepted ? 2U : 0U;
        size_t needed;
        char *grown;

        if (!cookie_name_valid(name) || !cookie_value_valid(cookie_value))
            continue;
        name_length = strlen(name);
        cookie_value_length = strlen(cookie_value);
        if (name_length > MAX_COOKIE_HEADER_SIZE ||
            cookie_value_length > MAX_COOKIE_HEADER_SIZE ||
            separator_length > MAX_COOKIE_HEADER_SIZE - length ||
            name_length >= MAX_COOKIE_HEADER_SIZE - length - separator_length ||
            cookie_value_length >
                MAX_COOKIE_HEADER_SIZE - length - separator_length - name_length - 1U) {
            errno = E2BIG;
            goto fail;
        }
        needed = length + separator_length + name_length + 1U +
                 cookie_value_length + 1U;
        grown = realloc(value, needed);
        if (!grown)
            goto fail;
        value = grown;
        if (separator_length) {
            value[length++] = ';';
            value[length++] = ' ';
        }
        memcpy(value + length, name, name_length);
        length += name_length;
        value[length++] = '=';
        memcpy(value + length, cookie_value, cookie_value_length);
        length += cookie_value_length;
        value[length] = '\0';
        accepted++;
    }
    if (!accepted) {
        OPENSSL_cleanse(value, length);
        free(value);
        return 0;
    }
    *header = value;
    return 1;

fail:
    OPENSSL_cleanse(value, length);
    free(value);
    return -1;
}

static void cleanse_cookie_headers(struct curl_slist *headers)
{
    struct curl_slist *item;

    for (item = headers; item; item = item->next) {
        if (item->data && strncasecmp(item->data, "Cookie:", 7) == 0)
            OPENSSL_cleanse(item->data, strlen(item->data));
    }
}

static int store_session_record(const char *path, const char *device_id,
                                const char *method, json_object *login_result)
{
    json_object *record = NULL;
    json_object *local = NULL;
    json_object *session_info;
    const char *session_id;
    const char *refresh_token;
    const char *serialized;
    int status = -1;

    if (!path || !valid_uuid(device_id) || !method ||
        !response_is_success(login_result))
        return -1;
    session_info = object_member(object_member(login_result, "data", json_type_object),
                                 "sessionInfo", json_type_object);
    session_id = string_member(session_info, "sessionId");
    refresh_token = string_member(session_info, "refreshToken");
    if (!session_id || !session_id[0] || !refresh_token || !refresh_token[0])
        return -1;

    record = json_object_new_object();
    local = json_object_new_object();
    if (!record || !local)
        goto out;
    json_object_object_add(record, "schemaVersion", json_object_new_int(1));
    json_object_object_add(local, "deviceId", json_object_new_string(device_id));
    json_object_object_add(local, "method", json_object_new_string(method));
    json_object_object_add(record, "local", local);
    local = NULL;
    json_object_object_add(record, "login", json_object_get(login_result));
    serialized = json_object_to_json_string_ext(record, JSON_C_TO_STRING_PRETTY);
    if (!serialized || atomic_private_write(path, (const unsigned char *)serialized,
                                             strlen(serialized)) != 0)
        goto out;
    status = 0;

out:
    json_object_put(local);
    json_object_put(record);
    return status;
}

static int store_private_json(const char *path, json_object *value)
{
    const char *serialized;

    if (!path || !value || path[0] != '/') {
        errno = EINVAL;
        return -1;
    }
    serialized = json_object_to_json_string_ext(value, JSON_C_TO_STRING_PRETTY);
    if (!serialized || strlen(serialized) > MAX_STATE_SIZE) {
        errno = EFBIG;
        return -1;
    }
    return atomic_private_write(path, (const unsigned char *)serialized,
                                strlen(serialized));
}

static int print_session_status(const char *path)
{
    json_object *record = NULL;
    json_object *output = NULL;
    char *cookie_header = NULL;
    const char *method = "none";
    const char *session_id = NULL;
    const char *refresh_token = NULL;
    size_t cookie_count = 0;
    bool stored = false;
    bool cookie_transport_ready = false;
    int status = 1;

    if (load_session_record(path, &record) == 0) {
        if (session_components(record, NULL, &method, &session_id, &refresh_token,
                               &cookie_count) != 0)
            goto out;
        int cookie_status =
            session_cookie_header(record, ACCELERATION_HOST, &cookie_header);

        if (cookie_status < 0)
            goto out;
        cookie_transport_ready = cookie_status == 1;
        stored = true;
    } else if (errno != ENOENT) {
        fprintf(stderr, "unable to read private session file: %s\n", strerror(errno));
        goto out;
    }
    output = json_object_new_object();
    if (!output)
        goto out;
    json_object_object_add(output, "authenticated",
                           json_object_new_boolean(session_id && session_id[0]));
    json_object_object_add(output, "refreshable",
                           json_object_new_boolean(refresh_token && refresh_token[0]));
    json_object_object_add(output, "cookieCount",
                           json_object_new_int64((int64_t)cookie_count));
    json_object_object_add(output, "cookieTransportReady",
                           json_object_new_boolean(cookie_transport_ready));
    json_object_object_add(output, "method", json_object_new_string(method));
    json_object_object_add(output, "deviceIdStored", json_object_new_boolean(stored));
    puts(json_object_to_json_string_ext(output, JSON_C_TO_STRING_PLAIN));
    status = 0;

out:
    json_object_put(output);
    if (cookie_header)
        OPENSSL_cleanse(cookie_header, strlen(cookie_header));
    free(cookie_header);
    cleanse_json_value(record);
    json_object_put(record);
    return status;
}

static int clear_session_record(const char *path)
{
    struct stat info;
    json_object *output = NULL;
    bool removed = false;
    int status = 1;

    if (!path || lstat(path, &info) != 0) {
        if (errno != ENOENT) {
            fprintf(stderr, "unable to inspect private session file: %s\n",
                    strerror(errno));
            return 1;
        }
    } else {
        if (!S_ISREG(info.st_mode) || info.st_uid != geteuid() ||
            (info.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
            fputs("refusing to remove an unsafe private session file\n", stderr);
            return 1;
        }
        if (unlink(path) != 0) {
            fprintf(stderr, "unable to remove private session file: %s\n",
                    strerror(errno));
            return 1;
        }
        removed = true;
    }
    output = json_object_new_object();
    if (!output)
        return 1;
    json_object_object_add(output, "success", json_object_new_boolean(true));
    json_object_object_add(output, "removed", json_object_new_boolean(removed));
    puts(json_object_to_json_string_ext(output, JSON_C_TO_STRING_PLAIN));
    status = 0;
    json_object_put(output);
    return status;
}

static int read_secret_line(char *buffer, size_t buffer_size)
{
    size_t length;
    int character;

    if (!buffer || buffer_size < 2 || !fgets(buffer, (int)buffer_size, stdin))
        return -1;
    length = strlen(buffer);
    if (length && buffer[length - 1] == '\n') {
        buffer[--length] = '\0';
    } else if (!feof(stdin)) {
        while ((character = fgetc(stdin)) != '\n' && character != EOF)
            ;
        OPENSSL_cleanse(buffer, buffer_size);
        errno = EOVERFLOW;
        return -1;
    }
    if (length && buffer[length - 1] == '\r')
        buffer[--length] = '\0';
    if (!length) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static int set_client_session(json_object *payload, const char *session_id)
{
    json_object *client_user = object_member(payload, "clientUser", json_type_object);

    if (!client_user || !session_id || !session_id[0])
        return -1;
    json_object_object_add(client_user, "sessionId",
                           json_object_new_string(session_id));
    return 0;
}

static json_object *build_client_context(const char *device_id)
{
    json_object *root = json_object_new_object();
    json_object *device = json_object_new_object();
    json_object *user = json_object_new_object();
    json_object *scene = json_object_new_object();

    if (!root || !device || !user || !scene)
        goto fail;
    json_object_object_add(device, "appVer", json_object_new_string("100843982"));
    json_object_object_add(device, "clientFlag", json_object_new_int(0));
    json_object_object_add(device, "deviceId", json_object_new_string(device_id));
    json_object_object_add(device, "utdid", json_object_new_string(device_id));
    json_object_object_add(device, "umid", json_object_new_string(""));
    json_object_object_add(
        device, "userAgent",
        json_object_new_string("Mozilla/5.0 AppleWebKit/537.36 (KHTML, like Gecko) "
                               "Chrome/59.0 Safari/537.36 "
                               "biubiu/windows/8.4.3 windows"));
    json_object_object_add(device, "clientBizId", json_object_new_string("biubiu"));
    json_object_object_add(device, "clientAppCode",
                           json_object_new_string("BIUBIU_DESKTOP"));
    json_object_object_add(device, "clientType", json_object_new_string("DESKTOP"));
    json_object_object_add(device, "os", json_object_new_string("linux"));
    json_object_object_add(device, "osVer", json_object_new_string("openwrt"));
    json_object_object_add(device, "sdkVer", json_object_new_string("100843982"));

    json_object_object_add(user, "sessionId", json_object_new_string(""));
    json_object_object_add(scene, "clientCaller",
                           json_object_new_string("biubiu-sdk-windows"));
    json_object_object_add(scene, "bizId", json_object_new_string("biubiu"));
    json_object_object_add(scene, "appCode",
                           json_object_new_string("BIUBIU_DESKTOP"));
    json_object_object_add(scene, "channel", json_object_new_string("official"));
    json_object_object_add(root, "clientDevice", device);
    json_object_object_add(root, "clientUser", user);
    json_object_object_add(root, "clientScene", scene);
    return root;

fail:
    json_object_put(root);
    json_object_put(device);
    json_object_put(user);
    json_object_put(scene);
    return NULL;
}

static int apply_pc_acceleration_context(json_object *client)
{
    static const char *const names[] = {
        "appSession", "speedupSession", "gameId", "gameArea", "accMode",
        "gamePlatform", "gamePlatformId",
    };
    json_object *context = NULL;
    json_object *extensions;
    size_t index;
    int status = -1;

    if (!client) {
        errno = EINVAL;
        return -1;
    }
    if (load_private_json(DEFAULT_PC_CONTEXT_FILE, &context) != 0)
        return -1;
    extensions = object_member(client, "ex", json_type_object);
    if (!extensions) {
        errno = EINVAL;
        goto out;
    }
    for (index = 0; index < sizeof(names) / sizeof(names[0]); index++) {
        json_object *value = NULL;

        if (!json_object_object_get_ex(context, names[index], &value) ||
            (!json_object_is_type(value, json_type_string) &&
             !json_object_is_type(value, json_type_int))) {
            errno = EINVAL;
            goto out;
        }
        json_object_object_add(extensions, names[index], json_object_get(value));
    }
    status = 0;

out:
    cleanse_json_value(context);
    json_object_put(context);
    return status;
}

static int load_pc_signal_context(char **acc_pod_id, uint64_t *server_id)
{
    json_object *context = NULL;
    json_object *server;
    const char *pod;
    int64_t server_value;
    char *pod_copy = NULL;
    int status = -1;

    if (!acc_pod_id && !server_id) {
        errno = EINVAL;
        return -1;
    }
    if (acc_pod_id)
        *acc_pod_id = NULL;
    if (server_id)
        *server_id = 0;
    if (load_private_json(DEFAULT_PC_CONTEXT_FILE, &context) != 0)
        return -1;
    errno = EINVAL;
    pod = string_member(context, "accPodId");
    server = object_member(context, "serverId", json_type_int);
    if (!pod || !pod[0] || strlen(pod) > 1023 || !server)
        goto out;
    server_value = json_object_get_int64(server);
    if (server_value < 0 || server_value > INT32_MAX)
        goto out;
    if (acc_pod_id) {
        pod_copy = strdup(pod);
        if (!pod_copy) {
            errno = ENOMEM;
            goto out;
        }
    }
    if (server_id)
        *server_id = (uint64_t)server_value;
    if (acc_pod_id) {
        *acc_pod_id = pod_copy;
        pod_copy = NULL;
    }
    status = 0;

out:
    if (status != 0 && errno != ENOMEM)
        errno = EINVAL;
    if (pod_copy) {
        OPENSSL_cleanse(pod_copy, strlen(pod_copy));
        free(pod_copy);
    }
    cleanse_json_value(context);
    json_object_put(context);
    return status;
}

static size_t response_write(void *contents, size_t size, size_t count, void *userdata)
{
    struct response_buffer *buffer = userdata;
    size_t incoming;
    char *resized;

    if (count && size > SIZE_MAX / count)
        return 0;
    incoming = size * count;
    if (incoming > MAX_RESPONSE_SIZE || buffer->len > MAX_RESPONSE_SIZE - incoming) {
        buffer->too_large = true;
        return 0;
    }
    resized = realloc(buffer->data, buffer->len + incoming + 1);
    if (!resized)
        return 0;
    buffer->data = resized;
    memcpy(buffer->data + buffer->len, contents, incoming);
    buffer->len += incoming;
    buffer->data[buffer->len] = '\0';
    return incoming;
}

static int pc_httpdns_parse_ipv4_member(json_object *root, const char *name,
                                        struct pc_httpdns_ipv4_list *result)
{
    json_object *values;
    size_t index;

    if (!root || !name || !result)
        return -1;
    memset(result, 0, sizeof(*result));
    values = object_member(root, name, json_type_array);
    if (!values)
        return -1;
    for (index = 0; index < json_object_array_length(values) &&
                    result->count < PC_HTTPDNS_MAX_IPV4;
         index++) {
        json_object *value = json_object_array_get_idx(values, index);
        struct in_addr address;
        char canonical[INET_ADDRSTRLEN];
        const char *text;
        size_t duplicate;

        if (!value || !json_object_is_type(value, json_type_string))
            continue;
        text = json_object_get_string(value);
        if (!text || inet_pton(AF_INET, text, &address) != 1 ||
            !inet_ntop(AF_INET, &address, canonical, sizeof(canonical)))
            continue;
        for (duplicate = 0; duplicate < result->count; duplicate++) {
            if (strcmp(result->values[duplicate], canonical) == 0)
                break;
        }
        if (duplicate != result->count)
            continue;
        memcpy(result->values[result->count], canonical, strlen(canonical) + 1);
        result->count++;
    }
    return result->count ? 0 : -1;
}

static int pc_httpdns_parse_resolution(json_object *root, const char *host,
                                       struct pc_httpdns_ipv4_list *result)
{
    json_object *ttl;
    const char *returned_host;
    int64_t ttl_seconds;

    if (!root || !host || strcmp(host, PC_SIGNAL_HOST) != 0)
        return -1;
    returned_host = string_member(root, "host");
    ttl = object_member(root, "ttl", json_type_int);
    if (!returned_host || strcmp(returned_host, host) != 0 || !ttl)
        return -1;
    ttl_seconds = json_object_get_int64(ttl);
    if (ttl_seconds < 1 || ttl_seconds > 86400)
        return -1;
    return pc_httpdns_parse_ipv4_member(root, "ips", result);
}

static int pc_httpdns_signature(const char *host, int64_t expires_at,
                                char result[33])
{
    EVP_MD_CTX *context = NULL;
    unsigned char digest[EVP_MAX_MD_SIZE] = {0};
    unsigned int digest_length = 0;
    char timestamp[32] = {0};
    size_t index;
    int length;
    int status = -1;

    if (!host || strcmp(host, PC_SIGNAL_HOST) != 0 || !result || expires_at <= 0)
        return -1;
    length = snprintf(timestamp, sizeof(timestamp), "%lld",
                      (long long)expires_at);
    if (length <= 0 || (size_t)length >= sizeof(timestamp))
        return -1;
    context = EVP_MD_CTX_new();
    if (!context || EVP_DigestInit_ex(context, EVP_md5(), NULL) != 1 ||
        EVP_DigestUpdate(context, host, strlen(host)) != 1 ||
        EVP_DigestUpdate(context, "-", 1) != 1 ||
        EVP_DigestUpdate(context, pc_httpdns_signing_key,
                         sizeof(pc_httpdns_signing_key) - 1) != 1 ||
        EVP_DigestUpdate(context, "-", 1) != 1 ||
        EVP_DigestUpdate(context, timestamp, (size_t)length) != 1 ||
        EVP_DigestFinal_ex(context, digest, &digest_length) != 1 ||
        digest_length != 16)
        goto out;
    for (index = 0; index < digest_length; index++)
        snprintf(result + index * 2, 3, "%02x", digest[index]);
    result[32] = '\0';
    status = 0;

out:
    OPENSSL_cleanse(digest, sizeof(digest));
    OPENSSL_cleanse(timestamp, sizeof(timestamp));
    EVP_MD_CTX_free(context);
    return status;
}

static int pc_httpdns_fetch_json(const char *url, json_object **result)
{
    CURL *curl = NULL;
    struct response_buffer response = {0};
    json_object *parsed = NULL;
    CURLcode curl_status;
    long http_status = 0;
    int status = -1;

    if (!url || !result)
        return -1;
    *result = NULL;
    curl = curl_easy_init();
    if (!curl)
        goto out;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, response_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "cpp-httplib/0.43.1");
    curl_status = curl_easy_perform(curl);
    if (curl_status != CURLE_OK || response.too_large ||
        response.len > PC_HTTPDNS_MAX_RESPONSE_SIZE)
        goto out;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
    if (http_status < 200 || http_status >= 300)
        goto out;
    parsed = json_tokener_parse(response.data ? response.data : "");
    if (!parsed || !json_object_is_type(parsed, json_type_object))
        goto out;
    *result = parsed;
    parsed = NULL;
    status = 0;

out:
    json_object_put(parsed);
    free(response.data);
    if (curl)
        curl_easy_cleanup(curl);
    return status;
}

static int pc_httpdns_service_ips(struct pc_httpdns_ipv4_list *result)
{
    size_t index;

    if (!result)
        return -1;
    memset(result, 0, sizeof(*result));
    for (index = 0;
         index < sizeof(pc_httpdns_bootstrap_ips) /
                     sizeof(pc_httpdns_bootstrap_ips[0]);
         index++) {
        json_object *response = NULL;
        char *url = NULL;
        int parsed = -1;

        if (asprintf(&url, "https://%s/%s/ss",
                     pc_httpdns_bootstrap_ips[index],
                     PC_HTTPDNS_ACCOUNT_ID) >= 0 &&
            pc_httpdns_fetch_json(url, &response) == 0)
            parsed = pc_httpdns_parse_ipv4_member(response, "service_ip", result);
        free(url);
        json_object_put(response);
        if (parsed == 0)
            return 0;
    }
    errno = EHOSTUNREACH;
    return -1;
}

static int pc_httpdns_resolve_signal(struct pc_httpdns_ipv4_list *result)
{
    struct pc_httpdns_ipv4_list services = {0};
    json_object *response = NULL;
    char signature[33] = {0};
    char *url = NULL;
    time_t now;
    int64_t expires_at;
    size_t index;
    int status = -1;

    if (!result)
        return -1;
    memset(result, 0, sizeof(*result));
    now = time(NULL);
    if (now < 0 || (int64_t)now > INT64_MAX - PC_HTTPDNS_SIGNATURE_LIFETIME)
        return -1;
    expires_at = (int64_t)now + PC_HTTPDNS_SIGNATURE_LIFETIME;
    if (pc_httpdns_signature(PC_SIGNAL_HOST, expires_at, signature) != 0 ||
        pc_httpdns_service_ips(&services) != 0)
        goto out;
    for (index = 0; index < services.count; index++) {
        if (asprintf(&url,
                     "http://%s/%s/sign_d?host=%s&t=%lld&s=%s",
                     services.values[index], PC_HTTPDNS_ACCOUNT_ID,
                     PC_SIGNAL_HOST, (long long)expires_at, signature) < 0)
            goto out;
        if (pc_httpdns_fetch_json(url, &response) == 0 &&
            pc_httpdns_parse_resolution(response, PC_SIGNAL_HOST, result) == 0) {
            status = 0;
            goto out;
        }
        free(url);
        url = NULL;
        json_object_put(response);
        response = NULL;
    }
    errno = EHOSTUNREACH;

out:
    OPENSSL_cleanse(signature, sizeof(signature));
    free(url);
    json_object_put(response);
    return status;
}

static char *pc_httpdns_curl_resolve_entry(
    const struct pc_httpdns_ipv4_list *addresses)
{
    const size_t prefix_length = sizeof(PC_SIGNAL_HOST ":80:") - 1;
    size_t length = prefix_length + 1;
    size_t index;
    size_t offset;
    char *entry;

    if (!addresses || !addresses->count)
        return NULL;
    for (index = 0; index < addresses->count; index++) {
        size_t address_length = strlen(addresses->values[index]);

        if (!address_length || address_length >= INET_ADDRSTRLEN ||
            length > SIZE_MAX - address_length - (index ? 1U : 0U))
            return NULL;
        length += address_length + (index ? 1U : 0U);
    }
    entry = malloc(length);
    if (!entry)
        return NULL;
    memcpy(entry, PC_SIGNAL_HOST ":80:", prefix_length);
    offset = prefix_length;
    for (index = 0; index < addresses->count; index++) {
        size_t address_length = strlen(addresses->values[index]);

        if (index)
            entry[offset++] = ',';
        memcpy(entry + offset, addresses->values[index], address_length);
        offset += address_length;
    }
    entry[offset] = '\0';
    return entry;
}

static int decrypt_response(json_object *outer, const unsigned char key[16],
                            json_object **result)
{
    json_object *encrypted_json = NULL;
    const char *encrypted;
    struct bytes ciphertext = {0};
    struct bytes plaintext = {0};
    json_object *parsed = NULL;
    json_object *nested = NULL;
    int status = -1;

    if (!json_object_object_get_ex(outer, "d", &encrypted_json) ||
        !json_object_is_type(encrypted_json, json_type_string)) {
        *result = json_object_get(outer);
        return 0;
    }
    encrypted = json_object_get_string(encrypted_json);
    if (base64_decode(encrypted, &ciphertext) != 0 ||
        aes_decrypt(key, ciphertext.data, ciphertext.len, &plaintext) != 0)
        goto out;
    parsed = json_tokener_parse((const char *)plaintext.data);
    if (!parsed)
        goto out;
    if (json_object_is_type(parsed, json_type_string)) {
        nested = json_tokener_parse(json_object_get_string(parsed));
        if (nested) {
            json_object_put(parsed);
            parsed = nested;
            nested = NULL;
        }
    }
    *result = parsed;
    parsed = NULL;
    status = 0;

out:
    json_object_put(parsed);
    json_object_put(nested);
    bytes_free(&ciphertext);
    bytes_free(&plaintext);
    return status;
}

static int api_request(const char *endpoint, json_object *payload, json_object **result)
{
    unsigned char key[16] = {0};
    json_object *envelope = NULL;
    json_object *outer = NULL;
    char *request_id = NULL;
    char *url = NULL;
    const char *body;
    CURL *curl = NULL;
    struct curl_slist *headers = NULL;
    struct response_buffer response = {0};
    CURLcode curl_status;
    long http_status = 0;
    int status = -1;

    *result = NULL;
    envelope = build_envelope(payload, key);
    request_id = new_uuid();
    if (!envelope || !request_id)
        goto out;
    json_object_object_add(envelope, "requestId", json_object_new_string(request_id));
    body = json_object_to_json_string_ext(envelope, JSON_C_TO_STRING_PLAIN);
    if (asprintf(&url, "%s%s", LOGIN_ORIGIN, endpoint) < 0)
        goto out;

    curl = curl_easy_init();
    if (!curl)
        goto out;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    if (!headers)
        goto out;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, response_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "biubiu-acc/" BIUBIU_ACC_VERSION);

    curl_status = curl_easy_perform(curl);
    if (curl_status != CURLE_OK) {
        fprintf(stderr, "request failed: %s%s\n", curl_easy_strerror(curl_status),
                response.too_large ? " (response too large)" : "");
        goto out;
    }
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
    if (http_status < 200 || http_status >= 300) {
        fprintf(stderr, "request failed: HTTP %ld\n", http_status);
        goto out;
    }
    outer = json_tokener_parse(response.data ? response.data : "");
    if (!outer) {
        fprintf(stderr, "request failed: invalid JSON response\n");
        goto out;
    }
    if (decrypt_response(outer, key, result) != 0) {
        fprintf(stderr, "request failed: unable to decrypt response\n");
        goto out;
    }
    status = 0;

out:
    OPENSSL_cleanse(key, sizeof(key));
    free(request_id);
    free(url);
    free(response.data);
    json_object_put(envelope);
    json_object_put(outer);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return status;
}

static char *new_request_id(void)
{
    struct timespec now;
    unsigned long long milliseconds;
    char *value = NULL;

    if (clock_gettime(CLOCK_REALTIME, &now) != 0)
        return NULL;
    milliseconds = (unsigned long long)now.tv_sec * 1000ULL +
                  (unsigned long long)now.tv_nsec / 1000000ULL;
    if (asprintf(&value, "%llu", milliseconds) < 0)
        return NULL;
    return value;
}

static char *new_native_trace_id(void)
{
    struct timespec now;
    unsigned long long milliseconds;
    char *value = NULL;

    /* The native client sends GetTickCount64(), i.e. milliseconds since boot. */
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return NULL;
    milliseconds = (unsigned long long)now.tv_sec * 1000ULL +
                   (unsigned long long)now.tv_nsec / 1000000ULL;
    if (asprintf(&value, "%llu", milliseconds) < 0)
        return NULL;
    return value;
}

static char *new_native_app_session(void)
{
    char *uuid = new_uuid();
    char *value;
    size_t source = 0;
    size_t target = 0;

    if (!uuid)
        return NULL;
    value = malloc(33);
    if (!value) {
        OPENSSL_cleanse(uuid, strlen(uuid));
        free(uuid);
        return NULL;
    }
    while (uuid[source]) {
        unsigned char character = (unsigned char)uuid[source++];

        if (character == '-')
            continue;
        value[target] = target >= 12 && target < 16 ?
            (char)tolower(character) : (char)toupper(character);
        target++;
    }
    value[target] = '\0';
    OPENSSL_cleanse(uuid, strlen(uuid));
    free(uuid);
    if (target != 32) {
        free(value);
        return NULL;
    }
    return value;
}

static char *new_native_speedup_session(void)
{
    unsigned char guid[16];
    uint32_t data1;
    time_t now;
    struct tm local_time;
    char timestamp[13];
    unsigned int time_parts[6];
    size_t index;
    char *value;

    if (RAND_bytes(guid, sizeof(guid)) != 1)
        return NULL;

    /* CoCreateGuid stores Data1 little-endian in memory on Windows. */
    guid[7] = (guid[7] & 0x0f) | 0x40;
    guid[8] = (guid[8] & 0x3f) | 0x80;
    data1 = (uint32_t)guid[0] |
            (uint32_t)guid[1] << 8 |
            (uint32_t)guid[2] << 16 |
            (uint32_t)guid[3] << 24;
    now = time(NULL);
    if (now == (time_t)-1 || !localtime_r(&now, &local_time)) {
        OPENSSL_cleanse(guid, sizeof(guid));
        return NULL;
    }
    time_parts[0] = ((unsigned int)local_time.tm_year + 1900U) % 100U;
    time_parts[1] = ((unsigned int)local_time.tm_mon + 1U) % 100U;
    time_parts[2] = (unsigned int)local_time.tm_mday % 100U;
    time_parts[3] = (unsigned int)local_time.tm_hour % 100U;
    time_parts[4] = (unsigned int)local_time.tm_min % 100U;
    time_parts[5] = (unsigned int)local_time.tm_sec % 100U;
    for (index = 0; index < 6; index++) {
        timestamp[index * 2] = (char)('0' + time_parts[index] / 10U);
        timestamp[index * 2 + 1] = (char)('0' + time_parts[index] % 10U);
    }
    timestamp[12] = '\0';
    if (asprintf(&value,
                 "v2-%s-%08X%02X%02X%02X%02X%02X%02X",
                 timestamp, data1, guid[10], guid[11], guid[12],
                 guid[13], guid[14], guid[15]) < 0) {
        OPENSSL_cleanse(guid, sizeof(guid));
        return NULL;
    }
    OPENSSL_cleanse(guid, sizeof(guid));
    if (strlen(value) != 36) {
        OPENSSL_cleanse(value, strlen(value));
        free(value);
        return NULL;
    }
    return value;
}

static char *new_client_session_id(void)
{
    char *request_id = new_request_id();
    char *value = NULL;

    if (!request_id)
        return NULL;
    if (asprintf(&value, "task-%s", request_id) < 0)
        value = NULL;
    free(request_id);
    return value;
}

static uint32_t native_utdid_hash(const char *utdid)
{
    uint32_t hash = 0;
    uint32_t counter = 0x100;

    while (utdid && *utdid) {
        uint8_t character = (uint8_t)*utdid++;
        uint32_t value = (uint32_t)(int32_t)(int8_t)character | counter;
        unsigned rotation = ((value >> 2) ^ character) & 0xf;

        counter += 0x100;
        if (rotation)
            hash = (hash << rotation) | (hash >> (32 - rotation));
        hash ^= value * value;
    }
    return hash ^ (hash >> 16);
}

static json_object *build_maga_client(const char *device_id)
{
    char *init_time = NULL;
    json_object *client = NULL;
    json_object *extensions = NULL;

    if (!device_id || !device_id[0] || !(init_time = new_request_id()))
        return NULL;
    client = json_object_new_object();
    extensions = json_object_new_object();
    if (!client || !extensions)
        goto fail;

    /* Match MagaManager.fillClientEx/ConnectRequest.Client. */
    json_object_object_add(client, "appId", json_object_new_string(MAGA_APP_ID));
    json_object_object_add(client, "deviceId",
                           json_object_new_string(device_id));
    json_object_object_add(client, "deviceIdType",
                           json_object_new_string("utdid"));
    json_object_object_add(extensions, "appName",
                           json_object_new_string(MAGA_PACKAGE_NAME));
    json_object_object_add(extensions, "os", json_object_new_string("android"));
    json_object_object_add(extensions, "ver",
                           json_object_new_string(MAGA_APP_VERSION));
    json_object_object_add(extensions, "ch", json_object_new_string(""));
    json_object_object_add(extensions, "imei", json_object_new_string(""));
    json_object_object_add(extensions, "build", json_object_new_string(""));
    json_object_object_add(extensions, "imsi", json_object_new_string(""));
    json_object_object_add(extensions, "apiLevel", json_object_new_string("0"));
    json_object_object_add(extensions, "height", json_object_new_string("0"));
    json_object_object_add(extensions, "width", json_object_new_string("0"));
    json_object_object_add(extensions, "mac", json_object_new_string(""));
    json_object_object_add(extensions, "model",
                           json_object_new_string("OpenWrt"));
    json_object_object_add(extensions, "brand",
                           json_object_new_string("OpenWrt"));
    json_object_object_add(extensions, "versionCode",
                           json_object_new_string(MAGA_APP_VERSION_CODE));
    json_object_object_add(extensions, "fr", json_object_new_string("0"));
    json_object_object_add(extensions, "network",
                           json_object_new_string("unknown"));
    json_object_object_add(extensions, "initTime",
                           json_object_new_string(init_time));
    json_object_object_add(extensions, "screen", json_object_new_string(""));
    json_object_object_add(extensions, "phoneinfo",
                           json_object_new_string(""));
    json_object_object_add(client, "ex", extensions);
    extensions = NULL;
    free(init_time);
    return client;

fail:
    free(init_time);
    json_object_put(extensions);
    json_object_put(client);
    return NULL;
}

static json_object *build_acceleration_client(const char *device_id,
                                              const char *biubiu_id,
                                              const char *service_ticket,
                                              enum acceleration_client_kind kind)
{
    json_object *client = NULL;
    json_object *extensions = NULL;
    char *app_session = NULL;
    int32_t biubiu_id_value = 0;

    if (!device_id || !device_id[0])
        goto fail;
    if (kind == ACCELERATION_CLIENT_WINDOWS) {
        if (!valid_native_utdid(device_id) ||
            !native_biubiu_id(biubiu_id, &biubiu_id_value))
            goto fail;
        app_session = new_native_app_session();
        client = json_object_new_object();
        extensions = json_object_new_object();
        if (!client || !extensions || !app_session)
            goto fail;
        json_object_object_add(client, "appId",
                               json_object_new_string(PC_APP_ID));
        json_object_object_add(client, "deviceId",
                               json_object_new_string(device_id));
        json_object_object_add(client, "deviceIdType",
                               json_object_new_string("utdid"));
        json_object_object_add(client, "os",
                               json_object_new_string("windows"));
        json_object_object_add(client, "osVersion",
                               json_object_new_string(PC_OS_VERSION));
        json_object_object_add(client, "biuid",
                               json_object_new_int(biubiu_id_value));
        json_object_object_add(extensions, "appId",
                               json_object_new_string(PC_APP_ID));
        json_object_object_add(extensions, "versionCode",
                               json_object_new_string(PC_APP_VERSION_CODE));
        json_object_object_add(extensions, "ch",
                               json_object_new_string(PC_CHANNEL));
        json_object_object_add(extensions, "os",
                               json_object_new_string("windows"));
        json_object_object_add(extensions, "build",
                               json_object_new_string(PC_BUILD_ID));
        json_object_object_add(extensions, "defaultChFlag",
                               json_object_new_string(""));
        json_object_object_add(extensions, "mac", json_object_new_string(""));
        json_object_object_add(extensions, "uid",
                               json_object_new_string(biubiu_id ? biubiu_id : ""));
        json_object_object_add(extensions, "ver",
                               json_object_new_string(PC_APP_VERSION));
        json_object_object_add(extensions, "appName",
                               json_object_new_string(PC_APP_NAME));
        json_object_object_add(extensions, "st",
                               json_object_new_string(service_ticket ?
                                                          service_ticket : ""));
        json_object_object_add(extensions, "appVersion",
                               json_object_new_string(PC_APP_VERSION));
        json_object_object_add(extensions, "osVersion",
                               json_object_new_string(PC_OS_VERSION));
        json_object_object_add(extensions, "osProductName",
                               json_object_new_string(""));
        json_object_object_add(extensions, "cpuArchitecture",
                               json_object_new_string("x64"));
        json_object_object_add(extensions, "packageType",
                               json_object_new_int(1));
        json_object_object_add(extensions, "appSession",
                               json_object_new_string(app_session));
        json_object_object_add(extensions, "speedupSession",
                               json_object_new_string(""));
        json_object_object_add(extensions, "gameId", json_object_new_int(-1));
        json_object_object_add(extensions, "gameArea", json_object_new_int(-1));
        json_object_object_add(extensions, "accMode", json_object_new_int(-1));
        json_object_object_add(extensions, "gamePlatform",
                               json_object_new_string(""));
        json_object_object_add(extensions, "gamePlatformId",
                               json_object_new_int(-1));
        json_object_object_add(extensions, "uthash",
                               json_object_new_int64(
                                   (int64_t)native_utdid_hash(device_id)));
        json_object_object_add(extensions, "network",
                               json_object_new_string(""));
        json_object_object_add(extensions, "rmac", json_object_new_string(""));
        json_object_object_add(client, "ex", extensions);
        extensions = NULL;
        OPENSSL_cleanse(app_session, strlen(app_session));
        free(app_session);
        app_session = NULL;
    } else {
        client = build_maga_client(device_id);
        if (!client)
            goto fail;
    }
    extensions = object_member(client, "ex", json_type_object);
    if (!extensions)
        goto fail;
    if (kind != ACCELERATION_CLIENT_WINDOWS)
        json_object_object_add(extensions, "st",
                               json_object_new_string(service_ticket ?
                                                          service_ticket : ""));
    return client;

fail:
    if (app_session) {
        OPENSSL_cleanse(app_session, strlen(app_session));
        free(app_session);
    }
    cleanse_json_value(extensions);
    json_object_put(extensions);
    cleanse_json_value(client);
    json_object_put(client);
    return NULL;
}

static json_object *build_acceleration_client_header(
    json_object *client, enum acceleration_client_kind kind)
{
    json_object *header = NULL;
    json_object *extensions;
    const char *version;
    const char *os;
    const char *channel;
    const char *app_name;

    if (!client)
        return NULL;
    if (kind != ACCELERATION_CLIENT_WINDOWS)
        return json_object_get(client);
    extensions = object_member(client, "ex", json_type_object);
    version = extensions ? string_member(extensions, "ver") : NULL;
    os = string_member(client, "os");
    channel = extensions ? string_member(extensions, "ch") : NULL;
    app_name = extensions ? string_member(extensions, "appName") : NULL;
    if (!version || !os || !channel || !app_name)
        return NULL;
    header = json_object_new_object();
    if (!header)
        return NULL;
    json_object_object_add(header, "ver", json_object_new_string(version));
    json_object_object_add(header, "os", json_object_new_string(os));
    json_object_object_add(header, "ch", json_object_new_string(channel));
    json_object_object_add(header, "appName", json_object_new_string(app_name));
    return header;
}

static json_object *build_acceleration_request(json_object *data,
                                               json_object *client)
{
    json_object *request = NULL;
    char *request_id = NULL;
    const char *client_json;

    if (!data || !client)
        return NULL;
    request_id = new_request_id();
    client_json = json_object_to_json_string_ext(client, JSON_C_TO_STRING_PLAIN);
    request = json_object_new_object();
    if (!request || !request_id || !client_json)
        goto fail;
    json_object_object_add(request, "data", json_object_get(data));
    json_object_object_add(request, "id", json_object_new_string(request_id));
    json_object_object_add(request, "client", json_object_new_string(client_json));
    free(request_id);
    return request;

fail:
    free(request_id);
    json_object_put(request);
    return NULL;
}

static json_object *build_native_control_request(json_object *data,
                                                 json_object *client)
{
    json_object *request = NULL;
    const char *client_json;

    if (!data || !client)
        return NULL;
    client_json = json_object_to_json_string_ext(client,
                                                  JSON_C_TO_STRING_PLAIN);
    request = json_object_new_object();
    if (!client_json || !request)
        goto fail;
    json_object_object_add(request, "client",
                           json_object_new_string(client_json));
    json_object_object_add(request, "data", json_object_get(data));
    /* bbaddon 5.0.2.64 leaves ClientData::id zero-initialized. */
    json_object_object_add(request, "id", json_object_new_string(""));
    return request;

fail:
    json_object_put(request);
    return NULL;
}

static bool acceleration_code_success(json_object *code, bool business_code)
{
    if (!code)
        return false;
    if (json_object_is_type(code, json_type_string)) {
        const char *value = json_object_get_string(code);

        return value && (!strcmp(value, "SUCCESS") || !strcmp(value, "200") ||
                         !strcmp(value, "2000000") || !strcmp(value, "2000001") ||
                         (!business_code && !strcmp(value, "0")));
    }
    if (json_object_is_type(code, json_type_int)) {
        int64_t value = json_object_get_int64(code);

        return value == 200 || value == 2000000 || value == 2000001 ||
               (!business_code && value == 0);
    }
    return false;
}

static json_object *acceleration_response_state_code(json_object *response)
{
    json_object *state = NULL;
    json_object *code = NULL;
    json_object *data = NULL;

    if (!response)
        return NULL;
    if (json_object_object_get_ex(response, "state", &state) &&
        json_object_is_type(state, json_type_object) &&
        json_object_object_get_ex(state, "code", &code))
        return code;
    if (json_object_object_get_ex(response, "data", &data) &&
        json_object_is_type(data, json_type_object) &&
        json_object_object_get_ex(data, "state", &state) &&
        json_object_is_type(state, json_type_object) &&
        json_object_object_get_ex(state, "code", &code))
        return code;
    return NULL;
}

static const char *acceleration_response_state_message(json_object *response)
{
    json_object *code = NULL;
    json_object *state = NULL;
    json_object *data = NULL;
    const char *message;

    if (!response)
        return NULL;
    if (json_object_object_get_ex(response, "code", &code) &&
        !acceleration_code_success(code, false)) {
        message = string_member(response, "message");
        if (message && message[0])
            return message;
    }
    if (json_object_object_get_ex(response, "state", &state) &&
        json_object_is_type(state, json_type_object)) {
        message = string_member(state, "msg");
        if (message && message[0])
            return message;
    }
    if (json_object_object_get_ex(response, "data", &data) &&
        json_object_is_type(data, json_type_object) &&
        json_object_object_get_ex(data, "state", &state) &&
        json_object_is_type(state, json_type_object)) {
        message = string_member(state, "msg");
        if (message && message[0])
            return message;
    }
    return NULL;
}

static bool acceleration_response_success(json_object *response)
{
    json_object *value = NULL;
    json_object *data = NULL;

    if (!response)
        return false;
    if (json_object_object_get_ex(response, "code", &value) &&
        !acceleration_code_success(value, false))
        return false;
    value = acceleration_response_state_code(response);
    if (value)
        return acceleration_code_success(value, true);
    if (json_object_object_get_ex(response, "data", &data) &&
        json_object_is_type(data, json_type_object) &&
        json_object_object_get_ex(data, "success", &value) &&
        json_object_is_type(value, json_type_boolean))
        return json_object_get_boolean(value);
    if (json_object_object_get_ex(response, "success", &value) &&
        json_object_is_type(value, json_type_boolean))
        return json_object_get_boolean(value);
    if (!json_object_object_get_ex(response, "code", &value))
        return false;
    return acceleration_code_success(value, false);
}

static int print_acceleration_result_summary(const char *operation,
                                             json_object *response,
                                             const char *stored_path)
{
    json_object *summary = NULL;
    json_object *code = NULL;
    json_object *data = NULL;
    json_object *response_keys = NULL;
    const char *state_message;
    size_t response_key_count = 0;

    summary = json_object_new_object();
    if (!summary)
        return 1;
    json_object_object_add(summary, "success",
                           json_object_new_boolean(acceleration_response_success(response)));
    json_object_object_add(summary, "operation",
                           json_object_new_string(operation ? operation : "control"));
    if (response && json_object_object_get_ex(response, "code", &code))
        json_object_object_add(summary, "code", json_object_get(code));
    if (response && json_object_object_get_ex(response, "c", &code) &&
        (json_object_is_type(code, json_type_int) ||
         (json_object_is_type(code, json_type_string) &&
          json_object_get_string_len(code) <= 32)))
        json_object_object_add(summary, "transportCode", json_object_get(code));
    code = acceleration_response_state_code(response);
    if (code)
        json_object_object_add(summary, "stateCode", json_object_get(code));
    state_message = acceleration_response_state_message(response);
    if (state_message && strlen(state_message) <= 512)
        json_object_object_add(summary, "stateMessage",
                               json_object_new_string(state_message));
    json_object_object_add(summary, "stored",
                           json_object_new_boolean(stored_path != NULL));
    if (stored_path)
        json_object_object_add(summary, "stateFile",
                               json_object_new_string(stored_path));
    if (response && json_object_object_get_ex(response, "data", &data))
        json_object_object_add(summary, "hasData", json_object_new_boolean(true));
    if (response && json_object_is_type(response, json_type_object)) {
        response_keys = json_object_new_array();
        if (response_keys) {
            json_object_object_foreach(response, name, value) {
                (void)value;
                if (response_key_count++ >= 32)
                    break;
                json_object_array_add(response_keys,
                                      json_object_new_string(name));
            }
            json_object_object_add(summary, "responseKeys", response_keys);
            response_keys = NULL;
        }
    }
    puts(json_object_to_json_string_ext(summary, JSON_C_TO_STRING_PLAIN));
    json_object_put(response_keys);
    json_object_put(summary);
    return 0;
}

static int print_runtime_result_summary(const char *operation,
                                        json_object *runtime,
                                        const char *stored_path)
{
    json_object *summary = json_object_new_object();
    json_object *outbounds = object_member(runtime, "outbounds", json_type_array);
    json_object *routes = object_member(runtime, "routes", json_type_array);

    if (!summary)
        return 1;
    json_object_object_add(summary, "success", json_object_new_boolean(true));
    json_object_object_add(summary, "operation",
                           json_object_new_string(operation));
    json_object_object_add(summary, "stored", json_object_new_boolean(true));
    json_object_object_add(summary, "stateFile",
                           json_object_new_string(stored_path));
    json_object_object_add(
        summary, "outboundCount",
        json_object_new_int64(outbounds ?
            (int64_t)json_object_array_length(outbounds) : 0));
    json_object_object_add(
        summary, "routeCount",
        json_object_new_int64(routes ?
            (int64_t)json_object_array_length(routes) : 0));
    puts(json_object_to_json_string_ext(summary, JSON_C_TO_STRING_PLAIN));
    json_object_put(summary);
    return 0;
}

static int build_acceleration_api_url(
    enum acceleration_client_kind client_kind, const char *endpoint,
    const char **host, bool *use_signal_httpdns, char **result)
{
    const char *origin;

    if (!endpoint || !host || !use_signal_httpdns || !result) {
        errno = EINVAL;
        return -1;
    }
    *result = NULL;
    *use_signal_httpdns = false;
    if (client_kind == ACCELERATION_CLIENT_WINDOWS) {
        /* loginV2 and getChannelStV2 are ping-signal API paths, but the
         * official native client sends both through the sz-maga API origin. */
        *host = PC_ACCELERATION_HOST;
        origin = PC_ACCELERATION_ORIGIN;
        if (!strstr(endpoint, "df=adat"))
            return asprintf(result, "%s%s&df=adat", origin, endpoint) < 0 ?
                       -1 : 0;
    } else if (client_kind == ACCELERATION_CLIENT_MOBILE) {
        *host = ACCELERATION_HOST;
        origin = ACCELERATION_ORIGIN;
    } else {
        errno = EINVAL;
        return -1;
    }
    return asprintf(result, "%s%s", origin, endpoint) < 0 ? -1 : 0;
}

static int acceleration_api_request_once(
    const char *endpoint, json_object *data, const char *session_file,
    const char *acceleration_key_file,
    enum acceleration_client_kind client_kind, json_object **result)
{
    json_object *record = NULL;
    json_object *client = NULL;
    json_object *client_header_value = NULL;
    json_object *request = NULL;
    json_object *envelope = NULL;
    json_object *outer = NULL;
    struct acceleration_public_key key = {0};
    struct adat_session_keys keys = {{0}, {0}};
    struct pc_httpdns_ipv4_list signal_addresses = {0};
    const char *service_ticket = NULL;
    char *url = NULL;
    char *trace_id = NULL;
    char *trace_header = NULL;
    char *client_header = NULL;
    char *version_header = NULL;
    char *cookie_header = NULL;
    char *pc_biubiu_id = NULL;
    char *pc_device_id = NULL;
    char *resolve_entry = NULL;
    const char *body = NULL;
    const char *client_header_json = NULL;
    const char *api_version;
    const char *api_version_end;
    const char *acceleration_host;
    CURL *curl = NULL;
    struct curl_slist *headers = NULL;
    struct curl_slist *resolve_hosts = NULL;
    struct response_buffer response = {0};
    CURLcode curl_status;
    long http_status = 0;
    bool signal_request;
    bool legacy_signal_response;
    bool native_signal_login;
    bool native_channel_ticket;
    bool use_signal_httpdns;
    int outer_code;
    int decrypt_status;
    int status = -1;

    if (!endpoint || !data || !result) {
        errno = EINVAL;
        return -1;
    }
    signal_request = strcmp(endpoint, SIGNAL_LOGIN_ENDPOINT) == 0 ||
                     strcmp(endpoint, CHANNEL_TICKET_ENDPOINT) == 0;
    native_signal_login = client_kind == ACCELERATION_CLIENT_WINDOWS &&
                          strcmp(endpoint, SIGNAL_LOGIN_ENDPOINT) == 0;
    native_channel_ticket = client_kind == ACCELERATION_CLIENT_WINDOWS &&
                            strcmp(endpoint, CHANNEL_TICKET_ENDPOINT) == 0;
    legacy_signal_response = signal_request &&
                             client_kind == ACCELERATION_CLIENT_MOBILE;
    if (!signal_request) {
        if ((client_kind == ACCELERATION_CLIENT_WINDOWS &&
             (!strstr(endpoint, "?ver=") || strstr(endpoint, "df=adat"))) ||
            (client_kind == ACCELERATION_CLIENT_MOBILE &&
             !strstr(endpoint, "df=adat"))) {
            errno = EINVAL;
            return -1;
        }
    }
    *result = NULL;
    if (build_acceleration_api_url(client_kind, endpoint, &acceleration_host,
                                   &use_signal_httpdns, &url) != 0)
        return -1;
    if (load_session_record(session_file, &record) != 0 ||
        session_acceleration_identity(record, NULL, &service_ticket) != 0) {
        fputs("acceleration control requires a valid private account session\n",
              stderr);
        goto out;
    }
    if (client_kind == ACCELERATION_CLIENT_MOBILE && !native_signal_login &&
        !native_channel_ticket &&
        session_cookie_header(record, acceleration_host, &cookie_header) < 0) {
        fputs("acceleration control rejected an invalid session Cookie set\n",
              stderr);
        goto out;
    }
    if (load_acceleration_key_or_seed(acceleration_key_file, &key) != 0) {
        fputs("acceleration control requires a cached ADAT public key\n", stderr);
        goto out;
    }
    if (client_kind == ACCELERATION_CLIENT_WINDOWS &&
        strcmp(endpoint, PC_USER_INFO_ENDPOINT) != 0 &&
        load_pc_biubiu_id(DEFAULT_PC_USER_FILE, &pc_biubiu_id) != 0 &&
        errno != ENOENT) {
        fputs("stored acceleration account identity is invalid\n", stderr);
        goto out;
    }
    if (client_kind == ACCELERATION_CLIENT_WINDOWS &&
        resolve_pc_device_identity(DEFAULT_PC_DEVICE_ID_FILE, &pc_device_id) != 0) {
        fprintf(stderr, "unable to load the private Windows UTDID: %s\n",
                strerror(errno));
        goto out;
    }
    client = build_acceleration_client(
        client_kind == ACCELERATION_CLIENT_WINDOWS ?
            pc_device_id :
            string_member(object_member(record, "local", json_type_object),
                          "deviceId"),
        pc_biubiu_id, service_ticket, client_kind);
    if (client_kind == ACCELERATION_CLIENT_WINDOWS && client &&
        apply_pc_acceleration_context(client) != 0 && errno != ENOENT) {
        fputs("stored acceleration context is invalid\n", stderr);
        goto out;
    }
    client_header_value = build_acceleration_client_header(client, client_kind);
    client_header_json = client_header_value ?
        json_object_to_json_string_ext(client_header_value,
                                       JSON_C_TO_STRING_PLAIN) : NULL;
    if (!client || !client_header_json ||
        asprintf(&client_header, "x-biu-client: %s", client_header_json) < 0)
        goto out;
    trace_id = client_kind == ACCELERATION_CLIENT_WINDOWS ?
        new_native_trace_id() : new_request_id();
    if (!trace_id ||
        asprintf(&trace_header, "x-biu-traceid: %s", trace_id) < 0)
        goto out;
    if (native_signal_login || native_channel_ticket)
        request = build_native_control_request(data, client);
    else
        request = build_acceleration_request(data, client);
    envelope = build_adat_envelope(request, key.version, key.key, &keys);
    body = envelope ? json_object_to_json_string_ext(
                          envelope, JSON_C_TO_STRING_PLAIN) : NULL;
    if (!body)
        goto out;
    if (client_kind == ACCELERATION_CLIENT_WINDOWS) {
        /* bbengine copies ClientData::appVersion to the C string used by
         * NetModule for x-biu-ver. It is distinct from engineVersion. */
        if (asprintf(&version_header, "x-biu-ver: %s", PC_APP_VERSION) < 0)
            goto out;
    } else if ((api_version = strstr(endpoint, "ver=")) != NULL) {
        api_version += strlen("ver=");
        api_version_end = strchr(api_version, '&');
        if (asprintf(&version_header, "x-biu-ver: %.*s",
                     (int)(api_version_end ?
                               (size_t)(api_version_end - api_version) :
                               strlen(api_version)),
                     api_version) < 0)
            goto out;
    }
    if (use_signal_httpdns) {
        if (pc_httpdns_resolve_signal(&signal_addresses) != 0) {
            fputs("unable to resolve the signal service through official HTTPDNS\n",
                  stderr);
            goto out;
        }
        resolve_entry = pc_httpdns_curl_resolve_entry(&signal_addresses);
        if (!resolve_entry ||
            !(resolve_hosts = curl_slist_append(resolve_hosts, resolve_entry)))
            goto out;
    }
    curl = curl_easy_init();
    if (!curl)
        goto out;
    headers = curl_slist_append(
        headers, native_signal_login || native_channel_ticket ?
                     "Content-Type: application/json;charset=utf-8" :
                     "Content-Type: application/json");
    if (client_kind == ACCELERATION_CLIENT_MOBILE && !native_signal_login &&
        !native_channel_ticket) {
        headers = curl_slist_append(headers, "Accept: application/json");
        headers = curl_slist_append(headers, "os: android");
    }
    /* The TLS capture confirms loginV2 uses the regular ADAT transport. */
    if (client_header)
        headers = curl_slist_append(headers, client_header);
    headers = curl_slist_append(headers, trace_header);
    if (version_header)
        headers = curl_slist_append(headers, version_header);
    if (native_signal_login || native_channel_ticket)
        headers = curl_slist_append(headers, "platform: windows");
    if (cookie_header) {
        struct curl_slist *updated_headers =
            curl_slist_append(headers, cookie_header);

        if (!updated_headers)
            goto out;
        headers = updated_headers;
    }
    if (!headers)
        goto out;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, response_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    if (resolve_hosts) {
        curl_easy_setopt(curl, CURLOPT_RESOLVE, resolve_hosts);
        curl_easy_setopt(curl, CURLOPT_NOPROXY, acceleration_host);
        curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    }
    if (client_kind == ACCELERATION_CLIENT_MOBILE && !native_signal_login &&
        !native_channel_ticket)
        curl_easy_setopt(curl, CURLOPT_USERAGENT, MAGA_USER_AGENT);
    curl_status = curl_easy_perform(curl);
    if (curl_status != CURLE_OK) {
        fprintf(stderr, "acceleration request failed: %s%s\n",
                curl_easy_strerror(curl_status),
                response.too_large ? " (response too large)" : "");
        goto out;
    }
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
    if (http_status < 200 || http_status >= 300) {
        const char *provider_message = NULL;

        outer = json_tokener_parse(response.data ? response.data : "");
        if (outer) {
            provider_message = string_member(outer, "message");
            if (!provider_message || !provider_message[0])
                provider_message = string_member(outer, "error");
        }
        if (provider_message && strlen(provider_message) <= 256)
            fprintf(stderr, "acceleration request failed: HTTP %ld (%s)\n",
                    http_status, provider_message);
        else
            fprintf(stderr, "acceleration request failed: HTTP %ld\n",
                    http_status);
        goto out;
    }
    outer = json_tokener_parse(response.data ? response.data : "");
    if (!outer) {
        fputs("acceleration request returned invalid JSON\n", stderr);
        goto out;
    }
    decrypt_status = legacy_signal_response ?
        decrypt_signal_response(outer, result) :
        decrypt_adat_response(outer, &keys, result);
    if (!legacy_signal_response && decrypt_status == 2) {
        status = 2;
        goto out;
    }
    if (decrypt_status != 0) {
        if (!legacy_signal_response && adat_outer_code(outer, &outer_code))
            fprintf(stderr,
                    "unable to decrypt acceleration response (ADAT c=%d)\n",
                    outer_code);
        else
            fputs("unable to decrypt acceleration response\n", stderr);
        goto out;
    }
    if (native_signal_login || native_channel_ticket)
        json_object_object_add(*result, "receivedAt",
                               json_object_new_int64((int64_t)time(NULL)));
    status = 0;

out:
    OPENSSL_cleanse(&keys, sizeof(keys));
    acceleration_public_key_free(&key);
    free(url);
    if (client_header)
        OPENSSL_cleanse(client_header, strlen(client_header));
    free(client_header);
    free(version_header);
    free(trace_header);
    free(trace_id);
    if (pc_biubiu_id)
        OPENSSL_cleanse(pc_biubiu_id, strlen(pc_biubiu_id));
    free(pc_biubiu_id);
    if (pc_device_id)
        OPENSSL_cleanse(pc_device_id, strlen(pc_device_id));
    free(pc_device_id);
    free(resolve_entry);
    if (cookie_header)
        OPENSSL_cleanse(cookie_header, strlen(cookie_header));
    free(cookie_header);
    free(response.data);
    json_object_put(outer);
    cleanse_json_value(envelope);
    json_object_put(envelope);
    cleanse_json_value(request);
    json_object_put(request);
    cleanse_json_value(client);
    json_object_put(client);
    cleanse_json_value(client_header_value);
    json_object_put(client_header_value);
    cleanse_json_value(record);
    json_object_put(record);
    cleanse_cookie_headers(headers);
    curl_slist_free_all(headers);
    curl_slist_free_all(resolve_hosts);
    curl_easy_cleanup(curl);
    return status;
}

static int acceleration_api_request(const char *endpoint, json_object *data,
                                    const char *session_file,
                                    const char *acceleration_key_file,
                                    enum acceleration_client_kind client_kind,
                                    json_object **result)
{
    json_object *rotation = NULL;
    struct acceleration_public_key key = {0};
    int status;

    if (!result) {
        errno = EINVAL;
        return -1;
    }
    *result = NULL;
    status = acceleration_api_request_once(endpoint, data, session_file,
                                           acceleration_key_file, client_kind,
                                           &rotation);
    if (status != 2) {
        *result = rotation;
        return status;
    }
    if (parse_native_key_rotation(rotation, &key) != 0) {
        fputs("acceleration service returned an invalid ADAT key rotation\n",
              stderr);
        goto rotation_failed;
    }
    if (store_acceleration_key(acceleration_key_file, &key) != 0) {
        fprintf(stderr, "unable to store rotated acceleration key: %s\n",
                strerror(errno));
        goto rotation_failed;
    }
    acceleration_public_key_free(&key);
    cleanse_json_value(rotation);
    json_object_put(rotation);
    return acceleration_api_request_once(endpoint, data, session_file,
                                         acceleration_key_file, client_kind,
                                         result);

rotation_failed:
    acceleration_public_key_free(&key);
    cleanse_json_value(rotation);
    json_object_put(rotation);
    *result = NULL;
    return 2;
}

static int parse_unsigned_argument(const char *value, uint64_t maximum,
                                   uint64_t *result)
{
    char *end = NULL;
    unsigned long long parsed;

    if (!value || !value[0] || !decimal_string(value, 1, 20))
        return -1;
    errno = 0;
    parsed = strtoull(value, &end, 10);
    if (errno == ERANGE || !end || *end || parsed == 0 || parsed > maximum)
        return -1;
    *result = (uint64_t)parsed;
    return 0;
}

static int parse_nonnegative_argument(const char *value, uint64_t maximum,
                                      uint64_t *result)
{
    char *end = NULL;
    unsigned long long parsed;

    if (!value || !value[0] || !decimal_string(value, 1, 20))
        return -1;
    errno = 0;
    parsed = strtoull(value, &end, 10);
    if (errno == ERANGE || !end || *end || parsed > maximum)
        return -1;
    *result = (uint64_t)parsed;
    return 0;
}

static int run_acceleration_operation(const char *operation,
                                      const char *endpoint, json_object *data,
                                      const char *store_path,
                                      const char *session_file,
                                      const char *acceleration_key_file,
                                      enum acceleration_client_kind client_kind)
{
    json_object *result = NULL;
    bool stored = false;
    int request_status;
    int status = 1;

    request_status = acceleration_api_request(endpoint, data, session_file,
                                              acceleration_key_file, client_kind,
                                              &result);
    if (request_status != 0)
        goto out;
    if (!acceleration_response_success(result)) {
        print_acceleration_result_summary(operation, result, NULL);
        status = 3;
        goto out;
    }
    if (store_path && store_private_json(store_path, result) != 0) {
        fprintf(stderr, "unable to store %s response: %s\n", operation,
                strerror(errno));
        goto out;
    }
    stored = store_path != NULL;
    print_acceleration_result_summary(operation, result, stored ? store_path : NULL);
    status = 0;

out:
    cleanse_json_value(result);
    json_object_put(result);
    return status == 1 && request_status == 2 ? 4 : status;
}

static int run_game_list(const char *session_file, const char *key_file,
                         int argument_count, char **arguments)
{
    json_object *data = NULL;
    json_object *page = NULL;
    uint64_t page_number = 1;
    uint64_t page_size = 50;
    int status;

    if (argument_count > 2 ||
        (argument_count > 0 && parse_unsigned_argument(arguments[0], 100000,
                                                        &page_number) != 0) ||
        (argument_count > 1 && parse_unsigned_argument(arguments[1], 1000,
                                                        &page_size) != 0)) {
        fputs("game-list accepts PAGE and SIZE as positive integers\n", stderr);
        return 2;
    }
    data = json_object_new_object();
    page = json_object_new_object();
    if (!data || !page) {
        json_object_put(data);
        json_object_put(page);
        return 1;
    }
    json_object_object_add(page, "page", json_object_new_int64((int64_t)page_number));
    json_object_object_add(page, "size", json_object_new_int64((int64_t)page_size));
    json_object_object_add(data, "page", page);
    status = run_acceleration_operation("game-list", GAME_LIST_ENDPOINT, data,
                                        DEFAULT_GAME_LIST_FILE, session_file,
                                        key_file, ACCELERATION_CLIENT_MOBILE);
    json_object_put(data);
    return status;
}

static int run_pc_game_list(const char *session_file, const char *key_file,
                            int argument_count)
{
    json_object *data;
    int status;

    if (argument_count != 0) {
        fputs("pc-game-list does not accept arguments\n", stderr);
        return 2;
    }
    data = json_object_new_object();
    if (!data)
        return 1;
    status = run_acceleration_operation("pc-game-list", PC_GAME_LIST_ENDPOINT,
                                        data, DEFAULT_PC_GAME_LIST_FILE,
                                        session_file, key_file,
                                        ACCELERATION_CLIENT_WINDOWS);
    json_object_put(data);
    return status;
}

static int run_service_config_fetch(const char *session_file,
                                    const char *key_file,
                                    int argument_count)
{
    json_object *data;
    int status;

    if (argument_count != 0) {
        fputs("service-config-fetch does not accept arguments\n", stderr);
        return 2;
    }
    data = json_object_new_object();
    if (!data)
        return 1;
    json_object_object_add(data, "appId", json_object_new_int(6));
    status = run_acceleration_operation("service-config-fetch",
                                        SERVICE_CONFIG_ENDPOINT, data,
                                        DEFAULT_SERVICE_CONFIG_FILE,
                                        session_file, key_file,
                                        ACCELERATION_CLIENT_WINDOWS);
    json_object_put(data);
    return status;
}

static int run_pc_game_search(const char *session_file, const char *key_file,
                              int argument_count, char **arguments)
{
    json_object *data = NULL;
    json_object *platforms = NULL;
    json_object *page = NULL;
    uint64_t page_number = 1;
    uint64_t page_size = 36;
    int status = 1;

    if (argument_count < 1 || argument_count > 3 || !arguments[0][0] ||
        strlen(arguments[0]) > 128 ||
        (argument_count > 1 && parse_unsigned_argument(arguments[1], 100000,
                                                        &page_number) != 0) ||
        (argument_count > 2 && parse_unsigned_argument(arguments[2], 1000,
                                                        &page_size) != 0)) {
        fputs("pc-game-search accepts KEYWORD [PAGE SIZE]\n", stderr);
        return 2;
    }
    data = json_object_new_object();
    platforms = json_object_new_array();
    page = json_object_new_object();
    if (!data || !platforms || !page)
        goto out;
    json_object_object_add(data, "keyword", json_object_new_string(arguments[0]));
    json_object_array_add(platforms, json_object_new_int(6));
    json_object_object_add(data, "limitPlatformIds", platforms);
    platforms = NULL;
    json_object_object_add(page, "page", json_object_new_int64((int64_t)page_number));
    json_object_object_add(page, "size", json_object_new_int64((int64_t)page_size));
    json_object_object_add(data, "page", page);
    page = NULL;
    json_object_object_add(data, "isLimitedFree", json_object_new_boolean(false));
    status = run_acceleration_operation("pc-game-search",
                                        PC_GAME_SEARCH_ENDPOINT, data,
                                        DEFAULT_PC_GAME_LIST_FILE,
                                        session_file, key_file,
                                        ACCELERATION_CLIENT_WINDOWS);

out:
    json_object_put(page);
    json_object_put(platforms);
    json_object_put(data);
    return status;
}

static int run_pc_game_profile(const char *session_file, const char *key_file,
                               int argument_count, char **arguments)
{
    json_object *data;
    uint64_t game_id;
    int status;

    if (argument_count != 1 ||
        parse_unsigned_argument(arguments[0], INT32_MAX, &game_id) != 0) {
        fputs("pc-game-profile accepts GAME_ID\n", stderr);
        return 2;
    }
    data = json_object_new_object();
    if (!data)
        return 1;
    json_object_object_add(data, "gameId", json_object_new_int64((int64_t)game_id));
    status = run_acceleration_operation("pc-game-profile",
                                        PC_GAME_PROFILE_ENDPOINT, data,
                                        DEFAULT_PC_GAME_PROFILE_FILE,
                                        session_file, key_file,
                                        ACCELERATION_CLIENT_WINDOWS);
    json_object_put(data);
    return status;
}

static int run_pc_game_map(const char *session_file, const char *key_file,
                           int argument_count, char **arguments)
{
    json_object *data = NULL;
    json_object *game_ids = NULL;
    uint64_t game_id;
    int status = 1;

    if (argument_count != 1 ||
        parse_unsigned_argument(arguments[0], INT32_MAX, &game_id) != 0) {
        fputs("pc-game-map accepts GAME_ID\n", stderr);
        return 2;
    }
    data = json_object_new_object();
    game_ids = json_object_new_array();
    if (!data || !game_ids)
        goto out;
    if (json_object_array_add(
            game_ids, json_object_new_int64((int64_t)game_id)) != 0)
        goto out;
    json_object_object_add(data, "gameIds", game_ids);
    game_ids = NULL;
    status = run_acceleration_operation("pc-game-map", PC_GAME_MAP_ENDPOINT,
                                        data, DEFAULT_PC_GAME_MAP_FILE,
                                        session_file, key_file,
                                        ACCELERATION_CLIENT_WINDOWS);

out:
    json_object_put(game_ids);
    json_object_put(data);
    return status;
}

static int run_pc_user_sync(const char *session_file, const char *key_file,
                            int argument_count)
{
    json_object *data;
    int status;

    if (argument_count != 0) {
        fputs("pc-user-sync does not accept arguments\n", stderr);
        return 2;
    }
    data = json_object_new_object();
    if (!data)
        return 1;
    status = run_acceleration_operation("pc-user-sync",
                                        PC_USER_INFO_ENDPOINT, data,
                                        DEFAULT_PC_USER_FILE,
                                        session_file, key_file,
                                        ACCELERATION_CLIENT_WINDOWS);
    json_object_put(data);
    if (status == 0) {
        char *biubiu_id = NULL;

        if (load_pc_biubiu_id(DEFAULT_PC_USER_FILE, &biubiu_id) != 0) {
            remove_private_file_if_safe(DEFAULT_PC_USER_FILE);
            fputs("account response has no valid biubiu ID\n", stderr);
            return 1;
        }
        OPENSSL_cleanse(biubiu_id, strlen(biubiu_id));
        free(biubiu_id);
    }
    return status;
}

static int run_game_search(const char *session_file, const char *key_file,
                           int argument_count, char **arguments)
{
    json_object *data = NULL;
    json_object *page = NULL;
    uint64_t page_number = 1;
    uint64_t page_size = 50;
    int status;

    if (argument_count < 1 || argument_count > 3 || !arguments[0][0] ||
        strlen(arguments[0]) > 128 ||
        (argument_count > 1 && parse_unsigned_argument(arguments[1], 100000,
                                                        &page_number) != 0) ||
        (argument_count > 2 && parse_unsigned_argument(arguments[2], 1000,
                                                        &page_size) != 0)) {
        fputs("game-search accepts KEYWORD [PAGE SIZE]\n", stderr);
        return 2;
    }
    data = json_object_new_object();
    page = json_object_new_object();
    if (!data || !page) {
        json_object_put(data);
        json_object_put(page);
        return 1;
    }
    json_object_object_add(data, "keyword", json_object_new_string(arguments[0]));
    json_object_object_add(page, "page", json_object_new_int64((int64_t)page_number));
    json_object_object_add(page, "size", json_object_new_int64((int64_t)page_size));
    json_object_object_add(data, "page", page);
    status = run_acceleration_operation("game-search", SEARCH_GAME_ENDPOINT, data,
                                        DEFAULT_GAME_LIST_FILE, session_file,
                                        key_file, ACCELERATION_CLIENT_MOBILE);
    json_object_put(data);
    return status;
}

static int run_check_speedup(const char *session_file, const char *key_file,
                             int argument_count, char **arguments)
{
    json_object *data = NULL;
    uint64_t game_id;
    uint64_t area_id;
    uint64_t polling = 0;
    int status;

    if ((argument_count != 2 && argument_count != 3) ||
        parse_unsigned_argument(arguments[0], INT32_MAX, &game_id) != 0 ||
        parse_unsigned_argument(arguments[1], INT32_MAX, &area_id) != 0 ||
        (argument_count == 3 &&
         parse_nonnegative_argument(arguments[2], INT32_MAX, &polling) != 0)) {
        fputs("check-speedup accepts GAME_ID AREA_ID [POLLING]\n", stderr);
        return 2;
    }
    data = json_object_new_object();
    if (!data)
        return 1;
    json_object_object_add(data, "gameId", json_object_new_int64((int64_t)game_id));
    json_object_object_add(data, "areaId", json_object_new_int64((int64_t)area_id));
    json_object_object_add(data, "polling", json_object_new_int64((int64_t)polling));
    json_object_object_add(data, "space", json_object_new_int(0));
    status = run_acceleration_operation("check-speedup", CHECK_SPEEDUP_ENDPOINT,
                                        data, DEFAULT_ENTITLEMENT_FILE,
                                        session_file, key_file,
                                        ACCELERATION_CLIENT_MOBILE);
    json_object_put(data);
    return status;
}

static int run_pc_check_speedup(const char *session_file, const char *key_file,
                                int argument_count, char **arguments)
{
    json_object *data = NULL;
    uint64_t game_id;
    uint64_t area_id;
    uint64_t polling = 0;
    uint64_t last_jitter_time = 0;
    int status;

    if (argument_count < 2 || argument_count > 4 ||
        parse_unsigned_argument(arguments[0], INT32_MAX, &game_id) != 0 ||
        parse_unsigned_argument(arguments[1], INT32_MAX, &area_id) != 0 ||
        (argument_count >= 3 &&
         parse_nonnegative_argument(arguments[2], INT32_MAX, &polling) != 0) ||
        (argument_count == 4 &&
         parse_nonnegative_argument(arguments[3], INT64_MAX,
                                    &last_jitter_time) != 0)) {
        fputs("pc-check-speedup accepts GAME_ID AREA_ID [POLLING "
              "LAST_JITTER_TIME]\n", stderr);
        return 2;
    }
    data = json_object_new_object();
    if (!data)
        return 1;
    json_object_object_add(data, "gameId", json_object_new_int64((int64_t)game_id));
    json_object_object_add(data, "areaId", json_object_new_int64((int64_t)area_id));
    json_object_object_add(data, "polling", json_object_new_int64((int64_t)polling));
    json_object_object_add(data, "useMemberSpeedUpExperience",
                           json_object_new_boolean(false));
    if (argument_count == 4)
        json_object_object_add(
            data, "lastJitterTime",
            json_object_new_int64((int64_t)last_jitter_time));
    status = run_acceleration_operation("pc-check-speedup",
                                        PC_CHECK_SPEEDUP_ENDPOINT, data,
                                        DEFAULT_PC_ENTITLEMENT_FILE,
                                        session_file, key_file,
                                        ACCELERATION_CLIENT_WINDOWS);
    json_object_put(data);
    return status;
}

static const char *pc_platform_name(uint64_t platform_id)
{
    static const char *const names[] = {
        "all", "undefine", "android", "ios", "simulator", "pcweb",
        "pc", "switch", "ps", "xbox", "steamdeck",
    };

    return platform_id < sizeof(names) / sizeof(names[0]) ? names[platform_id]
                                                          : NULL;
}

static bool pc_acceleration_mode(uint64_t mode)
{
    return mode == 3 || mode == 4 || mode == 5;
}

static json_object *pc_game_map_entry(json_object *response, uint64_t game_id)
{
    json_object *data = object_member(response, "data", json_type_object);
    json_object *list = object_member(data, "list", json_type_array);
    size_t index;

    if (!list)
        return NULL;
    for (index = 0; index < json_object_array_length(list); index++) {
        json_object *entry = json_object_array_get_idx(list, index);
        json_object *game_info = object_member(entry, "gameInfo",
                                                json_type_object);
        json_object *id = object_member(game_info, "gameId", json_type_int);
        int64_t value;

        if (!id)
            continue;
        value = json_object_get_int64(id);
        if (value > 0 && (uint64_t)value == game_id)
            return entry;
    }
    return NULL;
}

static int pc_game_start_selection(json_object *response, uint64_t game_id,
                                   uint64_t area_id, uint64_t platform_id,
                                   bool requested_mode_set,
                                   uint64_t requested_mode,
                                   json_object **ordered_modes,
                                   uint64_t *selected_mode)
{
    static const uint64_t fallback_modes[] = {3, 5};
    uint64_t modes[3];
    size_t mode_count = 0;
    json_object *entry;
    json_object *game_info;
    json_object *platform;
    json_object *areas;
    json_object *mode_entries = NULL;
    json_object *result = NULL;
    bool area_found = false;
    bool selected_found = false;
    size_t index;

    if (!response || !ordered_modes || !selected_mode ||
        !pc_platform_name(platform_id)) {
        errno = EINVAL;
        return -1;
    }
    *ordered_modes = NULL;
    entry = pc_game_map_entry(response, game_id);
    game_info = object_member(entry, "gameInfo", json_type_object);
    platform = object_member(game_info, "platformId", json_type_int);
    areas = object_member(entry, "areaList", json_type_array);
    if (!entry || !platform || !areas || json_object_get_int64(platform) < 0 ||
        (uint64_t)json_object_get_int64(platform) != platform_id) {
        errno = EINVAL;
        return -1;
    }
    for (index = 0; index < json_object_array_length(areas); index++) {
        json_object *area = json_object_array_get_idx(areas, index);
        json_object *id = object_member(area, "areaId", json_type_int);
        int64_t value;

        if (!id)
            continue;
        value = json_object_get_int64(id);
        if (value > 0 && (uint64_t)value == area_id) {
            area_found = true;
            break;
        }
    }
    if (!area_found) {
        errno = EINVAL;
        return -1;
    }
    if (json_object_object_get_ex(entry, "speedupModelList", &mode_entries) &&
        !json_object_is_type(mode_entries, json_type_array)) {
        errno = EINVAL;
        return -1;
    }
    if (mode_entries) {
        for (index = 0; index < json_object_array_length(mode_entries); index++) {
            json_object *mode_entry = json_object_array_get_idx(mode_entries,
                                                                 index);
            json_object *id = object_member(mode_entry, "speedupModelId",
                                             json_type_int);
            int64_t value;
            size_t duplicate;

            if (!id || (value = json_object_get_int64(id)) < 0 ||
                !pc_acceleration_mode((uint64_t)value)) {
                errno = EINVAL;
                return -1;
            }
            for (duplicate = 0; duplicate < mode_count; duplicate++) {
                if (modes[duplicate] == (uint64_t)value)
                    break;
            }
            if (duplicate != mode_count)
                continue;
            if (mode_count >= sizeof(modes) / sizeof(modes[0])) {
                errno = EOVERFLOW;
                return -1;
            }
            modes[mode_count++] = (uint64_t)value;
        }
    }
    if (!mode_count) {
        memcpy(modes, fallback_modes, sizeof(fallback_modes));
        mode_count = sizeof(fallback_modes) / sizeof(fallback_modes[0]);
    }
    *selected_mode = requested_mode_set ? requested_mode : modes[0];
    if (!pc_acceleration_mode(*selected_mode)) {
        errno = EINVAL;
        return -1;
    }
    for (index = 0; index < mode_count; index++) {
        if (modes[index] == *selected_mode) {
            selected_found = true;
            break;
        }
    }
    if (!selected_found) {
        errno = EINVAL;
        return -1;
    }
    result = json_object_new_array();
    if (!result ||
        json_object_array_add(
            result, json_object_new_int64((int64_t)*selected_mode)) != 0)
        goto fail;
    for (index = 0; index < mode_count; index++) {
        if (modes[index] == *selected_mode)
            continue;
        if (json_object_array_add(
                result, json_object_new_int64((int64_t)modes[index])) != 0)
            goto fail;
    }
    *ordered_modes = result;
    return 0;

fail:
    json_object_put(result);
    errno = ENOMEM;
    return -1;
}

static int run_pc_context_start(int argument_count, char **arguments)
{
    json_object *map_response = NULL;
    json_object *context = NULL;
    json_object *mode_list = NULL;
    char *app_session = NULL;
    char *speedup_session = NULL;
    const char *platform_name;
    uint64_t game_id;
    uint64_t area_id;
    uint64_t platform_id;
    uint64_t requested_mode = 0;
    uint64_t acceleration_mode = 0;
    bool requested_mode_set;
    int status = 1;

    if (argument_count < 3 || argument_count > 4 ||
        parse_unsigned_argument(arguments[0], INT32_MAX, &game_id) != 0 ||
        parse_unsigned_argument(arguments[1], INT32_MAX, &area_id) != 0 ||
        parse_unsigned_argument(arguments[2], 10, &platform_id) != 0 ||
        (argument_count == 4 &&
         parse_unsigned_argument(arguments[3], INT32_MAX,
                                 &requested_mode) != 0)) {
        fputs("pc-context-start accepts GAME_ID AREA_ID PLATFORM_ID "
              "[ACC_MODE]\n", stderr);
        return 2;
    }
    requested_mode_set = argument_count == 4;
    platform_name = pc_platform_name(platform_id);
    if (!platform_name ||
        load_private_json(DEFAULT_PC_GAME_MAP_FILE, &map_response) != 0 ||
        pc_game_start_selection(map_response, game_id, area_id, platform_id,
                                requested_mode_set, requested_mode, &mode_list,
                                &acceleration_mode) != 0) {
        fputs("stored PC game map does not contain the requested game, area, "
              "platform, and acceleration mode\n", stderr);
        goto out;
    }
    app_session = new_native_app_session();
    speedup_session = new_native_speedup_session();
    context = json_object_new_object();
    if (!app_session || !speedup_session || !context)
        goto out;
    json_object_object_add(context, "appSession",
                           json_object_new_string(app_session));
    json_object_object_add(context, "speedupSession",
                           json_object_new_string(speedup_session));
    json_object_object_add(context, "gameId",
                           json_object_new_int64((int64_t)game_id));
    json_object_object_add(context, "gameArea",
                           json_object_new_int64((int64_t)area_id));
    json_object_object_add(context, "serverId", json_object_new_int(0));
    json_object_object_add(context, "accMode",
                           json_object_new_int64((int64_t)acceleration_mode));
    json_object_object_add(context, "accModeList", mode_list);
    mode_list = NULL;
    json_object_object_add(context, "accPodId",
                           json_object_new_string("auto"));
    json_object_object_add(context, "gamePlatform",
                           json_object_new_string(platform_name));
    json_object_object_add(context, "gamePlatformId",
                           json_object_new_int64((int64_t)platform_id));
    if (store_private_json(DEFAULT_PC_CONTEXT_FILE, context) != 0) {
        fprintf(stderr, "unable to store acceleration context: %s\n",
                strerror(errno));
        goto out;
    }
    puts("{\"success\":true,\"operation\":\"pc-context-start\","
         "\"stored\":true}");
    status = 0;

out:
    if (app_session) {
        OPENSSL_cleanse(app_session, strlen(app_session));
        free(app_session);
    }
    if (speedup_session) {
        OPENSSL_cleanse(speedup_session, strlen(speedup_session));
        free(speedup_session);
    }
    json_object_put(mode_list);
    cleanse_json_value(context);
    json_object_put(context);
    cleanse_json_value(map_response);
    json_object_put(map_response);
    return status;
}

static int compare_scout_sample(const void *left, const void *right)
{
    int a = *(const int *)left;
    int b = *(const int *)right;

    return (a > b) - (a < b);
}

static int pc_scout_unsigned_member(json_object *object, const char *name,
                                    uint64_t fallback, uint64_t minimum,
                                    uint64_t maximum, uint64_t *result)
{
    json_object *value = NULL;
    int64_t parsed;

    if (!object || !name || !result) {
        errno = EINVAL;
        return -1;
    }
    if (!json_object_object_get_ex(object, name, &value)) {
        *result = fallback;
        return 0;
    }
    if (!json_object_is_type(value, json_type_int)) {
        errno = EINVAL;
        return -1;
    }
    parsed = json_object_get_int64(value);
    if (parsed < 0 || (uint64_t)parsed < minimum ||
        (uint64_t)parsed > maximum) {
        errno = ERANGE;
        return -1;
    }
    *result = (uint64_t)parsed;
    return 0;
}

static int load_pc_scout_config(json_object *object,
                                struct pc_scout_config *config)
{
    json_object *protocol_value = NULL;
    const char *protocol = "UDP";
    uint64_t port;
    uint64_t detect_rounds;
    uint64_t loss_threshold_ms;
    uint64_t round_sleep_ms;
    uint64_t batch_count;
    uint64_t batch_sleep_ms;
    uint64_t discard_head_rounds;

    if (!object || !json_object_is_type(object, json_type_object) || !config ||
        pc_scout_unsigned_member(object, "port", 14125, 1, UINT16_MAX,
                                 &port) != 0 ||
        pc_scout_unsigned_member(object, "detectRound", 10, 1, 64,
                                 &detect_rounds) != 0 ||
        pc_scout_unsigned_member(object, "lossThresholdMs", 1000, 1, 60000,
                                 &loss_threshold_ms) != 0 ||
        pc_scout_unsigned_member(object, "roundSleepMs", 50, 0, 60000,
                                 &round_sleep_ms) != 0 ||
        pc_scout_unsigned_member(object, "batchCount", 30, 1, 4096,
                                 &batch_count) != 0 ||
        pc_scout_unsigned_member(object, "batchSleepMs", 30, 0, 60000,
                                 &batch_sleep_ms) != 0 ||
        pc_scout_unsigned_member(object, "discardHeadRound", 1, 0, 64,
                                 &discard_head_rounds) != 0) {
        errno = EINVAL;
        return -1;
    }
    if (json_object_object_get_ex(object, "proto", &protocol_value)) {
        if (!json_object_is_type(protocol_value, json_type_string)) {
            errno = EINVAL;
            return -1;
        }
        protocol = json_object_get_string(protocol_value);
    }
    if (!protocol || strcasecmp(protocol, "UDP") != 0 ||
        discard_head_rounds > detect_rounds) {
        errno = EINVAL;
        return -1;
    }
    config->port = (uint16_t)port;
    config->detect_rounds = (unsigned int)detect_rounds;
    config->loss_threshold_ms = (unsigned int)loss_threshold_ms;
    config->round_sleep_ms = (unsigned int)round_sleep_ms;
    config->batch_count = (unsigned int)batch_count;
    config->batch_sleep_ms = (unsigned int)batch_sleep_ms;
    config->discard_head_rounds = (unsigned int)discard_head_rounds;
    return 0;
}

static int monotonic_milliseconds(int64_t *result)
{
    struct timespec now;

    if (!result || clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return -1;
    *result = (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
    return 0;
}

static void free_pc_scout_endpoints(struct pc_scout_endpoint *endpoints,
                                    size_t endpoint_count)
{
    size_t index;

    if (!endpoints)
        return;
    for (index = 0; index < endpoint_count; index++)
        free(endpoints[index].samples);
    free(endpoints);
}

static int prepare_pc_scout_endpoints(json_object *lighthouses, uint16_t port,
                                      unsigned int rounds,
                                      struct pc_scout_endpoint **result,
                                      size_t *result_count)
{
    struct pc_scout_endpoint *endpoints = NULL;
    size_t endpoint_count;
    size_t index;

    if (!result || !result_count || port == 0 || rounds == 0) {
        errno = EINVAL;
        return -1;
    }
    *result = NULL;
    *result_count = 0;
    if (!lighthouses)
        return 0;
    if (!json_object_is_type(lighthouses, json_type_array)) {
        errno = EINVAL;
        return -1;
    }
    endpoint_count = json_object_array_length(lighthouses);
    if (endpoint_count > 256) {
        errno = EINVAL;
        return -1;
    }
    if (endpoint_count == 0)
        return 0;
    endpoints = calloc(endpoint_count, sizeof(*endpoints));
    if (!endpoints)
        return -1;
    for (index = 0; index < endpoint_count; index++) {
        json_object *entry = json_object_array_get_idx(lighthouses, index);
        const char *id = string_member(entry, "id");
        const char *ip = string_member(entry, "ip");

        if (!id || !id[0] || !ip || !ip[0]) {
            errno = EINVAL;
            free_pc_scout_endpoints(endpoints, endpoint_count);
            return -1;
        }
        endpoints[index].id = id;
        endpoints[index].address.sin_family = AF_INET;
        endpoints[index].address.sin_port = htons(port);
        endpoints[index].samples = calloc(rounds, sizeof(int));
        if (!endpoints[index].samples ||
            inet_pton(AF_INET, ip, &endpoints[index].address.sin_addr) != 1) {
            errno = EINVAL;
            free_pc_scout_endpoints(endpoints, endpoint_count);
            return -1;
        }
    }
    *result = endpoints;
    *result_count = endpoint_count;
    return 0;
}

static int drain_pc_scout_socket(int socket_fd, struct pc_scout_probe *probes,
                                 size_t probe_count,
                                 unsigned int loss_threshold_ms,
                                 unsigned int discard_head_rounds)
{
    unsigned char packet[512];

    for (;;) {
        uint32_t token;
        struct pc_scout_probe *probe;
        int64_t received_at;
        int64_t elapsed;
        ssize_t received;

        received = recvfrom(socket_fd, packet, sizeof(packet), 0, NULL, NULL);
        if (received < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK ||
                errno == ECONNREFUSED || errno == EHOSTUNREACH ||
                errno == ENETUNREACH)
                return 0;
            return -1;
        }
        if (received < 4)
            continue;
        token = read_le32(packet);
        if ((uint64_t)token >= probe_count)
            continue;
        probe = &probes[token];
        if (probe->token != token || !probe->sent || probe->received ||
            monotonic_milliseconds(&received_at) != 0)
            continue;
        probe->received = true;
        elapsed = received_at - probe->sent_at;
        if (elapsed < 0 || (uint64_t)elapsed >= loss_threshold_ms ||
            probe->round < discard_head_rounds)
            continue;
        if (probe->endpoint->sample_count >= 64) {
            errno = EOVERFLOW;
            return -1;
        }
        probe->endpoint->samples[probe->endpoint->sample_count++] =
            (int)elapsed;
    }
}

static int wait_pc_scout_socket(int socket_fd, struct pc_scout_probe *probes,
                                size_t probe_count,
                                const struct pc_scout_config *config,
                                unsigned int duration_ms)
{
    int64_t deadline;

    if (drain_pc_scout_socket(socket_fd, probes, probe_count,
                              config->loss_threshold_ms,
                              config->discard_head_rounds) != 0)
        return -1;
    if (duration_ms == 0)
        return 0;
    if (monotonic_milliseconds(&deadline) != 0)
        return -1;
    deadline += duration_ms;
    for (;;) {
        struct pollfd descriptor = {.fd = socket_fd, .events = POLLIN};
        int64_t now;
        int remaining;
        int poll_status;

        if (monotonic_milliseconds(&now) != 0)
            return -1;
        if (now >= deadline)
            return 0;
        remaining = (int)(deadline - now);
        poll_status = poll(&descriptor, 1, remaining);
        if (poll_status < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (poll_status == 0)
            return 0;
        if (descriptor.revents & POLLNVAL) {
            errno = EBADF;
            return -1;
        }
        if (drain_pc_scout_socket(socket_fd, probes, probe_count,
                                  config->loss_threshold_ms,
                                  config->discard_head_rounds) != 0)
            return -1;
    }
}

static int send_pc_scout_list(int socket_fd,
                              struct pc_scout_endpoint *endpoints,
                              size_t endpoint_count, unsigned int round,
                              const struct pc_scout_config *config,
                              struct pc_scout_probe *probes,
                              size_t probe_capacity, size_t *probe_count)
{
    size_t index;

    for (index = 0; index < endpoint_count; index++) {
        struct pc_scout_probe *probe;
        unsigned char packet[4];
        ssize_t sent;

        if (*probe_count >= probe_capacity || *probe_count > UINT32_MAX) {
            errno = EOVERFLOW;
            return -1;
        }
        probe = &probes[*probe_count];
        probe->endpoint = &endpoints[index];
        probe->token = (uint32_t)*probe_count;
        probe->round = round;
        write_le32(packet, probe->token);
        sent = sendto(socket_fd, packet, sizeof(packet), 0,
                      (struct sockaddr *)&endpoints[index].address,
                      sizeof(endpoints[index].address));
        if (monotonic_milliseconds(&probe->sent_at) != 0)
            return -1;
        probe->sent = sent == (ssize_t)sizeof(packet);
        (*probe_count)++;
        if (wait_pc_scout_socket(socket_fd, probes, *probe_count, config, 0) != 0)
            return -1;
        if ((index + 1) % config->batch_count == 0 &&
            wait_pc_scout_socket(socket_fd, probes, *probe_count, config,
                                 config->batch_sleep_ms) != 0)
            return -1;
    }
    return 0;
}

static void pc_scout_statistics(int *samples, size_t sample_count,
                                size_t eligible_probe_count,
                                unsigned int loss_threshold_ms, int *average,
                                int *loss, int *p90, int *count,
                                int *minimum, int *maximum)
{
    int64_t total = 0;
    size_t index;
    size_t percentile_index;

    *average = (int)loss_threshold_ms;
    *loss = (int)(eligible_probe_count - sample_count);
    *p90 = -1;
    *count = (int)eligible_probe_count;
    *minimum = -1;
    *maximum = -1;
    if (sample_count == 0)
        return;
    qsort(samples, sample_count, sizeof(*samples), compare_scout_sample);
    for (index = 0; index < sample_count; index++)
        total += samples[index];
    *average = (int)(total / (int64_t)sample_count);
    percentile_index = (sample_count * 9) / 10;
    if (percentile_index > 0)
        percentile_index--;
    if (percentile_index >= sample_count)
        percentile_index = sample_count - 1;
    *p90 = samples[percentile_index];
    *minimum = samples[0];
    *maximum = samples[sample_count - 1];
}

static int render_pc_scout_results(struct pc_scout_endpoint *endpoints,
                                   size_t endpoint_count,
                                   size_t eligible_probe_count,
                                   unsigned int loss_threshold_ms,
                                   json_object **result)
{
    json_object *output = NULL;
    size_t index;

    if (!result) {
        errno = EINVAL;
        return -1;
    }
    *result = NULL;
    output = json_object_new_array();
    if (!output)
        return -1;
    for (index = 0; index < endpoint_count; index++) {
        struct pc_scout_endpoint *endpoint = &endpoints[index];
        json_object *entry = json_object_new_object();
        int average;
        int loss;
        int p90;
        int count;
        int minimum;
        int maximum;

        if (!entry || endpoint->sample_count > eligible_probe_count) {
            json_object_put(output);
            return -1;
        }
        pc_scout_statistics(endpoint->samples, endpoint->sample_count,
                            eligible_probe_count, loss_threshold_ms, &average,
                            &loss, &p90, &count, &minimum, &maximum);
        json_object_object_add(entry, "id",
                               json_object_new_string(endpoint->id));
        json_object_object_add(entry, "avgMs", json_object_new_int(average));
        json_object_object_add(entry, "loss", json_object_new_int(loss));
        json_object_object_add(entry, "pt90Ms", json_object_new_int(p90));
        json_object_object_add(entry, "count", json_object_new_int(count));
        json_object_object_add(entry, "minMs", json_object_new_int(minimum));
        json_object_object_add(entry, "maxMs", json_object_new_int(maximum));
        json_object_array_add(output, entry);
    }
    *result = output;
    return 0;
}

static int build_pc_scout_results(json_object *lighthouses,
                                  json_object *transfer_lighthouses,
                                  const struct pc_scout_config *config,
                                  json_object **detect_result,
                                  json_object **transfer_result)
{
    struct pc_scout_endpoint *endpoints = NULL;
    struct pc_scout_endpoint *transfer_endpoints = NULL;
    struct pc_scout_probe *probes = NULL;
    size_t endpoint_count = 0;
    size_t transfer_endpoint_count = 0;
    size_t total_endpoint_count;
    size_t probe_capacity;
    size_t probe_count = 0;
    unsigned int round;
    int socket_fd = -1;
    int flags;
    int status = -1;

    if (!config || !detect_result || !transfer_result ||
        config->port == 0 || config->detect_rounds == 0 ||
        config->batch_count == 0 ||
        config->discard_head_rounds > config->detect_rounds) {
        errno = EINVAL;
        return -1;
    }
    *detect_result = NULL;
    *transfer_result = NULL;
    if (prepare_pc_scout_endpoints(lighthouses, config->port,
                                   config->detect_rounds, &endpoints,
                                   &endpoint_count) != 0 ||
        prepare_pc_scout_endpoints(transfer_lighthouses, config->port,
                                   config->detect_rounds, &transfer_endpoints,
                                   &transfer_endpoint_count) != 0)
        goto out;
    total_endpoint_count = endpoint_count + transfer_endpoint_count;
    if (total_endpoint_count > SIZE_MAX / config->detect_rounds) {
        errno = EOVERFLOW;
        goto out;
    }
    probe_capacity = total_endpoint_count * config->detect_rounds;
    if (probe_capacity) {
        probes = calloc(probe_capacity, sizeof(*probes));
        if (!probes)
            goto out;
        socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socket_fd < 0)
            goto out;
        flags = fcntl(socket_fd, F_GETFL, 0);
        if (flags < 0 || fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) != 0)
            goto out;

        /* The native netbar-only BIUBIU preflight has no OpenWrt equivalent;
         * the ordinary PC path starts directly with these four-byte probes. */
        for (round = 0; round < config->detect_rounds; round++) {
            if (send_pc_scout_list(socket_fd, endpoints, endpoint_count, round,
                                   config, probes, probe_capacity,
                                   &probe_count) != 0 ||
                (transfer_endpoint_count > 0 &&
                 wait_pc_scout_socket(socket_fd, probes, probe_count, config,
                                      config->batch_sleep_ms) != 0) ||
                send_pc_scout_list(socket_fd, transfer_endpoints,
                                   transfer_endpoint_count, round, config,
                                   probes, probe_capacity, &probe_count) != 0 ||
                wait_pc_scout_socket(socket_fd, probes, probe_count, config,
                                     config->round_sleep_ms) != 0)
                goto out;
        }
        if (wait_pc_scout_socket(
                socket_fd, probes, probe_count, config,
                (config->loss_threshold_ms < 200 ? 200 :
                                                   config->loss_threshold_ms) +
                    15) != 0)
            goto out;
    }
    if (render_pc_scout_results(endpoints, endpoint_count,
                                config->detect_rounds -
                                    config->discard_head_rounds,
                                config->loss_threshold_ms,
                                detect_result) != 0 ||
        render_pc_scout_results(transfer_endpoints, transfer_endpoint_count,
                                config->detect_rounds -
                                    config->discard_head_rounds,
                                config->loss_threshold_ms,
                                transfer_result) != 0)
        goto out;
    status = 0;

out:
    if (socket_fd >= 0)
        close(socket_fd);
    if (status != 0) {
        json_object_put(*transfer_result);
        json_object_put(*detect_result);
        *transfer_result = NULL;
        *detect_result = NULL;
    }
    free(probes);
    free_pc_scout_endpoints(transfer_endpoints, transfer_endpoint_count);
    free_pc_scout_endpoints(endpoints, endpoint_count);
    return status;
}

static int pc_profile_context_mode_value(json_object *context,
                                         uint64_t game_id, uint64_t area_id,
                                         uint64_t *speedup_model_id)
{
    json_object *context_game;
    json_object *context_area;
    json_object *server;
    json_object *mode;
    json_object *mode_list;
    int64_t game_value;
    int64_t area_value;
    int64_t server_value;
    int64_t mode_value;

    if (!context || !speedup_model_id)
        return -1;
    context_game = object_member(context, "gameId", json_type_int);
    context_area = object_member(context, "gameArea", json_type_int);
    server = object_member(context, "serverId", json_type_int);
    mode = object_member(context, "accMode", json_type_int);
    mode_list = object_member(context, "accModeList", json_type_array);
    if (!context_game || !context_area || !server || !mode || !mode_list ||
        json_object_array_length(mode_list) == 0)
        return -1;
    game_value = json_object_get_int64(context_game);
    area_value = json_object_get_int64(context_area);
    server_value = json_object_get_int64(server);
    mode_value = json_object_get_int64(mode);
    if (game_value <= 0 || area_value <= 0 || server_value != 0 ||
        mode_value < 0 || (uint64_t)game_value != game_id ||
        (uint64_t)area_value != area_id ||
        !pc_acceleration_mode((uint64_t)mode_value))
        return -1;
    mode = json_object_array_get_idx(mode_list, 0);
    if (!mode || !json_object_is_type(mode, json_type_int) ||
        json_object_get_int64(mode) != mode_value)
        return -1;
    *speedup_model_id = (uint64_t)mode_value;
    return 0;
}

static int pc_profile_context_mode(uint64_t game_id, uint64_t area_id,
                                   uint64_t *speedup_model_id)
{
    json_object *context = NULL;
    int status;

    if (load_private_json(DEFAULT_PC_CONTEXT_FILE, &context) != 0)
        return -1;
    status = pc_profile_context_mode_value(context, game_id, area_id,
                                           speedup_model_id);

    cleanse_json_value(context);
    json_object_put(context);
    if (status != 0)
        errno = EINVAL;
    return status;
}

static int run_pc_profile_request(const char *session_file, const char *key_file,
                                  int argument_count, char **arguments)
{
    json_object *entitlement = NULL;
    json_object *data_wrapper;
    json_object *value;
    json_object *scout_config;
    json_object *data = NULL;
    json_object *scout_result = NULL;
    json_object *detect_result = NULL;
    json_object *transfer_result = NULL;
    struct pc_scout_config scout_settings;
    const char *strategy_id;
    uint64_t game_id;
    uint64_t area_id;
    uint64_t speedup_model_id;
    int status = 1;

    if (argument_count != 2 ||
        parse_unsigned_argument(arguments[0], INT32_MAX, &game_id) != 0 ||
        parse_unsigned_argument(arguments[1], INT32_MAX, &area_id) != 0) {
        fputs("pc-profile-fetch accepts GAME_ID AREA_ID\n", stderr);
        return 2;
    }
    if (pc_profile_context_mode(game_id, area_id, &speedup_model_id) != 0) {
        fputs("profile fetch requires a matching PC acceleration context\n",
              stderr);
        goto out;
    }
    if (load_private_json(DEFAULT_PC_ENTITLEMENT_FILE, &entitlement) != 0) {
        fputs("profile fetch requires a stored acceleration entitlement\n", stderr);
        goto out;
    }
    data_wrapper = object_member(entitlement, "data", json_type_object);
    value = object_member(data_wrapper, "value", json_type_object);
    scout_config = object_member(value, "scoutPathConfig", json_type_object);
    strategy_id = string_member(scout_config, "strategyId");
    if (!strategy_id || !strategy_id[0] ||
        load_pc_scout_config(scout_config, &scout_settings) != 0) {
        fputs("stored acceleration entitlement has no valid scout configuration\n",
              stderr);
        goto out;
    }
    data = json_object_new_object();
    scout_result = json_object_new_object();
    if (!data || !scout_result ||
        build_pc_scout_results(
            object_member(scout_config, "lighthouseList", json_type_array),
            object_member(scout_config, "transferLighthouseList", json_type_array),
            &scout_settings, &detect_result, &transfer_result) != 0) {
        fputs("unable to complete acceleration lighthouse detection\n", stderr);
        goto out;
    }
    json_object_object_add(data, "gameId",
                           json_object_new_int64((int64_t)game_id));
    json_object_object_add(data, "areaId",
                           json_object_new_int64((int64_t)area_id));
    json_object_object_add(data, "serverId", json_object_new_int(0));
    json_object_object_add(data, "speedupModelId",
                           json_object_new_int64((int64_t)speedup_model_id));
    json_object_object_add(data, "useMemberSpeedUpExperience",
                           json_object_new_boolean(false));
    json_object_object_add(scout_result, "strategyId",
                           json_object_new_string(strategy_id));
    json_object_object_add(scout_result, "detectResult", detect_result);
    detect_result = NULL;
    json_object_object_add(scout_result, "transferDetectResult",
                           transfer_result);
    transfer_result = NULL;
    json_object_object_add(data, "scoutPathResult", scout_result);
    scout_result = NULL;
    status = run_acceleration_operation("pc-profile-fetch",
                                        PC_SPEEDUP_CONFIG_ENDPOINT, data,
                                        DEFAULT_PC_PROFILE_FILE, session_file,
                                        key_file, ACCELERATION_CLIENT_WINDOWS);

out:
    json_object_put(transfer_result);
    json_object_put(detect_result);
    json_object_put(scout_result);
    json_object_put(data);
    cleanse_json_value(entitlement);
    json_object_put(entitlement);
    return status;
}

static int run_profile_request(const char *session_file, const char *key_file,
                               int argument_count, char **arguments)
{
    json_object *data = NULL;
    json_object *package_request = NULL;
    json_object *package_info = NULL;
    json_object *scout_path_result = NULL;
    json_object *detect_result = NULL;
    char *client_session_id = NULL;
    uint64_t game_id;
    uint64_t area_id;
    uint64_t platform_id;
    int status = 1;

    if (argument_count != 3 ||
        parse_unsigned_argument(arguments[0], INT32_MAX, &game_id) != 0 ||
        parse_unsigned_argument(arguments[1], INT32_MAX, &area_id) != 0 ||
        parse_unsigned_argument(arguments[2], INT32_MAX, &platform_id) != 0 ||
        platform_id < 2 || platform_id > 10) {
        fputs("profile-fetch accepts GAME_ID AREA_ID PLATFORM_ID\n", stderr);
        return 2;
    }
    data = json_object_new_object();
    package_request = json_object_new_object();
    package_info = json_object_new_object();
    client_session_id = new_client_session_id();
    if (!data || !package_request || !package_info || !client_session_id) {
        json_object_put(data);
        json_object_put(package_request);
        json_object_put(package_info);
        free(client_session_id);
        return 1;
    }
    json_object_object_add(package_info, "appName", json_object_new_string(""));
    json_object_object_add(package_info, "packageName", json_object_new_string(""));
    json_object_object_add(package_info, "versionName", json_object_new_string(""));
    json_object_object_add(package_info, "versionCode", json_object_new_string(""));
    json_object_object_add(package_request, "gamePackageInfo", package_info);
    package_info = NULL;
    json_object_object_add(package_request, "signCheckPackageList",
                           json_object_new_array());
    json_object_object_add(package_request, "sourcePkgList", json_object_new_array());
    json_object_object_add(data, "gameId", json_object_new_int64((int64_t)game_id));
    json_object_object_add(data, "areaId", json_object_new_int64((int64_t)area_id));
    json_object_object_add(data, "space", json_object_new_int(0));
    json_object_object_add(data, "platformId",
                           json_object_new_int64((int64_t)platform_id));
    json_object_object_add(data, "clientSessionId",
                           json_object_new_string(client_session_id));
    scout_path_result = json_object_new_object();
    detect_result = json_object_new_array();
    if (!scout_path_result || !detect_result)
        goto out;
    json_object_object_add(scout_path_result, "strategyId",
                           json_object_new_string(""));
    json_object_object_add(scout_path_result, "detectResult", detect_result);
    detect_result = NULL;
    json_object_object_add(data, "scoutPathResult", scout_path_result);
    scout_path_result = NULL;
    json_object_object_add(data, "optimizeMode", json_object_new_int(0));
    json_object_object_add(data, "dualNetOnline", json_object_new_int(0));
    json_object_object_add(data, "pkgRequest", package_request);
    package_request = NULL;
    status = run_acceleration_operation("profile-fetch", SPEEDUP_CONFIG_ENDPOINT,
                                        data, DEFAULT_PROFILE_FILE, session_file,
                                        key_file, ACCELERATION_CLIENT_MOBILE);

out:
    json_object_put(detect_result);
    json_object_put(scout_path_result);
    json_object_put(package_info);
    json_object_put(package_request);
    json_object_put(data);
    free(client_session_id);
    return status;
}

static int load_private_json(const char *path, json_object **result)
{
    struct bytes contents = {0};
    json_object *parsed = NULL;
    int status = -1;

    if (!result || read_private_file(path, &contents) != 0)
        return -1;
    parsed = json_tokener_parse((const char *)contents.data);
    if (!parsed || !json_object_is_type(parsed, json_type_object)) {
        errno = EINVAL;
        goto out;
    }
    *result = parsed;
    parsed = NULL;
    status = 0;

out:
    cleanse_json_value(parsed);
    json_object_put(parsed);
    bytes_free(&contents);
    return status;
}

static int load_pc_biubiu_id(const char *path, char **result)
{
    json_object *response = NULL;
    json_object *data;
    json_object *details;
    const char *value;
    int status = -1;

    if (!result) {
        errno = EINVAL;
        return -1;
    }
    *result = NULL;
    if (load_private_json(path, &response) != 0)
        return -1;
    data = object_member(response, "data", json_type_object);
    details = object_member(data, "data", json_type_object);
    value = json_scalar_string(details, "id");
    if (!decimal_string(value, 1, 24) || value[0] == '0') {
        errno = EINVAL;
        goto out;
    }
    *result = strdup(value);
    if (!*result)
        goto out;
    status = 0;

out:
    cleanse_json_value(response);
    json_object_put(response);
    return status;
}

static void remove_private_file_if_safe(const char *path)
{
    struct stat info;

    if (!path || lstat(path, &info) != 0 || !S_ISREG(info.st_mode) ||
        info.st_uid != geteuid() || info.st_nlink != 1 ||
        (info.st_mode & (S_IRWXG | S_IRWXO)) != 0)
        return;
    (void)unlink(path);
}

static json_object *control_data_object(json_object *response)
{
    json_object *data = NULL;

    if (response && json_object_object_get_ex(response, "data", &data) &&
        json_object_is_type(data, json_type_object))
        return data;
    return response;
}

static json_object *profile_signal_config(json_object *profile)
{
    json_object *outbound = object_member(profile, "outboundProfile", json_type_object);

    return object_member(outbound, "signalConfig", json_type_object);
}

static json_object *signal_control_data_object(json_object *response)
{
    json_object *data = control_data_object(response);
    json_object *nested = object_member(data, "data", json_type_object);

    /* The desktop ADAT response nests signal results under data.data. */
    return nested ? nested : data;
}

static const char *profile_route_outbound_id(json_object *route)
{
    const char *value = string_member(route, "bypathId");

    if (value && value[0])
        return value;
    return string_member(route, "outboundId");
}

static const char *profile_acceleration_outbound_id(json_object *profile,
                                                    bool *ambiguous)
{
    json_object *router = object_member(profile, "routerProfile", json_type_object);
    json_object *routes = object_member(router, "routeList", json_type_array);
    const char *selected = NULL;
    size_t index;

    if (ambiguous)
        *ambiguous = false;
    if (routes) {
        for (index = 0; index < json_object_array_length(routes); index++) {
            json_object *route = json_object_array_get_idx(routes, index);
            const char *mode = string_member(route, "mode");
            const char *outbound_id;

            if (!mode || strcasecmp(mode, "bolt") != 0)
                continue;
            outbound_id = profile_route_outbound_id(route);
            if (!outbound_id || !outbound_id[0]) {
                if (ambiguous)
                    *ambiguous = true;
                return NULL;
            }
            if (selected && strcmp(selected, outbound_id) != 0) {
                if (ambiguous)
                    *ambiguous = true;
                return NULL;
            }
            selected = outbound_id;
        }
    }
    if (selected)
        return selected;
    return string_member(router, "defaultOutboundId");
}

static json_object *profile_channel_list(json_object *profile,
                                         const char *outbound_id,
                                         json_object **owned_detail)
{
    json_object *outbound = object_member(profile, "outboundProfile", json_type_object);
    json_object *configs = object_member(outbound, "outboundConfigList", json_type_array);
    size_t index;

    if (owned_detail)
        *owned_detail = NULL;
    if (!configs || !json_object_is_type(configs, json_type_array))
        return NULL;
    for (index = 0; index < json_object_array_length(configs); index++) {
        json_object *config = json_object_array_get_idx(configs, index);
        json_object *detail = NULL;
        json_object *channels = NULL;
        const char *config_id;

        if (!config || !json_object_is_type(config, json_type_object))
            continue;
        config_id = string_member(config, "id");
        if (outbound_id && outbound_id[0] &&
            (!config_id || strcmp(config_id, outbound_id) != 0))
            continue;
        if (!json_object_object_get_ex(config, "rawDetailConfig", &detail))
            json_object_object_get_ex(config, "config", &detail);
        if (!detail)
            continue;
        if (json_object_is_type(detail, json_type_string)) {
            detail = json_tokener_parse(json_object_get_string(detail));
            if (!detail)
                continue;
            if (owned_detail)
                *owned_detail = detail;
        }
        if (!json_object_is_type(detail, json_type_object) ||
            !json_object_object_get_ex(detail, "dataChannelList", &channels) ||
            !json_object_is_type(channels, json_type_array) ||
            !json_object_array_length(channels)) {
            if (owned_detail && *owned_detail) {
                json_object_put(*owned_detail);
                *owned_detail = NULL;
            }
            continue;
        }
        return channels;
    }
    return NULL;
}

static __attribute__((unused)) json_object *
profile_channel_by_protocol(json_object *channels, const char *protocol)
{
    size_t index;

    if (!channels || !protocol || !json_object_is_type(channels, json_type_array))
        return NULL;
    for (index = 0; index < json_object_array_length(channels); index++) {
        json_object *channel = json_object_array_get_idx(channels, index);
        const char *value = string_member(channel, "proType");

        if (value && !strcasecmp(value, protocol))
            return channel;
    }
    return NULL;
}

static bool profile_outbound_has_channels(const char *type)
{
    return type && (!strcasecmp(type, "bolt") ||
                    !strcasecmp(type, "bypath") ||
                    !strcasecmp(type, "spare"));
}

static int compare_profile_outbound_config(const void *left,
                                           const void *right)
{
    json_object *const *left_config = left;
    json_object *const *right_config = right;
    const char *left_id = string_member(*left_config, "id");
    const char *right_id = string_member(*right_config, "id");

    if (!left_id)
        return right_id ? 1 : 0;
    if (!right_id)
        return -1;
    return strcmp(left_id, right_id);
}

static json_object *profile_acceleration_channel_entries(json_object *profile)
{
    json_object *outbound = object_member(profile, "outboundProfile",
                                          json_type_object);
    json_object *configs = object_member(outbound, "outboundConfigList",
                                         json_type_array);
    json_object *entries = NULL;
    json_object **ordered_configs = NULL;
    size_t config_count;
    size_t config_index;

    if (!configs)
        return NULL;
    config_count = json_object_array_length(configs);
    ordered_configs = calloc(config_count, sizeof(*ordered_configs));
    if (!ordered_configs)
        return NULL;
    for (config_index = 0; config_index < config_count; config_index++)
        ordered_configs[config_index] =
            json_object_array_get_idx(configs, config_index);
    qsort(ordered_configs, config_count, sizeof(*ordered_configs),
          compare_profile_outbound_config);
    entries = json_object_new_array();
    if (!entries)
        goto fail;
    for (config_index = 0; config_index < config_count; config_index++) {
        json_object *config = ordered_configs[config_index];
        json_object *detail = NULL;
        json_object *owned_detail = NULL;
        json_object *channels;
        const char *outbound_id = string_member(config, "id");
        const char *outbound_type = string_member(config, "type");
        size_t channel_index;

        if (!outbound_id || !outbound_id[0] ||
            !profile_outbound_has_channels(outbound_type))
            continue;
        if (!json_object_object_get_ex(config, "rawDetailConfig", &detail))
            json_object_object_get_ex(config, "config", &detail);
        if (json_object_is_type(detail, json_type_string)) {
            owned_detail = json_tokener_parse(json_object_get_string(detail));
            detail = owned_detail;
        }
        channels = object_member(detail, "dataChannelList", json_type_array);
        if (!channels) {
            json_object_put(owned_detail);
            continue;
        }
        for (channel_index = 0;
             channel_index < json_object_array_length(channels); channel_index++) {
            json_object *channel = json_object_array_get_idx(channels,
                                                              channel_index);
            json_object *entry;

            if (!json_object_is_type(channel, json_type_object))
                continue;
            entry = json_object_new_object();
            if (!entry)
                goto fail;
            json_object_object_add(entry, "outboundId",
                                   json_object_new_string(outbound_id));
            json_object_object_add(entry, "outboundType",
                                   json_object_new_string(outbound_type));
            json_object_object_add(entry, "channel", json_object_get(channel));
            json_object_array_add(entries, entry);
        }
        json_object_put(owned_detail);
    }
    if (!json_object_array_length(entries))
        goto fail;
    free(ordered_configs);
    return entries;

fail:
    free(ordered_configs);
    json_object_put(entries);
    return NULL;
}

static __attribute__((unused)) json_object *
profile_channel_entry_for_authorization(
    json_object *entries, json_object *authorization)
{
    const char *protocol = string_member(authorization, "proType");
    const char *channel_ip = string_member(authorization, "channelIp");
    json_object *authorization_port = NULL;
    size_t index;

    if (!entries || !protocol || !channel_ip)
        return NULL;
    json_object_object_get_ex(authorization, "port", &authorization_port);
    for (index = 0; index < json_object_array_length(entries); index++) {
        json_object *entry = json_object_array_get_idx(entries, index);
        json_object *channel = object_member(entry, "channel", json_type_object);
        json_object *profile_port = NULL;
        const char *profile_protocol = string_member(channel, "proType");
        const char *profile_ip = string_member(channel, "ip");

        if (!profile_protocol || strcasecmp(profile_protocol, protocol) ||
            !profile_ip || strcmp(profile_ip, channel_ip))
            continue;
        json_object_object_get_ex(channel, "port", &profile_port);
        if (authorization_port && profile_port &&
            json_object_get_int64(authorization_port) !=
                json_object_get_int64(profile_port))
            continue;
        return entry;
    }
    return NULL;
}

static int profile_route_protocol(json_object *route, int64_t *protocol)
{
    json_object *value = NULL;
    const char *text;
    char *end = NULL;
    unsigned long parsed;

    if (!route || !protocol)
        return -1;
    if (!json_object_object_get_ex(route, "protocol", &value) ||
        json_object_is_type(value, json_type_null)) {
        *protocol = 0;
        return 0;
    }
    if (json_object_is_type(value, json_type_int)) {
        *protocol = json_object_get_int64(value);
    } else if (json_object_is_type(value, json_type_string)) {
        text = json_object_get_string(value);
        if (!text || !text[0])
            return -1;
        if (!strcasecmp(text, "tcp"))
            *protocol = 6;
        else if (!strcasecmp(text, "udp"))
            *protocol = 17;
        else if (!strcasecmp(text, "all") || !strcasecmp(text, "any"))
            *protocol = 0;
        else {
            errno = 0;
            parsed = strtoul(text, &end, 10);
            if (errno == ERANGE || !end || *end || parsed > 255)
                return -1;
            *protocol = (int64_t)parsed;
        }
    } else {
        return -1;
    }
    /* The Windows profile uses 2 for UDP and 3 for TCP. */
    if (*protocol == 2)
        *protocol = 17;
    else if (*protocol == 3)
        *protocol = 6;
    return *protocol >= 0 && *protocol <= 255 ? 0 : -1;
}

static int normalize_profile_cidr(const char *input, char *output,
                                  size_t output_size)
{
    const char *separator;
    char address[INET_ADDRSTRLEN];
    char *end = NULL;
    unsigned long prefix = 32;
    struct in_addr parsed;
    size_t address_length;

    if (!input || !output || output_size < 8 || !input[0])
        return -1;
    separator = strchr(input, '/');
    address_length = separator ? (size_t)(separator - input) : strlen(input);
    if (!address_length || address_length >= sizeof(address) ||
        (separator && strchr(separator + 1, '/')))
        return -1;
    memcpy(address, input, address_length);
    address[address_length] = '\0';
    if (inet_pton(AF_INET, address, &parsed) != 1)
        return -1;
    if (separator) {
        if (!separator[1])
            return -1;
        errno = 0;
        prefix = strtoul(separator + 1, &end, 10);
        if (errno == ERANGE || !end || *end || prefix > 32)
            return -1;
    }
    {
        int written = snprintf(output, output_size, "%s/%lu", address, prefix);

        if (written < 0 || (size_t)written >= output_size)
            return -1;
    }
    return 0;
}

static int normalize_profile_domain(const char *input, char *output,
                                    size_t output_size)
{
    size_t length;
    size_t label_length = 0;
    size_t index;

    if (!input || !output || output_size < 2)
        return -1;
    length = strlen(input);
    if (!length || length > 253 || length >= output_size || input[0] == '.' ||
        input[length - 1] == '.')
        return -1;
    for (index = 0; index < length; index++) {
        unsigned char character = (unsigned char)input[index];

        if (character == '.') {
            if (!label_length || input[index - 1] == '-')
                return -1;
            label_length = 0;
            continue;
        }
        if (!(character >= 'A' && character <= 'Z') &&
            !(character >= 'a' && character <= 'z') &&
            !(character >= '0' && character <= '9') && character != '-')
            return -1;
        if (label_length >= 63 || (character == '-' && !label_length))
            return -1;
        label_length++;
    }
    if (!label_length || input[length - 1] == '-')
        return -1;
    if (snprintf(output, output_size, "%s", input) < 0)
        return -1;
    for (index = 0; output[index]; index++)
        if (output[index] >= 'A' && output[index] <= 'Z')
            output[index] = (char)(output[index] - 'A' + 'a');
    return 0;
}

static int normalize_route_port(json_object *value, char *output,
                                size_t output_size)
{
    char input[64];
    char *separator;
    char *end;
    unsigned long first;
    unsigned long second;
    const char *text;
    bool has_range = false;

    if (!value || !output || output_size < 2)
        return -1;
    if (json_object_is_type(value, json_type_int)) {
        int64_t port = json_object_get_int64(value);

        if (port < 0 || port > 65535)
            return -1;
        if (port == 0) {
            snprintf(output, output_size, "*");
            return 0;
        }
        snprintf(output, output_size, "%lld", (long long)port);
        return 0;
    }
    if (!json_object_is_type(value, json_type_string))
        return -1;
    text = json_object_get_string(value);
    if (!text || !text[0] || strlen(text) >= sizeof(input))
        return -1;
    snprintf(input, sizeof(input), "%s", text);
    for (char *cursor = input; *cursor; cursor++)
        if (*cursor == '~')
            *cursor = '-';
    separator = strchr(input, '-');
    if (separator) {
        has_range = true;
        *separator++ = '\0';
        if (strchr(separator, '-'))
            return -1;
    }
    if (!input[0] || (has_range && !separator[0]))
        return -1;
    errno = 0;
    first = strtoul(input, &end, 10);
    if (errno == ERANGE || !end || *end || first > 65535)
        return -1;
    if (!has_range)
        second = first;
    else {
        errno = 0;
        second = strtoul(separator, &end, 10);
        if (errno == ERANGE || !end || *end || second > 65535)
            return -1;
    }
    if (first == 0 && second == 0) {
        snprintf(output, output_size, "*");
        return 0;
    }
    if (first == 0 || second == 0)
        return -1;
    if (first > second) {
        unsigned long swap = first;

        first = second;
        second = swap;
    }
    if (first == second)
        snprintf(output, output_size, "%lu", first);
    else
        snprintf(output, output_size, "%lu-%lu", first, second);
    return 0;
}

static int append_profile_route_rule(json_object *rules, const char *cidr,
                                     const char *ports)
{
    char value[320];

    if (!rules || !cidr || !cidr[0] || !ports || !ports[0] ||
        strlen(cidr) + strlen(ports) + 2 > sizeof(value))
        return -1;
    snprintf(value, sizeof(value), "%s|%s", cidr, ports);
    json_object_array_add(rules, json_object_new_string(value));
    return 0;
}

static int append_profile_route_cidrs(json_object *rules, json_object *route,
                                      const char *ports)
{
    json_object *cidr_list = object_member(route, "cidrList", json_type_array);
    const char *single = string_member(route, "cidrIp");
    char normalized[INET_ADDRSTRLEN + 5];
    size_t index;

    if (!cidr_list && !single)
        return 0;
    if (cidr_list && json_object_array_length(cidr_list)) {
        for (index = 0; index < json_object_array_length(cidr_list); index++) {
            json_object *value = json_object_array_get_idx(cidr_list, index);
            const char *cidr;

            if (!json_object_is_type(value, json_type_string))
                return -1;
            cidr = json_object_get_string(value);
            if (!cidr || normalize_profile_cidr(cidr, normalized,
                                                sizeof(normalized)) != 0 ||
                append_profile_route_rule(rules, normalized, ports) != 0)
                return -1;
        }
        return 0;
    }
    if (!single || normalize_profile_cidr(single, normalized,
                                          sizeof(normalized)) != 0)
        return -1;
    return append_profile_route_rule(rules, normalized, ports);
}

static int append_profile_route_domains(json_object *rules, json_object *route,
                                        const char *ports)
{
    json_object *domain_list = object_member(route, "domainList", json_type_array);
    const char *single = string_member(route, "domain");
    char normalized[254];
    size_t index;

    if (domain_list && json_object_array_length(domain_list)) {
        for (index = 0; index < json_object_array_length(domain_list); index++) {
            json_object *value = json_object_array_get_idx(domain_list, index);
            const char *domain;

            if (!json_object_is_type(value, json_type_string))
                return -1;
            domain = json_object_get_string(value);
            if (!domain || normalize_profile_domain(domain, normalized,
                                                     sizeof(normalized)) != 0 ||
                append_profile_route_rule(rules, normalized, ports) != 0)
                return -1;
        }
        return 0;
    }
    if (!single)
        return 0;
    if (normalize_profile_domain(single, normalized, sizeof(normalized)) != 0)
        return -1;
    return append_profile_route_rule(rules, normalized, ports);
}

static int append_profile_route_selector(json_object *cidr_rules,
                                         json_object *domain_rules,
                                         json_object *route,
                                         const char *ports)
{
    json_object *domain_list = object_member(route, "domainList", json_type_array);
    const char *domain = string_member(route, "domain");

    if ((domain_list && json_object_array_length(domain_list)) || domain)
        return append_profile_route_domains(domain_rules, route, ports);
    return append_profile_route_cidrs(cidr_rules, route, ports);
}

static int append_profile_route(json_object *tcp_rules, json_object *udp_rules,
                                json_object *tcp_domains,
                                json_object *udp_domains, json_object *route,
                                const char *outbound_id)
{
    const char *mode = string_member(route, "mode");
    const char *route_outbound_id;
    json_object *port_list;
    json_object *port_value = NULL;
    char ports[256] = "";
    size_t port_count = 0;
    size_t index;
    int64_t protocol = 0;

    if (!mode || strcasecmp(mode, "bolt") != 0)
        return 0;
    if (outbound_id && outbound_id[0]) {
        route_outbound_id = profile_route_outbound_id(route);
        if (!route_outbound_id || strcmp(route_outbound_id, outbound_id) != 0)
            return 0;
    }
    if (profile_route_protocol(route, &protocol) != 0)
        return -1;
    if (json_object_object_get_ex(route, "port", &port_value)) {
        char port[32];

        if (normalize_route_port(port_value, port, sizeof(port)) != 0)
            return -1;
        snprintf(ports, sizeof(ports), "%s", port);
        port_count = 1;
    }
    port_list = object_member(route, "portList", json_type_array);
    if (port_list) {
        for (index = 0; index < json_object_array_length(port_list); index++) {
            char port[32];
            size_t current_length;

            if (normalize_route_port(json_object_array_get_idx(port_list, index),
                                     port, sizeof(port)) != 0)
                return -1;
            if (!strcmp(port, "*")) {
                snprintf(ports, sizeof(ports), "*");
                port_count = 1;
                break;
            }
            current_length = strlen(ports);
            if (current_length && current_length + 1 >= sizeof(ports))
                return -1;
            if (current_length)
                ports[current_length++] = ',';
            if (current_length + strlen(port) >= sizeof(ports))
                return -1;
            snprintf(ports + current_length, sizeof(ports) - current_length,
                     "%s", port);
            port_count++;
        }
    }
    if (!port_count)
        snprintf(ports, sizeof(ports), "*");
    if (protocol == 0 || protocol == 6) {
        if (append_profile_route_selector(tcp_rules, tcp_domains, route,
                                          ports) != 0)
            return -1;
    }
    if (protocol == 0 || protocol == 17) {
        if (append_profile_route_selector(udp_rules, udp_domains, route,
                                          ports) != 0)
            return -1;
    }
    return 0;
}

static int build_profile_route_rules(json_object *profile, const char *outbound_id,
                                     json_object *tcp_rules, json_object *udp_rules,
                                     json_object *tcp_domains,
                                     json_object *udp_domains)
{
    json_object *router = object_member(profile, "routerProfile", json_type_object);
    json_object *routes = object_member(router, "routeList", json_type_array);
    size_t index;

    if (!routes)
        return 0;
    for (index = 0; index < json_object_array_length(routes); index++)
        if (append_profile_route(tcp_rules, udp_rules, tcp_domains, udp_domains,
                                 json_object_array_get_idx(routes, index),
                                 outbound_id) != 0)
            return -1;
    return 0;
}

static int run_profile_route_self_test(void)
{
    json_object *profile = NULL;
    json_object *router = NULL;
    json_object *routes = NULL;
    json_object *tcp_rules = NULL;
    json_object *udp_rules = NULL;
    json_object *tcp_domains = NULL;
    json_object *udp_domains = NULL;
    json_object *route = NULL;
    json_object *cidrs = NULL;
    json_object *ports = NULL;
    json_object *invalid_port = NULL;
    const char *tcp_rule;
    const char *tcp_domain_rule;
    const char *udp_rule;
    const char *selected_outbound;
    char normalized_port[32];
    int status = 1;
    bool ambiguous = false;

    profile = json_object_new_object();
    router = json_object_new_object();
    routes = json_object_new_array();
    tcp_rules = json_object_new_array();
    udp_rules = json_object_new_array();
    tcp_domains = json_object_new_array();
    udp_domains = json_object_new_array();
    route = json_object_new_object();
    ports = json_object_new_array();
    invalid_port = json_object_new_string("0-80");
    if (!profile || !router || !routes || !tcp_rules || !udp_rules ||
        !tcp_domains || !udp_domains || !route || !ports || !invalid_port ||
        normalize_route_port(invalid_port, normalized_port,
                             sizeof(normalized_port)) == 0)
        goto out;
    json_object_array_add(ports, json_object_new_int(443));
    json_object_array_add(ports, json_object_new_string("27015~27050"));
    json_object_object_add(route, "mode", json_object_new_string("bolt"));
    json_object_object_add(route, "outboundId",
                           json_object_new_string("accelerated"));
    json_object_object_add(route, "protocol", json_object_new_string("TCP"));
    json_object_object_add(route, "cidrIp",
                           json_object_new_string("198.51.100.0/24"));
    json_object_object_add(route, "portList", ports);
    ports = NULL;
    json_object_array_add(routes, route);
    route = NULL;

    route = json_object_new_object();
    if (!route)
        goto out;
    json_object_object_add(route, "mode", json_object_new_string("bolt"));
    json_object_object_add(route, "outboundId",
                           json_object_new_string("accelerated"));
    json_object_object_add(route, "protocol", json_object_new_string("tcp"));
    json_object_object_add(route, "domain", json_object_new_string("Example.COM"));
    json_object_object_add(route, "port", json_object_new_int(443));
    json_object_array_add(routes, route);
    route = NULL;

    route = json_object_new_object();
    if (!route)
        goto out;
    json_object_object_add(route, "mode", json_object_new_string("bolt"));
    json_object_object_add(route, "outboundId",
                           json_object_new_string("accelerated"));
    json_object_object_add(route, "protocol", json_object_new_string("udp"));
    json_object_object_add(route, "cidrIp", json_object_new_string("192.0.2.1"));
    json_object_object_add(route, "port", json_object_new_int(53));
    json_object_array_add(routes, route);
    route = NULL;

    route = json_object_new_object();
    cidrs = json_object_new_array();
    if (!route || !cidrs)
        goto out;
    json_object_array_add(cidrs, json_object_new_string("203.0.113.0/24"));
    json_object_object_add(route, "mode", json_object_new_string("bolt"));
    json_object_object_add(route, "outboundId",
                           json_object_new_string("accelerated"));
    json_object_object_add(route, "protocol", json_object_new_int(17));
    json_object_object_add(route, "cidrList", cidrs);
    cidrs = NULL;
    json_object_object_add(route, "port", json_object_new_int(0));
    json_object_array_add(routes, route);
    route = NULL;

    route = json_object_new_object();
    if (!route)
        goto out;
    json_object_object_add(route, "mode", json_object_new_string("direct"));
    json_object_object_add(route, "domainList", json_object_new_array());
    json_object_array_add(routes, route);
    route = NULL;
    json_object_object_add(router, "routeList", routes);
    routes = NULL;
    json_object_object_add(profile, "routerProfile", router);
    router = NULL;
    selected_outbound = profile_acceleration_outbound_id(profile, &ambiguous);
    if (ambiguous || !selected_outbound ||
        strcmp(selected_outbound, "accelerated") != 0 ||
        build_profile_route_rules(profile, selected_outbound, tcp_rules, udp_rules,
                                  tcp_domains, udp_domains) != 0 ||
        json_object_array_length(tcp_rules) != 1 ||
        json_object_array_length(udp_rules) != 2 ||
        json_object_array_length(tcp_domains) != 1 ||
        json_object_array_length(udp_domains) != 0)
        goto out;
    tcp_rule = json_object_get_string(json_object_array_get_idx(tcp_rules, 0));
    tcp_domain_rule =
        json_object_get_string(json_object_array_get_idx(tcp_domains, 0));
    udp_rule = json_object_get_string(json_object_array_get_idx(udp_rules, 0));
    const char *udp_rule_2 =
        json_object_get_string(json_object_array_get_idx(udp_rules, 1));
    if (!tcp_rule || strcmp(tcp_rule, "198.51.100.0/24|443,27015-27050") != 0 ||
        !tcp_domain_rule || strcmp(tcp_domain_rule, "example.com|443") != 0 ||
        !udp_rule || strcmp(udp_rule, "192.0.2.1/32|53") != 0 ||
        !udp_rule_2 || strcmp(udp_rule_2, "203.0.113.0/24|*") != 0)
        goto out;
    status = 0;

out:
    if (status != 0)
        fputs("profile route self-test failed\n", stderr);
    json_object_put(cidrs);
    json_object_put(ports);
    json_object_put(route);
    json_object_put(routes);
    json_object_put(router);
    json_object_put(profile);
    json_object_put(invalid_port);
    json_object_put(tcp_rules);
    json_object_put(udp_rules);
    json_object_put(tcp_domains);
    json_object_put(udp_domains);
    return status;
}

static const char *native_channel_protocol(json_object *value)
{
    const char *text;
    int64_t number;

    if (!value)
        return NULL;
    if (json_object_is_type(value, json_type_string)) {
        text = json_object_get_string(value);
        if (!text)
            return NULL;
        if (!strcasecmp(text, "TCP") || !strcmp(text, "6"))
            return "TCP";
        if (!strcasecmp(text, "UDP") || !strcmp(text, "17"))
            return "UDP";
        if (!strcasecmp(text, "ICMP") || !strcmp(text, "1"))
            return "ICMP";
        return NULL;
    }
    if (!json_object_is_type(value, json_type_int))
        return NULL;
    number = json_object_get_int64(value);
    if (number == 6)
        return "TCP";
    if (number == 17)
        return "UDP";
    if (number == 1)
        return "ICMP";
    return NULL;
}

static int native_channel_port(json_object *value, int64_t *result)
{
    const char *text;
    char *end = NULL;
    unsigned long parsed;

    if (!value || !result)
        return -1;
    if (json_object_is_type(value, json_type_int)) {
        *result = json_object_get_int64(value);
    } else if (json_object_is_type(value, json_type_string)) {
        text = json_object_get_string(value);
        if (!text || !text[0])
            return -1;
        errno = 0;
        parsed = strtoul(text, &end, 10);
        if (errno == ERANGE || !end || *end || parsed > UINT16_MAX)
            return -1;
        *result = (int64_t)parsed;
    } else {
        return -1;
    }
    return *result >= 1 && *result <= UINT16_MAX ? 0 : -1;
}

static json_object *build_engine_client(uint64_t game_id, uint64_t area_id,
                                        uint64_t platform_id, const char *uid,
                                        const char *signal_session_id,
                                        uint64_t server_id,
                                        bool include_server_id,
                                        json_object *profile,
                                        enum acceleration_client_kind client_kind)
{
    json_object *client = NULL;
    const char *engine_version;

    if (!uid || !signal_session_id)
        return NULL;
    client = json_object_new_object();
    if (!client)
        return NULL;
    /* The Windows start path writes 1.0.0.0 into LoginData::engineVersion
     * immediately before serializing loginV2. */
    engine_version = client_kind == ACCELERATION_CLIENT_WINDOWS ?
        SIGNAL_ENGINE_VERSION : string_member(profile, "engineVersion");
    json_object_object_add(client, "appId", json_object_new_string("biubiu"));
    json_object_object_add(client, "engineVersion",
                           json_object_new_string(
                               client_kind == ACCELERATION_CLIENT_WINDOWS
                                   ? SIGNAL_ENGINE_VERSION
                                   : (engine_version && engine_version[0]
                                          ? engine_version
                                          : "3.0.0")));
    json_object_object_add(client, "gameId",
                           json_object_new_int64((int64_t)game_id));
    json_object_object_add(client, "areaId",
                           json_object_new_int64((int64_t)area_id));
    if (include_server_id)
        json_object_object_add(client, "serverId",
                               json_object_new_int64((int64_t)server_id));
    json_object_object_add(client, "signalSessionId",
                           json_object_new_string(signal_session_id));
    json_object_object_add(
        client, "type",
        json_object_new_int64(client_kind == ACCELERATION_CLIENT_WINDOWS
                                  ? 4
                                  : (int64_t)platform_id));
    json_object_object_add(client, "uid", json_object_new_string(uid));
    return client;
}

static int run_signal_login_mode(const char *session_file, const char *key_file,
                                 int argument_count, char **arguments,
                                 const char *profile_file,
                                 const char *authorization_file,
                                 enum acceleration_client_kind client_kind,
                                 bool all_channels, const char *operation)
{
    json_object *record = NULL;
    json_object *profile_response = NULL;
    json_object *profile;
    json_object *signal_config;
    json_object *channels = NULL;
    json_object *channel_entries = NULL;
    json_object *owned_detail = NULL;
    json_object *engine_client = NULL;
    json_object *data = NULL;
    json_object *channel_list = NULL;
    const char *uid = NULL;
    const char *service_ticket = NULL;
    char *pc_uid = NULL;
    const char *signal_ticket;
    const char *signal_session_id = "";
    const char *outbound_id;
    uint64_t game_id;
    uint64_t area_id;
    uint64_t platform_id;
    uint64_t server_id = 0;
    size_t index;
    bool ambiguous = false;
    int status = 1;

    if (argument_count != 3 ||
        parse_unsigned_argument(arguments[0], INT32_MAX, &game_id) != 0 ||
        parse_unsigned_argument(arguments[1], INT32_MAX, &area_id) != 0 ||
        parse_unsigned_argument(arguments[2], INT32_MAX, &platform_id) != 0) {
        fprintf(stderr, "%s accepts GAME_ID AREA_ID PLATFORM_ID\n", operation);
        return 2;
    }
    if (load_session_record(session_file, &record) != 0 ||
        session_acceleration_identity(record, &uid, &service_ticket) != 0 ||
        load_private_json(profile_file, &profile_response) != 0) {
        fprintf(stderr, "%s requires a stored account session and profile\n",
                operation);
        goto out;
    }
    if (client_kind == ACCELERATION_CLIENT_WINDOWS) {
        if (load_pc_biubiu_id(DEFAULT_PC_USER_FILE, &pc_uid) != 0) {
            fputs("signal login requires a synchronized acceleration account "
                  "identity\n", stderr);
            goto out;
        }
        if (load_pc_signal_context(NULL, &server_id) != 0) {
            fputs("signal login requires a valid PC acceleration context\n",
                  stderr);
            goto out;
        }
        /* The native SignalManager starts with an empty session. loginV2
         * returns signalSessionId, which is used by renewal and heartbeat. */
        uid = pc_uid;
    }
    (void)service_ticket;
    profile = control_data_object(profile_response);
    signal_config = profile_signal_config(profile);
    signal_ticket = string_member(signal_config, "signalSt");
    if (all_channels) {
        outbound_id = NULL;
        channel_entries = profile_acceleration_channel_entries(profile);
        channels = channel_entries;
    } else {
        outbound_id = profile_acceleration_outbound_id(profile, &ambiguous);
        if (ambiguous) {
            fputs("profile contains multiple Bolt outbounds; one is required\n",
                  stderr);
            goto out;
        }
        channels = profile_channel_list(profile, outbound_id, &owned_detail);
    }
    if (!signal_ticket || !signal_ticket[0] ||
        !channels || !json_object_array_length(channels)) {
        fputs("stored profile has no signal ticket or required data channel list\n",
              stderr);
        goto out;
    }
    engine_client = build_engine_client(game_id, area_id, platform_id, uid,
                                         signal_session_id, server_id,
                                         client_kind == ACCELERATION_CLIENT_WINDOWS,
                                         profile, client_kind);
    data = json_object_new_object();
    channel_list = json_object_new_array();
    if (!engine_client || !data || !channel_list)
        goto out;
    for (index = 0; index < json_object_array_length(channels); index++) {
        json_object *channel_entry = json_object_array_get_idx(channels, index);
        json_object *channel = all_channels ?
            object_member(channel_entry, "channel", json_type_object) :
            channel_entry;
        json_object *ip = NULL;
        json_object *port = NULL;
        json_object *protocol = NULL;
        json_object *wire = json_object_new_object();
        const char *wire_protocol;
        int64_t wire_port;

        if (!channel || !wire || !json_object_object_get_ex(channel, "ip", &ip) ||
            !json_object_object_get_ex(channel, "port", &port) ||
            !json_object_object_get_ex(channel, "proType", &protocol)) {
            json_object_put(wire);
            goto out;
        }
        wire_protocol = native_channel_protocol(protocol);
        if (!wire_protocol || native_channel_port(port, &wire_port) != 0 ||
            !json_object_is_type(ip, json_type_string)) {
            json_object_put(wire);
            fputs("profile contains an invalid native data channel\n", stderr);
            goto out;
        }
        json_object_object_add(wire, "dataChannelIp", json_object_get(ip));
        json_object_object_add(wire, "port",
                               json_object_new_int64(wire_port));
        json_object_object_add(wire, "proType",
                               json_object_new_string(wire_protocol));
        json_object_array_add(channel_list, wire);
    }
    if (client_kind == ACCELERATION_CLIENT_WINDOWS) {
        /* Initial login authorizes every profile channel in one ADAT request. */
        json_object_object_add(data, "signalSt",
                               json_object_new_string(signal_ticket));
        json_object_object_add(data, "engineClient", engine_client);
        engine_client = NULL;
        json_object_object_add(data, "list", channel_list);
        channel_list = NULL;
    } else {
        json_object_object_add(data, "engineClient", engine_client);
        engine_client = NULL;
        json_object_object_add(data, "list", channel_list);
        channel_list = NULL;
        json_object_object_add(data, "signalSt",
                               json_object_new_string(signal_ticket));
    }
    status = run_acceleration_operation(operation, SIGNAL_LOGIN_ENDPOINT, data,
                                        authorization_file, session_file,
                                        key_file, client_kind);

out:
    json_object_put(data);
    json_object_put(channel_list);
    json_object_put(engine_client);
    json_object_put(channel_entries);
    if (owned_detail)
        json_object_put(owned_detail);
    cleanse_json_value(profile_response);
    json_object_put(profile_response);
    cleanse_json_value(record);
    json_object_put(record);
    if (pc_uid) {
        OPENSSL_cleanse(pc_uid, strlen(pc_uid));
        free(pc_uid);
    }
    return status;
}

static int run_signal_login(const char *session_file, const char *key_file,
                            int argument_count, char **arguments)
{
    return run_signal_login_mode(session_file, key_file, argument_count,
                                 arguments, DEFAULT_PROFILE_FILE,
                                 DEFAULT_AUTHORIZATION_FILE,
                                 ACCELERATION_CLIENT_MOBILE, false,
                                 "signal-login");
}

static int run_pc_signal_login(const char *session_file, const char *key_file,
                               int argument_count, char **arguments)
{
    int status = run_signal_login_mode(session_file, key_file, argument_count,
                                 arguments, DEFAULT_PC_PROFILE_FILE,
                                 DEFAULT_PC_AUTHORIZATION_FILE,
                                 ACCELERATION_CLIENT_WINDOWS, true,
                                 "pc-signal-login");

    if (status == 0) {
        remove_private_file_if_safe(DEFAULT_PC_CHANNEL_TICKET_FILE);
        remove_private_file_if_safe(DEFAULT_RUNTIME_FILE);
    }
    return status;
}

static int run_channel_renew_mode(const char *session_file, const char *key_file,
                                  int argument_count, char **arguments,
                                  const char *profile_file,
                                  const char *authorization_file,
                                  const char *channel_ticket_file,
                                  enum acceleration_client_kind client_kind,
                                  const char *operation)
{
    json_object *record = NULL;
    json_object *profile_response = NULL;
    json_object *authorization = NULL;
    json_object *profile;
    json_object *auth_data;
    json_object *auth_channels;
    json_object *engine_client = NULL;
    json_object *data = NULL;
    json_object *dto = NULL;
    json_object *list = NULL;
    const char *uid = NULL;
    const char *service_ticket = NULL;
    char *pc_uid = NULL;
    const char *signal_session_id;
    uint64_t game_id;
    uint64_t area_id;
    uint64_t platform_id;
    size_t index;
    int status = 1;

    if (argument_count != 3 ||
        parse_unsigned_argument(arguments[0], INT32_MAX, &game_id) != 0 ||
        parse_unsigned_argument(arguments[1], INT32_MAX, &area_id) != 0 ||
        parse_unsigned_argument(arguments[2], INT32_MAX, &platform_id) != 0) {
        fprintf(stderr, "%s accepts GAME_ID AREA_ID PLATFORM_ID\n", operation);
        return 2;
    }
    if (load_session_record(session_file, &record) != 0 ||
        session_acceleration_identity(record, &uid, &service_ticket) != 0 ||
        load_private_json(profile_file, &profile_response) != 0 ||
        load_private_json(authorization_file, &authorization) != 0) {
        fprintf(stderr, "%s requires a stored profile and authorization\n",
                operation);
        goto out;
    }
    if (client_kind == ACCELERATION_CLIENT_WINDOWS) {
        if (load_pc_biubiu_id(DEFAULT_PC_USER_FILE, &pc_uid) != 0) {
            fputs("channel renewal requires a synchronized acceleration account "
                  "identity\n", stderr);
            goto out;
        }
        uid = pc_uid;
    }
    (void)service_ticket;
    profile = control_data_object(profile_response);
    auth_data = signal_control_data_object(authorization);
    auth_channels = object_member(auth_data, "channelAuthList", json_type_array);
    signal_session_id = string_member(auth_data, "signalSessionId");
    if (!signal_session_id || !signal_session_id[0] || !auth_channels ||
        !json_object_is_type(auth_channels, json_type_array) ||
        !json_object_array_length(auth_channels)) {
        fputs("stored authorization has no renewable channel list\n", stderr);
        goto out;
    }
    engine_client = build_engine_client(game_id, area_id, platform_id, uid,
                                         signal_session_id, 0, true, profile,
                                         client_kind);
    data = json_object_new_object();
    dto = json_object_new_object();
    list = json_object_new_array();
    if (!engine_client || !data || !dto || !list)
        goto out;
    for (index = 0; index < json_object_array_length(auth_channels); index++) {
        json_object *channel = json_object_array_get_idx(auth_channels, index);
        json_object *wire = json_object_new_object();
        json_object *channel_ip = NULL;
        json_object *session_id = NULL;
        json_object *port = NULL;
        json_object *protocol = NULL;
        json_object *secret_type = NULL;
        json_object *type = NULL;
        const char *wire_protocol;
        int64_t wire_port;

        if (!channel || !wire ||
            !json_object_object_get_ex(channel, "channelIp", &channel_ip) ||
            !json_object_object_get_ex(channel, "dataChannelSessionId",
                                       &session_id) ||
            !json_object_object_get_ex(channel, "port", &port) ||
            !json_object_object_get_ex(channel, "proType", &protocol) ||
            !json_object_object_get_ex(channel, "secretType", &secret_type)) {
            json_object_put(wire);
            goto out;
        }
        json_object_object_get_ex(channel, "type", &type);
        wire_protocol = native_channel_protocol(protocol);
        if (!wire_protocol || native_channel_port(port, &wire_port) != 0 ||
            !json_object_is_type(channel_ip, json_type_string) ||
            !json_object_is_type(session_id, json_type_int) ||
            !json_object_is_type(secret_type, json_type_string) ||
            (type && !json_object_is_type(type, json_type_int)) ||
            (!type && client_kind == ACCELERATION_CLIENT_MOBILE)) {
            json_object_put(wire);
            fputs("authorization contains an invalid native data channel\n",
                  stderr);
            goto out;
        }
        json_object_object_add(wire, "proType",
                               json_object_new_string(wire_protocol));
        json_object_object_add(wire, "dataChannelSessionId",
                               json_object_get(session_id));
        json_object_object_add(wire, "port",
                               json_object_new_int64(wire_port));
        json_object_object_add(wire, "secretType",
                               json_object_get(secret_type));
        json_object_object_add(wire, "channelIp",
                               json_object_get(channel_ip));
        if (type)
            json_object_object_add(wire, "type", json_object_get(type));
        json_object_array_add(list, wire);
    }
    json_object_object_add(dto, "dataChannelList", list);
    list = NULL;
    json_object_object_add(data, "engineClient", engine_client);
    engine_client = NULL;
    json_object_object_add(data, "channelAuthDTO", dto);
    dto = NULL;
    status = run_acceleration_operation(operation, CHANNEL_TICKET_ENDPOINT,
                                        data, channel_ticket_file, session_file,
                                        key_file, client_kind);

out:
    json_object_put(data);
    json_object_put(dto);
    json_object_put(list);
    json_object_put(engine_client);
    cleanse_json_value(authorization);
    json_object_put(authorization);
    cleanse_json_value(profile_response);
    json_object_put(profile_response);
    cleanse_json_value(record);
    json_object_put(record);
    if (pc_uid) {
        OPENSSL_cleanse(pc_uid, strlen(pc_uid));
        free(pc_uid);
    }
    return status;
}

static int run_channel_renew(const char *session_file, const char *key_file,
                             int argument_count, char **arguments)
{
    return run_channel_renew_mode(session_file, key_file, argument_count,
                                  arguments, DEFAULT_PROFILE_FILE,
                                  DEFAULT_AUTHORIZATION_FILE,
                                  DEFAULT_CHANNEL_TICKET_FILE,
                                  ACCELERATION_CLIENT_MOBILE,
                                  "channel-renew");
}

static int run_pc_channel_renew(const char *session_file, const char *key_file,
                                int argument_count, char **arguments)
{
    return run_channel_renew_mode(session_file, key_file, argument_count,
                                  arguments, DEFAULT_PC_PROFILE_FILE,
                                  DEFAULT_PC_AUTHORIZATION_FILE,
                                  DEFAULT_PC_CHANNEL_TICKET_FILE,
                                  ACCELERATION_CLIENT_WINDOWS,
                                  "pc-channel-renew");
}

static json_object *runtime_outbound(json_object *outbounds, const char *id,
                                     const char *type)
{
    size_t index;

    for (index = 0; index < json_object_array_length(outbounds); index++) {
        json_object *outbound = json_object_array_get_idx(outbounds, index);
        const char *candidate_id = string_member(outbound, "id");
        const char *candidate_type = string_member(outbound, "type");

        if (candidate_id && candidate_type && !strcmp(candidate_id, id) &&
            !strcmp(candidate_type, type))
            return outbound;
    }
    {
        json_object *outbound = json_object_new_object();
        json_object *channels = json_object_new_array();

        if (!outbound || !channels) {
            json_object_put(outbound);
            json_object_put(channels);
            return NULL;
        }
        json_object_object_add(outbound, "id", json_object_new_string(id));
        json_object_object_add(outbound, "type", json_object_new_string(type));
        json_object_object_add(outbound, "channels", channels);
        json_object_array_add(outbounds, outbound);
        return outbound;
    }
}

static json_object *build_runtime_channel(json_object *auth_channel,
                                          json_object *profile_channel,
                                          int64_t received_at,
                                          bool native_bolt)
{
    static const char *names[] = {
        "channelIp", "port", "dataChannelSessionId", "channelSt", "secretType"
    };
    static const char *output_names[] = {
        "ip", "port", "sessionId", "ticket", "secretType"
    };
    const char *protocol = string_member(auth_channel, "proType");
    const char *profile_field;
    json_object *encryption;
    json_object *wire = NULL;
    json_object *value = NULL;
    size_t index;

    if (!protocol || (strcasecmp(protocol, "TCP") &&
                      strcasecmp(protocol, "UDP")))
        return NULL;
    wire = json_object_new_object();
    if (!wire)
        return NULL;
    json_object_object_add(wire, "transport",
                           json_object_new_string(native_bolt ? "bolt" : "bbnet"));
    json_object_object_add(wire, "protocol",
                           json_object_new_string(!strcasecmp(protocol, "TCP")
                                                      ? "TCP" : "UDP"));
    for (index = 0; index < sizeof(names) / sizeof(names[0]); index++) {
        if (!json_object_object_get_ex(auth_channel, names[index], &value))
            goto fail;
        json_object_object_add(wire, output_names[index],
                               json_object_get(value));
    }
    if (json_object_object_get_ex(auth_channel, "type", &value))
        json_object_object_add(wire, "type", json_object_get(value));
    else
        json_object_object_add(wire, "type", json_object_new_int(0));
    if (json_object_object_get_ex(auth_channel, "expireTime", &value)) {
        int64_t expiry = json_object_get_int64(value);

        if (!json_object_is_type(value, json_type_int) || expiry <= 0 ||
            received_at < 0 || expiry > INT64_MAX - received_at)
            goto fail;
        json_object_object_add(wire, "expiresAt",
                               json_object_new_int64(expiry + received_at));
    }
    encryption = object_member(profile_channel, "encryption", json_type_boolean);
    json_object_object_add(wire, "encrypted",
                           json_object_new_boolean(encryption &&
                                                   json_object_get_boolean(encryption)));
    profile_field = string_member(profile_channel, "bbCliParam");
    if (profile_field)
        json_object_object_add(wire, "clientParameter",
                               json_object_new_string(profile_field));
    profile_field = string_member(profile_channel, "bbSrvParam");
    if (profile_field)
        json_object_object_add(wire, "serverParameter",
                               json_object_new_string(profile_field));
    profile_field = string_member(profile_channel, "bbstrategy");
    if (profile_field)
        json_object_object_add(wire, "strategy",
                               json_object_new_string(profile_field));
    profile_field = string_member(profile_channel, "bip");
    if (profile_field)
        json_object_object_add(wire, "bip", json_object_new_string(profile_field));
    return wire;

fail:
    json_object_put(wire);
    return NULL;
}

static int append_runtime_route_cidrs(json_object *output, json_object *route)
{
    json_object *list = object_member(route, "cidrList", json_type_array);
    const char *single = string_member(route, "cidrIp");
    size_t index;

    if (list && json_object_array_length(list)) {
        for (index = 0; index < json_object_array_length(list); index++) {
            json_object *value = json_object_array_get_idx(list, index);
            char normalized[INET_ADDRSTRLEN + 5];

            if (!json_object_is_type(value, json_type_string) ||
                normalize_profile_cidr(json_object_get_string(value), normalized,
                                       sizeof(normalized)) != 0)
                return -1;
            json_object_array_add(output, json_object_new_string(normalized));
        }
    } else if (single) {
        char normalized[INET_ADDRSTRLEN + 5];

        if (normalize_profile_cidr(single, normalized, sizeof(normalized)) != 0)
            return -1;
        json_object_array_add(output, json_object_new_string(normalized));
    }
    return 0;
}

static int append_runtime_route_ports(json_object *output, json_object *route)
{
    json_object *list = object_member(route, "portList", json_type_array);
    json_object *single = NULL;
    size_t index;

    if (json_object_object_get_ex(route, "port", &single) &&
        json_object_get_int64(single) != 0) {
        char normalized[32];

        if (normalize_route_port(single, normalized, sizeof(normalized)) != 0)
            return -1;
        json_object_array_add(output, json_object_new_string(normalized));
    }
    if (!list)
        return 0;
    for (index = 0; index < json_object_array_length(list); index++) {
        char normalized[32];

        if (normalize_route_port(json_object_array_get_idx(list, index),
                                 normalized, sizeof(normalized)) != 0)
            return -1;
        if (strcmp(normalized, "*"))
            json_object_array_add(output, json_object_new_string(normalized));
    }
    return 0;
}

static int build_runtime_routes(json_object *profile, json_object *output,
                                const char **default_outbound_id)
{
    json_object *router = object_member(profile, "routerProfile", json_type_object);
    json_object *routes = object_member(router, "routeList", json_type_array);
    size_t index;

    *default_outbound_id = NULL;
    if (!routes)
        return -1;
    for (index = 0; index < json_object_array_length(routes); index++) {
        json_object *route = json_object_array_get_idx(routes, index);
        const char *mode = string_member(route, "mode");
        const char *primary_id = string_member(route, "outboundId");
        const char *selected_id;
        json_object *wire;
        json_object *cidrs;
        json_object *ports;
        int64_t protocol;

        if (!mode || strcasecmp(mode, "bolt"))
            continue;
        selected_id = profile_route_outbound_id(route);
        if (!primary_id || !primary_id[0] || !selected_id || !selected_id[0] ||
            profile_route_protocol(route, &protocol) != 0)
            return -1;
        if (!*default_outbound_id)
            *default_outbound_id = primary_id;
        wire = json_object_new_object();
        cidrs = json_object_new_array();
        ports = json_object_new_array();
        if (!wire || !cidrs || !ports) {
            json_object_put(wire);
            json_object_put(cidrs);
            json_object_put(ports);
            return -1;
        }
        if (append_runtime_route_cidrs(cidrs, route) != 0 ||
            append_runtime_route_ports(ports, route) != 0) {
            json_object_put(wire);
            json_object_put(cidrs);
            json_object_put(ports);
            return -1;
        }
        if (!json_object_array_length(cidrs)) {
            json_object_put(wire);
            json_object_put(cidrs);
            json_object_put(ports);
            continue;
        }
        json_object_object_add(wire, "outboundId",
                               json_object_new_string(selected_id));
        json_object_object_add(wire, "primaryOutboundId",
                               json_object_new_string(primary_id));
        json_object_object_add(wire, "protocol",
                               json_object_new_int64(protocol));
        json_object_object_add(wire, "cidrs", cidrs);
        json_object_object_add(wire, "ports", ports);
        json_object_array_add(output, wire);
    }
    return *default_outbound_id ? 0 : -1;
}

static int run_runtime_prepare_mode(int argument_count, char **arguments,
                                    const char *profile_file,
                                    const char *authorization_file,
                                    const char *channel_ticket_file,
                                    const char *runtime_file,
                                    bool native_bolt,
                                    const char *operation)
{
    json_object *profile_response = NULL;
    json_object *authorization = NULL;
    json_object *channel_ticket = NULL;
    json_object *profile;
    json_object *auth_data;
    json_object *auth_channels;
    json_object *renewed_channels = NULL;
    json_object *profile_entries = NULL;
    json_object *runtime = NULL;
    json_object *runtime_outbounds = NULL;
    json_object *runtime_routes = NULL;
    json_object *tcp_rules = NULL;
    json_object *udp_rules = NULL;
    json_object *tcp_domains = NULL;
    json_object *udp_domains = NULL;
    const char *signal_session_id;
    const char *default_outbound_id = NULL;
    size_t index;
    bool renewal_present = false;
    int64_t received_at = 0;
    int status = 1;

    if (argument_count != 0) {
        fprintf(stderr, "%s takes no arguments\n", operation);
        return 2;
    }
    (void)arguments;
    if (load_private_json(profile_file, &profile_response) != 0 ||
        load_private_json(authorization_file, &authorization) != 0) {
        fprintf(stderr, "%s requires a stored profile and authorization\n",
                operation);
        goto out;
    }
    profile = control_data_object(profile_response);
    auth_data = signal_control_data_object(authorization);
    received_at = json_object_get_int64(object_member(authorization,
                                                      "receivedAt", json_type_int));
    auth_channels = object_member(auth_data, "channelAuthList", json_type_array);
    {
        int ticket_status = load_private_json(channel_ticket_file,
                                              &channel_ticket);

        if (ticket_status == 0) {
            json_object *ticket_data = signal_control_data_object(channel_ticket);

            renewal_present = true;
            received_at = json_object_get_int64(object_member(channel_ticket,
                                                              "receivedAt", json_type_int));
            renewed_channels = object_member(ticket_data, "dataChannelList",
                                             json_type_array);
            if (!renewed_channels || !json_object_array_length(renewed_channels)) {
                fputs("stored channel renewal has no usable channel list\n",
                      stderr);
                goto out;
            }
            auth_channels = renewed_channels;
        } else if (errno != ENOENT) {
            renewal_present = true;
            fputs("stored channel renewal is invalid\n", stderr);
            goto out;
        }
    }
    profile_entries = profile_acceleration_channel_entries(profile);
    signal_session_id = string_member(auth_data, "signalSessionId");
    if (!auth_channels || !json_object_array_length(auth_channels) ||
        !profile_entries || !signal_session_id || !signal_session_id[0]) {
        fputs("stored profile or authorization has no usable channel data\n", stderr);
        goto out;
    }
    runtime = json_object_new_object();
    runtime_outbounds = json_object_new_array();
    runtime_routes = json_object_new_array();
    tcp_rules = json_object_new_array();
    udp_rules = json_object_new_array();
    tcp_domains = json_object_new_array();
    udp_domains = json_object_new_array();
    if (!runtime || !runtime_outbounds || !runtime_routes ||
        !tcp_rules || !udp_rules ||
        !tcp_domains || !udp_domains)
        goto out;
    json_object_object_add(runtime, "schemaVersion", json_object_new_int(2));
    json_object_object_add(runtime, "signalSessionId",
                           json_object_new_string(signal_session_id));
    for (index = 0; index < json_object_array_length(auth_channels); index++) {
        json_object *auth_channel = json_object_array_get_idx(auth_channels, index);
        json_object *entry;
        json_object *profile_channel;
        json_object *outbound;
        json_object *channels;
        json_object *wire;
        const char *protocol = string_member(auth_channel, "proType");
        const char *outbound_id;
        const char *outbound_type;

        if (!protocol || (strcasecmp(protocol, "TCP") != 0 &&
                         strcasecmp(protocol, "UDP") != 0))
            continue;
        entry = profile_channel_entry_for_authorization(profile_entries,
                                                         auth_channel);
        profile_channel = object_member(entry, "channel", json_type_object);
        outbound_id = string_member(entry, "outboundId");
        outbound_type = string_member(entry, "outboundType");
        if (!profile_channel || !outbound_id || !outbound_type)
            goto out;
        outbound = runtime_outbound(runtime_outbounds, outbound_id,
                                    outbound_type);
        channels = object_member(outbound, "channels", json_type_array);
        wire = build_runtime_channel(auth_channel, profile_channel, received_at,
                                      native_bolt);
        if (!outbound || !channels || !wire) {
            json_object_put(wire);
            goto out;
        }
        json_object_array_add(channels, wire);
    }
    if (!json_object_array_length(runtime_outbounds)) {
        fputs("authorization contains no TCP or UDP channel\n", stderr);
        goto out;
    }
    if (build_runtime_routes(profile, runtime_routes, &default_outbound_id) != 0 ||
        build_profile_route_rules(profile, NULL, tcp_rules, udp_rules,
                                  tcp_domains, udp_domains) != 0) {
        fputs("profile contains an invalid Bolt route\n", stderr);
        goto out;
    }
    json_object_object_add(runtime, "defaultOutboundId",
                           json_object_new_string(default_outbound_id));
    json_object_object_add(runtime, "outbounds", runtime_outbounds);
    runtime_outbounds = NULL;
    json_object_object_add(runtime, "routes", runtime_routes);
    runtime_routes = NULL;
    json_object_object_add(runtime, "tcpRules", tcp_rules);
    tcp_rules = NULL;
    json_object_object_add(runtime, "udpRules", udp_rules);
    udp_rules = NULL;
    json_object_object_add(runtime, "tcpDomains", tcp_domains);
    tcp_domains = NULL;
    json_object_object_add(runtime, "udpDomains", udp_domains);
    udp_domains = NULL;
    if (store_private_json(runtime_file, runtime) != 0) {
        fprintf(stderr, "unable to store runtime configuration: %s\n",
                strerror(errno));
        goto out;
    }
    print_runtime_result_summary(operation, runtime, runtime_file);
    status = 0;

out:
    if (status != 0 && renewal_present)
        remove_private_file_if_safe(runtime_file);
    json_object_put(runtime_outbounds);
    json_object_put(runtime_routes);
    json_object_put(tcp_rules);
    json_object_put(udp_rules);
    json_object_put(tcp_domains);
    json_object_put(udp_domains);
    json_object_put(runtime);
    json_object_put(profile_entries);
    cleanse_json_value(channel_ticket);
    json_object_put(channel_ticket);
    cleanse_json_value(authorization);
    json_object_put(authorization);
    cleanse_json_value(profile_response);
    json_object_put(profile_response);
    return status;
}

static int run_runtime_prepare(int argument_count, char **arguments)
{
    return run_runtime_prepare_mode(argument_count, arguments,
                                    DEFAULT_PROFILE_FILE,
                                    DEFAULT_AUTHORIZATION_FILE,
                                    DEFAULT_CHANNEL_TICKET_FILE,
                                    DEFAULT_RUNTIME_FILE,
                                    false,
                                    "runtime-prepare");
}

static int run_pc_runtime_prepare(int argument_count, char **arguments)
{
    return run_runtime_prepare_mode(argument_count, arguments,
                                    DEFAULT_PC_PROFILE_FILE,
                                    DEFAULT_PC_AUTHORIZATION_FILE,
                                    DEFAULT_PC_CHANNEL_TICKET_FILE,
                                    DEFAULT_RUNTIME_FILE,
                                    true,
                                    "pc-runtime-prepare");
}

static void print_login_summary(const char *method)
{
    json_object *summary = json_object_new_object();

    if (!summary)
        return;
    json_object_object_add(summary, "success", json_object_new_boolean(true));
    json_object_object_add(summary, "sessionStored", json_object_new_boolean(true));
    json_object_object_add(summary, "method", json_object_new_string(method));
    puts(json_object_to_json_string_ext(summary, JSON_C_TO_STRING_PLAIN));
    json_object_put(summary);
}

static int refresh_stored_session(const char *session_file)
{
    json_object *record = NULL;
    json_object *payload = NULL;
    json_object *result = NULL;
    const char *device_id = NULL;
    const char *session_id = NULL;
    const char *refresh_token = NULL;
    int status = 1;

    if (load_session_record(session_file, &record) != 0 ||
        session_components(record, &device_id, NULL, &session_id, &refresh_token,
                           NULL) != 0 ||
        !session_id || !session_id[0] || !refresh_token || !refresh_token[0]) {
        fputs("session refresh requires a valid private session file\n", stderr);
        goto out;
    }
    payload = build_client_context(device_id);
    if (!payload || set_client_session(payload, session_id) != 0)
        goto out;
    json_object_object_add(payload, "sessionToken",
                           json_object_new_string(refresh_token));
    if (api_request("capi/login.autoLogin", payload, &result) != 0)
        goto out;
    if (!response_is_success(result)) {
        fputs("session refresh was rejected by the service\n", stderr);
        status = 3;
        goto out;
    }
    if (store_session_record(session_file, device_id, "refresh", result) != 0) {
        fputs("unable to replace the private session file\n", stderr);
        goto out;
    }
    print_login_summary("refresh");
    status = 0;

out:
    cleanse_json_value(result);
    json_object_put(result);
    cleanse_json_value(payload);
    json_object_put(payload);
    cleanse_json_value(record);
    json_object_put(record);
    return status;
}

static int run_login_cipher_self_test(void)
{
    static const unsigned char key[16] = "0123456789abcdef";
    size_t length;

    for (length = 0; length <= 64; length++) {
        unsigned char input[65];
        struct bytes ciphertext = {0};
        struct bytes plaintext = {0};
        size_t i;

        for (i = 0; i < length; i++)
            input[i] = (unsigned char)('A' + (i % 26));
        if (aes_encrypt(key, input, length, &ciphertext) != 0 ||
            aes_decrypt(key, ciphertext.data, ciphertext.len, &plaintext) != 0 ||
            plaintext.len != length || memcmp(plaintext.data, input, length) != 0) {
            bytes_free(&ciphertext);
            bytes_free(&plaintext);
            fprintf(stderr, "login cipher self-test failed at length %zu\n", length);
            return 1;
        }
        bytes_free(&ciphertext);
        bytes_free(&plaintext);
    }
    return 0;
}

static EVP_PKEY *new_test_rsa_key(void)
{
    EVP_PKEY_CTX *ctx = NULL;
    EVP_PKEY *key = NULL;

    ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    if (!ctx || EVP_PKEY_keygen_init(ctx) <= 0 ||
        EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 1024) <= 0 ||
        EVP_PKEY_keygen(ctx, &key) <= 0) {
        EVP_PKEY_free(key);
        key = NULL;
    }
    EVP_PKEY_CTX_free(ctx);
    return key;
}

static int run_native_client_self_test(void)
{
    static const char expected_header[] =
        "{\"ver\":\"1.0.0.0\",\"os\":\"windows\",\"ch\":\"BPC_1\","
        "\"appName\":\"biubiu\"}";
    static const char *const ordered_client_names[] = {
        "appId", "deviceId", "deviceIdType", "os", "osVersion", "biuid",
        "ex",
    };
    static const char *const ordered_extension_names[] = {
        "appId", "versionCode", "ch", "os", "build", "defaultChFlag",
        "mac", "uid", "ver", "appName", "st", "appVersion", "osVersion",
        "osProductName", "cpuArchitecture", "packageType", "appSession",
        "speedupSession", "gameId", "gameArea", "accMode", "gamePlatform",
        "gamePlatformId", "uthash", "network", "rmac",
    };
    json_object *client = NULL;
    json_object *extensions = NULL;
    json_object *header = NULL;
    json_object *engine_client = NULL;
    json_object *channel_data = NULL;
    json_object *channel_request = NULL;
    json_object *login_data = NULL;
    json_object *login_channels = NULL;
    json_object *login_request = NULL;
    json_object *login_inner = NULL;
    json_object *value = NULL;
    const char *client_text;
    const char *extension_text;
    const char *header_text;
    const char *channel_request_text;
    const char *login_request_text;
    const char *session;
    const char *cursor;
    char *trace_id = NULL;
    char *speedup_session = NULL;
    char *device_id = NULL;
    size_t index;
    int result = 1;

    device_id = new_native_utdid();
    client = build_acceleration_client(device_id, NULL, "self-test-ticket",
                                       ACCELERATION_CLIENT_WINDOWS);
    extensions = object_member(client, "ex", json_type_object);
    header = build_acceleration_client_header(
        client, ACCELERATION_CLIENT_WINDOWS);
    engine_client = build_engine_client(100, 200, 6, "300", "", 0, true,
                                        NULL, ACCELERATION_CLIENT_WINDOWS);
    client_text = client ? json_object_to_json_string_ext(
                               client, JSON_C_TO_STRING_PLAIN) : NULL;
    extension_text = extensions ? json_object_to_json_string_ext(
                                      extensions, JSON_C_TO_STRING_PLAIN) : NULL;
    header_text = header ? json_object_to_json_string_ext(
                               header, JSON_C_TO_STRING_PLAIN) : NULL;
    if (!device_id || !valid_native_utdid(device_id) || !client_text ||
        !extension_text || !header_text || !engine_client ||
        strcmp(string_member(client, "appId"), PC_APP_ID) != 0 ||
        strcmp(string_member(client, "deviceId"), device_id) != 0 ||
        strcmp(string_member(client, "deviceIdType"), "utdid") != 0 ||
        strcmp(string_member(client, "os"), "windows") != 0 ||
        strcmp(string_member(client, "osVersion"), PC_OS_VERSION) != 0 ||
        strcmp(header_text, expected_header) != 0 ||
        strcmp(string_member(engine_client, "engineVersion"),
               SIGNAL_ENGINE_VERSION) != 0 ||
        strcmp(string_member(engine_client, "signalSessionId"), "") != 0 ||
        json_object_object_get_ex(extensions, "biuid", &value))
        goto out;
    if (!json_object_object_get_ex(client, "biuid", &value) ||
        !json_object_is_type(value, json_type_int) ||
        json_object_get_int(value) != 0)
        goto out;
    if (!json_object_object_get_ex(engine_client, "type", &value) ||
        !json_object_is_type(value, json_type_int) ||
        json_object_get_int(value) != 4)
        goto out;
    if (strcmp(string_member(extensions, "defaultChFlag"), "") != 0 ||
        strcmp(string_member(extensions, "st"), "self-test-ticket") != 0 ||
        !json_object_object_get_ex(extensions, "packageType", &value) ||
        !json_object_is_type(value, json_type_int) ||
        json_object_get_int(value) != 1)
        goto out;
    if (!json_object_object_get_ex(extensions, "uthash", &value) ||
        !json_object_is_type(value, json_type_int) ||
        json_object_get_int64(value) != (int64_t)native_utdid_hash(device_id))
        goto out;
    if (!json_object_object_get_ex(extensions, "gameId", &value) ||
        !json_object_is_type(value, json_type_int) ||
        json_object_get_int(value) != -1 ||
        !json_object_object_get_ex(extensions, "gameArea", &value) ||
        json_object_get_int(value) != -1 ||
        !json_object_object_get_ex(extensions, "accMode", &value) ||
        json_object_get_int(value) != -1 ||
        !json_object_object_get_ex(extensions, "gamePlatformId", &value) ||
        json_object_get_int(value) != -1)
        goto out;
    session = string_member(extensions, "appSession");
    if (!session || strlen(session) != 32)
        goto out;
    cursor = client_text;
    for (index = 0;
         index < sizeof(ordered_client_names) / sizeof(ordered_client_names[0]);
         index++) {
        char needle[64];

        if (snprintf(needle, sizeof(needle), "\"%s\":",
                     ordered_client_names[index]) >= (int)sizeof(needle) ||
            !(cursor = strstr(cursor, needle)))
            goto out;
        cursor += strlen(needle);
    }
    for (index = 0; index < 32; index++) {
        unsigned char character = (unsigned char)session[index];

        if (!isxdigit(character) ||
            (isalpha(character) && index >= 12 && index < 16 &&
             !islower(character)) ||
            (isalpha(character) && !(index >= 12 && index < 16) &&
             !isupper(character)))
            goto out;
    }
    cursor = extension_text;
    for (index = 0;
         index < sizeof(ordered_extension_names) /
                     sizeof(ordered_extension_names[0]);
         index++) {
        char needle[64];

        if (snprintf(needle, sizeof(needle), "\"%s\":",
                     ordered_extension_names[index]) >= (int)sizeof(needle) ||
            !(cursor = strstr(cursor, needle)))
            goto out;
        cursor += strlen(needle);
    }
    trace_id = new_native_trace_id();
    if (!trace_id || !trace_id[0])
        goto out;
    for (index = 0; trace_id[index]; index++) {
        if (!isdigit((unsigned char)trace_id[index]))
            goto out;
    }
    speedup_session = new_native_speedup_session();
    if (!speedup_session || strlen(speedup_session) != 36 ||
        strncmp(speedup_session, "v2-", 3) != 0 || speedup_session[15] != '-')
        goto out;
    for (index = 3; index < 15; index++) {
        if (!isdigit((unsigned char)speedup_session[index]))
            goto out;
    }
    for (index = 16; index < 36; index++) {
        if (!isxdigit((unsigned char)speedup_session[index]) ||
            (isalpha((unsigned char)speedup_session[index]) &&
             !isupper((unsigned char)speedup_session[index])))
            goto out;
    }
    channel_data = json_object_new_object();
    channel_request = build_native_control_request(channel_data, client);
    channel_request_text = channel_request ? json_object_to_json_string_ext(
                                                channel_request,
                                                JSON_C_TO_STRING_PLAIN) : NULL;
    if (!channel_request_text ||
        strncmp(channel_request_text, "{\"client\":", 10) != 0 ||
        strcmp(string_member(channel_request, "id"), "") != 0)
        goto out;
    login_data = json_object_new_object();
    login_channels = json_object_new_array();
    if (!login_data || !login_channels)
        goto out;
    json_object_object_add(login_data, "signalSt",
                           json_object_new_string("self-test-signal-ticket"));
    json_object_object_add(login_data, "engineClient",
                           json_object_get(engine_client));
    json_object_array_add(login_channels, json_tokener_parse(
        "{\"dataChannelIp\":\"192.0.2.5\",\"port\":2250,\"proType\":\"TCP\"}"));
    json_object_object_add(login_data, "list", json_object_get(login_channels));
    login_request = build_native_control_request(login_data, client);
    login_request_text = login_request ? json_object_to_json_string_ext(
                                             login_request,
                                             JSON_C_TO_STRING_PLAIN) : NULL;
    if (!login_request_text ||
        strncmp(login_request_text, "{\"client\":", 10) != 0 ||
        strcmp(string_member(login_request, "id"), "") != 0 ||
        !(login_inner = object_member(login_request, "data",
                                      json_type_object)) ||
        strcmp(string_member(login_inner, "signalSt"),
               "self-test-signal-ticket") != 0 ||
        !object_member(login_inner, "engineClient", json_type_object) ||
        !json_object_object_get_ex(login_inner, "list", &value) ||
        !json_object_is_type(value, json_type_array) ||
        json_object_array_length(value) != 1 ||
        !json_object_object_get_ex(engine_client, "serverId", &value) ||
        json_object_get_int(value) != 0)
        goto out;
    result = 0;

out:
    if (result != 0)
        fputs("native client contract self-test failed\n", stderr);
    free(trace_id);
    if (speedup_session)
        OPENSSL_cleanse(speedup_session, strlen(speedup_session));
    free(speedup_session);
    if (device_id)
        OPENSSL_cleanse(device_id, strlen(device_id));
    free(device_id);
    json_object_put(login_request);
    json_object_put(login_channels);
    json_object_put(login_data);
    json_object_put(channel_request);
    json_object_put(channel_data);
    json_object_put(engine_client);
    json_object_put(header);
    cleanse_json_value(client);
    json_object_put(client);
    return result;
}

static int run_acceleration_key_store_self_test(void)
{
    char directory_template[] = "/tmp/biubiu-key-selftest.XXXXXX";
    char *directory = NULL;
    char *path = NULL;
    char *encoded = NULL;
    char *security_value = NULL;
    unsigned char *der = NULL;
    EVP_PKEY *generated = NULL;
    struct acceleration_public_key parsed = {0};
    struct acceleration_public_key loaded = {0};
    struct acceleration_public_key unsafe = {0};
    struct stat info;
    int der_length;
    int result = 1;

    directory = mkdtemp(directory_template);
    generated = new_test_rsa_key();
    if (!directory || !generated || asprintf(&path, "%s/key.json", directory) < 0)
        goto out;
    der_length = i2d_PUBKEY(generated, &der);
    if (der_length <= 0)
        goto out;
    encoded = base64_encode(der, (size_t)der_length);
    if (!encoded || asprintf(&security_value, "7|%s", encoded) < 0 ||
        parse_security_key_value(security_value, &parsed) != 0 ||
        parsed.version != 7 || strcmp(parsed.der_b64, encoded) != 0 ||
        store_acceleration_key(path, &parsed) != 0 || stat(path, &info) != 0 ||
        (info.st_mode & (S_IRWXG | S_IRWXO)) != 0 ||
        load_acceleration_key(path, &loaded) != 0 || loaded.version != 7 ||
        strcmp(loaded.der_b64, encoded) != 0 ||
        EVP_PKEY_bits(loaded.key) != EVP_PKEY_bits(generated))
        goto out;
    if (chmod(path, S_IRUSR | S_IWUSR | S_IRGRP) != 0 ||
        load_acceleration_key(path, &unsafe) == 0 || errno != EACCES ||
        chmod(path, S_IRUSR | S_IWUSR) != 0)
        goto out;
    result = 0;

out:
    if (result != 0)
        fputs("acceleration key store self-test failed\n", stderr);
    acceleration_public_key_free(&unsafe);
    acceleration_public_key_free(&loaded);
    acceleration_public_key_free(&parsed);
    EVP_PKEY_free(generated);
    OPENSSL_free(der);
    free(encoded);
    free(security_value);
    if (path)
        unlink(path);
    if (directory)
        rmdir(directory);
    free(path);
    return result;
}

static int run_native_key_rotation_self_test(void)
{
    EVP_PKEY *generated = NULL;
    unsigned char *der = NULL;
    char *encoded = NULL;
    json_object *response = NULL;
    struct acceleration_public_key parsed = {0};
    struct acceleration_public_key seed = {0};
    char fingerprint[65] = {0};
    int der_length;
    int result = 1;

    generated = new_test_rsa_key();
    response = json_object_new_object();
    if (!generated || !response)
        goto out;
    der_length = i2d_PUBKEY(generated, &der);
    if (der_length <= 0 || !(encoded = base64_encode(der, (size_t)der_length)))
        goto out;
    json_object_object_add(response, "v", json_object_new_int(11));
    json_object_object_add(response, "rsaPublicKey", json_object_new_string(encoded));
    if (parse_native_key_rotation(response, &parsed) != 0 ||
        parsed.version != 11 || strcmp(parsed.der_b64, encoded) != 0 ||
        EVP_PKEY_bits(parsed.key) != EVP_PKEY_bits(generated))
        goto out;

    acceleration_public_key_free(&parsed);
    json_object_object_add(response, "v", json_object_new_string("11"));
    if (parse_native_key_rotation(response, &parsed) == 0)
        goto out;
    json_object_object_add(response, "v", json_object_new_int(11));
    json_object_object_del(response, "rsaPublicKey");
    if (parse_native_key_rotation(response, &parsed) == 0 ||
        load_native_seed_acceleration_key(&seed) != 0 ||
        seed.version != NATIVE_SEED_KEY_VERSION ||
        EVP_PKEY_bits(seed.key) != 1024 ||
        acceleration_key_fingerprint(seed.der_b64, fingerprint) != 0 ||
        strcmp(fingerprint,
               "d309e09af41ea6ddd9580da555ae738a94ec17a9efd75c09c51ba252a5d1eda1") != 0)
        goto out;
    result = 0;

out:
    if (result != 0)
        fputs("native seed and key-rotation self-test failed\n", stderr);
    OPENSSL_cleanse(fingerprint, sizeof(fingerprint));
    acceleration_public_key_free(&seed);
    acceleration_public_key_free(&parsed);
    json_object_put(response);
    EVP_PKEY_free(generated);
    OPENSSL_free(der);
    free(encoded);
    return result;
}

static int run_adat_cipher_self_test(void)
{
    EVP_PKEY *rsa_key = NULL;
    struct adat_session_keys keys = {{0}, {0}};
    struct bytes wrapped_key = {0};
    struct bytes wrapped_iv = {0};
    struct bytes clear_key = {0};
    struct bytes clear_iv = {0};
    json_object *payload = NULL;
    json_object *envelope = NULL;
    json_object *outer = NULL;
    json_object *decrypted = NULL;
    json_object *value = NULL;
    const char *envelope_text;
    const char *payload_text;
    const char *key_field;
    const char *iv_field;
    const char *data_field;
    size_t i;
    int result = 1;

    rsa_key = new_test_rsa_key();
    payload = json_object_new_object();
    if (!rsa_key || !payload)
        goto out;
    json_object_object_add(payload, "id", json_object_new_string("offline-test"));
    json_object_object_add(payload, "value", json_object_new_int(7));
    envelope = build_adat_envelope(payload, NATIVE_SEED_KEY_VERSION,
                                   rsa_key, &keys);
    if (!envelope)
        goto out;
    for (i = 0; i < sizeof(keys.key); i++) {
        if (keys.key[i] < 'A' || keys.key[i] > 'Y' ||
            keys.iv[i] < 'A' || keys.iv[i] > 'Y')
            goto out;
    }
    envelope_text = json_object_to_json_string_ext(
        envelope, JSON_C_TO_STRING_PLAIN);
    key_field = envelope_text ? strstr(envelope_text, ",\"k\":") : NULL;
    iv_field = envelope_text ? strstr(envelope_text, ",\"i\":") : NULL;
    data_field = envelope_text ? strstr(envelope_text, ",\"d\":") : NULL;
    if (!envelope_text || strncmp(envelope_text, "{\"v\":1,", 7) != 0 ||
        !key_field || !iv_field || !data_field ||
        !(key_field < iv_field && iv_field < data_field))
        goto out;

    if (!json_object_object_get_ex(envelope, "k", &value) ||
        base64_decode(json_object_get_string(value), &wrapped_key) != 0 ||
        rsa_decrypt_value(rsa_key, wrapped_key.data, wrapped_key.len,
                          &clear_key) != 0 ||
        clear_key.len != sizeof(keys.key) ||
        CRYPTO_memcmp(clear_key.data, keys.key, sizeof(keys.key)) != 0)
        goto out;
    if (!json_object_object_get_ex(envelope, "i", &value) ||
        base64_decode(json_object_get_string(value), &wrapped_iv) != 0 ||
        rsa_decrypt_value(rsa_key, wrapped_iv.data, wrapped_iv.len,
                          &clear_iv) != 0 ||
        clear_iv.len != sizeof(keys.iv) ||
        CRYPTO_memcmp(clear_iv.data, keys.iv, sizeof(keys.iv)) != 0)
        goto out;

    outer = json_object_new_object();
    if (!outer || !json_object_object_get_ex(envelope, "d", &value))
        goto out;
    json_object_object_add(outer, "c", json_object_new_int(0));
    json_object_object_add(outer, "d", json_object_get(value));
    if (decrypt_adat_response(outer, &keys, &decrypted) != 0)
        goto out;
    payload_text = json_object_to_json_string_ext(payload, JSON_C_TO_STRING_PLAIN);
    if (strcmp(payload_text,
               json_object_to_json_string_ext(decrypted,
                                              JSON_C_TO_STRING_PLAIN)) != 0)
        goto out;
    json_object_put(decrypted);
    decrypted = NULL;

    json_object_object_add(outer, "c", json_object_new_int(2));
    if (decrypt_adat_response(outer, &keys, &decrypted) != 2 || !decrypted ||
        strcmp(payload_text,
               json_object_to_json_string_ext(decrypted,
                                              JSON_C_TO_STRING_PLAIN)) != 0)
        goto out;
    json_object_put(decrypted);
    decrypted = NULL;
    json_object_object_del(outer, "d");
    json_object_object_add(outer, "c", json_object_new_int(-1));
    {
        int code;

        if (!adat_outer_code(outer, &code) || code != -1 ||
            decrypt_adat_response(outer, &keys, &decrypted) != -1 || decrypted)
            goto out;
    }
    result = 0;

out:
    if (result != 0)
        fputs("ADAT cipher self-test failed\n", stderr);
    OPENSSL_cleanse(&keys, sizeof(keys));
    bytes_free(&wrapped_key);
    bytes_free(&wrapped_iv);
    bytes_free(&clear_key);
    bytes_free(&clear_iv);
    json_object_put(decrypted);
    json_object_put(outer);
    json_object_put(envelope);
    json_object_put(payload);
    EVP_PKEY_free(rsa_key);
    return result;
}

static int run_session_storage_self_test(void)
{
    static const char device_id[] = "5f234ff7-cf79-492a-aa16-f7509d37dd61";
    char directory_template[] = "/tmp/biubiu-acc-selftest.XXXXXX";
    char *directory;
    char *path = NULL;
    char *link_path = NULL;
    json_object *response = NULL;
    json_object *data = NULL;
    json_object *session_info = NULL;
    json_object *cookies = NULL;
    json_object *cookie = NULL;
    json_object *domains = NULL;
    json_object *loaded = NULL;
    json_object *unsafe = NULL;
    char *cookie_header = NULL;
    char *blocked_header = NULL;
    const char *loaded_device_id = NULL;
    const char *method = NULL;
    const char *session_id = NULL;
    const char *refresh_token = NULL;
    size_t cookie_count = 0;
    struct stat info;
    int result = 1;

    directory = mkdtemp(directory_template);
    if (!directory || asprintf(&path, "%s/session.json", directory) < 0)
        goto out;
    response = json_object_new_object();
    data = json_object_new_object();
    session_info = json_object_new_object();
    cookies = json_object_new_array();
    cookie = json_object_new_object();
    domains = json_object_new_array();
    if (!response || !data || !session_info || !cookies || !cookie || !domains)
        goto out;
    json_object_object_add(response, "code", json_object_new_string("SUCCESS"));
    json_object_object_add(session_info, "sessionId",
                           json_object_new_string("selftest-session"));
    json_object_object_add(session_info, "refreshToken",
                           json_object_new_string("selftest-refresh"));
    json_object_object_add(cookie, "keyName", json_object_new_string("sessionId"));
    json_object_object_add(cookie, "value",
                           json_object_new_string("selftest-cookie"));
    json_object_array_add(cookies, cookie);
    cookie = NULL;
    json_object_object_add(session_info, "cookies", cookies);
    cookies = NULL;
    json_object_array_add(domains,
                          json_object_new_string("https://.biubiu001.com/"));
    json_object_object_add(session_info, "domains", domains);
    domains = NULL;
    json_object_object_add(data, "sessionInfo", session_info);
    session_info = NULL;
    json_object_object_add(response, "data", data);
    data = NULL;

    if (store_session_record(path, device_id, "self-test", response) != 0 ||
        stat(path, &info) != 0 || (info.st_mode & (S_IRWXG | S_IRWXO)) != 0 ||
        load_session_record(path, &loaded) != 0 ||
        session_components(loaded, &loaded_device_id, &method, &session_id,
                           &refresh_token, &cookie_count) != 0 ||
        strcmp(loaded_device_id, device_id) != 0 ||
        strcmp(method, "self-test") != 0 ||
        !session_id || strcmp(session_id, "selftest-session") != 0 ||
        !refresh_token || strcmp(refresh_token, "selftest-refresh") != 0 ||
        cookie_count != 1 ||
        session_cookie_header(loaded, ACCELERATION_HOST, &cookie_header) != 1 ||
        !cookie_header || strcmp(cookie_header,
                                 "Cookie: sessionId=selftest-cookie") != 0 ||
        session_cookie_header(loaded, "example.com", &blocked_header) != 0 ||
        blocked_header)
        goto out;
    if (chmod(path, S_IRUSR | S_IWUSR | S_IRGRP) != 0 ||
        load_session_record(path, &unsafe) == 0 || errno != EACCES ||
        chmod(path, S_IRUSR | S_IWUSR) != 0 ||
        asprintf(&link_path, "%s/session-link.json", directory) < 0 ||
        symlink(path, link_path) != 0 || load_session_record(link_path, &unsafe) == 0)
        goto out;
    result = 0;

out:
    if (result != 0)
        fputs("private session storage self-test failed\n", stderr);
    cleanse_json_value(loaded);
    json_object_put(loaded);
    cleanse_json_value(unsafe);
    json_object_put(unsafe);
    cleanse_json_value(response);
    json_object_put(response);
    json_object_put(data);
    json_object_put(session_info);
    json_object_put(cookies);
    json_object_put(cookie);
    json_object_put(domains);
    if (cookie_header)
        OPENSSL_cleanse(cookie_header, strlen(cookie_header));
    free(cookie_header);
    free(blocked_header);
    if (link_path)
        unlink(link_path);
    if (path)
        unlink(path);
    if (directory)
        rmdir(directory);
    free(path);
    free(link_path);
    return result;
}

static int run_bolt_v3_self_test(void)
{
    static const unsigned char endpoint[] = {0xc0, 0x00, 0x02, 0x01, 0x01, 0xbb};
    static const unsigned char alpha[] = {'a', 'l', 'p', 'h', 'a'};
    static const unsigned char enabled[] = {0x01};
    static const struct bolt_extension extensions[] = {
        {1, endpoint, sizeof(endpoint)},
        {6, alpha, sizeof(alpha)},
        {5, enabled, sizeof(enabled)},
    };
    static const unsigned char expected_request[] = {
        0x03, 0x1c, 0x1c, 0x00, 0x22, 0x04, 0x03, 0x02, 0x01, 0x03,
        0x01, 0x06, 0xc0, 0x00, 0x02, 0x01, 0x01, 0xbb,
        0x06, 0x05, 0x61, 0x6c, 0x70, 0x68, 0x61, 0x05, 0x01, 0x01,
    };
    static const unsigned char marker[] = {0xde, 0xad, 0xbe, 0xef};
    static const unsigned char expected_data[] = {
        0x03, 0x0b, 0x0f, 0x00, 0x11, 0x04, 0x03, 0x02,
        0x01, 0x34, 0x12, 0xde, 0xad, 0xbe, 0xef,
    };
    static const unsigned char associate_response[] = {
        0x03, 0x15, 0x18, 0x00, 0x25, 0x04, 0x03, 0x02,
        0x01, 0x34, 0x12, 0x22, 0x01, 0x01, 0x06, 0xcb,
        0x00, 0x71, 0x08, 0x69, 0x87, 0x75, 0x64, 0x70,
    };
    struct bytes request = {0};
    struct bytes data = {0};
    struct bolt_response parsed = {0};
    int status = 1;

    if (bolt_encode_request(BOLT_COMMAND_CONNECT_REQUEST, 0x01020304U,
                            extensions, sizeof(extensions) / sizeof(extensions[0]),
                            NULL, 0, &request) != 0 ||
        request.len != sizeof(expected_request) ||
        memcmp(request.data, expected_request, sizeof(expected_request)) != 0 ||
        bolt_encode_data(0x01020304U, 0x1234U, marker, sizeof(marker), &data) != 0 ||
        data.len != sizeof(expected_data) ||
        memcmp(data.data, expected_data, sizeof(expected_data)) != 0 ||
        bolt_parse_response(data.data, data.len, &parsed) != 0 ||
        parsed.command != BOLT_COMMAND_DATA || parsed.session_id != 0x01020304U ||
        parsed.connection_id != 0x1234U || parsed.payload_length != sizeof(marker) ||
        memcmp(parsed.payload, marker, sizeof(marker)) != 0 ||
        bolt_parse_response(associate_response, sizeof(associate_response),
                            &parsed) != 0 ||
        !bolt_response_successful_for(&parsed,
                                      BOLT_COMMAND_ASSOCIATE_REQUEST) ||
        bolt_response_successful_for(&parsed, BOLT_COMMAND_CONNECT_REQUEST) ||
        parsed.payload_length != 3 || memcmp(parsed.payload, "udp", 3) != 0 ||
        bolt_parse_response(associate_response,
                            sizeof(associate_response) - 1, &parsed) == 0)
        goto out;
    status = 0;

out:
    if (status != 0)
        fputs("Bolt v3 frame self-test failed\n", stderr);
    bytes_free(&request);
    bytes_free(&data);
    return status;
}

static int run_control_response_self_test(void)
{
    json_object *response = json_object_new_object();
    json_object *state = NULL;
    json_object *data = NULL;
    int status = 1;

    if (!response)
        return 1;
    json_object_object_add(response, "code", json_object_new_int(200));
    state = json_object_new_object();
    if (!state)
        goto out;
    json_object_object_add(state, "code", json_object_new_int(2000000));
    json_object_object_add(response, "state", state);
    if (!acceleration_response_success(response))
        goto out;
    json_object_object_add(state, "code", json_object_new_int(2000001));
    if (!acceleration_response_success(response))
        goto out;
    json_object_object_add(state, "code", json_object_new_int(5000001));
    data = json_object_new_object();
    if (!data)
        goto out;
    json_object_object_add(data, "success", json_object_new_boolean(false));
    json_object_object_add(response, "data", data);
    if (acceleration_response_success(response))
        goto out;
    json_object_object_del(response, "state");
    json_object_object_add(response, "code", json_object_new_string("SUCCESS"));
    json_object_object_add(data, "success", json_object_new_boolean(true));
    if (!acceleration_response_success(response))
        goto out;
    status = 0;

out:
    if (status != 0)
        fputs("control response self-test failed\n", stderr);
    json_object_put(response);
    return status;
}

static int run_self_test(void)
{
    if (run_login_cipher_self_test() != 0 || run_adat_cipher_self_test() != 0 ||
        run_native_client_self_test() != 0 ||
        run_acceleration_key_store_self_test() != 0 ||
        run_native_key_rotation_self_test() != 0 ||
        run_session_storage_self_test() != 0 ||
        run_control_response_self_test() != 0 || run_bolt_v3_self_test() != 0 ||
        run_profile_route_self_test() != 0)
        return 1;
    puts("{\"success\":true,\"tests\":[\"account-envelope\","
         "\"acceleration-adat\",\"native-client-contract\","
         "\"acceleration-key-store\","
         "\"native-seed-key\",\"native-key-rotation\","
         "\"private-session-store\",\"session-cookie-transport\","
         "\"control-response-codes\","
         "\"bolt-v3-frame\","
         "\"profile-route-rules\"]}");
    return 0;
}

static void usage(FILE *stream)
{
    fprintf(stream,
            "Usage: biubiu-accctl [OPTIONS] COMMAND [ARGS]\n"
            "Options:\n"
            "  --device-id UUID       Override the persistent device identity\n"
            "  --device-id-file PATH  Device identity file (default: %s)\n"
            "  --session-file PATH    Private session file (default: %s)\n"
            "  --acc-key-file PATH    Acceleration public-key cache (default: %s)\n"
            "Commands:\n"
            "  qr-start          Create a QR login challenge\n"
            "  qr-poll TOKEN     Query a QR login challenge\n"
            "  login-code CODE   Exchange an authorized QR code for a session\n"
            "  sms-send PHONE [AREA_CODE]\n"
            "                    Send a login code (default area code: 86)\n"
            "  sms-login PHONE CODE [AREA_CODE]\n"
            "                    Exchange a login code for an account session\n"
            "  sms-login-stdin PHONE [AREA_CODE]\n"
            "                    Read a login code from stdin and store a session\n"
            "  password-login LOGIN_NAME [AREA_CODE]\n"
            "                    Prompt privately for a password and log in\n"
            "  session-status    Print redacted local session state\n"
            "  session-refresh   Refresh and atomically replace the session\n"
            "  session-clear     Safely remove the local account session\n"
            "  acc-key-import PATH\n"
            "                    Import VERSION|BASE64_DER from a private file\n"
            "  acc-key-fetch     Install the official native seed public key\n"
            "  acc-key-status    Print version and fingerprint, never key data\n"
            "  game-list [PAGE SIZE]\n"
            "                    Fetch and store the provider game catalog\n"
            "  game-search KEYWORD [PAGE SIZE]\n"
            "                    Search and store provider game results\n"
            "  service-config-fetch\n"
            "                    Refresh the provider service configuration\n"
            "  pc-game-list      Fetch and store the full game catalog\n"
            "  pc-game-search KEYWORD [PAGE SIZE]\n"
            "                    Search the full accelerator game catalog\n"
            "  pc-game-profile GAME_ID\n"
            "                    Fetch launch-platform metadata\n"
            "  pc-game-map GAME_ID\n"
            "                    Fetch exact area, platform, and mode metadata\n"
            "  pc-user-sync     Resolve the accelerator account identity\n"
            "  pc-context-start GAME_ID AREA_ID PLATFORM_ID [ACC_MODE]\n"
            "                    Begin one reusable acceleration session\n"
            "  pc-check-speedup GAME_ID AREA_ID [POLLING LAST_JITTER_TIME]\n"
            "                    Check account entitlement for a game\n"
            "  pc-profile-fetch GAME_ID AREA_ID\n"
            "                    Fetch and store a speedup profile\n"
            "  check-speedup GAME_ID AREA_ID [POLLING]\n"
            "                    Check account entitlement for a game\n"
            "  profile-fetch GAME_ID AREA_ID PLATFORM_ID\n"
            "                    Fetch and store an authorized speedup profile\n"
            "  signal-login GAME_ID AREA_ID PLATFORM_ID\n"
            "                    Exchange a profile for channel authorization\n"
            "  pc-signal-login GAME_ID AREA_ID PLATFORM_ID\n"
            "                    Exchange a profile for all channel authorizations\n"
            "  channel-renew GAME_ID AREA_ID PLATFORM_ID\n"
            "                    Renew stored channel tickets\n"
            "  pc-channel-renew GAME_ID AREA_ID PLATFORM_ID\n"
            "                    Renew stored channel tickets\n"
            "  runtime-prepare   Materialize an authorized TCP/UDP runtime\n"
            "  pc-runtime-prepare\n"
            "                    Materialize the multi-outbound runtime\n"
            "  self-test         Verify ciphers and private session storage\n"
            "  version           Print the program version\n",
            DEFAULT_DEVICE_ID_FILE, DEFAULT_SESSION_FILE,
            DEFAULT_ACCELERATION_KEY_FILE);
}

int main(int argc, char **argv)
{
    const char *explicit_device_id = NULL;
    const char *device_id_file = DEFAULT_DEVICE_ID_FILE;
    const char *session_file = DEFAULT_SESSION_FILE;
    const char *acceleration_key_file = DEFAULT_ACCELERATION_KEY_FILE;
    const char *store_method = NULL;
    char *device_id = NULL;
    const char *command;
    char **arguments;
    int argument_count;
    json_object *payload = NULL;
    json_object *result = NULL;
    bool curl_initialized = false;
    int index = 1;
    int status = 1;

    while (index < argc) {
        const char *option = argv[index];

        if (strcmp(option, "--device-id") == 0 && index + 1 < argc) {
            explicit_device_id = argv[index + 1];
            index += 2;
        } else if (strcmp(option, "--device-id-file") == 0 && index + 1 < argc) {
            device_id_file = argv[index + 1];
            index += 2;
        } else if (strcmp(option, "--session-file") == 0 && index + 1 < argc) {
            session_file = argv[index + 1];
            index += 2;
        } else if (strcmp(option, "--acc-key-file") == 0 && index + 1 < argc) {
            acceleration_key_file = argv[index + 1];
            index += 2;
        } else {
            break;
        }
    }
    if (index >= argc) {
        usage(stderr);
        return 2;
    }
    command = argv[index++];
    arguments = &argv[index];
    argument_count = argc - index;
    if (strcmp(command, "version") == 0 && argument_count == 0) {
        printf("biubiu-accctl %s\n", BIUBIU_ACC_VERSION);
        return 0;
    }
    if (strcmp(command, "self-test") == 0 && argument_count == 0)
        return run_self_test();
    if (strcmp(command, "session-status") == 0 && argument_count == 0)
        return print_session_status(session_file);
    if (strcmp(command, "session-clear") == 0 && argument_count == 0)
        return clear_session_record(session_file);
    if (strcmp(command, "acc-key-status") == 0 && argument_count == 0)
        return print_acceleration_key_status(acceleration_key_file);
    if (strcmp(command, "acc-key-import") == 0 && argument_count == 1)
        return import_acceleration_key(arguments[0], acceleration_key_file);
    if (strcmp(command, "acc-key-status") == 0 ||
        strcmp(command, "acc-key-import") == 0 ||
        (strcmp(command, "acc-key-fetch") == 0 && argument_count != 0)) {
        usage(stderr);
        return 2;
    }
    if (strcmp(command, "runtime-prepare") == 0) {
        return run_runtime_prepare(argument_count, arguments);
    }
    if (strcmp(command, "pc-runtime-prepare") == 0)
        return run_pc_runtime_prepare(argument_count, arguments);
    if (strcmp(command, "pc-context-start") == 0)
        return run_pc_context_start(argument_count, arguments);
    if (strcmp(command, "acc-key-fetch") == 0 && argument_count == 0)
        return fetch_acceleration_key(acceleration_key_file);

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        fprintf(stderr, "unable to initialize HTTP client\n");
        goto out;
    }
    curl_initialized = true;
    if (strcmp(command, "session-refresh") == 0 && argument_count == 0) {
        status = refresh_stored_session(session_file);
        goto out;
    }
    if (resolve_device_identity(explicit_device_id, device_id_file, &device_id) != 0) {
        fprintf(stderr, "unable to load or create the private device identity: %s\n",
                strerror(errno));
        goto out;
    }
    if (strcmp(command, "game-list") == 0) {
        status = run_game_list(session_file, acceleration_key_file, argument_count,
                               arguments);
        goto out;
    }
    if (strcmp(command, "game-search") == 0) {
        status = run_game_search(session_file, acceleration_key_file, argument_count,
                                 arguments);
        goto out;
    }
    if (strcmp(command, "service-config-fetch") == 0) {
        status = run_service_config_fetch(session_file, acceleration_key_file,
                                          argument_count);
        goto out;
    }
    if (strcmp(command, "pc-game-list") == 0) {
        status = run_pc_game_list(session_file, acceleration_key_file,
                                  argument_count);
        goto out;
    }
    if (strcmp(command, "pc-game-search") == 0) {
        status = run_pc_game_search(session_file, acceleration_key_file,
                                    argument_count, arguments);
        goto out;
    }
    if (strcmp(command, "pc-game-profile") == 0) {
        status = run_pc_game_profile(session_file, acceleration_key_file,
                                     argument_count, arguments);
        goto out;
    }
    if (strcmp(command, "pc-game-map") == 0) {
        status = run_pc_game_map(session_file, acceleration_key_file,
                                 argument_count, arguments);
        goto out;
    }
    if (strcmp(command, "pc-user-sync") == 0) {
        status = run_pc_user_sync(session_file, acceleration_key_file,
                                  argument_count);
        goto out;
    }
    if (strcmp(command, "pc-check-speedup") == 0) {
        status = run_pc_check_speedup(session_file, acceleration_key_file,
                                      argument_count, arguments);
        goto out;
    }
    if (strcmp(command, "pc-profile-fetch") == 0) {
        status = run_pc_profile_request(session_file, acceleration_key_file,
                                        argument_count, arguments);
        goto out;
    }
    if (strcmp(command, "check-speedup") == 0) {
        status = run_check_speedup(session_file, acceleration_key_file,
                                   argument_count, arguments);
        goto out;
    }
    if (strcmp(command, "profile-fetch") == 0) {
        status = run_profile_request(session_file, acceleration_key_file,
                                     argument_count, arguments);
        goto out;
    }
    if (strcmp(command, "signal-login") == 0) {
        status = run_signal_login(session_file, acceleration_key_file,
                                  argument_count, arguments);
        goto out;
    }
    if (strcmp(command, "pc-signal-login") == 0) {
        status = run_pc_signal_login(session_file, acceleration_key_file,
                                     argument_count, arguments);
        goto out;
    }
    if (strcmp(command, "channel-renew") == 0) {
        status = run_channel_renew(session_file, acceleration_key_file,
                                   argument_count, arguments);
        goto out;
    }
    if (strcmp(command, "pc-channel-renew") == 0) {
        status = run_pc_channel_renew(session_file, acceleration_key_file,
                                      argument_count, arguments);
        goto out;
    }
    payload = build_client_context(device_id);
    if (!payload) {
        fprintf(stderr, "unable to allocate request payload\n");
        goto out;
    }

    if (strcmp(command, "qr-start") == 0 && argument_count == 0) {
        json_object_object_add(payload, "qrCodeScene", json_object_new_int(1));
        json_object_object_add(payload, "loginAuthUrl",
                               json_object_new_string("pages/Home/index"));
        status = api_request("capi/qrcodelogin.startQRCodeLogin", payload, &result);
    } else if (strcmp(command, "qr-poll") == 0 && argument_count == 1) {
        json_object_object_add(payload, "qrToken",
                               json_object_new_string(arguments[0]));
        status = api_request("capi/qrcodelogin.queryLoginStatus", payload, &result);
    } else if (strcmp(command, "login-code") == 0 && argument_count == 1) {
        json_object_object_add(payload, "connectCode",
                               json_object_new_string(arguments[0]));
        status = api_request("capi/login.autoLoginByCode", payload, &result);
        store_method = "qr";
    } else if (strcmp(command, "sms-send") == 0 &&
               (argument_count == 1 || argument_count == 2)) {
        const char *area_code = argument_count == 2 ? arguments[1] : "86";

        if (!decimal_string(arguments[0], 4, 20) ||
            !decimal_string(area_code, 1, 4)) {
            fprintf(stderr, "PHONE and AREA_CODE must contain digits only\n");
            status = 2;
            goto out;
        }
        json_object_object_add(payload, "mobile",
                               json_object_new_string(arguments[0]));
        json_object_object_add(payload, "areaCode",
                               json_object_new_string(area_code));
        status = api_request("capi/login.sendSmsCode", payload, &result);
    } else if (strcmp(command, "sms-login") == 0 &&
               (argument_count == 2 || argument_count == 3)) {
        const char *area_code = argument_count == 3 ? arguments[2] : "86";

        if (!decimal_string(arguments[0], 4, 20) ||
            !decimal_string(arguments[1], 4, 10) ||
            !decimal_string(area_code, 1, 4)) {
            fprintf(stderr, "PHONE, CODE, and AREA_CODE must contain digits only\n");
            status = 2;
            goto out;
        }
        json_object_object_add(payload, "mobile",
                               json_object_new_string(arguments[0]));
        json_object_object_add(payload, "smsCode",
                               json_object_new_string(arguments[1]));
        json_object_object_add(payload, "areaCode",
                               json_object_new_string(area_code));
        status = api_request("capi/login.loginWithSmsCode", payload, &result);
        store_method = "sms";
    } else if (strcmp(command, "sms-login-stdin") == 0 &&
               (argument_count == 1 || argument_count == 2)) {
        const char *area_code = argument_count == 2 ? arguments[1] : "86";
        char sms_code[32] = {0};

        if (!decimal_string(arguments[0], 4, 20) ||
            !decimal_string(area_code, 1, 4)) {
            fprintf(stderr, "PHONE and AREA_CODE must contain digits only\n");
            status = 2;
            goto out;
        }
        if (read_secret_line(sms_code, sizeof(sms_code)) != 0 ||
            !decimal_string(sms_code, 4, 10)) {
            OPENSSL_cleanse(sms_code, sizeof(sms_code));
            fprintf(stderr, "CODE must contain between 4 and 10 digits\n");
            status = 2;
            goto out;
        }
        json_object_object_add(payload, "mobile",
                               json_object_new_string(arguments[0]));
        json_object_object_add(payload, "smsCode",
                               json_object_new_string(sms_code));
        json_object_object_add(payload, "areaCode",
                               json_object_new_string(area_code));
        OPENSSL_cleanse(sms_code, sizeof(sms_code));
        status = api_request("capi/login.loginWithSmsCode", payload, &result);
        store_method = "sms";
    } else if (strcmp(command, "password-login") == 0 &&
               (argument_count == 1 || argument_count == 2)) {
        const char *area_code = argument_count == 2 ? arguments[1] : "86";
        char *password;
        size_t password_len;

        if (!arguments[0][0] || strlen(arguments[0]) > 128 ||
            !decimal_string(area_code, 1, 4)) {
            fprintf(stderr, "invalid LOGIN_NAME or AREA_CODE\n");
            status = 2;
            goto out;
        }
        password = getpass("biubiu password: ");
        if (!password || !(password_len = strlen(password)) || password_len > 256) {
            fprintf(stderr, "password must contain between 1 and 256 bytes\n");
            status = 2;
            goto out;
        }
        json_object_object_add(payload, "loginName",
                               json_object_new_string(arguments[0]));
        json_object_object_add(payload, "password", json_object_new_string(password));
        json_object_object_add(payload, "areaCode",
                               json_object_new_string(area_code));
        OPENSSL_cleanse(password, password_len);
        status = api_request("capi/login.loginByPassword", payload, &result);
        store_method = "password";
    } else {
        usage(stderr);
        status = 2;
        goto out;
    }
    if (status == 0 && result) {
        if (!response_is_success(result)) {
            puts(json_object_to_json_string_ext(result, JSON_C_TO_STRING_PRETTY));
            status = 3;
        } else if (store_method) {
            if (store_session_record(session_file, device_id, store_method, result) != 0) {
                fputs("login succeeded but the private session could not be stored\n",
                      stderr);
                status = 1;
            } else {
                print_login_summary(store_method);
            }
        } else {
            puts(json_object_to_json_string_ext(result, JSON_C_TO_STRING_PRETTY));
        }
    }

out:
    cleanse_json_value(result);
    json_object_put(result);
    cleanse_json_string(payload, "password");
    cleanse_json_string(payload, "smsCode");
    cleanse_json_value(payload);
    json_object_put(payload);
    free(device_id);
    if (curl_initialized)
        curl_global_cleanup();
    return status;
}
