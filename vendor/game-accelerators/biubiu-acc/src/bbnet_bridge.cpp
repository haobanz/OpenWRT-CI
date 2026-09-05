#include "bbnet_bridge.h"

#include "quicknet/network/ProtocolImp.h"

#include <climits>
#include <cstring>
#include <new>
#include <string>

struct bbnet_client {
    QuickNet::QuickClient transport;
};

static bool valid_size(size_t length)
{
    return length <= static_cast<size_t>(INT_MAX);
}

extern "C" struct bbnet_client *bbnet_client_create(void)
{
    try {
        bbnet_client *client = new bbnet_client();
        client->transport.SetTrace(&QuickNet::Trace::Null);
        return client;
    } catch (...) {
        return NULL;
    }
}

extern "C" void bbnet_client_destroy(struct bbnet_client *client)
{
    if (!client)
        return;
    try {
        client->transport.Close();
    } catch (...) {
    }
    delete client;
}

extern "C" int bbnet_client_connect(struct bbnet_client *client,
                                      const char *address, uint16_t port,
                                      const void *server_parameter,
                                      size_t server_parameter_length,
                                      const void *client_parameter,
                                      size_t client_parameter_length,
                                      int keepalive_ms,
                                      int connect_timeout_ms)
{
    if (!client || !address || !*address || port == 0 ||
        (!server_parameter && server_parameter_length != 0) ||
        (!client_parameter && client_parameter_length != 0) ||
        !valid_size(server_parameter_length) ||
        !valid_size(client_parameter_length))
        return -1;

    try {
        std::string server;
        std::string client_option;
        if (server_parameter_length != 0) {
            server.assign(static_cast<const char *>(server_parameter),
                          server_parameter_length);
        }
        if (client_parameter_length != 0) {
            client_option.assign(static_cast<const char *>(client_parameter),
                                 client_parameter_length);
        }

        client->transport.Close();
        client->transport.SetTimeOut(-1, connect_timeout_ms);
        client->transport.SetServerParameter(server.c_str());
        if (!client->transport.Connect(address, port))
            return -2;
        if (!client_option.empty() &&
            client->transport.Option(client_option.c_str()) != 0) {
            client->transport.Close();
            return -3;
        }
        client->transport.Keepalive(keepalive_ms);
        return 0;
    } catch (...) {
        try {
            client->transport.Close();
        } catch (...) {
        }
        return -4;
    }
}

extern "C" void bbnet_client_close(struct bbnet_client *client)
{
    if (!client)
        return;
    try {
        client->transport.Close();
    } catch (...) {
    }
}

extern "C" void bbnet_client_update(struct bbnet_client *client)
{
    if (!client)
        return;
    try {
        client->transport.Update();
    } catch (...) {
        client->transport.Close();
    }
}

extern "C" int bbnet_client_send(struct bbnet_client *client, int protocol,
                                   const void *data, size_t length)
{
    if (!client || (!data && length != 0) || !valid_size(length))
        return -1;
    try {
        return client->transport.Send(protocol, data,
                                      static_cast<int>(length), -1)
                   ? 1
                   : 0;
    } catch (...) {
        return -2;
    }
}

extern "C" int bbnet_client_receive(struct bbnet_client *client,
                                      int *protocol, void *data,
                                      size_t capacity)
{
    if (!client || (!data && capacity != 0) || !valid_size(capacity))
        return -3;
    try {
        return client->transport.Recv(protocol, data,
                                      static_cast<int>(capacity));
    } catch (...) {
        return -3;
    }
}

extern "C" int bbnet_client_state(const struct bbnet_client *client)
{
    if (!client)
        return BBNET_STATE_CLOSED;
    try {
        return client->transport.GetState();
    } catch (...) {
        return BBNET_STATE_CLOSED;
    }
}

extern "C" int bbnet_client_rtt(const struct bbnet_client *client)
{
    if (!client)
        return -1;
    try {
        return client->transport.GetRtt();
    } catch (...) {
        return -1;
    }
}

extern "C" int bbnet_client_statistics(
    struct bbnet_client *client, struct bbnet_statistics *statistics)
{
    if (!client || !statistics)
        return -1;
    try {
        QuickNet::ProtocolUdp::Statistic source;
        std::memset(&source, 0, sizeof(source));
        client->transport.Statistic(source);
        statistics->outbound_packets = source.out_count;
        statistics->outbound_bytes = source.out_size;
        statistics->outbound_payload_bytes = source.out_data;
        statistics->inbound_packets = source.in_count;
        statistics->inbound_bytes = source.in_size;
        statistics->inbound_payload_bytes = source.in_data;
        statistics->discarded_packets = source.discard_count;
        statistics->discarded_bytes = source.discard_size;
        statistics->discarded_payload_bytes = source.discard_data;
        return 0;
    } catch (...) {
        return -2;
    }
}
