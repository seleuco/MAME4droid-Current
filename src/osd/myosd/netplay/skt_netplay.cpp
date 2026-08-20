// license:BSD-3-Clause
// copyright-holders: David Valdeita (Seleuco)
/***************************************************************************

    skt_netplay.cpp

    UDP transport for netplay.h/netplay.cpp: one non-blocking datagram
    socket, a receive thread, and the read_pkt_data/send_pkt_data callbacks
    netplay_t is wired with (see skt_netplay_init).  No netplay protocol
    knowledge lives here beyond wire framing (netplay_msg_wire_size); frame
    ordering, prediction and rollback are all netplay.cpp's concern.

    Layout, most central first:
      1. Session entry point (the trunk): skt_netplay_init
      2. Network thread: skt_threaded_data
      3. Packet I/O callbacks: skt_read_pkt_data / skt_send_pkt_data
      4. Socket/session setup helpers

***************************************************************************/

#include <sys/socket.h>
#include <netdb.h>

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <errno.h>

#include <signal.h>

#include <unistd.h>

#include <pthread.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cstddef>

#ifndef ANDROID
#include <ifaddrs.h>
#else
//#include <ifaddrs-android.h>
#endif

#include "netplay.h"
#include "skt_netplay.h"
#include <android/log.h>

#define NLOG(...) do { if(NETPLAY_LOG_ENABLED) __android_log_print(ANDROID_LOG_DEBUG, "MAME4droid_Netplay", __VA_ARGS__); } while(0)
#define NLOG_VERBOSE(...) do { if(0) __android_log_print(ANDROID_LOG_DEBUG, "MAME4droid_Netplay", __VA_ARGS__); } while(0)

static skt_netplay_t skt_netplay_impl;   /* single-session singleton, see skt_get_handle_impl */
static pthread_t main_tid;               /* network thread, created detached in skt_netplay_init */

/* Internet-play config/results.  punch host/port/dirty are shared between
 * the Java threads (setters) and the network thread (consumer) -> mutex;
 * the STUN result statics are only written during init, before the network
 * thread exists, and read after init returns (same Java thread).          */
static pthread_mutex_t s_inet_mutex = PTHREAD_MUTEX_INITIALIZER;
static char     s_punch_host[128] = "";
static uint16_t s_punch_port = 0;
static int      s_punch_dirty = 0;
static int      s_internet_mode = 0;
static int      s_ip_mode = SKT_IPPROTO_V4; /* protocol family for the next init */
static uint16_t s_local_bind_port = 0;    /* client bind; 0 = destination port */
static char     s_public_addr[160] = "";  /* "ip:port|pp=X|sym=Y[|alt=ip4:port]" ("[ip]:port" on v6) */
static char     s_public_ip[64]   = "";
static uint16_t s_public_port = 0;
static int      s_stun_ok = 0, s_stun_pp = 0, s_stun_sym = 0;
static char     s_diagnostics[512] = "";

/* Forward declarations: skt_netplay_init (right below) is the entry point
 * and is defined before the helpers it calls, so this section reads
 * top-to-bottom as trunk first, then helpers in call order. */
static skt_netplay_t * skt_get_handle_impl();
static int skt_init_handle_impl(skt_netplay_t *impl);
static int skt_init_udp_socket(netplay_t *handle, const char *server, uint16_t port);
static int skt_read_pkt_data(netplay_t *handle, netplay_msg_t *msg);
static int skt_send_pkt_data(netplay_t *handle, netplay_msg_t *msg);
static void* skt_threaded_data(void* args);
static size_t netplay_msg_wire_size(uint32_t msg_type);
static void skt_run_stun(skt_netplay_t *impl, uint16_t local_port);
static void skt_resolve_punch(skt_netplay_t *impl, const char *host, uint16_t port, int numeric_only);
static void skt_format_diagnostics(uint16_t local_port, int inet_mode, const char *punch_host, uint16_t punch_port);
static void skt_addr_to_str(const struct sockaddr *sa, char *buf, size_t len);

/* ============================================================
 * SECTION 1 -- Session entry point (the trunk)
 * ============================================================ */

/* Create the UDP socket for this session, wire netplay_t's read/send
 * callbacks to it, and spawn the detached network thread. */
int skt_netplay_init(netplay_t *handle,const char *server, uint16_t port, void (*warn_cb)(char *))
{
    int res = 0;

    skt_netplay_t *impl = skt_get_handle_impl();

    NLOG("Init Netplay %s %d",server,port);

    if(impl->fd != -1)
    {
        usleep(1000 * 1000);//Thread?
        close(impl->fd );//anyway
    }
    /* Client sessions keep impl->addr alive for sendto: release the previous
     * one here (old thread is gone by now) before memset loses the pointer. */
    if (impl->addr)
        freeaddrinfo(impl->addr);

    skt_init_handle_impl(impl);

    netplay_init_handle(handle);

    handle->impl_data = impl;
    handle->read_pkt_data = skt_read_pkt_data;
    handle->send_pkt_data = skt_send_pkt_data;
    handle->netplay_warn = warn_cb;

    handle->player1 = server ? 0 : 1;
    handle->type = NETPLAY_TYPE_SKT;

    if (!skt_init_udp_socket(handle, server, port))
        return 0;

    /* Internet play: STUN and the initial punch-target resolve run HERE,
     * strictly before the network thread exists -- once it is select()ing
     * it would steal the STUN replies.  Hostnames are allowed only in this
     * initial resolve (worker thread); the hot path is numeric-only.      */
    {
        int inet_mode;
        char ph[128]; uint16_t pp;
        pthread_mutex_lock(&s_inet_mutex);
        inet_mode = s_internet_mode;
        strncpy(ph, s_punch_host, sizeof(ph)); ph[sizeof(ph)-1] = 0;
        pp = s_punch_port ? s_punch_port : port;
        s_punch_dirty = 0;  /* consumed here; the network thread starts clean */
        pthread_mutex_unlock(&s_inet_mutex);

        s_public_addr[0] = 0; s_public_ip[0] = 0; s_diagnostics[0] = 0;
        s_public_port = 0; s_stun_ok = s_stun_pp = s_stun_sym = 0;

        uint16_t local_port = port;
        {
            struct sockaddr_storage sn; socklen_t sl = sizeof(sn);
            if (getsockname(impl->fd, (struct sockaddr*)&sn, &sl) == 0)
                local_port = ntohs(sn.ss_family == AF_INET6
                        ? ((struct sockaddr_in6 *)&sn)->sin6_port
                        : ((struct sockaddr_in *)&sn)->sin_port);
        }

        if (inet_mode)
            skt_run_stun(impl, local_port);

        if (!server && ph[0])
            skt_resolve_punch(impl, ph, pp, 0);

        skt_format_diagnostics(local_port, inet_mode, ph, pp);
    }

    handle->has_connection = 1;

    /* Create the thread as detached so it self-cleans on exit and we
     * never leak a joinable thread handle across successive skt_netplay_init
     * calls. */
    {
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        res = pthread_create(&main_tid, &attr, skt_threaded_data, (void *)handle);
        pthread_attr_destroy(&attr);
    }
    if(res!=0)
    {
        NLOG("Error setting creating pthread %d",res);
        close(impl->fd);
        impl->fd = -1;
        return 0;
    }

    NLOG("Conexion creada OK!");

    return 1;
}

