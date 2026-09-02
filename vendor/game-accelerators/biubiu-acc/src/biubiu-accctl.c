#define _GNU_SOURCE

#include <curl/curl.h>
#include <json-c/json.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BIUBIU_ACC_VERSION "0.2.0"
#define LOGIN_ORIGIN "https://member-login.biubiu001.com/"
#define MAX_RESPONSE_SIZE (4U * 1024U * 1024U)

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
    if (root)
        json_object_put(root);
    else {
        json_object_put(device);
        json_object_put(user);
        json_object_put(scene);
    }
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

static int run_self_test(void)
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
            fprintf(stderr, "self-test failed at plaintext length %zu\n", length);
            return 1;
        }
        bytes_free(&ciphertext);
        bytes_free(&plaintext);
    }
    puts("{\"success\":true,\"test\":\"adat-aes-roundtrip\"}");
    return 0;
}

static void usage(FILE *stream)
{
    fprintf(stream,
            "Usage: biubiu-accctl [--device-id UUID] COMMAND [ARGS]\n"
            "Commands:\n"
            "  qr-start          Create a QR login challenge\n"
            "  qr-poll TOKEN     Query a QR login challenge\n"
            "  login-code CODE   Exchange an authorized QR code for a session\n"
            "  sms-send PHONE [AREA_CODE]\n"
            "                    Send a login code (default area code: 86)\n"
            "  sms-login PHONE CODE [AREA_CODE]\n"
            "                    Exchange a login code for an account session\n"
            "  password-login LOGIN_NAME [AREA_CODE]\n"
            "                    Prompt privately for a password and log in\n"
            "  self-test         Verify the local ADAT cipher implementation\n"
            "  version           Print the program version\n");
}

int main(int argc, char **argv)
{
    const char *device_id = NULL;
    const char *command;
    char **arguments;
    int argument_count;
    char *generated_device_id = NULL;
    json_object *payload = NULL;
    json_object *result = NULL;
    bool curl_initialized = false;
    int index = 1;
    int status = 1;

    if (argc > 2 && strcmp(argv[index], "--device-id") == 0) {
        device_id = argv[index + 1];
        index += 2;
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

    if (!device_id) {
        generated_device_id = new_uuid();
        device_id = generated_device_id;
    }
    if (!device_id) {
        fprintf(stderr, "unable to generate device ID\n");
        goto out;
    }
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        fprintf(stderr, "unable to initialize HTTP client\n");
        goto out;
    }
    curl_initialized = true;
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
    } else {
        usage(stderr);
        status = 2;
        goto out;
    }
    if (status == 0 && result) {
        puts(json_object_to_json_string_ext(result, JSON_C_TO_STRING_PRETTY));
        if (!response_is_success(result))
            status = 3;
    }

out:
    if (result)
        json_object_put(result);
    if (payload) {
        cleanse_json_string(payload, "password");
        json_object_put(payload);
    }
    free(generated_device_id);
    if (curl_initialized)
        curl_global_cleanup();
    return status;
}
