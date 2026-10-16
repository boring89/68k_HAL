/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "process.h"

#include "hal/cpu.h"
#include "hal/timer.h"

#include "kernel/lib/k_errno.h"
#include "kernel/lib/k_memory.h"
#include "kernel/memory/heap.h"
#include "kernel/net/net.h"
#include "kernel/net/tcp.h"

/*
 * BSD sockets for user programs: AF_INET stream (TCP) and datagram (UDP)
 * sockets as file descriptors. On i386 Linux all socket calls go through
 * the single system call socketcall(call, args) (numbers from
 * linux/net.h); musl uses it. A socket is a vfs file, so read, write,
 * close, poll and dup work on it like on any descriptor.
 *
 * Blocking calls poll the network and sleep (they cannot be interrupted);
 * a socket in non-blocking mode gives -EAGAIN instead of waiting.
 */

#define SYS_SOCKET 1
#define SYS_BIND 2
#define SYS_CONNECT 3
#define SYS_LISTEN 4
#define SYS_ACCEPT 5
#define SYS_GETSOCKNAME 6
#define SYS_GETPEERNAME 7
#define SYS_SEND 9
#define SYS_RECV 10
#define SYS_SENDTO 11
#define SYS_RECVFROM 12
#define SYS_SHUTDOWN 13
#define SYS_SETSOCKOPT 14
#define SYS_GETSOCKOPT 15
#define SYS_SENDMSG 16
#define SYS_RECVMSG 17
#define SYS_ACCEPT4 18

#define AF_INET 2
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_TYPE_MASK 0xFF
#define SOCK_NONBLOCK 04000

#define SOL_SOCKET 1
#define SO_ERROR 4
#define SO_RCVTIMEO 20
#define SO_TYPE 3

#define VFS_S_IFSOCK 0140000u

#define UDP_QUEUE 6
#define UDP_MAX 1500

typedef struct
{
    uint8_t ip[4];
    uint16_t port;
    uint16_t length;
    uint8_t data[UDP_MAX];
} datagram_t;

typedef struct
{
    int type;
    int nonblock;
    uint32_t receive_timeout_ms; /* 0 = wait forever */
    int connection;              /* TCP: connection or listener number, -1 if none */
    int listening;
    uint16_t local_port;         /* 0 = not bound */
    uint8_t remote_ip[4];
    uint16_t remote_port;
    int connected;
    int shutdown_write;
    datagram_t queue[UDP_QUEUE];
    int queue_head;
    int queue_count;
} socket_t;

static const vfs_file_ops_t socket_ops;

/* --- helpers --- */

static uint16_t ephemeral_port = 50000;

/* Poll the network and idle until check() is true. 0 = done, -ETIMEDOUT or -EAGAIN otherwise. */
static int32_t wait_for(socket_t *s, int (*ready)(socket_t *), uint32_t timeout_ms)
{
    uint64_t end = timeout_ms ? hal_timer_ns() + (uint64_t)timeout_ms * 1000000u : 0;

    for (;;)
    {
        net_poll();

        if (ready(s))
            return 0;

        if (s->nonblock)
            return -K_EAGAIN;

        if (end != 0 && hal_timer_ns() >= end)
            return -K_EAGAIN;

        hal_cpu_enable_interrupts();
        hal_cpu_idle();
    }
}

static int tcp_error_to_errno(int error)
{
    switch (error)
    {
    case TCP_ERROR_REFUSED:
        return K_ECONNREFUSED;

    case TCP_ERROR_TIMEOUT:
        return K_ETIMEDOUT;

    default:
        return K_ECONNRESET;
    }
}

static void udp_handler(const uint8_t source_ip[4], uint16_t source_port, const uint8_t *data,
                        size_t length, void *context)
{
    socket_t *s = context;

    if (s->queue_count == UDP_QUEUE || length > UDP_MAX)
        return;

    datagram_t *d = &s->queue[(s->queue_head + s->queue_count++) % UDP_QUEUE];

    k_memcpy(d->ip, source_ip, 4);
    d->port = source_port;
    d->length = (uint16_t)length;
    k_memcpy(d->data, data, length);
}

