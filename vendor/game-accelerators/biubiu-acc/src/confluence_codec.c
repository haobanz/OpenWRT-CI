#include "confluence_codec.h"

#include <limits.h>
#include <string.h>

#define CONFLUENCE_VERSION 1U
#define CONFLUENCE_MESSAGE_TYPE 1U

static void codec_write_le16(unsigned char *output, uint16_t value)
{
    output[0] = (unsigned char)value;
    output[1] = (unsigned char)(value >> 8);
}

static void codec_write_le32(unsigned char *output, uint32_t value)
{
    output[0] = (unsigned char)value;
    output[1] = (unsigned char)(value >> 8);
    output[2] = (unsigned char)(value >> 16);
    output[3] = (unsigned char)(value >> 24);
}

static uint16_t codec_read_le16(const unsigned char *input)
{
    return (uint16_t)(input[0] | ((uint16_t)input[1] << 8));
}

static uint32_t codec_read_le32(const unsigned char *input)
{
    return input[0] | ((uint32_t)input[1] << 8) |
           ((uint32_t)input[2] << 16) | ((uint32_t)input[3] << 24);
}

static int confluence_control_event(uint8_t event)
{
    return event >= CONFLUENCE_EVENT_CONNECT &&
           event <= CONFLUENCE_EVENT_CLOSE;
}

int confluence_encode_control(uint8_t event, uint32_t link_id,
                              unsigned char output[CONFLUENCE_CONTROL_FRAME_SIZE])
{
    if (!output || !confluence_control_event(event))
        return -1;
    output[0] = CONFLUENCE_VERSION;
    output[1] = CONFLUENCE_MESSAGE_TYPE;
    output[2] = event;
    codec_write_le32(output + 3, link_id);
    return 0;
}

int confluence_encode_data(uint32_t link_id, const void *payload,
                           size_t payload_length, void *output,
                           size_t output_capacity, size_t *output_length)
{
    unsigned char *bytes = output;
    size_t total;

    if (!output_length || (!payload && payload_length != 0) ||
        payload_length > UINT32_MAX ||
        payload_length > SIZE_MAX - CONFLUENCE_DATA_HEADER_SIZE)
        return -1;
    total = CONFLUENCE_DATA_HEADER_SIZE + payload_length;
    if (!bytes || output_capacity < total)
        return -1;
    bytes[0] = CONFLUENCE_VERSION;
    bytes[1] = CONFLUENCE_MESSAGE_TYPE;
    bytes[2] = CONFLUENCE_EVENT_DATA;
    codec_write_le32(bytes + 3, link_id);
    codec_write_le32(bytes + 7, (uint32_t)payload_length);
    if (payload_length)
        memcpy(bytes + CONFLUENCE_DATA_HEADER_SIZE, payload, payload_length);
    *output_length = total;
    return 0;
}

int confluence_parse(const void *data, size_t length,
                     struct confluence_frame *frame)
{
    const unsigned char *bytes = data;
    uint32_t payload_length;

    if (!bytes || !frame || length < CONFLUENCE_CONTROL_FRAME_SIZE ||
        bytes[0] != CONFLUENCE_VERSION ||
        bytes[1] != CONFLUENCE_MESSAGE_TYPE ||
        bytes[2] < CONFLUENCE_EVENT_CONNECT ||
        bytes[2] > CONFLUENCE_EVENT_DATA)
        return -1;
    memset(frame, 0, sizeof(*frame));
    frame->event = bytes[2];
    frame->link_id = codec_read_le32(bytes + 3);
    if (frame->event != CONFLUENCE_EVENT_DATA)
        return length == CONFLUENCE_CONTROL_FRAME_SIZE ? 0 : -1;
    if (length < CONFLUENCE_DATA_HEADER_SIZE)
        return -1;
    payload_length = codec_read_le32(bytes + 7);
    if ((size_t)payload_length != length - CONFLUENCE_DATA_HEADER_SIZE)
        return -1;
    frame->payload = bytes + CONFLUENCE_DATA_HEADER_SIZE;
    frame->payload_length = payload_length;
    return 0;
}

int biubiu_udp_tunnel_encode(uint8_t protocol,
                             const struct sockaddr_in *source,
                             const struct sockaddr_in *target,
                             uint32_t route_context, const void *payload,
                             size_t payload_length, void *output,
                             size_t output_capacity, size_t *output_length)
{
    unsigned char *bytes = output;
    size_t total;

    if (!source || !target || !output_length ||
        source->sin_family != AF_INET || target->sin_family != AF_INET ||
        (!payload && payload_length != 0) ||
        payload_length > UINT16_MAX - BIUBIU_UDP_TUNNEL_HEADER_SIZE)
        return -1;
    total = BIUBIU_UDP_TUNNEL_HEADER_SIZE + payload_length;
    if (!bytes || output_capacity < total)
        return -1;
    bytes[0] = 0;
    bytes[1] = BIUBIU_UDP_TUNNEL_HEADER_SIZE;
    codec_write_le16(bytes + 2, (uint16_t)total);
    bytes[4] = protocol;
    memcpy(bytes + 5, &source->sin_addr.s_addr, 4);
    memcpy(bytes + 9, &source->sin_port, 2);
    memcpy(bytes + 11, &target->sin_addr.s_addr, 4);
    memcpy(bytes + 15, &target->sin_port, 2);
    codec_write_le32(bytes + 17, route_context);
    if (payload_length)
        memcpy(bytes + BIUBIU_UDP_TUNNEL_HEADER_SIZE, payload, payload_length);
    *output_length = total;
    return 0;
}

int biubiu_udp_tunnel_parse(const void *data, size_t length,
                            struct biubiu_udp_tunnel_frame *frame)
{
    const unsigned char *bytes = data;

    if (!bytes || !frame || length < BIUBIU_UDP_TUNNEL_HEADER_SIZE ||
        (bytes[0] != 0 && bytes[0] != 1) ||
        bytes[1] != BIUBIU_UDP_TUNNEL_HEADER_SIZE ||
        codec_read_le16(bytes + 2) != length)
        return -1;
    memset(frame, 0, sizeof(*frame));
    frame->protocol = bytes[4];
    frame->source.sin_family = AF_INET;
    memcpy(&frame->source.sin_addr.s_addr, bytes + 5, 4);
    memcpy(&frame->source.sin_port, bytes + 9, 2);
    frame->target.sin_family = AF_INET;
    memcpy(&frame->target.sin_addr.s_addr, bytes + 11, 4);
    memcpy(&frame->target.sin_port, bytes + 15, 2);
    frame->route_context = codec_read_le32(bytes + 17);
    frame->payload = bytes + BIUBIU_UDP_TUNNEL_HEADER_SIZE;
    frame->payload_length = length - BIUBIU_UDP_TUNNEL_HEADER_SIZE;
    return 0;
}
