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
static uint16_t s_local_bind_port = 0;    /* client bind; 0 = destination port */
static char     s_public_addr[96] = "";   /* "ip:port|pp=X|sym=Y", "" = none */
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
            struct sockaddr_in sn; socklen_t sl = sizeof(sn);
            if (getsockname(impl->fd, (struct sockaddr*)&sn, &sl) == 0)
                local_port = ntohs(sn.sin_port);
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

    NLOG("Creada threaded_data");

    while(handle->has_connection){

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
                    if (sendto(impl->fd, &pm, netplay_msg_wire_size(NETPLAY_MSG_PUNCH), 0, pa, pal) < 0)
                        NLOG("punch sendto failed (%s), ignored", strerror(errno));
                    else
                        NLOG_VERBOSE("punch sent");
                }
            }
        }

        tmp_tv = tv;

        FD_ZERO(&fds);
        FD_SET(impl->fd, &fds);

        if (select(impl->fd + 1, &fds, NULL, NULL, &tmp_tv) < 0)
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

        if (FD_ISSET(impl->fd, &fds)) // packet arrive
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
                FD_SET(impl->fd, &drain_fds);
                if (select(impl->fd + 1, &drain_fds, NULL, NULL, &zero_tv) <= 0)
                    break; /* No more packets immediately available */
                if (!FD_ISSET(impl->fd, &drain_fds))
                    break;
            }
        }
    }

    close(impl->fd);
    impl->fd = -1;

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
               struct sockaddr_in *sin = (struct sockaddr_in *)&impl->client_addr;
               NLOG("peer latched from %s:%d (msg_type=%u)",
                    inet_ntoa(sin->sin_addr), ntohs(sin->sin_port), mt);
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

        struct sockaddr_in *sin = (struct sockaddr_in *)addr;
        char *ip = inet_ntoa(sin->sin_addr);
        int dest_port = ntohs(sin->sin_port);
        NLOG_VERBOSE("sendto about to send to IP: %s, Port: %d", ip, dest_port);

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

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (!server)
        hints.ai_flags = AI_PASSIVE;

    char port_buf[16];
    snprintf(port_buf, sizeof(port_buf), "%hu", (unsigned short)port);
    if (getaddrinfo(server, port_buf, &hints, &impl->addr) < 0)
        return 0;

    if (!impl->addr)
        return 0;

    impl->fd = socket(impl->addr->ai_family, impl->addr->ai_socktype, impl->addr->ai_protocol);
    if (impl->fd < 0)
    {
        NLOG("socket() failed: %s", strerror(errno));
        return 0;
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
            struct sockaddr_in *sin = (struct sockaddr_in *)impl->addr->ai_addr;
            char *ip = inet_ntoa(sin->sin_addr);
            int bound_port = ntohs(sin->sin_port);
            NLOG("bind() succeeded on fd %d. Bound to IP: %s, Port: %d", impl->fd, ip, bound_port);
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
        struct sockaddr_in local;
        memset(&local, 0, sizeof(local));
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = htonl(INADDR_ANY);
        local.sin_port = htons(lp);
        if (bind(impl->fd, (struct sockaddr*)&local, sizeof(local)) < 0)
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
 * ============================================================ */

static const struct { const char *host; const char *port; } s_stun_servers[] = {
    { "stun.l.google.com",  "19302" },
    { "stun1.l.google.com", "19302" },
    { "stun.cloudflare.com","3478"  },
};
#define STUN_SERVER_COUNT 3

/* One RFC 5389 Binding round-trip: 20-byte request, parse XOR-MAPPED-ADDRESS
 * (fallback MAPPED-ADDRESS) from the response.  Caller sets SO_RCVTIMEO. */
static int skt_stun_query(int fd, const char *host, const char *port,
                          char *out_ip, size_t ip_len, uint16_t *out_port)
{
    struct addrinfo hints, *ai = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, port, &hints, &ai) != 0 || !ai) {
        if (ai) freeaddrinfo(ai);
        NLOG("STUN: resolve failed for %s", host);
        return 0;
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
    if (sendto(fd, req, sizeof(req), 0, ai->ai_addr, ai->ai_addrlen) == (int)sizeof(req))
    {
        uint8_t rsp[512];
        struct sockaddr_storage from; socklen_t fl = sizeof(from);
        int l = recvfrom(fd, rsp, sizeof(rsp), 0, (struct sockaddr*)&from, &fl);
        /* Validate: Binding Success + our transaction id (an early peer
         * JOIN landing here just fails the checks and costs one attempt).  */
        if (l >= 20 && rsp[0] == 0x01 && rsp[1] == 0x01 &&
            memcmp(rsp + 8, req + 8, 12) == 0)
        {
            int mlen = (rsp[2] << 8) | rsp[3];
            if (mlen > l - 20) mlen = l - 20;
            int off = 20;
            while (off + 4 <= 20 + mlen) {
                int atype = (rsp[off] << 8) | rsp[off + 1];
                int alen  = (rsp[off + 2] << 8) | rsp[off + 3];
                const uint8_t *v = rsp + off + 4;
                if (off + 4 + alen > 20 + mlen) break;
                if ((atype == 0x0020 || atype == 0x0001) && alen >= 8 && v[1] == 0x01) {
                    uint16_t p = (uint16_t)((v[2] << 8) | v[3]);
                    uint8_t a[4] = { v[4], v[5], v[6], v[7] };
                    if (atype == 0x0020) {                /* XOR-MAPPED       */
                        p ^= 0x2112;
                        a[0] ^= 0x21; a[1] ^= 0x12; a[2] ^= 0xA4; a[3] ^= 0x42;
                    }
                    snprintf(out_ip, ip_len, "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
                    *out_port = p;
                    ok = 1;
                    if (atype == 0x0020) break;           /* prefer XOR form  */
                }
                off += 4 + ((alen + 3) & ~3);
            }
        }
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

    char ip1[64]; uint16_t p1 = 0; int got1 = 0; int used = 0;
    for (int i = 0; i < STUN_SERVER_COUNT && !got1; i++) {
        got1 = skt_stun_query(impl->fd, s_stun_servers[i].host, s_stun_servers[i].port,
                              ip1, sizeof(ip1), &p1);
        if (got1) used = i;
    }

    int sym = 0;
    if (got1) {
        char ip2[64]; uint16_t p2 = 0;
        /* Prefer a second server on a different PORT and provider: mobile
         * CGNATs can reuse one mapping toward two same-port servers and
         * still be per-destination in practice (field-tested on 4G). */
        int j = (used + 1) % STUN_SERVER_COUNT;
        for (int i = 0; i < STUN_SERVER_COUNT; i++) {
            if (i != used && strcmp(s_stun_servers[i].port, s_stun_servers[used].port) != 0) {
                j = i;
                break;
            }
        }
        if (skt_stun_query(impl->fd, s_stun_servers[j].host, s_stun_servers[j].port,
                           ip2, sizeof(ip2), &p2))
            sym = (strcmp(ip1, ip2) != 0 || p1 != p2) ? 1 : 0;
    }

    tmo.tv_sec = 0; tmo.tv_usec = 0;
    setsockopt(impl->fd, SOL_SOCKET, SO_RCVTIMEO, &tmo, sizeof(tmo));

    if (got1) {
        s_stun_ok  = 1;
        s_stun_sym = sym;
        s_stun_pp  = (p1 == local_port) ? 1 : 0;
        strncpy(s_public_ip, ip1, sizeof(s_public_ip) - 1);
        s_public_ip[sizeof(s_public_ip) - 1] = 0;
        s_public_port = p1;
        snprintf(s_public_addr, sizeof(s_public_addr), "%s:%u|pp=%d|sym=%d",
                 ip1, (unsigned)p1, s_stun_pp, s_stun_sym);
        NLOG("STUN: public=%s:%u port_preserving=%d symmetric=%d (via %s)",
             ip1, (unsigned)p1, s_stun_pp, s_stun_sym, s_stun_servers[used].host);
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

    char ip[64]; uint16_t port = 0; int ok = 0;
    for (int i = 0; i < STUN_SERVER_COUNT && !ok; i++)
        ok = skt_stun_query(fd, s_stun_servers[i].host, s_stun_servers[i].port,
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
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (numeric_only)
        hints.ai_flags = AI_NUMERICHOST;
    char pb[16];
    snprintf(pb, sizeof(pb), "%hu", (unsigned short)port);
    impl->has_punch_addr = 0;
    if (getaddrinfo(host, pb, &hints, &ai) == 0 && ai) {
        memcpy(&impl->punch_addr, ai->ai_addr, ai->ai_addrlen);
        impl->punch_addr_len = ai->ai_addrlen;
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

    if (s_stun_ok)
        snprintf(pub, sizeof(pub), "%s:%u", s_public_ip, (unsigned)s_public_port);
    else
        snprintf(pub, sizeof(pub), "%s", inet_mode ? "unavailable" : "n/a");

    if (punch_host && punch_host[0])
        snprintf(peer, sizeof(peer), "%s:%u", punch_host, (unsigned)punch_port);
    else
        snprintf(peer, sizeof(peer), "none");

    if (!s_stun_ok)
        snprintf(nat, sizeof(nat), "Unknown");
    else if (s_stun_pp)
        snprintf(nat, sizeof(nat), "Port preserving");
    else
        snprintf(nat, sizeof(nat), "Rewritten (%u)", (unsigned)s_public_port);

    snprintf(s_diagnostics, sizeof(s_diagnostics),
             "Netplay diagnostics\n"
             "Local:    *:%u\n"
             "Public:   %s\n"
             "Peer:     %s\n"
             "NAT:      %s\n"
             "Symmetric: %s\n"
             "Internet mode: %s",
             (unsigned)local_port, pub, peer, nat,
             s_stun_ok ? (s_stun_sym ? "Yes" : "No") : "Unknown",
             inet_mode ? "Yes" : "No");

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