/* ============================================================
 * SECTION 2 -- Network thread
 * ============================================================ */

/* Runs for the life of handle->has_connection: blocks in select() for an
 * incoming datagram, then drains every packet already queued in the kernel
 * buffer (via netplay_read_data) before going back to sleep, so a burst
 * doesn't pile up latency waiting for repeated wakeups. */
static void* skt_threaded_data(void* args)
{
    fd_set fds;
    struct timeval tv = {0};
    tv.tv_sec = 0;
    //tv.tv_usec = 1;
    tv.tv_usec = 500 * 1000;

    struct timeval tmp_tv = tv;

    netplay_t *handle = (netplay_t *)args;
    skt_netplay_t *impl = (skt_netplay_t *)handle->impl_data;

    uint32_t last_punch_ms = 0;

    /* Ours. A new session may replace impl->fd, and closing that one on the
     * way out would pull the socket from under it. */
    int my_fd = impl->fd;

    NLOG("Creada threaded_data");

    while(handle->has_connection){

        /* Once per pass: a new session can close it and set -1 while we sleep
         * in select(), and FD_ISSET(-1) aborts in bionic. */
        int fd = impl->fd;
        if (fd < 0)
            break;

        /* Internet play: consume hot punch-target updates and send a PUNCH
         * probe every ~500ms until the peer is latched (host) / joined
         * (client).  Checked on EVERY pass so packet bursts can't starve
         * it; resolve runs numeric-only here so this thread never blocks. */
        {
            struct timespec pts;
            clock_gettime(CLOCK_MONOTONIC, &pts);
            uint32_t now_ms = (uint32_t)(pts.tv_sec * 1000 + pts.tv_nsec / 1000000);

            int dirty = 0; char ph[128]; uint16_t pp = 0;
            pthread_mutex_lock(&s_inet_mutex);
            if (s_punch_dirty) {
                dirty = 1;
                strncpy(ph, s_punch_host, sizeof(ph)); ph[sizeof(ph)-1] = 0;
                pp = s_punch_port;
                s_punch_dirty = 0;
            }
            pthread_mutex_unlock(&s_inet_mutex);
            if (dirty) {
                if (ph[0] && pp)
                    skt_resolve_punch(impl, ph, pp, 1);
                else
                    impl->has_punch_addr = 0;
            }

            if (now_ms - last_punch_ms >= 500) {
                last_punch_ms = now_ms;
                const struct sockaddr *pa = NULL; socklen_t pal = 0;
                if (handle->player1) {
                    if (impl->has_punch_addr && !impl->has_client_addr && !handle->has_begun_game) {
                        pa = (const struct sockaddr*)&impl->punch_addr;
                        pal = impl->punch_addr_len;
                    }
                } else {
                    if (impl->addr && !handle->has_joined) {
                        pa = impl->addr->ai_addr;
                        pal = impl->addr->ai_addrlen;
                    }
                }
                if (pa) {
                    netplay_msg_t pm;
                    pm.packetid = 0;
                    pm.msg_type = htonl(NETPLAY_MSG_PUNCH);
                    if (sendto(fd, &pm, netplay_msg_wire_size(NETPLAY_MSG_PUNCH), 0, pa, pal) < 0)
                        NLOG("punch sendto failed (%s), ignored", strerror(errno));
                    else
                        NLOG_VERBOSE("punch sent");
                }
            }
        }

        tmp_tv = tv;

        FD_ZERO(&fds);
        FD_SET(fd, &fds);

        if (select(fd + 1, &fds, NULL, NULL, &tmp_tv) < 0)
        {
            NLOG("select failed: %s", strerror(errno));
            if (handle->has_connection) {
                handle->has_connection = 0;
                netplay_warn_hangup(handle);
            }
            /* Wake the game thread if it is sleeping in the cond wait
             * so it sees has_connection==0 immediately instead of waiting
             * up to SYNC_TIMEOUT_MS (30 s) for the timedwait to expire.  */
            pthread_mutex_lock(&handle->sync_mutex);
            pthread_cond_broadcast(&handle->sync_cond);
            pthread_mutex_unlock(&handle->sync_mutex);
            break;
        }

        if (FD_ISSET(fd, &fds)) // packet arrive
        {
            NLOG_VERBOSE("select unblocked, packet arrived!");

            /* Drain loop: Consume ALL pending packets in the kernel buffer
             * to minimize latency and ensure state freshness. */
            while (1) {
                if(!netplay_read_data(handle))
                {
                    /* Wake up the game thread on recv/parse failure. */
                    pthread_mutex_lock(&handle->sync_mutex);
                    pthread_cond_broadcast(&handle->sync_cond);
                    pthread_mutex_unlock(&handle->sync_mutex);
                    break;
                }

                /* Check if more data is available without blocking */
                fd_set drain_fds;
                struct timeval zero_tv = {0, 0};
                FD_ZERO(&drain_fds);
                FD_SET(fd, &drain_fds);
                if (select(fd + 1, &drain_fds, NULL, NULL, &zero_tv) <= 0)
                    break; /* No more packets immediately available */
                if (!FD_ISSET(fd, &drain_fds))
                    break;
            }
        }
    }

    if (my_fd >= 0 && impl->fd == my_fd) {
        close(my_fd);
        impl->fd = -1;
    } else {
        NLOG("threaded_data exits: fd %d already replaced/closed, leaving impl->fd %d alone", my_fd, impl->fd);
    }

    NLOG("Muere threaded_data y cierro socket!");

	return 0;
}

