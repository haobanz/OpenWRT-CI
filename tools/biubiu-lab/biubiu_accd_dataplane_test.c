/* Compile this test with the daemon source so it exercises the same relay code. */
#define main biubiu_accd_program_main
#include "../../vendor/game-accelerators/biubiu-acc/src/biubiu-accd.c"
#undef main

struct bbnet_transport {
    pthread_mutex_t lock;
    pthread_cond_t ready;
    int peer_fd;
    int outbound_write_fd;
    int outbound_read_fd;
    uint32_t link_id;
    enum bbnet_transport_mode mode;
    int application_protocol;
    unsigned char datagram[MAX_FRAME_SIZE];
    size_t datagram_length;
};

struct test_tcp_peer {
    struct bbnet_transport *transport;
    struct sockaddr_in source;
    struct sockaddr_in target;
    uint32_t session_id;
    int status;
};

struct test_relay {
    int client_fd;
    struct bolt_channel *channel;
    struct sockaddr_in source;
    struct sockaddr_in target;
    int status;
};

static int fake_pair(int pair[2])
{
    return socketpair(AF_UNIX, SOCK_SEQPACKET, 0, pair);
}

struct bbnet_transport *bbnet_transport_start(
    const struct bbnet_transport_config *config)
{
    struct bbnet_transport *transport;

    if (!config)
        return NULL;
    transport = calloc(1, sizeof(*transport));
    if (!transport)
        return NULL;
    if (pthread_mutex_init(&transport->lock, NULL) != 0 ||
        pthread_cond_init(&transport->ready, NULL) != 0) {
        free(transport);
        return NULL;
    }
    transport->peer_fd = -1;
    transport->outbound_write_fd = -1;
    transport->outbound_read_fd = -1;
    transport->mode = config->mode;
    transport->application_protocol = config->application_protocol;
    return transport;
}

void bbnet_transport_destroy(struct bbnet_transport *transport)
{
    if (!transport)
        return;
    if (transport->peer_fd >= 0)
        close(transport->peer_fd);
    if (transport->outbound_write_fd >= 0)
        close(transport->outbound_write_fd);
    if (transport->outbound_read_fd >= 0)
        close(transport->outbound_read_fd);
    pthread_cond_destroy(&transport->ready);
    pthread_mutex_destroy(&transport->lock);
    free(transport);
}

int bbnet_transport_open_link(struct bbnet_transport *transport,
                              uint32_t *link_id, int *receive_fd,
                              int timeout_ms)
{
    int pair[2] = {-1, -1};
    int outbound[2] = {-1, -1};

    (void)timeout_ms;
    if (!transport || !link_id || !receive_fd || fake_pair(pair) != 0 ||
        fake_pair(outbound) != 0) {
        if (pair[0] >= 0)
            close(pair[0]);
        if (pair[1] >= 0)
            close(pair[1]);
        return -1;
    }
    pthread_mutex_lock(&transport->lock);
    transport->peer_fd = pair[0];
    transport->outbound_write_fd = outbound[0];
    transport->outbound_read_fd = outbound[1];
    transport->link_id = 0x10203040U;
    *link_id = transport->link_id;
    *receive_fd = pair[1];
    pthread_cond_broadcast(&transport->ready);
    pthread_mutex_unlock(&transport->lock);
    return 0;
}

int bbnet_transport_send_link(struct bbnet_transport *transport,
                              uint32_t link_id, const void *data,
                              size_t length)
{
    int fd;

    pthread_mutex_lock(&transport->lock);
    fd = link_id == transport->link_id ? transport->outbound_write_fd : -1;
    pthread_mutex_unlock(&transport->lock);
    return fd >= 0 && write(fd, data, length) == (ssize_t)length ? 0 : -1;
}

void bbnet_transport_close_link(struct bbnet_transport *transport,
                                uint32_t link_id)
{
    if (!transport)
        return;
    pthread_mutex_lock(&transport->lock);
    if (link_id == transport->link_id && transport->peer_fd >= 0) {
        close(transport->peer_fd);
        transport->peer_fd = -1;
    }
    if (link_id == transport->link_id && transport->outbound_write_fd >= 0) {
        close(transport->outbound_write_fd);
        transport->outbound_write_fd = -1;
    }
    pthread_mutex_unlock(&transport->lock);
}