static int32_t udp_bind(socket_t *s, uint16_t port)
{
    if (port == 0)
    {
        port = ephemeral_port++;

        if (ephemeral_port < 50000)
            ephemeral_port = 50000;
    }

    if (net_udp_listen(port, udp_handler, s) != 0)
        return -K_EADDRINUSE;

    s->local_port = port;

    return 0;
}

/* --- reading and writing --- */

static int has_data(socket_t *s)
{
    if (s->type == SOCK_DGRAM)
        return s->queue_count > 0;

    tcp_stack_t *tcp = net_tcp();

    if (s->listening)
        return 0;

    return tcp_receive_available(tcp, s->connection) > 0 || tcp_error(tcp, s->connection) != 0 ||
           tcp_state(tcp, s->connection) == TCP_CLOSE_WAIT || tcp_state(tcp, s->connection) == TCP_LAST_ACK ||
           tcp_state(tcp, s->connection) == TCP_CLOSED || tcp_state(tcp, s->connection) == TCP_TIME_WAIT ||
           tcp_state(tcp, s->connection) == TCP_CLOSING;
}

static int32_t recv_data(socket_t *s, void *buffer, uint32_t length, uint8_t from_ip[4], uint16_t *from_port)
{
    if (s->type == SOCK_DGRAM)
    {
        int32_t status = wait_for(s, has_data, s->receive_timeout_ms);

        if (status != 0)
            return status;

        datagram_t *d = &s->queue[s->queue_head];
        uint32_t n = d->length < length ? d->length : length;

        k_memcpy(buffer, d->data, n);

        if (from_ip != NULL)
        {
            k_memcpy(from_ip, d->ip, 4);
            *from_port = d->port;
        }

        s->queue_head = (s->queue_head + 1) % UDP_QUEUE;
        s->queue_count--;

        return (int32_t)n;
    }

    if (s->connection < 0 || s->listening || !s->connected)
        return -K_ENOTCONN;

    int32_t status = wait_for(s, has_data, s->receive_timeout_ms);

    if (status != 0)
        return status;

    int n = tcp_recv(net_tcp(), s->connection, buffer, length);

    if (n == TCP_EOF)
        return 0;

    if (n < 0)
        return -tcp_error_to_errno(tcp_error(net_tcp(), s->connection));

    if (from_ip != NULL)
    {
        k_memcpy(from_ip, s->remote_ip, 4);
        *from_port = s->remote_port;
    }

    return n;
}

static int can_send(socket_t *s)
{
    tcp_stack_t *tcp = net_tcp();

    return tcp_send_space(tcp, s->connection) > 0 || tcp_error(tcp, s->connection) != 0 ||
           tcp_state(tcp, s->connection) == TCP_CLOSED;
}

static int32_t send_data(socket_t *s, const void *data, uint32_t length, const uint8_t *to_ip, uint16_t to_port)
{
    if (s->type == SOCK_DGRAM)
    {
        if (to_ip == NULL && !s->connected)
            return -K_EDESTADDRREQ;

        if (length > UDP_MAX - 100)
            return -K_EMSGSIZE;

        if (s->local_port == 0)
        {
            int32_t status = udp_bind(s, 0);

            if (status != 0)
                return status;
        }

        if (net_udp_send(to_ip != NULL ? to_ip : s->remote_ip, to_ip != NULL ? to_port : s->remote_port,
                         s->local_port, data, length) != 0)
            return -K_ENETUNREACH;

        return (int32_t)length;
    }

    if (s->connection < 0 || s->listening || !s->connected)
        return -K_ENOTCONN;

    if (s->shutdown_write)
        return -K_EPIPE;

    /* A stream send waits until everything is queued (like a blocking write). */
    uint32_t done = 0;

    while (done < length)
    {
        int n = tcp_send(net_tcp(), s->connection, (const uint8_t *)data + done, length - done);

        if (n < 0)
            return done > 0 ? (int32_t)done : -K_EPIPE;

        done += (uint32_t)n;

        if (done < length)
        {
            int32_t status = wait_for(s, can_send, 0);

            if (status != 0)
                return done > 0 ? (int32_t)done : status;
        }
    }

    return (int32_t)done;
}

static int32_t socket_read(vfs_file_t *file, uint64_t pos, void *buffer, uint32_t count)
{
    (void)pos;

    return recv_data(file->priv, buffer, count, NULL, NULL);
}