/* ============================================================
 * SECTION 3 -- Packet I/O callbacks
 * Wired into netplay_t as read_pkt_data/send_pkt_data (skt_netplay_init);
 * netplay.cpp calls them without knowing this is UDP underneath.
 * ============================================================ */

/* Wire size for a given (host-order) msg_type: header + only that type's own
 * union member, instead of always the full netplay_msg_t (which is padded to
 * the size of the LARGEST member).  Protocol v8. */
static size_t netplay_msg_wire_size(uint32_t msg_type)
{
    size_t hdr = offsetof(netplay_msg_t, u);
    switch (msg_type) {
        case NETPLAY_MSG_DATA:          return hdr + sizeof(netplay_msg_data_t);
        case NETPLAY_MSG_JOIN:
        case NETPLAY_MSG_JOIN_ACK:      return hdr + sizeof(netplay_msg_join_t);
        case NETPLAY_MSG_STATE_CHUNK:   return hdr + sizeof(netplay_msg_state_chunk_t);
        case NETPLAY_MSG_STATE_ACK:     return hdr + sizeof(netplay_msg_state_ack_t);
        case NETPLAY_MSG_STATE_SIZE:    return hdr + sizeof(netplay_msg_state_size_t);
        case NETPLAY_MSG_ITEMCRC_CHUNK: return hdr + sizeof(netplay_msg_itemcrc_chunk_t);
        case NETPLAY_MSG_DISCONNECT:
        case NETPLAY_MSG_READY:
        case NETPLAY_MSG_RESYNC:
        case NETPLAY_MSG_PUNCH:         return hdr;   /* no payload used     */
        default:                        return sizeof(netplay_msg_t); /* unknown type: safe fallback */
    }
}

/* recvfrom() one datagram and validate it.  Returns 0 = fatal socket error,
 * 1 = valid packet, 2 = foreign datagram dropped (internet scanners, stray
 * STUN replies): no hangup, no latch, no session state touched.  Pre-game,
 * the sender is latched/re-latched as our peer address. */
static int skt_read_pkt_data(netplay_t *handle,netplay_msg_t *msg)
{
    skt_netplay_t *impl = (skt_netplay_t *)handle->impl_data;
    socklen_t addrlen = sizeof(impl->client_addr);
    struct sockaddr_storage client_addr;

    int l = recvfrom(impl->fd, msg,  sizeof(netplay_msg_t), 0, (struct sockaddr*)&client_addr, &addrlen);

    if (l < 0)
    {
        NLOG("recvfrom failed. error=%s", strerror(errno));
        if (handle->has_connection) {
            handle->has_connection = 0;
            netplay_warn_hangup(handle);
        }
        return 0;
    }

    if (l < (int)offsetof(netplay_msg_t, u))
    {
        NLOG("dropped runt datagram l=%d", l);
        return 2;
    }

    uint32_t mt = ntohl(msg->msg_type);
    if (mt < NETPLAY_MSG_DATA || mt > NETPLAY_MSG_PUNCH)
    {
        NLOG("dropped foreign datagram msg_type=%u l=%d", mt, l);
        return 2;
    }

    /* Each message type is sent at its own (smaller) wire size, not padded
     * to sizeof(netplay_msg_t) -- shorter than its own type's size means
     * truncated/corrupt/foreign, so it is dropped, never a hangup.         */
    size_t expected = netplay_msg_wire_size(mt);
    if ((size_t)l < expected)
    {
        NLOG("dropped short packet msg_type=%u l=%d expected>=%zu", mt, l, expected);
        return 2;
    }

    NLOG_VERBOSE("recvfrom read %d bytes, msg_type=%d", l, mt);

    if(!impl->has_client_addr)
    {
       /* First latch restricted to JOIN/PUNCH: an internet stranger's
        * (otherwise valid-looking) packet cannot claim the peer slot.      */
       if (mt == NETPLAY_MSG_JOIN || mt == NETPLAY_MSG_PUNCH)
       {
           memcpy(&impl->client_addr,&client_addr,addrlen);
           impl->client_addr_len = addrlen;
           impl->has_client_addr = 1;
           {
               char as[INET6_ADDRSTRLEN + 16];
               skt_addr_to_str((struct sockaddr *)&impl->client_addr, as, sizeof(as));
               NLOG("peer latched from %s (msg_type=%u)", as, mt);
           }
       }
    }
    else if (mt == NETPLAY_MSG_JOIN && !handle->has_begun_game)
    {
       /* Pre-game JOIN always re-latches the sender; once has_begun_game
        * the address stays locked so a stray mid-session JOIN cannot
        * redirect the stream.                                              */
       memcpy(&impl->client_addr,&client_addr,addrlen);
       impl->client_addr_len = addrlen;
    }

    return 1;
}

/* sendto() msg at its own protocol-v8 wire size to the latched peer
 * address (client: the server we connected to; host: whoever last sent us
 * a packet).  A full send-buffer just drops the packet (netplay.cpp's own
 * redundancy/retransmit covers loss); any other failure hangs up. */