int bbnet_transport_datagram_fd(const struct bbnet_transport *transport)
{
    (void)transport;
    return -1;
}

int bbnet_transport_send_datagram(struct bbnet_transport *transport,
                                  const void *data, size_t length)
{
    if (!transport || !data || length > sizeof(transport->datagram))
        return -1;
    memcpy(transport->datagram, data, length);
    transport->datagram_length = length;
    return 0;
}

int bbnet_transport_state(const struct bbnet_transport *transport)
{
    return transport ? BBNET_STATE_ESTABLISHED : BBNET_STATE_CLOSED;
}

int bbnet_transport_rtt(const struct bbnet_transport *transport)
{
    return transport ? 10 : -1;
}

int bbnet_transport_statistics(struct bbnet_transport *transport,
                               struct bbnet_statistics *statistics)
{
    if (!transport || !statistics)
        return -1;
    memset(statistics, 0, sizeof(*statistics));
    return 0;
}

static int run_tcp_bind_test(void)
{
    static const unsigned char ticket[] = "authorized-channel-ticket";
    struct bolt_channel channel = {
        .protocol = "TCP",
        .address = "127.0.0.1",
        .channel_token = "outbound-A",
        .session_id = 0x11223344,
        .ticket = {(unsigned char *)ticket, sizeof(ticket) - 1},
    };
    struct bytes request = {0};
    size_t header_length = 26;
    size_t token_offset = header_length + 5U;
    size_t session_offset = token_offset + strlen(channel.channel_token);
    size_t ticket_offset = session_offset + 4U;
    int status = -1;

    if (bolt_encode_bind_at_tick(&channel, 0x55667788U, &request) == 0 &&
        request.len == 99 &&
        request.data[0] == BOLT_BIND_TCP_REQUEST_TYPE &&
        request.data[1] == header_length &&
        read_le16(request.data + 2) == request.len &&
        request.data[header_length] == 1U &&
        read_le32(request.data + header_length + 1U) ==
            0x55667788U &&
        memcmp(request.data + token_offset, channel.channel_token,
               strlen(channel.channel_token)) == 0 &&
        read_le32(request.data + session_offset) == channel.session_id &&
        memcmp(request.data + ticket_offset, channel.ticket.data,
               channel.ticket.len) == 0)
        status = 0;
    bytes_free(&request);
    return status;
}

static int write_runtime_fixture(const char *document, char path[])
{
    int fd = mkstemp(path);
    int status = -1;

    if (fd >= 0 && fchmod(fd, S_IRUSR | S_IWUSR) == 0 &&
        dprintf(fd, "%s", document) == (ssize_t)strlen(document))
        status = 0;
    if (fd >= 0)
        close(fd);
    return status;
}

static int run_udp_bind_refused_test(void)
{
    static unsigned char ticket[] = "synthetic-channel-ticket";
    struct bolt_channel channel = {
        .protocol = "UDP",
        .address = "127.0.0.1",
        .channel_token = "outbound-A",
        .session_id = 0x11223344,
        .ticket = {ticket, sizeof(ticket) - 1},
    };
    struct sockaddr_in address = {.sin_family = AF_INET};
    socklen_t size = sizeof(address);
    int fd = socket(AF_INET, SOCK_DGRAM, 0);

    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (fd < 0)
        return -1;
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        getsockname(fd, (struct sockaddr *)&address, &size) != 0) {
        close(fd);
        return -1;
    }
    channel.port = ntohs(address.sin_port);
    close(fd);
    return bind_channel_endpoint(&channel, "127.0.0.1") == -1 ? 0 : -1;
}

static int run_bip_mapping_test(void)
{
    static const char document[] =
        "{\"schemaVersion\":1,\"signalSessionId\":\"test-session\","
        "\"channels\":[{\"protocol\":\"TCP\","
        "\"ip\":\"192.0.2.1\",\"port\":1234,"
        "\"channelToken\":\"legacy-outbound\","
        "\"sessionId\":270544960,\"ticket\":\"opaque-ticket\","
        "\"bip\":\"192.0.2.10\"}]}";
    char path[] = "/tmp/biubiu-accd-runtime.XXXXXX";
    struct runtime_config runtime = {0};
    int status = -1;

    if (write_runtime_fixture(document, path) != 0)
        return -1;
    if (load_runtime(path, &runtime) == 0 && runtime.has_tcp &&
        strcmp(runtime.tcp.address, "192.0.2.1") == 0 &&
        runtime.tcp.port == 1234 &&
        strcmp(runtime.tcp.bip_address, "192.0.2.10") == 0)
        status = 0;
    unlink(path);
    runtime_free(&runtime);
    return status;
}

