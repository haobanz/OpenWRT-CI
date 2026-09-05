#define _GNU_SOURCE

#include "bbnet_transport.h"

#include "confluence_codec.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define BBNET_MAX_LINKS 128U
#define BBNET_MAX_MESSAGE UINT16_MAX
#define BBNET_PUMP_INTERVAL_NS 10000000L

enum bbnet_link_state {
    BBNET_LINK_UNUSED = 0,
    BBNET_LINK_OPENING,
    BBNET_LINK_OPEN,
    BBNET_LINK_FAILED,
    BBNET_LINK_CLOSED
};

struct bbnet_link {
    uint32_t id;
    int pump_fd;
    enum bbnet_link_state state;
};

struct bbnet_transport {
    struct bbnet_client *client;
    pthread_t pump_thread;
    pthread_mutex_t lock;
    pthread_cond_t state_changed;
    struct bbnet_link links[BBNET_MAX_LINKS];
    uint32_t next_link_id;
    int datagram_fds[2];
    int application_protocol;
    enum bbnet_transport_mode mode;
    bool thread_started;
    bool ready;
    bool failed;
    bool stopping;
};

static int set_nonblocking_cloexec(int fd)
{
    int flags;

    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0)
        return -1;
    flags = fcntl(fd, F_GETFD, 0);
    if (flags < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) != 0)
        return -1;
    return 0;
}

static int make_socket_pair(int fds[2])
{
    int size = 256 * 1024;

    if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fds) != 0)
        return -1;
    if (set_nonblocking_cloexec(fds[0]) != 0 ||
        fcntl(fds[1], F_SETFD, FD_CLOEXEC) != 0) {
        close(fds[0]);
        close(fds[1]);
        fds[0] = -1;
        fds[1] = -1;
        return -1;
    }
    (void)setsockopt(fds[0], SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
    (void)setsockopt(fds[1], SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
    return 0;
}

static struct timespec realtime_deadline(int timeout_ms)
{
    struct timespec deadline = {0};

    (void)clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += timeout_ms / 1000;
    deadline.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }
    return deadline;
}

static struct bbnet_link *find_link(struct bbnet_transport *transport,
                                    uint32_t link_id)
{
    size_t index;

    for (index = 0; index < BBNET_MAX_LINKS; index++)
        if (transport->links[index].state != BBNET_LINK_UNUSED &&
            transport->links[index].id == link_id)
            return &transport->links[index];
    return NULL;
}

static void close_pump_fd(struct bbnet_link *link)
{
    if (link->pump_fd >= 0) {
        close(link->pump_fd);
        link->pump_fd = -1;
    }
}

static void fail_all_links_locked(struct bbnet_transport *transport)
{
    size_t index;

    for (index = 0; index < BBNET_MAX_LINKS; index++) {
        struct bbnet_link *link = &transport->links[index];

        if (link->state == BBNET_LINK_UNUSED)
            continue;
        link->state = BBNET_LINK_FAILED;
        close_pump_fd(link);
    }
    if (transport->datagram_fds[0] >= 0) {
        close(transport->datagram_fds[0]);
        transport->datagram_fds[0] = -1;
    }
}

static int send_wire(struct bbnet_transport *transport, const void *data,
                     size_t length)
{
    if (!transport || (!data && length) || length > BBNET_MAX_MESSAGE) {
        errno = EINVAL;
        return -1;
    }
    (void)pthread_mutex_lock(&transport->lock);
    if (!transport->ready || transport->failed || transport->stopping) {
        (void)pthread_mutex_unlock(&transport->lock);
        errno = ENOTCONN;
        return -1;
    }
    (void)pthread_mutex_unlock(&transport->lock);
    if (bbnet_client_send(transport->client, transport->application_protocol,
                          data, length) != 1) {
        errno = EIO;
        return -1;
    }
    return 0;
}

static int send_control(struct bbnet_transport *transport, uint8_t event,
                        uint32_t link_id)
{
    unsigned char frame[CONFLUENCE_CONTROL_FRAME_SIZE];

    if (confluence_encode_control(event, link_id, frame) != 0) {
        errno = EINVAL;
        return -1;
    }
    return send_wire(transport, frame, sizeof(frame));
}

