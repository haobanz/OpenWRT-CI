/* Exercise the production PC profile-to-runtime materializer without a network. */
#define main biubiu_accctl_program_main
#include "../../vendor/game-accelerators/biubiu-acc/src/biubiu-accctl.c"
#undef main

static int test_write_private(const char *path, const char *contents)
{
    size_t length = strlen(contents);
    size_t offset = 0;
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);

    if (fd < 0)
        return -1;
    while (offset < length) {
        ssize_t count = write(fd, contents + offset, length - offset);

        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0) {
            close(fd);
            return -1;
        }
        offset += (size_t)count;
    }
    return close(fd);
}

static json_object *test_array_entry(json_object *array, const char *id,
                                     const char *type)
{
    size_t index;

    if (!array)
        return NULL;
    for (index = 0; index < json_object_array_length(array); index++) {
        json_object *entry = json_object_array_get_idx(array, index);
        const char *entry_id = string_member(entry, "id");
        const char *entry_type = string_member(entry, "type");

        if (entry_id && entry_type && !strcmp(entry_id, id) &&
            !strcmp(entry_type, type))
            return entry;
    }
    return NULL;
}

static int test_integer_member(json_object *object, const char *name,
                               int64_t *result)
{
    json_object *value = object_member(object, name, json_type_int);

    if (!value || !result)
        return -1;
    *result = json_object_get_int64(value);
    return 0;
}

static bool test_string_equals(json_object *object, const char *name,
                               const char *expected)
{
    const char *value = string_member(object, name);

    return value && !strcmp(value, expected);
}