static int run_runtime_expiry_test(void)
{
    static const char document[] =
        "{\"schemaVersion\":1,\"signalSessionId\":\"test-session\","
        "\"channels\":[{\"protocol\":\"TCP\","
        "\"ip\":\"192.0.2.1\",\"port\":1234,"
        "\"channelToken\":\"legacy-outbound\","
        "\"sessionId\":270544960,\"ticket\":\"opaque-ticket\","
        "\"expiresAt\":4102444799000}]}";
    char path[] = "/tmp/biubiu-accd-expiry.XXXXXX";
    struct runtime_config runtime = {0};
    int status = -1;

    if (write_runtime_fixture(document, path) != 0)
        return -1;
    if (load_runtime(path, &runtime) == 0 &&
        runtime.expires_at == 4102444799LL &&
        !runtime_is_expired(&runtime, (time_t)4102444798) &&
        runtime_is_expired(&runtime, (time_t)4102444799))
        status = 0;
    unlink(path);
    runtime_free(&runtime);
    return status;
}

static int run_runtime_schema_test(void)
{
    static const char document[] =
        "{\"channels\":[{\"protocol\":\"TCP\","
        "\"ip\":\"192.0.2.1\",\"port\":1234,"
        "\"sessionId\":270544960,\"ticket\":\"opaque-ticket\"}]}";
    char path[] = "/tmp/biubiu-accd-schema.XXXXXX";
    struct runtime_config runtime = {0};
    int status = -1;

    if (write_runtime_fixture(document, path) != 0)
        return -1;
    if (load_runtime(path, &runtime) != 0)
        status = 0;
    unlink(path);
    runtime_free(&runtime);
    return status;
}

static int run_multi_outbound_route_test(void)
{
    static const char document[] =
        "{\"schemaVersion\":2,\"signalSessionId\":\"test-session\","
        "\"defaultOutboundId\":\"main\",\"outbounds\":["
        "{\"id\":\"main\",\"type\":\"bolt\",\"channels\":["
        "{\"protocol\":\"UDP\",\"ip\":\"192.0.2.1\",\"port\":2001,"
        "\"sessionId\":1,\"ticket\":\"main-ticket\"}]},"
        "{\"id\":\"main\",\"type\":\"spare\",\"channels\":["
        "{\"protocol\":\"UDP\",\"ip\":\"192.0.2.2\",\"port\":2002,"
        "\"sessionId\":2,\"ticket\":\"spare-ticket\"}]},"
        "{\"id\":\"edge\",\"type\":\"bypath\",\"channels\":["
        "{\"protocol\":\"UDP\",\"ip\":\"192.0.2.3\",\"port\":2003,"
        "\"sessionId\":3,\"ticket\":\"edge-ticket\"}]}],"
        "\"routes\":[{\"outboundId\":\"edge\","
        "\"primaryOutboundId\":\"main\",\"protocol\":17,"
        "\"cidrs\":[\"203.0.113.0/24\"],\"ports\":[\"27000-27100\"]}]}";
    char path[] = "/tmp/biubiu-accd-route.XXXXXX";
    struct runtime_config runtime = {0};
    struct sockaddr_in target = {.sin_family = AF_INET,
                                 .sin_port = htons(27015)};
    const struct bolt_channel *selected;
    int status = -1;

    if (write_runtime_fixture(document, path) != 0)
        return -1;
    if (load_runtime(path, &runtime) != 0 ||
        inet_pton(AF_INET, "203.0.113.42", &target.sin_addr) != 1)
        goto out;
    runtime.outbounds[0].channels[0].bound = true;
    runtime.outbounds[1].channels[0].bound = true;
    runtime.outbounds[2].channels[0].bound = true;
    selected = select_channel(&runtime, &target, IPPROTO_UDP);
    if (!selected || selected->session_id != 3)
        goto out;
    runtime.outbounds[2].channels[0].bound = false;
    selected = select_channel(&runtime, &target, IPPROTO_UDP);
    if (!selected || selected->session_id != 1)
        goto out;
    runtime.outbounds[0].channels[0].bound = false;
    selected = select_channel(&runtime, &target, IPPROTO_UDP);
    if (!selected || selected->session_id != 2)
        goto out;
    status = 0;

out:
    unlink(path);
    runtime_free(&runtime);
    return status;
}