static int skt_send_pkt_data(netplay_t *handle,netplay_msg_t *msg)
{
    const struct sockaddr *addr = NULL;
    skt_netplay_t *impl = (skt_netplay_t *)handle->impl_data;

    if (impl->addr)
        addr = impl->addr->ai_addr;
    else if (impl->has_client_addr)
        addr = (const struct sockaddr*)&impl->client_addr;

    if (addr)
    {
        socklen_t addr_len = impl->addr ? impl->addr->ai_addrlen : impl->client_addr_len;

        if (0) { /* dead like NLOG_VERBOSE: no per-packet formatting cost */
            char as[INET6_ADDRSTRLEN + 16];
            skt_addr_to_str(addr, as, sizeof(as));
            NLOG_VERBOSE("sendto about to send to %s", as);
        }

        size_t wire_size = netplay_msg_wire_size(ntohl(msg->msg_type));
        int l = sendto(impl->fd, msg, wire_size, 0, addr, addr_len);
        if (l != (int)wire_size)
        {
            if (l < 0 && (errno == ENOBUFS || errno == EWOULDBLOCK || errno == EAGAIN))
            {
                NLOG("sendto dropped packet (buffer full). error=%s", strerror(errno));
                return 1; // Just drop the packet, don't disconnect!
            }

            /* Transient outage tolerance (mesh Wi-Fi roam / AP handoff) */
            {
                struct timespec ts;
                clock_gettime(CLOCK_MONOTONIC, &ts);
                uint32_t now_ms = (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
                if (handle->send_fail_since_ms == 0)
                    handle->send_fail_since_ms = now_ms;
                if ((now_ms - handle->send_fail_since_ms) < NETPLAY_SEND_FAIL_GRACE_MS) {
                    NLOG("sendto failed (%s), tolerating as packet loss (%ums into grace window)",
                         strerror(errno), now_ms - handle->send_fail_since_ms);
                    return 1; /* treated as a lost packet */
                }
            }

            NLOG("sendto failed. l=%d, expected=%zu, error=%s", l, wire_size, l<0 ? strerror(errno) : "size mismatch");
            char buf[256];
            /* No TOAST* prefix -> Java shows it as a MODAL dialog (original
             * behavior); resolveNpMsg localizes "@key|arg" (the system error). */
            snprintf(buf, sizeof(buf), "@send_failed|%s", strerror(errno));
            handle->netplay_warn(buf);
            handle->has_connection = 0;
            return 0;
        }
        handle->send_fail_since_ms = 0; /* healthy again: close the failure run */
        NLOG_VERBOSE("sendto sent %d bytes successfully", l);
    }
    return 1;
}

/* ============================================================
 * SECTION 4 -- Socket/session setup helpers
 * ============================================================ */

/* Resolve server:port (client) or wildcard:port (host), open the UDP
 * socket, enlarge its kernel buffers, and bind it if we are the host. */
static int skt_init_udp_socket(netplay_t *handle, const char *server, uint16_t port)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));

    skt_netplay_t *impl = (skt_netplay_t *)handle->impl_data;

    int ipm;
    pthread_mutex_lock(&s_inet_mutex);
    ipm = s_ip_mode;
    pthread_mutex_unlock(&s_inet_mutex);
    impl->ip_mode = ipm;

    /* V4/V6 force the family; an AUTO client resolves AF_UNSPEC and just
     * follows the destination's family (dual-stack only matters for the
     * host).  Never AI_V4MAPPED: bionic rejects it (EAI_BADFLAGS). */
    if (ipm == SKT_IPPROTO_V4)      hints.ai_family = AF_INET;
    else if (ipm == SKT_IPPROTO_V6) hints.ai_family = AF_INET6;
    else                            hints.ai_family = server ? AF_UNSPEC : AF_INET6;
    hints.ai_socktype = SOCK_DGRAM;
    if (!server)
        hints.ai_flags = AI_PASSIVE;

    char port_buf[16];
    snprintf(port_buf, sizeof(port_buf), "%hu", (unsigned short)port);
    int gr = getaddrinfo(server, port_buf, &hints, &impl->addr);
    if (gr != 0 || !impl->addr) {
        NLOG("getaddrinfo(%s) failed: %s", server ? server : "(passive)",
             gr ? gai_strerror(gr) : "no results");
        return 0;
    }

    impl->fd = socket(impl->addr->ai_family, impl->addr->ai_socktype, impl->addr->ai_protocol);
    if (impl->fd < 0)
    {
        NLOG("socket() failed: %s", strerror(errno));
        freeaddrinfo(impl->addr);
        impl->addr = NULL;
        return 0;
    }
    impl->sock_family = impl->addr->ai_family;

    if (impl->sock_family == AF_INET6)
    {
        /* AUTO = dual-stack (v4 peers reach us as mapped ::ffff:a.b.c.d,
         * so LAN/UPnP v4 joins keep working); V6 = strict, v6-only.        */
        int v6only = (ipm == SKT_IPPROTO_V6) ? 1 : 0;
        setsockopt(impl->fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));
    }

    /* Enlarge kernel socket buffers to absorb burst packet loss on
     * mobile Wi-Fi hotspots.  The default on Android is often only 212 KB
     * receive / 112 KB send; under jitter bursts the kernel drops packets
     * before the network thread can read them.                            */
    {
        int rcvbuf = 256 * 1024;   /* 256 KB receive                      */
        int sndbuf = 128 * 1024;   /* 128 KB send                         */
        setsockopt(impl->fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
        setsockopt(impl->fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
    }

    if (!server)
    {
        int yes = 1;
        setsockopt(impl->fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if (bind(impl->fd, impl->addr->ai_addr, impl->addr->ai_addrlen) < 0)
        {
            char buf[256];
            /* No TOAST* prefix -> Java shows it as a MODAL dialog (original
             * behavior); resolveNpMsg localizes "@key|arg" (the system error). */
            snprintf(buf, sizeof(buf), "@bind_failed|%s", strerror(errno));
            NLOG("bind() failed: %s", strerror(errno));
            handle->netplay_warn(buf);
            close(impl->fd);
            impl->fd = -1;
        } else {
            char as[INET6_ADDRSTRLEN + 16];
            skt_addr_to_str(impl->addr->ai_addr, as, sizeof(as));
            NLOG("bind() succeeded on fd %d. Bound to %s", impl->fd, as);
        }

        freeaddrinfo(impl->addr);
        impl->addr = NULL;
        if(impl->fd == -1)
            return 0;
    } else {
        NLOG("socket() succeeded on fd %d for client", impl->fd);

        /* Bind our local port too: a predictable source port gives a
         * predictable public tuple on port-preserving NATs (internet play).
         * The CONFIGURED port, never the destination's: an explicit ip:port
         * join must not leak into our own public tuple.  Ephemeral on fail. */
        uint16_t lp;
        pthread_mutex_lock(&s_inet_mutex);
        lp = s_local_bind_port ? s_local_bind_port : port;
        pthread_mutex_unlock(&s_inet_mutex);
        int yes = 1;
        setsockopt(impl->fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
        struct sockaddr_storage local;
        socklen_t local_len;
        memset(&local, 0, sizeof(local));
        if (impl->sock_family == AF_INET6) {
            struct sockaddr_in6 *l6 = (struct sockaddr_in6 *)&local;
            l6->sin6_family = AF_INET6;
            l6->sin6_addr = in6addr_any;
            l6->sin6_port = htons(lp);
            local_len = sizeof(struct sockaddr_in6);
        } else {
            struct sockaddr_in *l4 = (struct sockaddr_in *)&local;
            l4->sin_family = AF_INET;
            l4->sin_addr.s_addr = htonl(INADDR_ANY);
            l4->sin_port = htons(lp);
            local_len = sizeof(struct sockaddr_in);
        }
        if (bind(impl->fd, (struct sockaddr*)&local, local_len) < 0)
            NLOG("client bind(%u) failed (%s), keeping ephemeral port", (unsigned)lp, strerror(errno));
        else
            NLOG("client bound to local port %u", (unsigned)lp);
    }

    return 1;
}

/* ============================================================
 * SECTION 5 -- Internet play: STUN, punch target, diagnostics
 * All of it runs on the GAME socket; no connect() is ever used on it (it
 * would filter out the peer's datagrams and break latch and punch).
 *
 * IP protocol (Java pref, skt_netplay_set_ip_family): V4 = classic AF_INET;
 * V6 = strict v6-only socket; AUTO = dual-stack AF_INET6 that reaches v4
 * peers as mapped ::ffff addrs -- an AUTO host STUNs BOTH families and
 * ships the extra v4 public as "|alt=" so one invite serves everyone.
 * ============================================================ */

static const struct { const char *host; const char *port; } s_stun_servers[] = {
    { "stun.l.google.com",  "19302" },
    { "stun1.l.google.com", "19302" },
    { "stun.cloudflare.com","3478"  },
};
#define STUN_SERVER_COUNT 3

/* Rebuild a v4 sockaddr as its v4-mapped-v6 form (::ffff:a.b.c.d), the only
 * shape a dual-stack v6 socket can sendto() a v4 destination with. */
static void skt_map_v4_to_v6(const struct sockaddr_in *s4, struct sockaddr_in6 *s6)
{
    memset(s6, 0, sizeof(*s6));
    s6->sin6_family = AF_INET6;
    s6->sin6_port = s4->sin_port;
    s6->sin6_addr.s6_addr[10] = 0xff;
    s6->sin6_addr.s6_addr[11] = 0xff;
    memcpy(&s6->sin6_addr.s6_addr[12], &s4->sin_addr, 4);
}

/* Cheap "do we have an IPv6 route" test: connect() on a UDP socket sends no
 * packet, it only asks the kernel for a route.  Keeps AUTO from burning
 * ~2s of STUN timeouts on v6-less devices. */
static int skt_have_ipv6_route(void)
{
    int fd = socket(AF_INET6, SOCK_DGRAM, 0);
    if (fd < 0) return 0;
    struct sockaddr_in6 d;
    memset(&d, 0, sizeof(d));
    d.sin6_family = AF_INET6;
    d.sin6_port = htons(53);
    inet_pton(AF_INET6, "2001:4860:4860::8888", &d.sin6_addr);
    int ok = connect(fd, (struct sockaddr*)&d, sizeof(d)) == 0;
    close(fd);
    return ok;
}

/* One RFC 5389 Binding round-trip: 20-byte request, parse XOR-MAPPED-ADDRESS
 * (fallback MAPPED-ADDRESS) from the response.  Caller sets SO_RCVTIMEO.
 * family picks which public tuple we learn (v4 or v6); sock_is_v6 routes a
 * v4 query through the dual-stack socket as a mapped destination. */
static int skt_stun_query(int fd, int family, int sock_is_v6, const char *host, const char *port,
                          char *out_ip, size_t ip_len, uint16_t *out_port)
{
    struct addrinfo hints, *ai = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = family;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, port, &hints, &ai) != 0 || !ai) {
        if (ai) freeaddrinfo(ai);
        NLOG("STUN: resolve failed for %s (family %d)", host, family);
        return 0;
    }

    const struct sockaddr *dst = ai->ai_addr;
    socklen_t dst_len = ai->ai_addrlen;
    struct sockaddr_in6 mapped;
    if (family == AF_INET && sock_is_v6) {
        skt_map_v4_to_v6((const struct sockaddr_in *)ai->ai_addr, &mapped);
        dst = (const struct sockaddr *)&mapped;
        dst_len = sizeof(mapped);
    }

    uint8_t req[20];
    memset(req, 0, sizeof(req));
    req[0] = 0x00; req[1] = 0x01;                        /* Binding Request  */
    req[4] = 0x21; req[5] = 0x12; req[6] = 0xA4; req[7] = 0x42; /* cookie    */
    {
        FILE *ur = fopen("/dev/urandom", "rb");
        size_t got = ur ? fread(req + 8, 1, 12, ur) : 0;
        if (ur) fclose(ur);
        if (got != 12) {
            struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
            for (int i = 0; i < 12; i++)
                req[8 + i] = (uint8_t)(rand() ^ (ts.tv_nsec >> i));
        }
    }

    int ok = 0;
    struct timespec t0; clock_gettime(CLOCK_MONOTONIC, &t0);
    if (sendto(fd, req, sizeof(req), 0, dst, dst_len) != (int)sizeof(req))
    {
        /* Instant fail (e.g. ENETUNREACH) = no route for this family. */
        NLOG("STUN: sendto %s failed (%s)", host, strerror(errno));
    }
    else for (;;)
    {
        /* Wait for OUR reply within a fixed budget, draining strays: on the
         * shared socket a late reply from a previous query (Auto runs a v6
         * phase then a v4 one) or an early peer JOIN would otherwise eat
         * this attempt and randomly lose a tuple (e.g. the v4 "alt=").      */
        struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
        long left = 700 - ((now.tv_sec - t0.tv_sec) * 1000
                           + (now.tv_nsec - t0.tv_nsec) / 1000000);
        if (left <= 0) {
            NLOG("STUN: no reply from %s (timeout)", host);
            break;
        }
        struct timeval tv; tv.tv_sec = 0; tv.tv_usec = (int)left * 1000;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        uint8_t rsp[512];
        struct sockaddr_storage from; socklen_t fl = sizeof(from);
        int l = recvfrom(fd, rsp, sizeof(rsp), 0, (struct sockaddr*)&from, &fl);
        if (l < 0) {
            NLOG("STUN: no reply from %s (%s)", host, strerror(errno));
            break;
        }
        /* Validate: Binding Success + our transaction id; anything else is
         * a stray -- drop it and keep listening for the real reply.         */
        if (!(l >= 20 && rsp[0] == 0x01 && rsp[1] == 0x01 &&
              memcmp(rsp + 8, req + 8, 12) == 0))
        {
            NLOG("STUN: dropped stray datagram (%d bytes) while waiting for %s", l, host);
            continue;
        }
        {
            int mlen = (rsp[2] << 8) | rsp[3];
            if (mlen > l - 20) mlen = l - 20;
            int off = 20;
            while (off + 4 <= 20 + mlen) {
                int atype = (rsp[off] << 8) | rsp[off + 1];
                int alen  = (rsp[off + 2] << 8) | rsp[off + 3];
                const uint8_t *v = rsp + off + 4;
                if (off + 4 + alen > 20 + mlen) break;
                if (atype == 0x0020 || atype == 0x0001) {
                    uint16_t p = (uint16_t)((v[2] << 8) | v[3]);
                    if (v[1] == 0x01 && alen >= 8) {          /* IPv4 form        */
                        uint8_t a4[4] = { v[4], v[5], v[6], v[7] };
                        if (atype == 0x0020) {                /* XOR-MAPPED       */
                            p ^= 0x2112;
                            a4[0] ^= 0x21; a4[1] ^= 0x12; a4[2] ^= 0xA4; a4[3] ^= 0x42;
                        }
                        snprintf(out_ip, ip_len, "%u.%u.%u.%u", a4[0], a4[1], a4[2], a4[3]);
                        *out_port = p;
                        ok = 1;
                    } else if (v[1] == 0x02 && alen >= 20) {  /* IPv6 form        */
                        uint8_t a6[16];
                        memcpy(a6, v + 4, 16);
                        if (atype == 0x0020) {                /* XOR: cookie+txid */
                            static const uint8_t ck[4] = { 0x21, 0x12, 0xA4, 0x42 };
                            p ^= 0x2112;
                            for (int i = 0; i < 16; i++)
                                a6[i] ^= (i < 4) ? ck[i] : req[8 + i - 4];
                        }
                        inet_ntop(AF_INET6, a6, out_ip, ip_len);
                        *out_port = p;
                        ok = 1;
                    }
                    if (ok && atype == 0x0020) break;         /* prefer XOR form  */
                }
                off += 4 + ((alen + 3) & ~3);
            }
        }
        break; /* got a valid STUN response: done (parsed or not) */
    }
    freeaddrinfo(ai);
    return ok;
}

/* Discover our public tuple on the game socket (init only, network thread
 * not yet running).  Budget: <=~2.8s worst case (700ms/query).  A second
 * query to a DIFFERENT server detects per-destination (symmetric) mapping.
 * Never fails the session; SO_RCVTIMEO is restored to blocking. */
static void skt_run_stun(skt_netplay_t *impl, uint16_t local_port)
{
    struct timeval tmo; tmo.tv_sec = 0; tmo.tv_usec = 700 * 1000;
    setsockopt(impl->fd, SOL_SOCKET, SO_RCVTIMEO, &tmo, sizeof(tmo));

    int sock_v6 = (impl->sock_family == AF_INET6);

    /* Which public tuples to learn: V4/V6 their own family; AUTO host BOTH
     * (one invite must serve v6, v4 and LAN peers alike), AUTO client only
     * the join target's family (the host punches back over that flow). */
    int want_v6 = 0, want_v4 = 0;
    if (impl->ip_mode == SKT_IPPROTO_V6) {
        /* ULA-only (router v6 on, ISP gives no global prefix) has no route to
         * a STUN server: skip it -- there is no public v6, LAN play still works. */
        want_v6 = skt_have_ipv6_route();
    } else if (impl->ip_mode == SKT_IPPROTO_V4) {
        want_v4 = 1;
    } else if (impl->addr) {
        struct sockaddr_in6 *d6 = (struct sockaddr_in6 *)impl->addr->ai_addr;
        int dest_v6 = impl->addr->ai_family == AF_INET6 && !IN6_IS_ADDR_V4MAPPED(&d6->sin6_addr);
        want_v6 = dest_v6;
        want_v4 = !dest_v6;
    } else {
        want_v6 = skt_have_ipv6_route(); /* skip ~2s of v6 timeouts if none */
        want_v4 = 1;
    }

    char ip6[64]; uint16_t p6 = 0; int got6 = 0;
    if (want_v6)
        for (int i = 0; i < STUN_SERVER_COUNT && !got6; i++)
            got6 = skt_stun_query(impl->fd, AF_INET6, sock_v6, s_stun_servers[i].host,
                                  s_stun_servers[i].port, ip6, sizeof(ip6), &p6);

    char ip4[64]; uint16_t p4 = 0; int got4 = 0; int used4 = 0;
    if (want_v4)
        for (int i = 0; i < STUN_SERVER_COUNT && !got4; i++) {
            got4 = skt_stun_query(impl->fd, AF_INET, sock_v6, s_stun_servers[i].host,
                                  s_stun_servers[i].port, ip4, sizeof(ip4), &p4);
            if (got4) used4 = i;
        }

    /* Symmetric NAT is a v4 problem (v6 has no NAT): second query to a
     * DIFFERENT server, preferring another port/provider -- mobile CGNATs
     * can reuse one mapping toward two same-port servers and still be
     * per-destination in practice (field-tested on 4G). */
    int sym = 0;
    if (got4) {
        char ip2[64]; uint16_t p2 = 0;
        int j = (used4 + 1) % STUN_SERVER_COUNT;
        for (int i = 0; i < STUN_SERVER_COUNT; i++) {
            if (i != used4 && strcmp(s_stun_servers[i].port, s_stun_servers[used4].port) != 0) {
                j = i;
                break;
            }
        }
        if (skt_stun_query(impl->fd, AF_INET, sock_v6, s_stun_servers[j].host,
                           s_stun_servers[j].port, ip2, sizeof(ip2), &p2))
            sym = (strcmp(ip4, ip2) != 0 || p4 != p2) ? 1 : 0;
    }

    tmo.tv_sec = 0; tmo.tv_usec = 0;
    setsockopt(impl->fd, SOL_SOCKET, SO_RCVTIMEO, &tmo, sizeof(tmo));

    if (got6 || got4) {
        /* Primary tuple = v6 when we have it (NAT-free path), v4 otherwise;
         * with both, the v4 one rides along as "|alt=" for the invite. */
        const char *ip1 = got6 ? ip6 : ip4;
        uint16_t p1 = got6 ? p6 : p4;
        s_stun_ok  = 1;
        s_stun_sym = sym;
        s_stun_pp  = (got4 ? p4 == local_port : p6 == local_port) ? 1 : 0;
        strncpy(s_public_ip, ip1, sizeof(s_public_ip) - 1);
        s_public_ip[sizeof(s_public_ip) - 1] = 0;
        s_public_port = p1;
        char host1[80];
        if (strchr(ip1, ':'))
            snprintf(host1, sizeof(host1), "[%s]", ip1);
        else
            snprintf(host1, sizeof(host1), "%s", ip1);
        int n = snprintf(s_public_addr, sizeof(s_public_addr), "%s:%u|pp=%d|sym=%d",
                         host1, (unsigned)p1, s_stun_pp, s_stun_sym);
        if (got6 && got4 && n > 0 && (size_t)n < sizeof(s_public_addr))
            snprintf(s_public_addr + n, sizeof(s_public_addr) - n, "|alt=%s:%u",
                     ip4, (unsigned)p4);
        NLOG("STUN: public=%s", s_public_addr);
    } else {
        NLOG("STUN: all servers failed");
    }
}

/* Throwaway one-shot STUN on a FRESH UDP socket to learn just our public IP
 * (same on every socket behind the NAT).  Lets Java tell same-router (LAN)
 * from a 192.168.x collision.  Bounded <=1.5s; 1=ok, 0=offline/blocked.      */
int skt_netplay_probe_public_ip(char *out_ip, size_t ip_len)
{
    if (!out_ip || ip_len == 0) return 0;
    out_ip[0] = 0;

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        NLOG("STUN probe: socket() failed: %s", strerror(errno));
        return 0;
    }

    struct timeval tmo; tmo.tv_sec = 0; tmo.tv_usec = 500 * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tmo, sizeof(tmo));

    /* Always v4: this probe only feeds the Java same-site heuristic that
     * compares v4 publics (the v6 join path never needs it). */
    char ip[64]; uint16_t port = 0; int ok = 0;
    for (int i = 0; i < STUN_SERVER_COUNT && !ok; i++)
        ok = skt_stun_query(fd, AF_INET, 0, s_stun_servers[i].host, s_stun_servers[i].port,
                            ip, sizeof(ip), &port);

    close(fd);

    if (ok) {
        strncpy(out_ip, ip, ip_len - 1);
        out_ip[ip_len - 1] = 0;
        NLOG("STUN probe: our public IP = %s", out_ip);
    } else {
        NLOG("STUN probe: failed (offline or STUN blocked)");
    }
    return ok;
}

