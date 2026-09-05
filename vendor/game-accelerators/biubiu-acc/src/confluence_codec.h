#ifndef BIUBIU_CONFLUENCE_CODEC_H
#define BIUBIU_CONFLUENCE_CODEC_H

#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>

#define CONFLUENCE_CONTROL_FRAME_SIZE 7U
#define CONFLUENCE_DATA_HEADER_SIZE 11U
#define BIUBIU_UDP_TUNNEL_HEADER_SIZE 21U

enum confluence_event {
    CONFLUENCE_EVENT_CONNECT = 1,
    CONFLUENCE_EVENT_CONNECTED = 2,
    CONFLUENCE_EVENT_CONNECT_FAILED = 3,
    CONFLUENCE_EVENT_CLOSE = 4,
    CONFLUENCE_EVENT_DATA = 5
};

struct confluence_frame {
    uint8_t event;
    uint32_t link_id;
    const unsigned char *payload;
    size_t payload_length;
};

struct biubiu_udp_tunnel_frame {
    uint8_t protocol;
    struct sockaddr_in source;
    struct sockaddr_in target;
    uint32_t route_context;
    const unsigned char *payload;
    size_t payload_length;
};

int confluence_encode_control(uint8_t event, uint32_t link_id,
                              unsigned char output[CONFLUENCE_CONTROL_FRAME_SIZE]);
int confluence_encode_data(uint32_t link_id, const void *payload,
                           size_t payload_length, void *output,
                           size_t output_capacity, size_t *output_length);
int confluence_parse(const void *data, size_t length,
                     struct confluence_frame *frame);

int biubiu_udp_tunnel_encode(uint8_t protocol,
                             const struct sockaddr_in *source,
                             const struct sockaddr_in *target,
                             uint32_t route_context, const void *payload,
                             size_t payload_length, void *output,
                             size_t output_capacity, size_t *output_length);
int biubiu_udp_tunnel_parse(const void *data, size_t length,
                            struct biubiu_udp_tunnel_frame *frame);

#endif