static int32_t socket_write(vfs_file_t *file, uint64_t pos, const void *buffer, uint32_t count)
{
    (void)pos;

    return send_data(file->priv, buffer, count, NULL, 0);
}

static uint32_t socket_poll(vfs_file_t *file, uint32_t events)
{
    socket_t *s = file->priv;
    uint32_t ready = 0;

    net_poll();

    if (s->listening)
    {
        /* Readable when a connection waits to be accepted. */
        tcp_stack_t *tcp = net_tcp();

        for (int i = 0; i < TCP_MAX_CONNECTIONS; i++)
        {
            if (tcp->connection[i].used && tcp->connection[i].parent == s->connection &&
                tcp->connection[i].state != TCP_SYN_RECEIVED)
                ready |= VFS_POLLIN;
        }

        return ready & events;
    }

    if (has_data(s) && (s->type == SOCK_DGRAM || s->connected))
        ready |= VFS_POLLIN;

    if (s->type == SOCK_DGRAM)
        ready |= VFS_POLLOUT;
    else if (s->connected && tcp_send_space(net_tcp(), s->connection) > 0)
        ready |= VFS_POLLOUT;

    if (s->type == SOCK_STREAM && s->connection >= 0 && tcp_error(net_tcp(), s->connection) != 0)
        ready |= VFS_POLLERR | VFS_POLLHUP;

    return ready & (events | VFS_POLLERR | VFS_POLLHUP);
}

static int32_t socket_stat(vfs_file_t *file, vfs_stat_t *stat)
{
    (void)file;
    stat->mode = VFS_S_IFSOCK | 0777;
    stat->size = 0;
    stat->inode = 7;
    stat->mtime = 0;

    return 0;
}

static void socket_close(vfs_file_t *file)
{
    socket_t *s = file->priv;

    if (s->type == SOCK_DGRAM)
    {
        if (s->local_port != 0)
            net_udp_close(s->local_port);
    }
    else if (s->connection >= 0)
    {
        if (s->listening)
            tcp_abort(net_tcp(), s->connection);
        else
            net_tcp_close(s->connection);
    }

    kfree(s);
}

static const vfs_file_ops_t socket_ops = {
    .read = socket_read,
    .write = socket_write,
    .stat = socket_stat,
    .poll = socket_poll,
    .close = socket_close,
};

/* --- the socket calls --- */

static socket_t *socket_of(int fd)
{
    vfs_file_t *file = user_fd_get(fd);

    return file != NULL && file->ops == &socket_ops ? file->priv : NULL;
}

/* struct sockaddr_in: u16 family, u16 port (big endian), 4 bytes address, 8 bytes padding. */
static int32_t read_address(uint32_t address, uint32_t length, uint8_t ip[4], uint16_t *port)
{
    uint8_t sockaddr[16];

    if (length < 8 || !user_range_ok(address, 8))
        return -K_EFAULT;

    k_memcpy(sockaddr, (const void *)(uintptr_t)address, 8);

    if (sockaddr[0] != AF_INET || sockaddr[1] != 0)
        return -K_EAFNOSUPPORT;

    *port = (uint16_t)((sockaddr[2] << 8) | sockaddr[3]);
    k_memcpy(ip, sockaddr + 4, 4);

    return 0;
}

static int32_t write_address(uint32_t address, uint32_t length_pointer, const uint8_t ip[4], uint16_t port)
{
    uint8_t sockaddr[16];
    uint32_t length = 16;

    if (address == 0 || length_pointer == 0)
        return 0;

    k_memset(sockaddr, 0, sizeof(sockaddr));
    sockaddr[0] = AF_INET;
    sockaddr[2] = (uint8_t)(port >> 8);
    sockaddr[3] = (uint8_t)port;
    k_memcpy(sockaddr + 4, ip, 4);

    if (!user_range_ok(length_pointer, 4))
        return -K_EFAULT;

    uint32_t room = *(uint32_t *)(uintptr_t)length_pointer;

    if (room < length)
        length = room;

    if (!user_range_ok(address, length))
        return -K_EFAULT;

    k_memcpy((void *)(uintptr_t)address, sockaddr, length);
    *(uint32_t *)(uintptr_t)length_pointer = 16;

    return 0;
}

