#include "../../vendor/game-accelerators/biubiu-acc/src/bbnet_bridge.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

static uint32_t read_le32(const unsigned char *input)
{
    return static_cast<uint32_t>(input[0]) |
           (static_cast<uint32_t>(input[1]) << 8) |
           (static_cast<uint32_t>(input[2]) << 16) |
           (static_cast<uint32_t>(input[3]) << 24);
}

static void write_le32(unsigned char *output, uint32_t value)
{
    output[0] = static_cast<unsigned char>(value);
    output[1] = static_cast<unsigned char>(value >> 8);
    output[2] = static_cast<unsigned char>(value >> 16);
    output[3] = static_cast<unsigned char>(value >> 24);
}

static unsigned char checksum(const unsigned char *data, size_t length)
{
    uint32_t sum = 0;
    for (size_t index = 0; index < length; ++index)
        sum += data[index];
    sum = (sum >> 16) + (sum & 0xffffU);
    return static_cast<unsigned char>(~sum);
}

static bool decode(std::vector<unsigned char> *packet)
{
    if (!packet || packet->size() < 4)
        return false;
    const unsigned char key = (*packet)[0] ^ 0x5aU;
    for (size_t index = 1; index < packet->size(); ++index)
        (*packet)[index] ^= key;
    return checksum(packet->data() + 2, packet->size() - 2) == (*packet)[1] &&
           (((*packet)[2] & 0xe0U) == 0xa0U);
}

static std::vector<unsigned char> response(unsigned char command,
                                            uint32_t conversation,
                                            uint32_t host_id)
{
    std::vector<unsigned char> packet(12, 0);
    packet[0] = 0x29;
    packet[2] = static_cast<unsigned char>(0xa0U | (command & 0x1fU));
    write_le32(packet.data() + 4, conversation);
    write_le32(packet.data() + 8, host_id);
    packet[1] = checksum(packet.data() + 2, packet.size() - 2);
    const unsigned char key = packet[0] ^ 0x5aU;
    for (size_t index = 1; index < packet.size(); ++index)
        packet[index] ^= key;
    return packet;
}

int main(void)
{
    const std::string server_parameter =
        "{\"ticket\":\"fixture\",\"channel\":17}";
    const uint32_t assigned_host_id = 0x10203040U;
    int server = socket(AF_INET, SOCK_DGRAM, 0);
    if (server < 0)
        return 1;

    int flags = fcntl(server, F_GETFL, 0);
    if (flags < 0 || fcntl(server, F_SETFL, flags | O_NONBLOCK) != 0)
        return 2;

    sockaddr_in local = {};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(server, reinterpret_cast<sockaddr *>(&local), sizeof(local)) != 0)
        return 3;
    socklen_t local_length = sizeof(local);
    if (getsockname(server, reinterpret_cast<sockaddr *>(&local),
                    &local_length) != 0)
        return 4;

    bbnet_client *client = bbnet_client_create();
    if (!client)
        return 5;
    if (bbnet_client_connect(client, "127.0.0.1", ntohs(local.sin_port),
                             server_parameter.data(), server_parameter.size(),
                             NULL, 0, 10000, 3000) != 0)
        return 6;

    bool acknowledged_syn1 = false;
    bool observed_syn2 = false;
    uint32_t conversation = 0;
    sockaddr_in peer = {};
    socklen_t peer_length = sizeof(peer);
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(3);

    while (std::chrono::steady_clock::now() < deadline && !observed_syn2) {
        bbnet_client_update(client);
        unsigned char buffer[4096];
        const ssize_t length =
            recvfrom(server, buffer, sizeof(buffer), 0,
                     reinterpret_cast<sockaddr *>(&peer), &peer_length);
        if (length > 0) {
            std::vector<unsigned char> packet(buffer, buffer + length);
            if (!decode(&packet))
                return 7;
            const unsigned char command = packet[2] & 0x1fU;
            if (command == 0x16U && packet.size() == 12 &&
                !acknowledged_syn1) {
                conversation = read_le32(packet.data() + 4);
                const std::vector<unsigned char> reply =
                    response(0x12U, conversation, assigned_host_id);
                if (sendto(server, reply.data(), reply.size(), 0,
                           reinterpret_cast<sockaddr *>(&peer),
                           peer_length) < 0)
                    return 8;
                acknowledged_syn1 = true;
            } else if (command == 0x19U) {
                if (!acknowledged_syn1 ||
                    packet.size() != 16 + server_parameter.size() ||
                    read_le32(packet.data() + 4) != conversation ||
                    read_le32(packet.data() + 8) != assigned_host_id ||
                    read_le32(packet.data() + 12) != 0x0cU)
                    return 9;
                const std::string actual(
                    reinterpret_cast<char *>(packet.data() + 16),
                    packet.size() - 16);
                if (actual != server_parameter)
                    return 10;
                observed_syn2 = true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    bbnet_client_destroy(client);
    close(server);
    if (!observed_syn2)
        return 11;
    puts("bbnet official SYN2 fixture passed");
    return 0;
}
