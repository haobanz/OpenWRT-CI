#include "../../vendor/game-accelerators/biubiu-acc/src/confluence_codec.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

static int test_confluence_control(void)
{
    static const unsigned char expected[] = {
        0x01, 0x01, 0x01, 0x12, 0x34, 0x56, 0x78
    };
    unsigned char encoded[CONFLUENCE_CONTROL_FRAME_SIZE];
    struct confluence_frame parsed;

    if (confluence_encode_control(CONFLUENCE_EVENT_CONNECT, 0x78563412,
                                  encoded) != 0 ||
        memcmp(encoded, expected, sizeof(expected)) != 0 ||
        confluence_parse(encoded, sizeof(encoded), &parsed) != 0 ||
        parsed.event != CONFLUENCE_EVENT_CONNECT ||
        parsed.link_id != 0x78563412 || parsed.payload ||
        parsed.payload_length != 0)
        return -1;
    encoded[2] = CONFLUENCE_EVENT_CONNECTED;
    if (confluence_parse(encoded, sizeof(encoded), &parsed) != 0 ||
        parsed.event != CONFLUENCE_EVENT_CONNECTED)
        return -1;
    encoded[2] = CONFLUENCE_EVENT_CONNECT_FAILED;
    if (confluence_parse(encoded, sizeof(encoded), &parsed) != 0 ||
        parsed.event != CONFLUENCE_EVENT_CONNECT_FAILED)
        return -1;
    encoded[2] = CONFLUENCE_EVENT_CLOSE;
    if (confluence_parse(encoded, sizeof(encoded), &parsed) != 0 ||
        parsed.event != CONFLUENCE_EVENT_CLOSE)
        return -1;
    return 0;
}

static int test_confluence_data(void)
{
    static const unsigned char expected[] = {
        0x01, 0x01, 0x05, 0x12, 0x34, 0x56, 0x78,
        0x03, 0x00, 0x00, 0x00, 'a', 'b', 'c'
    };
    unsigned char encoded[sizeof(expected)];
    struct confluence_frame parsed;
    size_t encoded_length = 0;

    if (confluence_encode_data(0x78563412, "abc", 3, encoded,
                               sizeof(encoded), &encoded_length) != 0 ||
        encoded_length != sizeof(expected) ||
        memcmp(encoded, expected, sizeof(expected)) != 0 ||
        confluence_parse(encoded, encoded_length, &parsed) != 0 ||
        parsed.event != CONFLUENCE_EVENT_DATA ||
        parsed.link_id != 0x78563412 || parsed.payload_length != 3 ||
        memcmp(parsed.payload, "abc", 3) != 0)
        return -1;
    encoded[7] = 4;
    if (confluence_parse(encoded, encoded_length, &parsed) == 0)
        return -1;
    if (confluence_parse(expected, CONFLUENCE_CONTROL_FRAME_SIZE, &parsed) == 0)
        return -1;
    return 0;
}

static int test_udp_tunnel(void)
{
    static const unsigned char expected[] = {
        0x00, 0x15, 0x1a, 0x00, 0x11,
        0xc0, 0x00, 0x02, 0x0a, 0xd6, 0xd8,
        0xc6, 0x33, 0x64, 0x07, 0x69, 0x87,
        0x44, 0x33, 0x22, 0x11,
        'h', 'e', 'l', 'l', 'o'
    };
    struct sockaddr_in source = {.sin_family = AF_INET,
                                 .sin_port = htons(55000)};
    struct sockaddr_in target = {.sin_family = AF_INET,
                                 .sin_port = htons(27015)};
    struct biubiu_udp_tunnel_frame parsed;
    unsigned char encoded[sizeof(expected)];
    size_t encoded_length = 0;

    if (inet_pton(AF_INET, "192.0.2.10", &source.sin_addr) != 1 ||
        inet_pton(AF_INET, "198.51.100.7", &target.sin_addr) != 1 ||
        biubiu_udp_tunnel_encode(17, &source, &target, 0x11223344,
                                 "hello", 5, encoded, sizeof(encoded),
                                 &encoded_length) != 0 ||
        encoded_length != sizeof(expected) ||
        memcmp(encoded, expected, sizeof(expected)) != 0 ||
        biubiu_udp_tunnel_parse(encoded, encoded_length, &parsed) != 0 ||
        parsed.protocol != 17 || parsed.route_context != 0x11223344 ||
        parsed.payload_length != 5 ||
        memcmp(parsed.payload, "hello", 5) != 0 ||
        parsed.source.sin_addr.s_addr != source.sin_addr.s_addr ||
        parsed.source.sin_port != source.sin_port ||
        parsed.target.sin_addr.s_addr != target.sin_addr.s_addr ||
        parsed.target.sin_port != target.sin_port)
        return -1;
    encoded[0] = 1;
    if (biubiu_udp_tunnel_parse(encoded, encoded_length, &parsed) != 0)
        return -1;
    encoded[0] = 2;
    if (biubiu_udp_tunnel_parse(encoded, encoded_length, &parsed) == 0)
        return -1;
    encoded[0] = 1;
    encoded[2]--;
    if (biubiu_udp_tunnel_parse(encoded, encoded_length, &parsed) == 0)
        return -1;
    return 0;
}

int main(void)
{
    if (test_confluence_control() != 0 || test_confluence_data() != 0 ||
        test_udp_tunnel() != 0) {
        fputs("biubiu Confluence codec fixture failed\n", stderr);
        return 1;
    }
    puts("biubiu Confluence and UDP tunnel fixtures passed");
    return 0;
}
