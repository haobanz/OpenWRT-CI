#define _GNU_SOURCE

#include <curl/curl.h>
#include <json-c/json.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define BIUBIU_ACC_VERSION "0.7.0"
#define LOGIN_ORIGIN "https://member-login.biubiu001.com/"
#define MAX_RESPONSE_SIZE (4U * 1024U * 1024U)
#define MAX_STATE_SIZE (1024U * 1024U)
#define DEFAULT_DEVICE_ID_FILE "/etc/biubiu-acc/device-id"
#define DEFAULT_SESSION_FILE "/etc/biubiu-acc/session.json"
#define DEFAULT_ACCELERATION_KEY_FILE "/etc/biubiu-acc/acceleration-key.json"

#define BOLT_PROTOCOL_VERSION 3U
#define BOLT_DATA_HEADER_LENGTH 11U
#define BOLT_COMMAND_DATA 0x11U
#define BOLT_COMMAND_CONNECT_REQUEST 0x22U
#define BOLT_COMMAND_CONNECT_RESPONSE 0x23U
#define BOLT_COMMAND_ASSOCIATE_REQUEST 0x24U
#define BOLT_COMMAND_ASSOCIATE_RESPONSE 0x25U
#define BOLT_COMMAND_ERROR 0x27U
#define BOLT_STATUS_SUCCESS 0x22U

static const char public_key_pem[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDCjYYIy9Are9QPRDOVug4e6Fdz\n"
    "8HK2HGyajKR4N8Wb/bB9gwXnieXqj4Mya0nLd6nBcBPN6qUJ0R7p5Cv6aPqQsc7\n"
    "pWfAxPr41GvcOlGixLtpLHLUH9m0093YEBhu4F7pKu0TZTQIPZINWUa1SLjQD/bc\n"
    "BlcaQyWbk6qJhSJFYkwIDAQAB\n"
    "-----END PUBLIC KEY-----\n";

struct bytes {
    unsigned char *data;
    size_t len;
};

struct response_buffer {
    char *data;
    size_t len;
    bool too_large;
};

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
    write_be16(output->data + 2, (uint16_t)total_length);
    output->data[4] = command;
    write_be32(output->data + 5, session_id);
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
    write_be16(output->data + 2, (uint16_t)total_length);
    output->data[4] = BOLT_COMMAND_DATA;
    write_be32(output->data + 5, session_id);
    write_be16(output->data + 9, connection_id);
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
    total_length = read_be16(frame + 2);
    if (header_length < minimum_header || header_length > total_length ||
        total_length != frame_length)
        return -1;
    response->flags = frame[0] >> 4;
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
    if (RAND_bytes(keys->key, sizeof(keys->key)) != 1 ||
        RAND_bytes(keys->iv, sizeof(keys->iv)) != 1) {
        OPENSSL_cleanse(keys, sizeof(*keys));
        return -1;
    }
    return 0;
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

    if (!payload || key_version < 1 || !public_key || !keys ||
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
    json_object_object_add(envelope, "k", json_object_new_string(encrypted_key_b64));
    json_object_object_add(envelope, "v", json_object_new_int(key_version));
    json_object_object_add(envelope, "d", json_object_new_string(ciphertext_b64));
    json_object_object_add(envelope, "i", json_object_new_string(encrypted_iv_b64));

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

static int decrypt_adat_response(json_object *outer,
                                 const struct adat_session_keys *keys,
                                 json_object **result)
{
    json_object *code = NULL;
    json_object *encoded_value = NULL;
    const char *encoded;
    struct bytes ciphertext = {0};
    struct bytes plaintext = {0};
    json_object *parsed = NULL;
    int status = -1;

    if (!outer || !keys || !result)
        return -1;
    *result = NULL;
    if (json_object_object_get_ex(outer, "c", &code) &&
        json_object_get_int(code) == 2)
        return 2;
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
    status = 0;

out:
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

static int print_session_status(const char *path)
{
    json_object *record = NULL;
    json_object *output = NULL;
    const char *method = "none";
    const char *session_id = NULL;
    const char *refresh_token = NULL;
    size_t cookie_count = 0;
    bool stored = false;
    int status = 1;

    if (load_session_record(path, &record) == 0) {
        if (session_components(record, NULL, &method, &session_id, &refresh_token,
                               &cookie_count) != 0)
            goto out;
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
    json_object_object_add(output, "method", json_object_new_string(method));
    json_object_object_add(output, "deviceIdStored", json_object_new_boolean(stored));
    puts(json_object_to_json_string_ext(output, JSON_C_TO_STRING_PLAIN));
    status = 0;

out:
    json_object_put(output);
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
    const char *payload_text;
    int result = 1;

    rsa_key = new_test_rsa_key();
    payload = json_object_new_object();
    if (!rsa_key || !payload)
        goto out;
    json_object_object_add(payload, "id", json_object_new_string("offline-test"));
    json_object_object_add(payload, "value", json_object_new_int(7));
    envelope = build_adat_envelope(payload, 3, rsa_key, &keys);
    if (!envelope)
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
    if (decrypt_adat_response(outer, &keys, &decrypted) != 2 || decrypted)
        goto out;
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
    json_object *loaded = NULL;
    json_object *unsafe = NULL;
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
    if (!response || !data || !session_info || !cookies)
        goto out;
    json_object_object_add(response, "code", json_object_new_string("SUCCESS"));
    json_object_object_add(session_info, "sessionId",
                           json_object_new_string("selftest-session"));
    json_object_object_add(session_info, "refreshToken",
                           json_object_new_string("selftest-refresh"));
    json_object_array_add(cookies, json_object_new_string("selftest-cookie"));
    json_object_object_add(session_info, "cookies", cookies);
    cookies = NULL;
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
        cookie_count != 1)
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
        0x03, 0x1c, 0x00, 0x1c, 0x22, 0x01, 0x02, 0x03, 0x04, 0x03,
        0x01, 0x06, 0xc0, 0x00, 0x02, 0x01, 0x01, 0xbb,
        0x06, 0x05, 0x61, 0x6c, 0x70, 0x68, 0x61, 0x05, 0x01, 0x01,
    };
    static const unsigned char marker[] = {0xde, 0xad, 0xbe, 0xef};
    static const unsigned char expected_data[] = {
        0x03, 0x0b, 0x00, 0x0f, 0x11, 0x01, 0x02, 0x03,
        0x04, 0x12, 0x34, 0xde, 0xad, 0xbe, 0xef,
    };
    static const unsigned char associate_response[] = {
        0x03, 0x15, 0x00, 0x18, 0x25, 0x01, 0x02, 0x03,
        0x04, 0x12, 0x34, 0x22, 0x01, 0x01, 0x06, 0xcb,
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

static int run_self_test(void)
{
    if (run_login_cipher_self_test() != 0 || run_adat_cipher_self_test() != 0 ||
        run_acceleration_key_store_self_test() != 0 ||
        run_session_storage_self_test() != 0 || run_bolt_v3_self_test() != 0)
        return 1;
    puts("{\"success\":true,\"tests\":[\"account-envelope\","
         "\"acceleration-adat\",\"acceleration-key-store\","
         "\"private-session-store\",\"bolt-v3-frame\"]}");
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
            "  acc-key-status    Print version and fingerprint, never key data\n"
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
        strcmp(command, "acc-key-import") == 0) {
        usage(stderr);
        return 2;
    }

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
