#ifndef BIUBIU_BBNET_BRIDGE_H
#define BIUBIU_BBNET_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct bbnet_client;

enum bbnet_client_state {
    BBNET_STATE_CLOSED = 0,
    BBNET_STATE_SYN1 = 1,
    BBNET_STATE_SYN2 = 2,
    BBNET_STATE_SYN_WAIT = 3,
    BBNET_STATE_ESTABLISHED = 4,
    BBNET_STATE_FIN_WAIT = 5
};

struct bbnet_statistics {
    int64_t outbound_packets;
    int64_t outbound_bytes;
    int64_t outbound_payload_bytes;
    int64_t inbound_packets;
    int64_t inbound_bytes;
    int64_t inbound_payload_bytes;
    int64_t discarded_packets;
    int64_t discarded_bytes;
    int64_t discarded_payload_bytes;
};

struct bbnet_client *bbnet_client_create(void);
void bbnet_client_destroy(struct bbnet_client *client);

/*
 * Starts the two-stage QuickNet handshake. The caller must subsequently call
 * bbnet_client_update() until the state becomes BBNET_STATE_ESTABLISHED.
 * server_parameter is copied before Connect; client_parameter is applied
 * immediately after Connect, matching the official client ordering.
 */
int bbnet_client_connect(struct bbnet_client *client, const char *address,
                         uint16_t port, const void *server_parameter,
                         size_t server_parameter_length,
                         const void *client_parameter,
                         size_t client_parameter_length,
                         int keepalive_ms, int connect_timeout_ms);
void bbnet_client_close(struct bbnet_client *client);
void bbnet_client_update(struct bbnet_client *client);

int bbnet_client_send(struct bbnet_client *client, int protocol,
                      const void *data, size_t length);
int bbnet_client_receive(struct bbnet_client *client, int *protocol,
                         void *data, size_t capacity);
int bbnet_client_state(const struct bbnet_client *client);
int bbnet_client_rtt(const struct bbnet_client *client);
int bbnet_client_statistics(struct bbnet_client *client,
                            struct bbnet_statistics *statistics);

#ifdef __cplusplus
}
#endif

#endif