/* Resolve host:port into impl->punch_addr.  numeric_only (AI_NUMERICHOST)
 * is mandatory on the network thread so it can never block on DNS. */
static void skt_resolve_punch(skt_netplay_t *impl, const char *host, uint16_t port, int numeric_only)
{
    struct addrinfo hints, *ai = NULL;
    memset(&hints, 0, sizeof(hints));
    /* Family follows the session's socket; AUTO resolves AF_UNSPEC and a
     * v4 target gets hand-mapped below (bionic lacks AI_V4MAPPED). */
    hints.ai_family = (impl->ip_mode == SKT_IPPROTO_V4) ? AF_INET
                    : (impl->ip_mode == SKT_IPPROTO_AUTO) ? AF_UNSPEC : AF_INET6;
    hints.ai_socktype = SOCK_DGRAM;
    if (numeric_only)
        hints.ai_flags = AI_NUMERICHOST;
    char pb[16];
    snprintf(pb, sizeof(pb), "%hu", (unsigned short)port);
    impl->has_punch_addr = 0;
    if (getaddrinfo(host, pb, &hints, &ai) == 0 && ai) {
        if (ai->ai_family == AF_INET && impl->sock_family == AF_INET6) {
            skt_map_v4_to_v6((const struct sockaddr_in *)ai->ai_addr,
                             (struct sockaddr_in6 *)&impl->punch_addr);
            impl->punch_addr_len = sizeof(struct sockaddr_in6);
        } else {
            memcpy(&impl->punch_addr, ai->ai_addr, ai->ai_addrlen);
            impl->punch_addr_len = ai->ai_addrlen;
        }
        impl->has_punch_addr = 1;
        NLOG("punch target resolved: %s:%u", host, (unsigned)port);
    } else {
        NLOG("punch target resolve failed: %s:%u", host, (unsigned)port);
    }
    if (ai) freeaddrinfo(ai);
}