static int validate_connect_request(const unsigned char *data, size_t length,
                                    uint32_t session_id,
                                    const struct sockaddr_in *source,
                                    const struct sockaddr_in *target)
{
    unsigned char source_bytes[6];
    unsigned char target_bytes[6];

    memcpy(source_bytes, &source->sin_addr.s_addr, 4);
    memcpy(source_bytes + 4, &source->sin_port, 2);
    memcpy(target_bytes, &target->sin_addr.s_addr, 4);
    memcpy(target_bytes + 4, &target->sin_port, 2);
    return length == 26 && data[0] == BOLT_PROTOCOL_VERSION && data[1] == 26 &&
                   read_le16(data + 2) == 26 &&
                   data[4] == BOLT_COMMAND_CONNECT_REQUEST &&
                   read_le32(data + 5) == session_id && data[9] == 2 &&
                   data[10] == 1 && data[11] == 6 &&
                   memcmp(data + 12, source_bytes, sizeof(source_bytes)) == 0 &&
                   data[18] == 2 && data[19] == 6 &&
                   memcmp(data + 20, target_bytes, sizeof(target_bytes)) == 0
               ? 0
               : -1;
}

static void *test_tcp_peer_main(void *argument)
{
    struct test_tcp_peer *peer = argument;
    unsigned char buffer[MAX_FRAME_SIZE];
    unsigned char response[13] = {
        BOLT_PROTOCOL_VERSION, 13, 13, 0, BOLT_COMMAND_CONNECT_RESPONSE,
        0, 0, 0, 0, 0x42, 0, BOLT_STATUS_SUCCESS, 0
    };
    int inbound_fd;
    int outbound_fd;
    ssize_t length;

    pthread_mutex_lock(&peer->transport->lock);
    while (peer->transport->peer_fd < 0)
        pthread_cond_wait(&peer->transport->ready, &peer->transport->lock);
    inbound_fd = peer->transport->peer_fd;
    outbound_fd = peer->transport->outbound_read_fd;
    pthread_mutex_unlock(&peer->transport->lock);
    length = recv(outbound_fd, buffer, sizeof(buffer), 0);
    if (length <= 0 ||
        validate_connect_request(buffer, (size_t)length, peer->session_id,
                                 &peer->source, &peer->target) != 0) {
        peer->status = -2;
        return NULL;
    }
    write_le32(response + 5, peer->session_id);
    if (write(inbound_fd, response, sizeof(response)) != sizeof(response)) {
        peer->status = -3;
        return NULL;
    }
    length = recv(outbound_fd, buffer, sizeof(buffer), 0);
    if (length != 5 || memcmp(buffer, "hello", 5) != 0) {
        peer->status = -4;
        return NULL;
    }
    if (write(inbound_fd, "reply", 5) != 5) {
        peer->status = -5;
        return NULL;
    }
    peer->status = 0;
    return NULL;
}

static void *test_relay_main(void *argument)
{
    struct test_relay *relay = argument;

    relay->status = forward_tcp_stream(relay->client_fd, relay->channel,
                                       &relay->source, &relay->target);
    return NULL;
}