static int32_t sock_socket(uint32_t domain, uint32_t type, uint32_t protocol)
{
    (void)protocol;

    if (domain != AF_INET)
        return -K_EAFNOSUPPORT;

    uint32_t kind = type & SOCK_TYPE_MASK;

    if (kind != SOCK_STREAM && kind != SOCK_DGRAM)
        return -K_EPROTONOSUPPORT;

    socket_t *s = kmalloc(sizeof(*s));

    if (s == NULL)
        return -K_ENOMEM;

    k_memset(s, 0, sizeof(*s));
    s->type = (int)kind;
    s->nonblock = (type & SOCK_NONBLOCK) != 0;
    s->connection = -1;

    vfs_file_t *file = vfs_file_new(&socket_ops, s, VFS_O_RDWR);

    if (file == NULL)
    {
        kfree(s);

        return -K_ENOMEM;
    }

    file->seekable = 0;

    int32_t fd = user_fd_install(file, 0);

    if (fd < 0)
        vfs_file_close(file);

    return fd;
}

static int32_t sock_bind(int fd, uint32_t address, uint32_t length)
{
    socket_t *s = socket_of(fd);
    uint8_t ip[4];
    uint16_t port;

    if (s == NULL)
        return -K_ENOTSOCK;

    int32_t status = read_address(address, length, ip, &port);

    if (status != 0)
        return status;

    if (s->type == SOCK_DGRAM)
        return udp_bind(s, port);

    s->local_port = port;

    return 0;
}

static int32_t sock_connect(int fd, uint32_t address, uint32_t length)
{
    socket_t *s = socket_of(fd);
    uint8_t ip[4];
    uint16_t port;

    if (s == NULL)
        return -K_ENOTSOCK;

    int32_t status = read_address(address, length, ip, &port);

    if (status != 0)
        return status;

    if (s->connected)
        return -K_EISCONN;

    k_memcpy(s->remote_ip, ip, 4);
    s->remote_port = port;

    if (s->type == SOCK_DGRAM)
    {
        s->connected = 1;

        return 0;
    }

    int connection = net_tcp_connect(ip, port, 10000);

    if (connection < 0)
        return -K_ECONNREFUSED;

    s->connection = connection;
    s->connected = 1;

    return 0;
}

static int accept_ready(socket_t *s)
{
    tcp_stack_t *tcp = net_tcp();

    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++)
    {
        if (tcp->connection[i].used && tcp->connection[i].parent == s->connection &&
            tcp->connection[i].state != TCP_SYN_RECEIVED)
            return 1;
    }

    return 0;
}

static int32_t sock_listen(int fd)
{
    socket_t *s = socket_of(fd);

    if (s == NULL)
        return -K_ENOTSOCK;

    if (s->type != SOCK_STREAM)
        return -K_EOPNOTSUPP;

    int listener = tcp_listen(net_tcp(), s->local_port);

    if (listener < 0)
        return -K_EADDRINUSE;

    s->connection = listener;
    s->listening = 1;

    return 0;
}

static int32_t sock_accept(int fd, uint32_t address, uint32_t length_pointer)
{
    socket_t *s = socket_of(fd);

    if (s == NULL)
        return -K_ENOTSOCK;

    if (!s->listening)
        return -K_EINVAL;

    int32_t status = wait_for(s, accept_ready, 0);

    if (status != 0)
        return status;

    int child = tcp_accept(net_tcp(), s->connection);

    if (child < 0)
        return -K_EAGAIN;

    /* The new socket is a fresh descriptor for that connection. */
    int32_t new_fd = sock_socket(AF_INET, SOCK_STREAM, 0);

    if (new_fd < 0)
    {
        tcp_abort(net_tcp(), child);

        return new_fd;
    }

    socket_t *n = socket_of(new_fd);
    const tcp_connection_t *c = &net_tcp()->connection[child];

    n->connection = child;
    n->connected = 1;
    n->local_port = c->local_port;
    k_memcpy(n->remote_ip, c->remote_ip, 4);
    n->remote_port = c->remote_port;
    write_address(address, length_pointer, c->remote_ip, c->remote_port);

    return new_fd;
}