/* One readable connection-diagnostics block (also kept for the Java-side
 * getter). */
static void skt_format_diagnostics(uint16_t local_port, int inet_mode,
                                   const char *punch_host, uint16_t punch_port)
{
    char pub[80], peer[80], nat[48];

    if (s_stun_ok && strchr(s_public_ip, ':'))
        snprintf(pub, sizeof(pub), "[%s]:%u", s_public_ip, (unsigned)s_public_port);
    else if (s_stun_ok)
        snprintf(pub, sizeof(pub), "%s:%u", s_public_ip, (unsigned)s_public_port);
    else
        snprintf(pub, sizeof(pub), "%s", inet_mode ? "unavailable" : "n/a");

    if (punch_host && punch_host[0] && strchr(punch_host, ':'))
        snprintf(peer, sizeof(peer), "[%s]:%u", punch_host, (unsigned)punch_port);
    else if (punch_host && punch_host[0])
        snprintf(peer, sizeof(peer), "%s:%u", punch_host, (unsigned)punch_port);
    else
        snprintf(peer, sizeof(peer), "none");

    if (!s_stun_ok)
        snprintf(nat, sizeof(nat), "Unknown");
    else if (s_stun_pp)
        snprintf(nat, sizeof(nat), "Port preserving");
    else
        snprintf(nat, sizeof(nat), "Rewritten (%u)", (unsigned)s_public_port);

    int ipm;
    pthread_mutex_lock(&s_inet_mutex);
    ipm = s_ip_mode;
    pthread_mutex_unlock(&s_inet_mutex);

    snprintf(s_diagnostics, sizeof(s_diagnostics),
             "Netplay diagnostics\n"
             "Local:    *:%u\n"
             "Public:   %s\n"
             "Peer:     %s\n"
             "NAT:      %s\n"
             "Symmetric: %s\n"
             "Internet mode: %s\n"
             "IP protocol: %s",
             (unsigned)local_port, pub, peer, nat,
             s_stun_ok ? (s_stun_sym ? "Yes" : "No") : "Unknown",
             inet_mode ? "Yes" : "No",
             ipm == SKT_IPPROTO_V6 ? "IPv6" : ipm == SKT_IPPROTO_AUTO ? "Auto (dual-stack)" : "IPv4");

    NLOG("%s", s_diagnostics);
}