int main(void)
{
    static const char *const platform_names[] = {
        "all", "undefine", "android", "ios", "simulator", "pcweb",
        "pc", "switch", "ps", "xbox", "steamdeck",
    };
    static const char profile_json[] =
        "{\"data\":{\"outboundProfile\":{\"outboundConfigList\":["
        "{\"id\":\"main\",\"type\":\"bolt\",\"config\":{\"dataChannelList\":["
        "{\"proType\":\"TCP\",\"ip\":\"127.0.0.1\",\"port\":\"2201\",\"encryption\":true,\"bip\":\"127.0.0.11\"},"
        "{\"proType\":\"UDP\",\"ip\":\"127.0.0.2\",\"port\":\"2202\",\"encryption\":false}]}},"
        "{\"id\":\"main\",\"type\":\"spare\",\"config\":{\"dataChannelList\":["
        "{\"proType\":\"TCP\",\"ip\":\"127.0.0.3\",\"port\":\"2203\",\"encryption\":false},"
        "{\"proType\":\"UDP\",\"ip\":\"127.0.0.4\",\"port\":\"2204\",\"encryption\":false}]}},"
        "{\"id\":\"bypath\",\"type\":\"bypath\",\"config\":{\"dataChannelList\":["
        "{\"proType\":\"TCP\",\"ip\":\"127.0.0.5\",\"port\":\"2205\",\"encryption\":false},"
        "{\"proType\":\"UDP\",\"ip\":\"127.0.0.6\",\"port\":\"2206\",\"encryption\":false}]}}]},"
        "\"routerProfile\":{\"defaultOutboundId\":\"main\",\"routeList\":["
        "{\"id\":1,\"mode\":\"bolt\",\"outboundId\":\"main\",\"cidrIp\":\"203.0.113.7/24\",\"protocol\":3,\"port\":27015},"
        "{\"id\":2,\"mode\":\"bolt\",\"outboundId\":\"main\",\"bypathId\":\"bypath\",\"cidrList\":[\"198.51.100.0/24\"],\"protocol\":2,\"port\":0,\"portList\":[\"27000~27100\",0,\"27000-27100\"]},"
        "{\"id\":3,\"mode\":\"bolt\",\"outboundId\":\"main\",\"cidrIp\":\"192.0.2.1\",\"protocol\":null,\"port\":0}]}}}";
    static const char authorization_json[] =
        "{\"data\":{\"signalSessionId\":\"test-signal-session\",\"channelAuthList\":["
        "{\"channelIp\":\"127.0.0.1\",\"port\":2201,\"dataChannelSessionId\":101,\"channelSt\":\"ticket-main-tcp\",\"secretType\":\"plain\",\"type\":7,\"expireTime\":2000000000,\"proType\":\"TCP\"},"
        "{\"channelIp\":\"127.0.0.2\",\"port\":2202,\"dataChannelSessionId\":102,\"channelSt\":\"ticket-main-udp\",\"secretType\":\"plain\",\"type\":7,\"expireTime\":2000000000,\"proType\":\"UDP\"},"
        "{\"channelIp\":\"127.0.0.3\",\"port\":2203,\"dataChannelSessionId\":103,\"channelSt\":\"ticket-spare-tcp\",\"secretType\":\"plain\",\"type\":7,\"expireTime\":2000000000,\"proType\":\"TCP\"},"
        "{\"channelIp\":\"127.0.0.4\",\"port\":2204,\"dataChannelSessionId\":104,\"channelSt\":\"ticket-spare-udp\",\"secretType\":\"plain\",\"type\":7,\"expireTime\":2000000000,\"proType\":\"UDP\"},"
        "{\"channelIp\":\"127.0.0.5\",\"port\":2205,\"dataChannelSessionId\":105,\"channelSt\":\"ticket-bypath-tcp\",\"secretType\":\"plain\",\"type\":7,\"expireTime\":2000000000,\"proType\":\"TCP\"},"
        "{\"channelIp\":\"127.0.0.6\",\"port\":2206,\"dataChannelSessionId\":106,\"channelSt\":\"ticket-bypath-udp\",\"secretType\":\"plain\",\"type\":7,\"expireTime\":2000000000,\"proType\":\"UDP\"}]}}";
    char profile_path[128];
    char authorization_path[128];
    char renewal_path[128];
    char runtime_path[128];
    json_object *profile_document = NULL;
    json_object *profile_channels = NULL;
    json_object *runtime = NULL;
    json_object *nested_authorization = NULL;
    json_object *outbounds;
    json_object *routes;
    json_object *first_route;
    json_object *second_route;
    json_object *third_route;
    json_object *main_outbound;
    json_object *main_channels;
    struct stat info;
    int64_t schema;
    int64_t protocol;
    int status = 1;
    size_t index;

    for (index = 0; index < sizeof(platform_names) / sizeof(platform_names[0]);
         index++) {
        if (strcmp(pc_platform_name(index), platform_names[index]) != 0)
            goto out;
    }
    if (pc_platform_name(sizeof(platform_names) / sizeof(platform_names[0])))
        goto out;

    snprintf(profile_path, sizeof(profile_path),
             "/tmp/biubiu-accctl-runtime-%ld-profile.json", (long)getpid());
    snprintf(authorization_path, sizeof(authorization_path),
             "/tmp/biubiu-accctl-runtime-%ld-authorization.json", (long)getpid());
    snprintf(renewal_path, sizeof(renewal_path),
             "/tmp/biubiu-accctl-runtime-%ld-renewal.json", (long)getpid());
    snprintf(runtime_path, sizeof(runtime_path),
             "/tmp/biubiu-accctl-runtime-%ld-output.json", (long)getpid());
    unlink(renewal_path);
    unlink(runtime_path);
    profile_document = json_tokener_parse(profile_json);
    profile_channels = profile_document ? profile_acceleration_channel_entries(
                                              control_data_object(profile_document)) :
                                          NULL;
    if (!profile_channels || json_object_array_length(profile_channels) != 6 ||
        !test_string_equals(json_object_array_get_idx(profile_channels, 0),
                            "outboundId", "bypath") ||
        test_write_private(profile_path, profile_json) != 0 ||
        test_write_private(authorization_path, authorization_json) != 0 ||
        run_runtime_prepare_mode(0, NULL, profile_path, authorization_path,
                                 renewal_path, runtime_path,
                                 true,
                                 "pc-runtime-test") != 0 ||
        load_private_json(runtime_path, &runtime) != 0 ||
        test_integer_member(runtime, "schemaVersion", &schema) != 0 || schema != 2 ||
        !test_string_equals(runtime, "defaultOutboundId", "main") ||
        !(outbounds = object_member(runtime, "outbounds", json_type_array)) ||
        json_object_array_length(outbounds) != 3 ||
        !test_array_entry(outbounds, "main", "spare") ||
        !test_array_entry(outbounds, "bypath", "bypath") ||
        !(main_outbound = test_array_entry(outbounds, "main", "bolt")) ||
        !(main_channels = object_member(main_outbound, "channels", json_type_array)) ||
        json_object_array_length(main_channels) != 2 ||
        !(routes = object_member(runtime, "routes", json_type_array)) ||
        json_object_array_length(routes) != 3)
        goto out;
    first_route = json_object_array_get_idx(routes, 0);
    second_route = json_object_array_get_idx(routes, 1);
    third_route = json_object_array_get_idx(routes, 2);
    if (test_integer_member(first_route, "protocol", &protocol) != 0 || protocol != 6 ||
        !test_string_equals(second_route, "outboundId", "bypath") ||
        test_integer_member(second_route, "protocol", &protocol) != 0 || protocol != 17 ||
        test_integer_member(third_route, "protocol", &protocol) != 0 || protocol != 0 ||
        stat(runtime_path, &info) != 0 || (info.st_mode & (S_IRWXG | S_IRWXO)))
        goto out;
    {
        json_object *port_list = object_member(second_route, "ports", json_type_array);
        json_object *rules = object_member(runtime, "udpRules", json_type_array);
        bool found = false;
        size_t index;

        if (!port_list || json_object_array_length(port_list) != 1 ||
            strcmp(json_object_get_string(json_object_array_get_idx(port_list, 0)),
                   "27000-27100") || !rules)
            goto out;
        for (index = 0; index < json_object_array_length(rules); index++) {
            const char *rule = json_object_get_string(json_object_array_get_idx(rules, index));

            if (!strcmp(rule, "198.51.100.0/24|27000-27100"))
                found = true;
            if (strstr(rule, "*,") || strstr(rule, ",*"))
                goto out;
        }
        if (!found)
            goto out;
    }
    nested_authorization = json_tokener_parse(authorization_json);
    {
        json_object *data = object_member(nested_authorization, "data", json_type_object);
        json_object *wrapped = json_object_new_object();
        json_object *channels = object_member(data, "channelAuthList", json_type_array);

        if (!data || !wrapped || !channels)
            goto out;
        for (index = 0; index < json_object_array_length(channels); index++)
            json_object_object_add(json_object_array_get_idx(channels, index),
                                   "expireTime", json_object_new_int(7200));
        json_object_object_add(wrapped, "data", json_object_get(data));
        json_object_object_add(nested_authorization, "data", wrapped);
        json_object_object_add(nested_authorization, "receivedAt",
                               json_object_new_int64(1788600000));
    }
    if (store_private_json(authorization_path, nested_authorization) != 0)
        goto out;
    for (index = 0; index < 2; index++) {
        json_object_put(runtime);
        runtime = NULL;
        if (run_runtime_prepare_mode(0, NULL, profile_path, authorization_path,
                                     renewal_path, runtime_path,
                                     true,
                                     "pc-runtime-test") != 0 ||
            load_private_json(runtime_path, &runtime) != 0)
            goto out;
        outbounds = object_member(runtime, "outbounds", json_type_array);
        main_outbound = test_array_entry(outbounds, "main", "bolt");
        main_channels = object_member(main_outbound, "channels", json_type_array);
        if (!main_channels ||
            test_integer_member(json_object_array_get_idx(main_channels, 0),
                                "expiresAt", &protocol) != 0 ||
            protocol != 1788607200)
            goto out;
    }
    status = 0;

out:
    cleanse_json_value(nested_authorization);
    json_object_put(nested_authorization);
    json_object_put(profile_channels);
    json_object_put(profile_document);
    cleanse_json_value(runtime);
    json_object_put(runtime);
    unlink(runtime_path);
    unlink(renewal_path);
    unlink(authorization_path);
    unlink(profile_path);
    if (status != 0) {
        fputs("biubiu-accctl PC runtime materialization test failed\n", stderr);
        return 1;
    }
    puts("{\"success\":true,\"tests\":[\"native-platform-map\",\"pc-runtime-schema2\",\"native-map-channel-order\",\"multi-outbound\",\"spare-grouping\",\"route-protocol-normalization\",\"private-runtime-file\",\"nested-adat-authorization\",\"fixed-relative-expiry\"]}");
    return 0;
}
