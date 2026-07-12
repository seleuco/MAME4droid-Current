// license:BSD-3-Clause
// copyright-holders: David Valdeita (Seleuco)
/***************************************************************************

    skt_netplay.h

    UDP transport for netplay.h: implements read_pkt_data/send_pkt_data
    (recvfrom/sendto over one UDP socket) and the network thread that
    drives netplay_read_data.  Kept separate so netplay.cpp / myosd-netplay.
    cpp never need <sys/socket.h> directly.

***************************************************************************/

#ifndef skt_netplay_h
#define skt_netplay_h


#include <netdb.h>

    /* Per-session UDP transport state, hung off netplay_t::impl_data. */
    typedef struct skt_netplay
    {
        struct addrinfo *addr;             /* client: peer to connect to; host: our own bind info (freed after bind) */
        int fd;                            /* UDP socket, -1 when closed */
        struct sockaddr_storage client_addr; /* peer address, latched from its packets (see skt_read_pkt_data) */
        socklen_t client_addr_len;
        int has_client_addr;               /* whether client_addr is valid yet */

        /* Internet play: peer public tuple the host punches toward until the
         * real client latches (network-thread-owned after init).           */
        struct sockaddr_storage punch_addr;
        socklen_t punch_addr_len;
        int has_punch_addr;

    }skt_netplay_t;


    int skt_netplay_init(netplay_t *handle,const char *server, uint16_t port,void (*warn_cb)(char *)); /* create the UDP socket + network thread for this session */
    void skt_netplay_set_punch_addr(const char *host, uint16_t port);  /* peer public tuple to punch; NULL/empty clears; hot-settable */
    void skt_netplay_set_internet_mode(int on);                        /* pre-init: run STUN on the game socket during next init */
    void skt_netplay_set_local_port(uint16_t port);                    /* pre-init: client's local bind port (its OWN settings port) */
    const char *skt_netplay_get_public_addr(void);                     /* "ip:port|pp=0/1|sym=0/1" or "" -- valid only after init returns */
    const char *skt_netplay_get_diagnostics(void);                     /* multi-line connection diagnostics block, same validity */
    int skt_netplay_probe_public_ip(char *out_ip, size_t ip_len);      /* throwaway one-shot STUN: learn our public IP BEFORE init (LAN-vs-internet decision); 1=ok, 0=offline/blocked */



#endif