/* Java-facing config/result surface (see skt_netplay.h for the threading
 * and validity contracts). */
void skt_netplay_set_punch_addr(const char *host, uint16_t port)
{
    pthread_mutex_lock(&s_inet_mutex);
    if (host && host[0]) {
        strncpy(s_punch_host, host, sizeof(s_punch_host) - 1);
        s_punch_host[sizeof(s_punch_host) - 1] = 0;
        s_punch_port = port;
    } else {
        s_punch_host[0] = 0;
        s_punch_port = 0;
    }
    s_punch_dirty = 1;
    pthread_mutex_unlock(&s_inet_mutex);
    NLOG("punch addr set: %s:%u", (host && host[0]) ? host : "(clear)", (unsigned)port);
}

void skt_netplay_set_internet_mode(int on)
{
    pthread_mutex_lock(&s_inet_mutex);
    s_internet_mode = on ? 1 : 0;
    pthread_mutex_unlock(&s_inet_mutex);
    NLOG("internet mode: %d", on ? 1 : 0);
}

void skt_netplay_set_ip_family(int mode)
{
    pthread_mutex_lock(&s_inet_mutex);
    s_ip_mode = (mode == SKT_IPPROTO_V6 || mode == SKT_IPPROTO_AUTO) ? mode : SKT_IPPROTO_V4;
    pthread_mutex_unlock(&s_inet_mutex);
    NLOG("ip family mode: %d", mode);
}