static int run_tcp_confluence_roundtrip_test(void)
{
    struct bbnet_transport_config config = {
        .mode = BBNET_TRANSPORT_CONFLUENCE,
        .application_protocol = BBNET_APPLICATION_KCP,
    };
    struct bolt_channel channel = {
        .protocol = "TCP",
        .session_id = 0x50607080U,
        .bound = true,
    };
    struct test_relay relay = {.client_fd = -1,
                               .channel = &channel,
                               .source = {.sin_family = AF_INET,
                                          .sin_port = htons(55000)},
                               .target = {.sin_family = AF_INET,
                                          .sin_port = htons(27015)},
                               .status = -1};
    struct test_tcp_peer peer;
    pthread_t peer_thread;
    pthread_t relay_thread;
    int pair[2] = {-1, -1};
    unsigned char reply[5];
    bool peer_started = false;
    bool relay_started = false;
    int status = -1;

    channel.transport = bbnet_transport_start(&config);
    if (!channel.transport ||
        inet_pton(AF_INET, "192.0.2.50", &relay.source.sin_addr) != 1 ||
        inet_pton(AF_INET, "198.51.100.7", &relay.target.sin_addr) != 1 ||
        fake_pair(pair) != 0)
        goto out;
    peer = (struct test_tcp_peer){.transport = channel.transport,
                                  .source = relay.source,
                                  .target = relay.target,
                                  .session_id = channel.session_id,
                                  .status = -1};
    relay.client_fd = pair[0];
    if (pthread_create(&peer_thread, NULL, test_tcp_peer_main, &peer) != 0)
        goto out;
    peer_started = true;
    if (pthread_create(&relay_thread, NULL, test_relay_main, &relay) != 0)
        goto out;
    relay_started = true;
    if (write(pair[1], "hello", 5) == 5 &&
        receive_link_packet(pair[1], reply, sizeof(reply), 2000) ==
            (ssize_t)sizeof(reply) &&
        memcmp(reply, "reply", sizeof(reply)) == 0)
        status = 0;
    close(pair[1]);
    pair[1] = -1;

out:
    if (relay_started)
        pthread_join(relay_thread, NULL);
    if (peer_started)
        pthread_join(peer_thread, NULL);
    if (status == 0 && (peer.status != 0 || relay.status != 0))
        status = -1;
    if (status != 0)
        fprintf(stderr, "tcp fixture peer=%d relay=%d errno=%d\n",
                peer_started ? peer.status : -99,
                relay_started ? relay.status : -99, errno);
    if (pair[0] >= 0)
        close(pair[0]);
    if (pair[1] >= 0)
        close(pair[1]);
    bbnet_transport_destroy(channel.transport);
    return status;
}

static int run_udp_roundtrip_case(bool native_bolt)
{
    struct bbnet_transport_config config = {
        .mode = BBNET_TRANSPORT_DATAGRAM,
        .application_protocol = BBNET_APPLICATION_NACK,
    };
    struct bolt_channel channel = {
        .protocol = "UDP",
        .session_id = 0x11223344U,
        .bound = true,
        .datagram_fd = -1,
    };
    struct udp_flow flows[MAX_UDP_FLOWS];
    struct biubiu_udp_tunnel_frame parsed;
    unsigned char inbound[128];
    unsigned char reply[5];
    size_t inbound_length;
    int pair[2] = {-1, -1};
    int datagram_pair[2] = {-1, -1};
    unsigned char outgoing[128];
    size_t outgoing_length;
    const unsigned char *outgoing_data;
    int status = -1;
    size_t index;

    memset(flows, 0, sizeof(flows));
    for (index = 0; index < MAX_UDP_FLOWS; index++)
        flows[index].reply_fd = -1;
    channel.native_bolt = native_bolt;
    if (native_bolt) {
        if (socketpair(AF_UNIX, SOCK_DGRAM, 0, datagram_pair) != 0)
            goto out;
        channel.datagram_fd = datagram_pair[0];
        datagram_pair[0] = -1;
    } else {
        channel.transport = bbnet_transport_start(&config);
        if (!channel.transport)
            goto out;
    }
    if (fake_pair(pair) != 0)
        goto out;
    flows[0].active = true;
    flows[0].reply_fd = pair[0];
    flows[0].channel = &channel;
    flows[0].client.sin_family = AF_INET;
    flows[0].client.sin_port = htons(50000);
    flows[0].target.sin_family = AF_INET;
    flows[0].target.sin_port = htons(27015);
    if (inet_pton(AF_INET, "192.0.2.50", &flows[0].client.sin_addr) != 1 ||
        inet_pton(AF_INET, "198.51.100.7", &flows[0].target.sin_addr) != 1 ||
        send_udp_flow_payload(&channel, &flows[0],
                              (const unsigned char *)"hello", 5) != 0)
        goto out;
    if (native_bolt) {
        ssize_t count = recv(datagram_pair[1], outgoing, sizeof(outgoing), 0);
        if (count <= 0)
            goto out;
        outgoing_data = outgoing;
        outgoing_length = (size_t)count;
    } else {
        outgoing_data = channel.transport->datagram;
        outgoing_length = channel.transport->datagram_length;
    }
    if (biubiu_udp_tunnel_parse(outgoing_data, outgoing_length,
                                &parsed) != 0 ||
        parsed.protocol != IPPROTO_UDP ||
        !same_endpoint(&parsed.source, native_bolt ? &flows[0].target : &flows[0].client) ||
        !same_endpoint(&parsed.target, native_bolt ? &flows[0].client : &flows[0].target) ||
        parsed.route_context != channel.session_id ||
        parsed.payload_length != 5 || memcmp(parsed.payload, "hello", 5) != 0)
        goto out;
    if (biubiu_udp_tunnel_encode(
            IPPROTO_UDP, &flows[0].target, &flows[0].client,
            channel.session_id, "reply", 5, inbound, sizeof(inbound),
            &inbound_length) != 0)
        goto out;
    if (native_bolt)
        inbound[0] = 1;
    if (inject_udp_tunnel_payload(flows, &channel, inbound, inbound_length) != 0 ||
        read(pair[1], reply, sizeof(reply)) != sizeof(reply) ||
        memcmp(reply, "reply", sizeof(reply)) != 0)
        goto out;
    write_le32(inbound + 17, channel.session_id + 1);
    if (inject_udp_tunnel_payload(flows, &channel, inbound, inbound_length) == 0)
        goto out;
    if (biubiu_udp_tunnel_encode(
            IPPROTO_UDP, &flows[0].client, &flows[0].target,
            channel.session_id, "wrong", 5, inbound, sizeof(inbound),
            &inbound_length) != 0 ||
        inject_udp_tunnel_payload(flows, &channel, inbound, inbound_length) == 0)
        goto out;
    status = 0;

out:
    close_udp_flow(&flows[0]);
    if (pair[1] >= 0)
        close(pair[1]);
    if (datagram_pair[0] >= 0)
        close(datagram_pair[0]);
    if (datagram_pair[1] >= 0)
        close(datagram_pair[1]);
    channel_free(&channel);
    return status;
}

