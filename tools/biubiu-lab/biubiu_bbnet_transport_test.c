#include "../../vendor/game-accelerators/biubiu-acc/src/bbnet_transport.h"
#include "../../vendor/game-accelerators/biubiu-acc/src/confluence_codec.h"

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TEST_QUEUE_CAPACITY 16U
#define TEST_PACKET_CAPACITY 4096U

struct test_packet {
    int protocol;
    size_t length;
    unsigned char data[TEST_PACKET_CAPACITY];
};

struct bbnet_client {
    pthread_mutex_t lock;
    struct test_packet inbound[TEST_QUEUE_CAPACITY];
    struct test_packet outbound[TEST_QUEUE_CAPACITY];
    size_t inbound_count;
    size_t outbound_count;
    int state;
};

static struct bbnet_client *active_client;

static int queue_packet(struct test_packet *queue, size_t *count, int protocol,
                        const void *data, size_t length)
{
    if (*count >= TEST_QUEUE_CAPACITY || length > TEST_PACKET_CAPACITY)
        return -1;
    queue[*count].protocol = protocol;
    queue[*count].length = length;
    if (length)
        memcpy(queue[*count].data, data, length);
    (*count)++;
    return 0;
}

static int inject_packet(int protocol, const void *data, size_t length)
{
    int status;

    if (!active_client)
        return -1;
    pthread_mutex_lock(&active_client->lock);
    status = queue_packet(active_client->inbound,
                          &active_client->inbound_count, protocol, data,
                          length);
    pthread_mutex_unlock(&active_client->lock);
    return status;
}

struct bbnet_client *bbnet_client_create(void)
{
    struct bbnet_client *client = calloc(1, sizeof(*client));

    if (!client || pthread_mutex_init(&client->lock, NULL) != 0) {
        free(client);
        return NULL;
    }
    active_client = client;
    return client;
}

void bbnet_client_destroy(struct bbnet_client *client)
{
    if (!client)
        return;
    if (active_client == client)
        active_client = NULL;
    pthread_mutex_destroy(&client->lock);
    free(client);
}

int bbnet_client_connect(struct bbnet_client *client, const char *address,
                         uint16_t port, const void *server_parameter,
                         size_t server_parameter_length,
                         const void *client_parameter,
                         size_t client_parameter_length, int keepalive_ms,
                         int connect_timeout_ms)
{
    (void)server_parameter;
    (void)server_parameter_length;
    (void)client_parameter;
    (void)client_parameter_length;
    (void)keepalive_ms;
    (void)connect_timeout_ms;
    if (!client || !address || !address[0] || !port)
        return -1;
    client->state = BBNET_STATE_ESTABLISHED;
    return 0;
}

void bbnet_client_close(struct bbnet_client *client)
{
    if (client)
        client->state = BBNET_STATE_CLOSED;
}

void bbnet_client_update(struct bbnet_client *client)
{
    (void)client;
}

int bbnet_client_send(struct bbnet_client *client, int protocol,
                      const void *data, size_t length)
{
    struct confluence_frame frame;
    unsigned char response[CONFLUENCE_CONTROL_FRAME_SIZE];
    int status;

    pthread_mutex_lock(&client->lock);
    status = queue_packet(client->outbound, &client->outbound_count, protocol,
                          data, length);
    if (status == 0 &&
        confluence_parse(data, length, &frame) == 0 &&
        frame.event == CONFLUENCE_EVENT_CONNECT &&
        confluence_encode_control(CONFLUENCE_EVENT_CONNECTED, frame.link_id,
                                  response) == 0)
        status = queue_packet(client->inbound, &client->inbound_count,
                              protocol, response, sizeof(response));
    pthread_mutex_unlock(&client->lock);
    return status == 0 ? 1 : 0;
}

int bbnet_client_receive(struct bbnet_client *client, int *protocol,
                         void *data, size_t capacity)
{
    struct test_packet packet;

    pthread_mutex_lock(&client->lock);
    if (!client->inbound_count) {
        pthread_mutex_unlock(&client->lock);
        return -1;
    }
    packet = client->inbound[0];
    if (packet.length > capacity) {
        pthread_mutex_unlock(&client->lock);
        return -2;
    }
    memmove(client->inbound, client->inbound + 1,
            (client->inbound_count - 1U) * sizeof(client->inbound[0]));
    client->inbound_count--;
    pthread_mutex_unlock(&client->lock);
    if (protocol)
        *protocol = packet.protocol;
    if (packet.length)
        memcpy(data, packet.data, packet.length);
    return (int)packet.length;
}

int bbnet_client_state(const struct bbnet_client *client)
{
    return client ? client->state : BBNET_STATE_CLOSED;
}

int bbnet_client_rtt(const struct bbnet_client *client)
{
    return client ? 12 : -1;
}

int bbnet_client_statistics(struct bbnet_client *client,
                            struct bbnet_statistics *statistics)
{
    if (!client || !statistics)
        return -1;
    memset(statistics, 0, sizeof(*statistics));
    statistics->outbound_packets = (int64_t)client->outbound_count;
    return 0;
}