static int32_t sock_name(int fd, uint32_t address, uint32_t length_pointer, int peer)
{
    socket_t *s = socket_of(fd);

    if (s == NULL)
        return -K_ENOTSOCK;

    if (peer)
    {
        if (!s->connected)
            return -K_ENOTCONN;

        return write_address(address, length_pointer, s->remote_ip, s->remote_port);
    }

    return write_address(address, length_pointer, net_config()->ip, s->local_port);
}

static int32_t sock_sendto(int fd, uint32_t buffer, uint32_t length, uint32_t address, uint32_t address_length)
{
    socket_t *s = socket_of(fd);
    uint8_t ip[4];
    uint16_t port = 0;

    if (s == NULL)
        return -K_ENOTSOCK;

    if (!user_range_ok(buffer, length))
        return -K_EFAULT;

    if (address != 0)
    {
        int32_t status = read_address(address, address_length, ip, &port);

        if (status != 0)
            return status;
    }

    return send_data(s, (const void *)(uintptr_t)buffer, length, address != 0 ? ip : NULL, port);
}

static int32_t sock_recvfrom(int fd, uint32_t buffer, uint32_t length, uint32_t address, uint32_t length_pointer)
{
    socket_t *s = socket_of(fd);
    uint8_t ip[4];
    uint16_t port = 0;

    if (s == NULL)
        return -K_ENOTSOCK;

    if (!user_range_ok(buffer, length))
        return -K_EFAULT;

    int32_t n = recv_data(s, (void *)(uintptr_t)buffer, length, ip, &port);

    if (n >= 0 && address != 0)
        write_address(address, length_pointer, ip, port);

    return n;
}

static int32_t sock_shutdown(int fd, uint32_t how)
{
    socket_t *s = socket_of(fd);

    if (s == NULL)
        return -K_ENOTSOCK;

    if (s->type == SOCK_STREAM && s->connected && (how == 1 || how == 2) && !s->shutdown_write)
    {
        s->shutdown_write = 1;
        tcp_close(net_tcp(), s->connection);
    }

    return 0;
}

static int32_t sock_setsockopt(int fd, uint32_t level, uint32_t name, uint32_t value, uint32_t length)
{
    socket_t *s = socket_of(fd);

    if (s == NULL)
        return -K_ENOTSOCK;

    /* Receive timeout: struct timeval {sec, usec}. Everything else is accepted and ignored. */
    if (level == SOL_SOCKET && name == SO_RCVTIMEO && length >= 8 && user_range_ok(value, 8))
    {
        const uint32_t *tv = (const uint32_t *)(uintptr_t)value;

        s->receive_timeout_ms = tv[0] * 1000u + tv[1] / 1000u;
    }

    return 0;
}

static int32_t sock_getsockopt(int fd, uint32_t level, uint32_t name, uint32_t value, uint32_t length_pointer)
{
    socket_t *s = socket_of(fd);
    int32_t result = 0;

    if (s == NULL)
        return -K_ENOTSOCK;

    if (level == SOL_SOCKET && name == SO_TYPE)
        result = s->type;
    else if (level == SOL_SOCKET && name == SO_ERROR && s->type == SOCK_STREAM && s->connection >= 0)
        result = tcp_error(net_tcp(), s->connection) ? tcp_error_to_errno(tcp_error(net_tcp(), s->connection)) : 0;

    if (value == 0 || length_pointer == 0 || !user_range_ok(value, 4) || !user_range_ok(length_pointer, 4))
        return -K_EFAULT;

    *(int32_t *)(uintptr_t)value = result;
    *(uint32_t *)(uintptr_t)length_pointer = 4;

    return 0;
}

