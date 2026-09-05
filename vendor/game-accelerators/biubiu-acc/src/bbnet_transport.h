#ifndef BIUBIU_BBNET_TRANSPORT_H
#define BIUBIU_BBNET_TRANSPORT_H

#include "bbnet_bridge.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct bbnet_transport;

enum bbnet_transport_mode {
    BBNET_TRANSPORT_CONFLUENCE = 1,
    BBNET_TRANSPORT_DATAGRAM = 2
};

enum bbnet_application_protocol {
    BBNET_APPLICATION_RAW = 0,
    BBNET_APPLICATION_KCP = 1,
    BBNET_APPLICATION_TCP = 2,
    BBNET_APPLICATION_NACK = 3
};

struct bbnet_transport_config {
    const char *address;
    uint16_t port;
    const void *server_parameter;
    size_t server_parameter_length;
    const void *client_parameter;
    size_t client_parameter_length;
    int keepalive_ms;
    int connect_timeout_ms;
    int application_protocol;
    enum bbnet_transport_mode mode;
};

struct bbnet_transport *bbnet_transport_start(
    const struct bbnet_transport_config *config);
void bbnet_transport_destroy(struct bbnet_transport *transport);

int bbnet_transport_open_link(struct bbnet_transport *transport,
                              uint32_t *link_id, int *receive_fd,
                              int timeout_ms);
int bbnet_transport_send_link(struct bbnet_transport *transport,
                              uint32_t link_id, const void *data,
                              size_t length);
void bbnet_transport_close_link(struct bbnet_transport *transport,
                                uint32_t link_id);

int bbnet_transport_datagram_fd(const struct bbnet_transport *transport);
int bbnet_transport_send_datagram(struct bbnet_transport *transport,
                                  const void *data, size_t length);

int bbnet_transport_state(const struct bbnet_transport *transport);
int bbnet_transport_rtt(const struct bbnet_transport *transport);
int bbnet_transport_statistics(struct bbnet_transport *transport,
                               struct bbnet_statistics *statistics);

#ifdef __cplusplus
}
#endif

#endif