static int wait_readable(int fd)
{
    struct pollfd descriptor = {.fd = fd, .events = POLLIN};

    return poll(&descriptor, 1, 1000) == 1 &&
           (descriptor.revents & POLLIN) != 0
               ? 0
               : -1;
}

static int last_outbound(struct test_packet *packet)
{
    if (!active_client)
        return -1;
    pthread_mutex_lock(&active_client->lock);
    if (!active_client->outbound_count) {
        pthread_mutex_unlock(&active_client->lock);
        return -1;
    }
    *packet = active_client->outbound[active_client->outbound_count - 1U];
    pthread_mutex_unlock(&active_client->lock);
    return 0;
}

static int test_confluence_transport(void)
{
    struct bbnet_transport_config config = {
        .address = "127.0.0.1",
        .port = 9000,
        .keepalive_ms = 10000,
        .connect_timeout_ms = 1000,
        .application_protocol = BBNET_APPLICATION_KCP,
        .mode = BBNET_TRANSPORT_CONFLUENCE,
    };
    struct bbnet_transport *transport = NULL;
    struct confluence_frame parsed;
    struct test_packet packet;
    unsigned char frame[64];
    unsigned char received[16];
    size_t frame_length;
    ssize_t received_length = -1;
    uint32_t link_id = 0;
    int receive_fd = -1;
    int status = -1;
    int step = 1;

    transport = bbnet_transport_start(&config);
    if (!transport || bbnet_transport_state(transport) !=
                          BBNET_STATE_ESTABLISHED ||
        bbnet_transport_rtt(transport) != 12 ||
        bbnet_transport_open_link(transport, &link_id, &receive_fd, 1000) != 0 ||
        link_id == 0 || receive_fd < 0)
        goto out;
    step = 2;
    if (bbnet_transport_send_link(transport, link_id, "request", 7) != 0 ||
        last_outbound(&packet) != 0 || packet.protocol != BBNET_APPLICATION_KCP ||
        confluence_parse(packet.data, packet.length, &parsed) != 0 ||
        parsed.event != CONFLUENCE_EVENT_DATA || parsed.link_id != link_id ||
        parsed.payload_length != 7 || memcmp(parsed.payload, "request", 7) != 0)
        goto out;
    step = 3;
    if (confluence_encode_data(link_id, "response", 8, frame, sizeof(frame),
                               &frame_length) != 0)
        goto out;
    step = 31;
    if (inject_packet(BBNET_APPLICATION_KCP, frame, frame_length) != 0)
        goto out;
    step = 32;
    if (wait_readable(receive_fd) != 0)
        goto out;
    step = 33;
    received_length = recv(receive_fd, received, sizeof(received), 0);
    if (received_length != 8)
        goto out;
    step = 34;
    if (memcmp(received, "response", 8) != 0)
        goto out;
    step = 4;
    status = 0;

out:
    if (status != 0)
        fprintf(stderr,
                "Confluence fixture failed at step %d (recv=%zd errno=%d)\n",
                step, received_length, errno);
    if (transport && link_id)
        bbnet_transport_close_link(transport, link_id);
    if (receive_fd >= 0)
        close(receive_fd);
    bbnet_transport_destroy(transport);
    return status;
}

static int test_datagram_transport(void)
{
    struct bbnet_transport_config config = {
        .address = "127.0.0.1",
        .port = 9001,
        .keepalive_ms = 10000,
        .connect_timeout_ms = 1000,
        .application_protocol = BBNET_APPLICATION_NACK,
        .mode = BBNET_TRANSPORT_DATAGRAM,
    };
    struct bbnet_transport *transport = NULL;
    struct test_packet packet;
    unsigned char received[16];
    int receive_fd;
    int status = -1;

    transport = bbnet_transport_start(&config);
    if (!transport)
        goto out;
    receive_fd = bbnet_transport_datagram_fd(transport);
    if (receive_fd < 0 ||
        bbnet_transport_send_datagram(transport, "outbound", 8) != 0 ||
        last_outbound(&packet) != 0 ||
        packet.protocol != BBNET_APPLICATION_NACK || packet.length != 8 ||
        memcmp(packet.data, "outbound", 8) != 0 ||
        inject_packet(BBNET_APPLICATION_NACK, "inbound", 7) != 0 ||
        wait_readable(receive_fd) != 0 ||
        recv(receive_fd, received, sizeof(received), 0) != 7 ||
        memcmp(received, "inbound", 7) != 0)
        goto out;
    status = 0;

out:
    bbnet_transport_destroy(transport);
    return status;
}

int main(void)
{
    if (test_confluence_transport() != 0) {
        fputs("BBNET Confluence transport test failed\n", stderr);
        return 1;
    }
    if (test_datagram_transport() != 0) {
        fputs("BBNET datagram transport test failed\n", stderr);
        return 1;
    }
    puts("BBNET transport lifecycle fixtures passed");
    return 0;
}