static int run_udp_tunnel_roundtrip_test(void)
{
    return run_udp_roundtrip_case(false);
}

static int run_native_udp_roundtrip_test(void)
{
    return run_udp_roundtrip_case(true);
}

struct native_tcp_fixture {
    int listener;
    struct sockaddr_in target;
    uint32_t session_id;
    unsigned char key;
    int status;
};

static void *native_tcp_fixture_main(void *argument)
{
    struct native_tcp_fixture *fixture = argument;
    struct pollfd pollfd = {.fd = fixture->listener, .events = POLLIN};
    unsigned char handshake[22];
    unsigned char ack[23] = {1, 21, 23, [21] = 0x21, [22] = 0x22};
    unsigned char payload[5];
    unsigned char eof;
    int fd = -1;
    size_t i;

    if (poll(&pollfd, 1, 2000) <= 0 ||
        (fd = accept(fixture->listener, NULL, NULL)) < 0 ||
        set_socket_timeout(fd, 2) != 0 || read_full(fd, handshake, sizeof(handshake)) != 0 ||
        handshake[0] != 1 || handshake[1] != 21 || read_le16(handshake + 2) != 22 ||
        memcmp(handshake + 5, &fixture->target.sin_addr, 4) ||
        memcmp(handshake + 9, &fixture->target.sin_port, 2) ||
        read_le32(handshake + 17) != fixture->session_id || handshake[21] != 0x20 ||
        write_full(fd, ack, sizeof(ack)) != 0 ||
        read_full(fd, payload, sizeof(payload)) != 0)
        goto out;
    for (i = 0; i < sizeof(payload); i++)
        payload[i] ^= fixture->key;
    if (memcmp(payload, "hello", 5) || recv(fd, &eof, 1, 0) != 0)
        goto out;
    memcpy(payload, "reply", 5);
    for (i = 0; i < sizeof(payload); i++)
        payload[i] ^= fixture->key;
    if (write_full(fd, payload, sizeof(payload)) == 0)
        fixture->status = 0;
out:
    if (fd >= 0)
        close(fd);
    return NULL;
}

