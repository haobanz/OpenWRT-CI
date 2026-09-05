/* Exercise the production native PC start-context contract without a network. */
#define main biubiu_accctl_program_main
#include "../../vendor/game-accelerators/biubiu-acc/src/biubiu-accctl.c"
#undef main

static bool mode_list_equals(json_object *modes, int first, int second)
{
    json_object *left;
    json_object *right;

    if (!modes || json_object_array_length(modes) != 2)
        return false;
    left = json_object_array_get_idx(modes, 0);
    right = json_object_array_get_idx(modes, 1);
    return left && right && json_object_is_type(left, json_type_int) &&
           json_object_is_type(right, json_type_int) &&
           json_object_get_int(left) == first &&
           json_object_get_int(right) == second;
}

static int test_platform_names(void)
{
    static const char *const expected[] = {
        "all", "undefine", "android", "ios", "simulator", "pcweb",
        "pc", "switch", "ps", "xbox", "steamdeck",
    };
    size_t index;

    for (index = 0; index < sizeof(expected) / sizeof(expected[0]); index++) {
        const char *actual = pc_platform_name(index);

        if (!actual || strcmp(actual, expected[index]))
            return -1;
    }
    return pc_platform_name(11) == NULL ? 0 : -1;
}

static int test_httpdns_contract(void)
{
    static const char response_json[] =
        "{\"host\":\"signal-sp.biubiu001.com\",\"ips\":["
        "\"59.110.166.118\",\"invalid\",\"47.93.39.45\","
        "\"59.110.166.118\"],\"ttl\":60}";
    json_object *response = json_tokener_parse(response_json);
    struct pc_httpdns_ipv4_list addresses = {0};
    char signature[33] = {0};
    char *resolve_entry = NULL;
    int status = -1;

    if (!response ||
        pc_httpdns_signature(PC_SIGNAL_HOST, 1777777777,
                             signature) != 0 ||
        strcmp(signature, "0ea7b6920b77351e93977fa4105b8de0") != 0 ||
        pc_httpdns_signature("example.invalid", 1777777777,
                             signature) == 0 ||
        pc_httpdns_parse_resolution(response, PC_SIGNAL_HOST, &addresses) != 0 ||
        addresses.count != 2 ||
        strcmp(addresses.values[0], "59.110.166.118") != 0 ||
        strcmp(addresses.values[1], "47.93.39.45") != 0)
        goto out;
    resolve_entry = pc_httpdns_curl_resolve_entry(&addresses);
    if (!resolve_entry ||
        strcmp(resolve_entry,
               "signal-sp.biubiu001.com:80:59.110.166.118,47.93.39.45") != 0)
        goto out;
    status = 0;

out:
    OPENSSL_cleanse(signature, sizeof(signature));
    free(resolve_entry);
    json_object_put(response);
    return status;
}

static int test_native_control_api_routes(void)
{
    static const char expected_login[] =
        PC_ACCELERATION_ORIGIN SIGNAL_LOGIN_ENDPOINT;
    static const char expected_ticket[] =
        PC_ACCELERATION_ORIGIN CHANNEL_TICKET_ENDPOINT;
    static const char expected_profile[] =
        PC_ACCELERATION_ORIGIN PC_SPEEDUP_CONFIG_ENDPOINT "&df=adat";
    static const char expected_mobile[] =
        ACCELERATION_ORIGIN SEARCH_GAME_ENDPOINT;
    const char *host = NULL;
    bool use_signal_httpdns = true;
    char *url = NULL;
    int status = -1;

    if (build_acceleration_api_url(ACCELERATION_CLIENT_WINDOWS,
                                   SIGNAL_LOGIN_ENDPOINT, &host,
                                   &use_signal_httpdns, &url) != 0 ||
        strcmp(host, PC_ACCELERATION_HOST) != 0 || use_signal_httpdns ||
        strcmp(url, expected_login) != 0)
        goto out;
    free(url);
    url = NULL;
    if (build_acceleration_api_url(ACCELERATION_CLIENT_WINDOWS,
                                   CHANNEL_TICKET_ENDPOINT, &host,
                                   &use_signal_httpdns, &url) != 0 ||
        strcmp(host, PC_ACCELERATION_HOST) != 0 || use_signal_httpdns ||
        strcmp(url, expected_ticket) != 0)
        goto out;
    free(url);
    url = NULL;
    if (build_acceleration_api_url(ACCELERATION_CLIENT_WINDOWS,
                                   PC_SPEEDUP_CONFIG_ENDPOINT, &host,
                                   &use_signal_httpdns, &url) != 0 ||
        strcmp(host, PC_ACCELERATION_HOST) != 0 || use_signal_httpdns ||
        strcmp(url, expected_profile) != 0)
        goto out;
    free(url);
    url = NULL;
    if (build_acceleration_api_url(ACCELERATION_CLIENT_MOBILE,
                                   SEARCH_GAME_ENDPOINT, &host,
                                   &use_signal_httpdns, &url) != 0 ||
        strcmp(host, ACCELERATION_HOST) != 0 || use_signal_httpdns ||
        strcmp(url, expected_mobile) != 0)
        goto out;
    status = 0;

out:
    free(url);
    return status;
}