/* struct msghdr (i386): name, namelen, iov, iovlen, control, controllen, flags; struct iovec: base, len. */
static int32_t sock_message(int fd, uint32_t msg_address, int sending)
{
    uint32_t msg[7];
    socket_t *s = socket_of(fd);

    if (s == NULL)
        return -K_ENOTSOCK;

    if (!user_range_ok(msg_address, sizeof(msg)))
        return -K_EFAULT;

    k_memcpy(msg, (const void *)(uintptr_t)msg_address, sizeof(msg));

    uint32_t iov_count = msg[3];

    if (iov_count == 0 || iov_count > 16 || !user_range_ok(msg[2], iov_count * 8))
        return -K_EINVAL;

    const uint32_t *iov = (const uint32_t *)(uintptr_t)msg[2];

    if (sending)
    {
        static uint8_t gather[UDP_MAX];
        uint32_t total = 0;
        uint8_t ip[4];
        uint16_t port = 0;

        for (uint32_t i = 0; i < iov_count; i++)
        {
            if (!user_range_ok(iov[2 * i], iov[2 * i + 1]) || total + iov[2 * i + 1] > sizeof(gather))
                return -K_EMSGSIZE;

            k_memcpy(gather + total, (const void *)(uintptr_t)iov[2 * i], iov[2 * i + 1]);
            total += iov[2 * i + 1];
        }

        if (msg[0] != 0)
        {
            int32_t status = read_address(msg[0], msg[1], ip, &port);

            if (status != 0)
                return status;
        }

        return send_data(s, gather, total, msg[0] != 0 ? ip : NULL, port);
    }

    /* Receiving: into the first buffer that has room (datagrams and short stream reads). */
    uint8_t ip[4];
    uint16_t port = 0;

    if (!user_range_ok(iov[0], iov[1]))
        return -K_EFAULT;

    int32_t n = recv_data(s, (void *)(uintptr_t)iov[0], iov[1], ip, &port);

    if (n >= 0 && msg[0] != 0)
    {
        uint32_t length = msg[1];

        if (length > 16)
            length = 16;

        uint8_t sockaddr[16] = {AF_INET, 0, (uint8_t)(port >> 8), (uint8_t)port, ip[0], ip[1], ip[2], ip[3]};

        if (user_range_ok(msg[0], length))
            k_memcpy((void *)(uintptr_t)msg[0], sockaddr, length);

        ((uint32_t *)(uintptr_t)msg_address)[1] = 16;
    }

    return n;
}

void user_socket_set_nonblock(vfs_file_t *file, int enabled)
{
    if (file->ops == &socket_ops)
        ((socket_t *)file->priv)->nonblock = enabled;
}

int32_t user_socketcall(uint32_t call, uint32_t args_address)
{
    static const uint8_t arg_count[] = {0, 3, 3, 3, 2, 3, 3, 3, 0, 4, 4, 6, 6, 2, 5, 5, 3, 3, 4};
    uint32_t a[6] = {0, 0, 0, 0, 0, 0};

    if (call < 1 || call >= sizeof(arg_count) || arg_count[call] == 0)
        return -K_EINVAL;

    if (!user_range_ok(args_address, arg_count[call] * 4u))
        return -K_EFAULT;

    k_memcpy(a, (const void *)(uintptr_t)args_address, arg_count[call] * 4u);

    switch (call)
    {
    case SYS_SOCKET:
        return sock_socket(a[0], a[1], a[2]);

    case SYS_BIND:
        return sock_bind((int)a[0], a[1], a[2]);

    case SYS_CONNECT:
        return sock_connect((int)a[0], a[1], a[2]);

    case SYS_LISTEN:
        return sock_listen((int)a[0]);

    case SYS_ACCEPT:
    case SYS_ACCEPT4:
        return sock_accept((int)a[0], a[1], a[2]);

    case SYS_GETSOCKNAME:
        return sock_name((int)a[0], a[1], a[2], 0);

    case SYS_GETPEERNAME:
        return sock_name((int)a[0], a[1], a[2], 1);

    case SYS_SEND:
        return sock_sendto((int)a[0], a[1], a[2], 0, 0);

    case SYS_RECV:
        return sock_recvfrom((int)a[0], a[1], a[2], 0, 0);

    case SYS_SENDTO:
        return sock_sendto((int)a[0], a[1], a[2], a[4], a[5]);

    case SYS_RECVFROM:
        return sock_recvfrom((int)a[0], a[1], a[2], a[4], a[5]);

    case SYS_SHUTDOWN:
        return sock_shutdown((int)a[0], a[1]);

    case SYS_SETSOCKOPT:
        return sock_setsockopt((int)a[0], a[1], a[2], a[3], a[4]);

    case SYS_GETSOCKOPT:
        return sock_getsockopt((int)a[0], a[1], a[2], a[3], a[4]);

    case SYS_SENDMSG:
        return sock_message((int)a[0], a[1], 1);

    case SYS_RECVMSG:
        return sock_message((int)a[0], a[1], 0);

    default:
        return -K_EINVAL;
    }
}