/* Family-agnostic "ip:port" / "[ip]:port" formatting for logs and UI. */
static void skt_addr_to_str(const struct sockaddr *sa, char *buf, size_t len)
{
    char ip[INET6_ADDRSTRLEN] = "?";
    if (sa->sa_family == AF_INET6) {
        const struct sockaddr_in6 *s6 = (const struct sockaddr_in6 *)sa;
        inet_ntop(AF_INET6, &s6->sin6_addr, ip, sizeof(ip));
        snprintf(buf, len, "[%s]:%u", ip, (unsigned)ntohs(s6->sin6_port));
    } else {
        const struct sockaddr_in *s4 = (const struct sockaddr_in *)sa;
        inet_ntop(AF_INET, &s4->sin_addr, ip, sizeof(ip));
        snprintf(buf, len, "%s:%u", ip, (unsigned)ntohs(s4->sin_port));
    }
}

void skt_netplay_set_local_port(uint16_t port)
{
    pthread_mutex_lock(&s_inet_mutex);
    s_local_bind_port = port;
    pthread_mutex_unlock(&s_inet_mutex);
    NLOG("local bind port: %u", (unsigned)port);
}

const char *skt_netplay_get_public_addr(void)
{
    return s_public_addr;
}

const char *skt_netplay_get_diagnostics(void)
{
    return s_diagnostics;
}

/* Zero the transport struct and mark the socket closed (fd = -1). */
static int skt_init_handle_impl(skt_netplay_t *impl){

    memset(impl,0,sizeof(skt_netplay_impl));

    impl->fd = -1;

    return 1;
}

/* Lazily-initialized singleton: this OSD only ever runs one netplay session
 * at a time, so a single static instance (instead of a heap allocation) is
 * enough. */
static skt_netplay_t * skt_get_handle_impl(){
    static int init = 0;
    if(!init)
    {
        skt_init_handle_impl(&skt_netplay_impl);

        signal(SIGPIPE, SIG_IGN); // Do not like SIGPIPE killing our app :(

        init = 1;
    }
    return &skt_netplay_impl;
}