static int test_public_game_catalog(void)
{
    static const char fixture[] =
        "{\"code\":200,\"secret\":\"do-not-return-root\",\"data\":{"
        "\"success\":true,\"hasNextPage\":true,\"ticket\":\"do-not-return-data\","
        "\"list\":[{\"gameInfo\":{\"gameId\":38780,\"gameName\":\"Counter-Strike 2\","
        "\"platformId\":6,\"token\":\"do-not-return-info\"},\"areaList\":["
        "{\"areaId\":146,\"name\":\"Auto\",\"ticket\":\"do-not-return-area\"},"
        "{\"areaId\":103,\"name\":\"HK\"},{\"areaId\":103,\"name\":\"Duplicate\"},"
        "{\"areaId\":0,\"name\":\"Invalid\"},{\"areaId\":123,\"name\":\"Bad\\nName\"}],"
        "\"speedupModelList\":[{\"speedupModelId\":3,\"speedupModelName\":\"Process\"},"
        "{\"speedupModelId\":5,\"speedupModelName\":\"Route\"},"
        "{\"speedupModelId\":8,\"speedupModelName\":\"Unknown\"}]},"
        "{\"gameInfo\":{\"gameId\":2,\"gameName\":\"Bad\\u0000Name\",\"platformId\":6}},"
        "{\"gameInfo\":{\"gameId\":3,\"gameName\":\"Bad platform\",\"platformId\":11}},"
        "{\"gameInfo\":{\"gameId\":\"4\",\"gameName\":\"Wrong ID type\",\"platformId\":6}}]}}";
    json_object *response = json_tokener_parse(fixture);
    json_object *catalog = NULL, *options = NULL, *game, *games;
    const char *serialized;
    int status = -1;

    if (!response || !(catalog = pc_catalog_summary(response, 0)) ||
        !(options = pc_catalog_summary(response, 38780)))
        goto out;
    serialized = json_object_to_json_string_ext(catalog, JSON_C_TO_STRING_PLAIN);
    if (strstr(serialized, "do-not-return") || strstr(serialized, "gameInfo") ||
        !json_object_get_boolean(object_member(catalog, "has_next_page", json_type_boolean)))
        goto out;
    games = object_member(catalog, "games", json_type_array);
    if (json_object_array_length(games) != 1)
        goto out;
    game = json_object_array_get_idx(games, 0);
    if (strcmp(string_member(game, "id"), "38780") ||
        strcmp(string_member(game, "platform_id"), "6") ||
        strcmp(string_member(game, "name"), "Counter-Strike 2"))
        goto out;
    serialized = json_object_to_json_string_ext(options, JSON_C_TO_STRING_PLAIN);
    game = object_member(options, "game", json_type_object);
    if (strstr(serialized, "do-not-return") ||
        json_object_array_length(object_member(game, "areas", json_type_array)) != 2 ||
        json_object_array_length(object_member(game, "modes", json_type_array)) != 2 ||
        pc_catalog_summary(response, 999) != NULL)
        goto out;
    json_object_object_add(response, "code", json_object_new_int(403));
    if (pc_catalog_summary(response, 0) != NULL)
        goto out;
    status = 0;
out:
    json_object_put(catalog);
    json_object_put(options);
    json_object_put(response);
    return status;
}