static void process_confluence(struct bbnet_transport *transport,
                               const unsigned char *data, size_t length)
{
    struct confluence_frame frame;
    struct bbnet_link *link;
    ssize_t written;

    if (confluence_parse(data, length, &frame) != 0)
        return;
    (void)pthread_mutex_lock(&transport->lock);
    link = find_link(transport, frame.link_id);
    if (!link) {
        (void)pthread_mutex_unlock(&transport->lock);
        return;
    }
    switch (frame.event) {
    case CONFLUENCE_EVENT_CONNECTED:
        if (link->state == BBNET_LINK_OPENING)
            link->state = BBNET_LINK_OPEN;
        break;
    case CONFLUENCE_EVENT_CONNECT_FAILED:
        link->state = BBNET_LINK_FAILED;
        close_pump_fd(link);
        break;
    case CONFLUENCE_EVENT_CLOSE:
        link->state = BBNET_LINK_CLOSED;
        close_pump_fd(link);
        break;
    case CONFLUENCE_EVENT_DATA:
        if (link->state == BBNET_LINK_OPEN && link->pump_fd >= 0) {
            written = write(link->pump_fd, frame.payload,
                            frame.payload_length);
            if (written < 0 || (size_t)written != frame.payload_length) {
                link->state = BBNET_LINK_FAILED;
                close_pump_fd(link);
            }
        }
        break;
    default:
        break;
    }
    (void)pthread_cond_broadcast(&transport->state_changed);
    (void)pthread_mutex_unlock(&transport->lock);
}

static void process_datagram(struct bbnet_transport *transport,
                             const unsigned char *data, size_t length)
{
    ssize_t written;

    (void)pthread_mutex_lock(&transport->lock);
    if (transport->datagram_fds[0] >= 0) {
        written = write(transport->datagram_fds[0], data, length);
        if ((written < 0 && errno != EAGAIN && errno != EWOULDBLOCK) ||
            (written >= 0 && (size_t)written != length)) {
            close(transport->datagram_fds[0]);
            transport->datagram_fds[0] = -1;
        }
    }
    (void)pthread_mutex_unlock(&transport->lock);
}

static void *pump_main(void *argument)
{
    struct bbnet_transport *transport = argument;
    unsigned char *buffer = malloc(BBNET_MAX_MESSAGE);
    struct timespec interval = {.tv_sec = 0,
                                .tv_nsec = BBNET_PUMP_INTERVAL_NS};
    sigset_t blocked_signals;

    sigemptyset(&blocked_signals);
    sigaddset(&blocked_signals, SIGPIPE);
    (void)pthread_sigmask(SIG_BLOCK, &blocked_signals, NULL);

    if (!buffer) {
        (void)pthread_mutex_lock(&transport->lock);
        transport->failed = true;
        (void)pthread_cond_broadcast(&transport->state_changed);
        (void)pthread_mutex_unlock(&transport->lock);
        return NULL;
    }
    for (;;) {
        int state;

        (void)pthread_mutex_lock(&transport->lock);
        if (transport->stopping) {
            (void)pthread_mutex_unlock(&transport->lock);
            break;
        }
        (void)pthread_mutex_unlock(&transport->lock);

        bbnet_client_update(transport->client);
        state = bbnet_client_state(transport->client);
        (void)pthread_mutex_lock(&transport->lock);
        if (state == BBNET_STATE_ESTABLISHED && !transport->ready) {
            transport->ready = true;
            (void)pthread_cond_broadcast(&transport->state_changed);
        } else if (transport->ready && state == BBNET_STATE_CLOSED) {
            transport->ready = false;
            transport->failed = true;
            fail_all_links_locked(transport);
            (void)pthread_cond_broadcast(&transport->state_changed);
        }
        (void)pthread_mutex_unlock(&transport->lock);

        for (;;) {
            int protocol = -1;
            int length = bbnet_client_receive(transport->client, &protocol,
                                               buffer, BBNET_MAX_MESSAGE);

            (void)protocol;
            if (length == -1)
                break;
            if (length < 0) {
                (void)pthread_mutex_lock(&transport->lock);
                transport->failed = true;
                fail_all_links_locked(transport);
                (void)pthread_cond_broadcast(&transport->state_changed);
                (void)pthread_mutex_unlock(&transport->lock);
                break;
            }
            if (transport->mode == BBNET_TRANSPORT_CONFLUENCE)
                process_confluence(transport, buffer, (size_t)length);
            else
                process_datagram(transport, buffer, (size_t)length);
        }
        (void)nanosleep(&interval, NULL);
    }
    free(buffer);
    return NULL;
}

static void dispose_transport(struct bbnet_transport *transport)
{
    size_t index;

    if (!transport)
        return;
    for (index = 0; index < BBNET_MAX_LINKS; index++)
        close_pump_fd(&transport->links[index]);
    if (transport->datagram_fds[0] >= 0)
        close(transport->datagram_fds[0]);
    if (transport->datagram_fds[1] >= 0)
        close(transport->datagram_fds[1]);
    bbnet_client_destroy(transport->client);
    (void)pthread_cond_destroy(&transport->state_changed);
    (void)pthread_mutex_destroy(&transport->lock);
    free(transport);
}