static int run_native_tcp_roundtrip_test(void)
{
    struct bolt_channel channel = {
        .protocol = "TCP", .address = "127.0.0.1",
        .session_id = 0x10203040, .native_bolt = true, .datagram_fd = -1,
        .bound = true, .ept_enabled = true, .ept_key = 0x5a,
    };
    struct test_relay relay = {.channel = &channel, .status = -1};
    struct native_tcp_fixture fixture = {.listener = -1, .key = 0x5a, .status = -1};
    struct sockaddr_in address = {.sin_family = AF_INET};
    socklen_t size = sizeof(address);
    pthread_t peer_thread, relay_thread;
    int pair[2] = {-1, -1};
    bool peer_started = false, relay_started = false;
    unsigned char reply[5];
    int status = -1;

    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    fixture.listener = socket(AF_INET, SOCK_STREAM, 0);
    if (fixture.listener < 0 ||
        bind(fixture.listener, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        getsockname(fixture.listener, (struct sockaddr *)&address, &size) != 0 ||
        listen(fixture.listener, 1) != 0 ||
        socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0 ||
        set_socket_timeout(pair[1], 3) != 0)
        goto out;
    channel.port = ntohs(address.sin_port);
    relay.client_fd = pair[0];
    relay.source.sin_family = relay.target.sin_family = AF_INET;
    relay.source.sin_port = htons(40000);
    relay.target.sin_port = htons(27015);
    inet_pton(AF_INET, "192.0.2.5", &relay.source.sin_addr);
    inet_pton(AF_INET, "198.51.100.7", &relay.target.sin_addr);
    fixture.target = relay.target;
    fixture.session_id = channel.session_id;
    if (pthread_create(&peer_thread, NULL, native_tcp_fixture_main, &fixture) != 0)
        goto out;
    peer_started = true;
    if (pthread_create(&relay_thread, NULL, test_relay_main, &relay) != 0)
        goto out;
    relay_started = true;
    if (write_full(pair[1], (const unsigned char *)"hello", 5) == 0 &&
        shutdown(pair[1], SHUT_WR) == 0 && read_full(pair[1], reply, 5) == 0 &&
        !memcmp(reply, "reply", 5))
        status = 0;
out:
    if (pair[1] >= 0) {
        shutdown(pair[1], SHUT_RDWR);
        close(pair[1]);
    }
    if (relay_started)
        pthread_join(relay_thread, NULL);
    if (peer_started)
        pthread_join(peer_thread, NULL);
    if (pair[0] >= 0)
        close(pair[0]);
    if (fixture.listener >= 0)
        close(fixture.listener);
    return status == 0 && fixture.status == 0 && relay.status == 0 ? 0 : -1;
}

static int run_test(const char *name, int (*test)(void))
{
    if (test() != 0) {
        fprintf(stderr, "biubiu-accd test failed: %s\n", name);
        return 1;
    }
    return 0;
}

int main(void)
{
    if (run_test("profile-bip-mapping", run_bip_mapping_test) != 0 ||
        run_test("runtime-expiry-guard", run_runtime_expiry_test) != 0 ||
        run_test("runtime-schema-guard", run_runtime_schema_test) != 0 ||
        run_test("multi-outbound-route-and-spare-fallback",
                 run_multi_outbound_route_test) != 0 ||
        run_test("bolt-v2-bind", run_tcp_bind_test) != 0 ||
        run_test("udp-bind-error-cleanup", run_udp_bind_refused_test) != 0 ||
        run_test("native-tcp-xor-half-close", run_native_tcp_roundtrip_test) != 0 ||
        run_test("native-udp-address-session", run_native_udp_roundtrip_test) != 0 ||
        run_test("tcp-confluence-raw-stream",
                 run_tcp_confluence_roundtrip_test) != 0 ||
        run_test("udp-bbnet-reverse-tuple",
                 run_udp_tunnel_roundtrip_test) != 0)
        return 1;
    puts("{\"success\":true,\"tests\":[\"profile-bip-mapping\","
         "\"runtime-expiry-guard\",\"runtime-schema-guard\","
         "\"multi-outbound-route\",\"spare-fallback\",\"bolt-v2-bind\","
         "\"udp-bind-error-cleanup\",\"native-tcp-xor-half-close\","
         "\"native-udp-address-session\","
         "\"tcp-confluence-connect\",\"tcp-raw-stream\","
         "\"udp-bbnet-frame\",\"udp-reverse-tuple\"]}");
    return 0;
}
