/* Exercise the production PC lighthouse detector against a local UDP echo. */
#define _GNU_SOURCE

#include <pthread.h>

#define main biubiu_accctl_program_main
#include "../../vendor/game-accelerators/biubiu-acc/src/biubiu-accctl.c"
#undef main

struct echo_fixture {
    int socket_fd;
    uint32_t tokens[16];
    size_t expected_count;
    size_t received_count;
    int status;
};

static void *run_echo_fixture(void *opaque)
{
    struct echo_fixture *fixture = opaque;

    while (fixture->received_count < fixture->expected_count) {
        struct pollfd descriptor = {
            .fd = fixture->socket_fd,
            .events = POLLIN,
        };
        struct sockaddr_in source;
        socklen_t source_length = sizeof(source);
        unsigned char packet[32];
        ssize_t received;

        if (poll(&descriptor, 1, 3000) <= 0)
            return NULL;
        received = recvfrom(fixture->socket_fd, packet, sizeof(packet), 0,
                            (struct sockaddr *)&source, &source_length);
        if (received != 4)
            return NULL;
        fixture->tokens[fixture->received_count++] = read_le32(packet);
        if (sendto(fixture->socket_fd, packet, (size_t)received, 0,
                   (struct sockaddr *)&source, source_length) != received)
            return NULL;
    }
    fixture->status = 0;
    return NULL;
}

static json_object *new_lighthouse(const char *id)
{
    json_object *entry = json_object_new_object();

    if (!entry)
        return NULL;
    json_object_object_add(entry, "id", json_object_new_string(id));
    json_object_object_add(entry, "ip", json_object_new_string("127.0.0.1"));
    return entry;
}

static int test_statistics(void)
{
    int two_samples[] = {20, 10};
    int ten_samples[] = {9, 0, 8, 1, 7, 2, 6, 3, 5, 4};
    int average;
    int loss;
    int p90;
    int count;
    int minimum;
    int maximum;

    pc_scout_statistics(NULL, 0, 9, 1000, &average, &loss, &p90, &count,
                        &minimum, &maximum);
    if (average != 1000 || loss != 9 || p90 != -1 || count != 9 ||
        minimum != -1 || maximum != -1)
        return -1;
    pc_scout_statistics(two_samples, 2, 9, 1000, &average, &loss, &p90,
                        &count, &minimum, &maximum);
    if (average != 15 || loss != 7 || p90 != 10 || count != 9 ||
        minimum != 10 || maximum != 20)
        return -1;
    pc_scout_statistics(ten_samples, 10, 10, 1000, &average, &loss, &p90,
                        &count, &minimum, &maximum);
    return average == 4 && loss == 0 && p90 == 8 && count == 10 &&
                   minimum == 0 && maximum == 9
               ? 0
               : -1;
}

static int test_config_defaults(void)
{
    struct pc_scout_config config;
    json_object *object = json_object_new_object();
    int status = -1;

    if (!object || load_pc_scout_config(object, &config) != 0)
        goto out;
    if (config.port != 14125 || config.detect_rounds != 10 ||
        config.loss_threshold_ms != 1000 || config.round_sleep_ms != 50 ||
        config.batch_count != 30 || config.batch_sleep_ms != 30 ||
        config.discard_head_rounds != 1)
        goto out;
    json_object_object_add(object, "proto", json_object_new_string("TCP"));
    if (load_pc_scout_config(object, &config) == 0)
        goto out;
    status = 0;

out:
    json_object_put(object);
    return status;
}

static int exact_result_shape(json_object *array, const char *id)
{
    json_object *entry;
    json_object *value;

    if (!array || json_object_array_length(array) != 1)
        return -1;
    entry = json_object_array_get_idx(array, 0);
    if (!entry || json_object_object_length(entry) != 7 ||
        !string_member(entry, "id") || strcmp(string_member(entry, "id"), id) ||
        !object_member(entry, "avgMs", json_type_int) ||
        !object_member(entry, "loss", json_type_int) ||
        !object_member(entry, "pt90Ms", json_type_int) ||
        !object_member(entry, "count", json_type_int) ||
        !object_member(entry, "minMs", json_type_int) ||
        !object_member(entry, "maxMs", json_type_int))
        return -1;
    value = object_member(entry, "loss", json_type_int);
    if (json_object_get_int(value) != 0)
        return -1;
    value = object_member(entry, "count", json_type_int);
    if (json_object_get_int(value) != 2)
        return -1;
    return 0;
}

int main(void)
{
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    struct echo_fixture fixture = {
        .socket_fd = -1,
        .expected_count = 6,
        .status = -1,
    };
    struct pc_scout_config config = {
        .detect_rounds = 3,
        .loss_threshold_ms = 50,
        .round_sleep_ms = 0,
        .batch_count = 30,
        .batch_sleep_ms = 0,
        .discard_head_rounds = 1,
    };
    json_object *lighthouses = NULL;
    json_object *transfer_lighthouses = NULL;
    json_object *detect_result = NULL;
    json_object *transfer_result = NULL;
    pthread_t thread;
    socklen_t address_length = sizeof(address);
    bool thread_started = false;
    int config_status;
    int statistics_status;
    int status = 1;
    size_t index;

    statistics_status = test_statistics();
    config_status = test_config_defaults();
    if (statistics_status != 0 || config_status != 0)
        goto out;
    fixture.socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fixture.socket_fd < 0 ||
        bind(fixture.socket_fd, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        getsockname(fixture.socket_fd, (struct sockaddr *)&address,
                    &address_length) != 0)
        goto out;
    config.port = ntohs(address.sin_port);
    lighthouses = json_object_new_array();
    transfer_lighthouses = json_object_new_array();
    if (!lighthouses || !transfer_lighthouses ||
        json_object_array_add(lighthouses, new_lighthouse("main")) != 0 ||
        json_object_array_add(transfer_lighthouses,
                              new_lighthouse("transfer")) != 0 ||
        pthread_create(&thread, NULL, run_echo_fixture, &fixture) != 0)
        goto out;
    thread_started = true;
    if (build_pc_scout_results(lighthouses, transfer_lighthouses, &config,
                               &detect_result, &transfer_result) != 0)
        goto out;
    if (pthread_join(thread, NULL) != 0)
        goto out;
    thread_started = false;
    if (fixture.status != 0 || fixture.received_count != fixture.expected_count ||
        exact_result_shape(detect_result, "main") != 0 ||
        exact_result_shape(transfer_result, "transfer") != 0)
        goto out;
    for (index = 0; index < fixture.expected_count; index++) {
        if (fixture.tokens[index] != index)
            goto out;
    }
    status = 0;

out:
    if (thread_started)
        pthread_join(thread, NULL);
    if (fixture.socket_fd >= 0)
        close(fixture.socket_fd);
    json_object_put(transfer_result);
    json_object_put(detect_result);
    json_object_put(transfer_lighthouses);
    json_object_put(lighthouses);
    if (status != 0) {
        fprintf(stderr,
                "biubiu-accctl PC scout test failed: stats=%d config=%d "
                "fixture=%d received=%zu errno=%d\n",
                statistics_status, config_status, fixture.status,
                fixture.received_count, errno);
        return 1;
    }
    puts("{\"success\":true,\"tests\":[\"official-defaults\","
         "\"official-p90\",\"single-socket-echo\","
         "\"little-endian-sequence\",\"official-result-shape\"]}");
    return 0;
}