int main(void)
{
    static const char map_json[] =
        "{\"data\":{\"list\":[{\"gameInfo\":{\"gameId\":38780,"
        "\"platformId\":6},\"areaList\":[{\"areaId\":146},"
        "{\"areaId\":103}],\"speedupModelList\":["
        "{\"speedupModelId\":3},{\"speedupModelId\":5}]}]}}";
    static const char fallback_json[] =
        "{\"data\":{\"list\":[{\"gameInfo\":{\"gameId\":38780,"
        "\"platformId\":6},\"areaList\":[{\"areaId\":146}]}]}}";
    static const char context_json[] =
        "{\"gameId\":38780,\"gameArea\":146,\"serverId\":0,"
        "\"accMode\":5,\"accModeList\":[5,3],\"accPodId\":\"auto\","
        "\"gamePlatform\":\"pc\",\"gamePlatformId\":6}";
    json_object *map = json_tokener_parse(map_json);
    json_object *fallback = json_tokener_parse(fallback_json);
    json_object *context = json_tokener_parse(context_json);
    json_object *modes = NULL;
    uint64_t selected = 0;
    uint64_t profile_mode = 0;
    uint64_t polling = 99;
    int status = 1;

    if (!map || !fallback || !context || test_platform_names() != 0 ||
        test_httpdns_contract() != 0 || test_native_control_api_routes() != 0 ||
        test_public_game_catalog() != 0 ||
        parse_nonnegative_argument("0", INT32_MAX, &polling) != 0 ||
        polling != 0 || parse_unsigned_argument("0", INT32_MAX, &polling) == 0)
        goto out;
    if (pc_game_start_selection(map, 38780, 146, 6, false, 0, &modes,
                                &selected) != 0 ||
        selected != 3 || !mode_list_equals(modes, 3, 5))
        goto out;
    json_object_put(modes);
    modes = NULL;
    if (pc_game_start_selection(map, 38780, 146, 6, true, 5, &modes,
                                &selected) != 0 ||
        selected != 5 || !mode_list_equals(modes, 5, 3))
        goto out;
    json_object_put(modes);
    modes = NULL;
    if (pc_game_start_selection(map, 38780, 999, 6, false, 0, &modes,
                                &selected) == 0 ||
        pc_game_start_selection(map, 38780, 146, 1, false, 0, &modes,
                                &selected) == 0 ||
        pc_game_start_selection(map, 38780, 146, 6, true, 4, &modes,
                                &selected) == 0)
        goto out;
    if (pc_game_start_selection(fallback, 38780, 146, 6, false, 0,
                                &modes, &selected) != 0 ||
        selected != 3 || !mode_list_equals(modes, 3, 5))
        goto out;
    if (pc_profile_context_mode_value(context, 38780, 146, &profile_mode) != 0 ||
        profile_mode != 5 ||
        pc_profile_context_mode_value(context, 38780, 103, &profile_mode) == 0)
        goto out;
    json_object_object_add(context, "serverId", json_object_new_int(1));
    if (pc_profile_context_mode_value(context, 38780, 146, &profile_mode) == 0)
        goto out;
    status = 0;

out:
    json_object_put(modes);
    json_object_put(context);
    json_object_put(fallback);
    json_object_put(map);
    if (status != 0) {
        fputs("biubiu native PC context contract test failed\n", stderr);
        return 1;
    }
    puts("{\"success\":true,\"tests\":[\"official-platform-map\","
         "\"provider-area-check\",\"selected-first-mode-list\","
         "\"official-mode-fallback\",\"zero-polling\","
         "\"profile-context-binding\",\"official-httpdns-contract\","
         "\"native-control-api-route\",\"public-game-catalog\"]}");
    return 0;
}