struct bbnet_transport *bbnet_transport_start(
    const struct bbnet_transport_config *config)
{
    struct bbnet_transport *transport;
    struct timespec deadline;
    int thread_error;
    size_t index;

    if (!config || !config->address || !config->address[0] ||
        config->port == 0 ||
        (config->mode != BBNET_TRANSPORT_CONFLUENCE &&
         config->mode != BBNET_TRANSPORT_DATAGRAM) ||
        config->application_protocol < BBNET_APPLICATION_RAW ||
        config->application_protocol > BBNET_APPLICATION_NACK ||
        (!config->server_parameter && config->server_parameter_length) ||
        (!config->client_parameter && config->client_parameter_length)) {
        errno = EINVAL;
        return NULL;
    }
    transport = calloc(1, sizeof(*transport));
    if (!transport)
        return NULL;
    transport->datagram_fds[0] = -1;
    transport->datagram_fds[1] = -1;
    transport->next_link_id = 1;
    transport->mode = config->mode;
    transport->application_protocol = config->application_protocol;
    for (index = 0; index < BBNET_MAX_LINKS; index++)
        transport->links[index].pump_fd = -1;
    if (pthread_mutex_init(&transport->lock, NULL) != 0) {
        free(transport);
        errno = ENOMEM;
        return NULL;
    }
    if (pthread_cond_init(&transport->state_changed, NULL) != 0) {
        (void)pthread_mutex_destroy(&transport->lock);
        free(transport);
        errno = ENOMEM;
        return NULL;
    }
    transport->client = bbnet_client_create();
    if (!transport->client ||
        (config->mode == BBNET_TRANSPORT_DATAGRAM &&
         make_socket_pair(transport->datagram_fds) != 0) ||
        bbnet_client_connect(
            transport->client, config->address, config->port,
            config->server_parameter, config->server_parameter_length,
            config->client_parameter, config->client_parameter_length,
            config->keepalive_ms > 0 ? config->keepalive_ms : 10000,
            config->connect_timeout_ms > 0 ? config->connect_timeout_ms
                                           : 10000) != 0) {
        dispose_transport(transport);
        errno = ECONNREFUSED;
        return NULL;
    }
    thread_error = pthread_create(&transport->pump_thread, NULL, pump_main,
                                  transport);
    if (thread_error != 0) {
        dispose_transport(transport);
        errno = thread_error;
        return NULL;
    }
    transport->thread_started = true;
    deadline = realtime_deadline(config->connect_timeout_ms > 0
                                     ? config->connect_timeout_ms
                                     : 10000);
    (void)pthread_mutex_lock(&transport->lock);
    while (!transport->ready && !transport->failed) {
        int wait_result = pthread_cond_timedwait(&transport->state_changed,
                                                 &transport->lock, &deadline);

        if (wait_result == ETIMEDOUT)
            break;
    }
    if (!transport->ready) {
        (void)pthread_mutex_unlock(&transport->lock);
        bbnet_transport_destroy(transport);
        errno = ETIMEDOUT;
        return NULL;
    }
    (void)pthread_mutex_unlock(&transport->lock);
    return transport;
}

void bbnet_transport_destroy(struct bbnet_transport *transport)
{
    if (!transport)
        return;
    (void)pthread_mutex_lock(&transport->lock);
    transport->stopping = true;
    transport->ready = false;
    fail_all_links_locked(transport);
    (void)pthread_cond_broadcast(&transport->state_changed);
    (void)pthread_mutex_unlock(&transport->lock);
    if (transport->thread_started)
        (void)pthread_join(transport->pump_thread, NULL);
    bbnet_client_close(transport->client);
    dispose_transport(transport);
}

int bbnet_transport_open_link(struct bbnet_transport *transport,
                              uint32_t *link_id, int *receive_fd,
                              int timeout_ms)
{
    struct bbnet_link *link = NULL;
    struct timespec deadline;
    int pair[2] = {-1, -1};
    int saved_errno = ETIMEDOUT;
    size_t index;

    if (!transport || !link_id || !receive_fd || timeout_ms <= 0 ||
        transport->mode != BBNET_TRANSPORT_CONFLUENCE) {
        errno = EINVAL;
        return -1;
    }
    *link_id = 0;
    *receive_fd = -1;
    if (make_socket_pair(pair) != 0)
        return -1;
    (void)pthread_mutex_lock(&transport->lock);
    if (!transport->ready || transport->failed || transport->stopping) {
        (void)pthread_mutex_unlock(&transport->lock);
        close(pair[0]);
        close(pair[1]);
        errno = ENOTCONN;
        return -1;
    }
    for (index = 0; index < BBNET_MAX_LINKS; index++)
        if (transport->links[index].state == BBNET_LINK_UNUSED) {
            link = &transport->links[index];
            break;
        }
    if (!link) {
        (void)pthread_mutex_unlock(&transport->lock);
        close(pair[0]);
        close(pair[1]);
        errno = ENOSPC;
        return -1;
    }
    do {
        link->id = transport->next_link_id++;
        if (transport->next_link_id == 0)
            transport->next_link_id = 1;
    } while (link->id == 0 || find_link(transport, link->id));
    link->pump_fd = pair[0];
    link->state = BBNET_LINK_OPENING;
    *link_id = link->id;
    (void)pthread_mutex_unlock(&transport->lock);

    if (send_control(transport, CONFLUENCE_EVENT_CONNECT, link->id) != 0) {
        saved_errno = errno;
        goto fail;
    }
    deadline = realtime_deadline(timeout_ms);
    (void)pthread_mutex_lock(&transport->lock);
    while (link->state == BBNET_LINK_OPENING && !transport->failed &&
           !transport->stopping) {
        int wait_result = pthread_cond_timedwait(&transport->state_changed,
                                                 &transport->lock, &deadline);

        if (wait_result == ETIMEDOUT)
            break;
    }
    if (link->state != BBNET_LINK_OPEN) {
        if (link->state == BBNET_LINK_FAILED)
            saved_errno = ECONNREFUSED;
        else if (link->state == BBNET_LINK_CLOSED)
            saved_errno = ECONNRESET;
        (void)pthread_mutex_unlock(&transport->lock);
        goto fail;
    }
    (void)pthread_mutex_unlock(&transport->lock);
    *receive_fd = pair[1];
    return 0;

fail:
    (void)send_control(transport, CONFLUENCE_EVENT_CLOSE, *link_id);
    (void)pthread_mutex_lock(&transport->lock);
    close_pump_fd(link);
    memset(link, 0, sizeof(*link));
    link->pump_fd = -1;
    (void)pthread_mutex_unlock(&transport->lock);
    close(pair[1]);
    *link_id = 0;
    errno = saved_errno;
    return -1;
}

int bbnet_transport_send_link(struct bbnet_transport *transport,
                              uint32_t link_id, const void *data,
                              size_t length)
{
    unsigned char *frame;
    struct bbnet_link *link;
    size_t frame_length;
    int status;

    if (!transport || !link_id || (!data && length) ||
        length > BBNET_MAX_MESSAGE - CONFLUENCE_DATA_HEADER_SIZE) {
        errno = EINVAL;
        return -1;
    }
    (void)pthread_mutex_lock(&transport->lock);
    link = find_link(transport, link_id);
    if (!link || link->state != BBNET_LINK_OPEN) {
        (void)pthread_mutex_unlock(&transport->lock);
        errno = ENOTCONN;
        return -1;
    }
    (void)pthread_mutex_unlock(&transport->lock);
    frame = malloc(CONFLUENCE_DATA_HEADER_SIZE + length);
    if (!frame)
        return -1;
    if (confluence_encode_data(link_id, data, length, frame,
                               CONFLUENCE_DATA_HEADER_SIZE + length,
                               &frame_length) != 0) {
        free(frame);
        errno = EINVAL;
        return -1;
    }
    status = send_wire(transport, frame, frame_length);
    free(frame);
    return status;
}

void bbnet_transport_close_link(struct bbnet_transport *transport,
                                uint32_t link_id)
{
    struct bbnet_link *link;

    if (!transport || !link_id)
        return;
    (void)send_control(transport, CONFLUENCE_EVENT_CLOSE, link_id);
    (void)pthread_mutex_lock(&transport->lock);
    link = find_link(transport, link_id);
    if (link) {
        close_pump_fd(link);
        memset(link, 0, sizeof(*link));
        link->pump_fd = -1;
    }
    (void)pthread_mutex_unlock(&transport->lock);
}

int bbnet_transport_datagram_fd(const struct bbnet_transport *transport)
{
    if (!transport || transport->mode != BBNET_TRANSPORT_DATAGRAM) {
        errno = EINVAL;
        return -1;
    }
    return transport->datagram_fds[1];
}

int bbnet_transport_send_datagram(struct bbnet_transport *transport,
                                  const void *data, size_t length)
{
    if (!transport || transport->mode != BBNET_TRANSPORT_DATAGRAM) {
        errno = EINVAL;
        return -1;
    }
    return send_wire(transport, data, length);
}

int bbnet_transport_state(const struct bbnet_transport *transport)
{
    return transport ? bbnet_client_state(transport->client)
                     : BBNET_STATE_CLOSED;
}

int bbnet_transport_rtt(const struct bbnet_transport *transport)
{
    return transport ? bbnet_client_rtt(transport->client) : -1;
}

int bbnet_transport_statistics(struct bbnet_transport *transport,
                               struct bbnet_statistics *statistics)
{
    if (!transport || !statistics) {
        errno = EINVAL;
        return -1;
    }
    return bbnet_client_statistics(transport->client, statistics);
}
