/*
 *  Reliable UDP host/client transport (ENet-style channels).
 *  Uses BSD sockets / Winsock; reliable channel has seq + ACK.
 */

#define MEM_FILE_SYS MEM_SYS_NET
#define MEM_FILE_SUB 0

#include "public/net.h"
#include "../lib/public/pf_string.h"
#include "../mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#define NET_CLOSE(s) closesocket(s)
#define NET_IOCTL(s, cmd, arg) ioctlsocket((s), (cmd), (arg))
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#define SOCKET int
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define NET_CLOSE(s) close(s)
#define NET_IOCTL(s, cmd, arg) ioctl((s), (cmd), (arg))
#endif

#include <SDL.h>

#undef PF_MALLOC
#undef PF_CALLOC
#undef PF_REALLOC
#define PF_MALLOC(_n)       PF_MALLOC_TAGGED((_n), MEM_SYS_NET, 0)
#define PF_CALLOC(_c, _n)   PF_CALLOC_TAGGED((_c), (_n), MEM_SYS_NET, 0)
#define PF_REALLOC(_p, _n)  PF_REALLOC_TAGGED((_p), (_n), MEM_SYS_NET, 0)

#define NET_MAGIC           0x50464E54u /* 'PFNT' */
#define NET_HDR_SIZE        16
#define NET_RELIABLE_WINDOW 64
#define NET_SEND_QUEUE      128

enum pkt_flags{
    PKT_CONNECT    = 1 << 0,
    PKT_DISCONNECT = 1 << 1,
    PKT_ACK        = 1 << 2,
    PKT_DATA       = 1 << 3,
    PKT_RELIABLE   = 1 << 4
};

#pragma pack(push, 1)
struct net_hdr{
    uint32_t magic;
    uint8_t  flags;
    uint8_t  channel;
    uint16_t seq;
    uint16_t ack;
    uint16_t len;
    uint32_t send_time_ms;
};
#pragma pack(pop)

struct queued_pkt{
    bool     used;
    uint16_t seq;
    uint32_t last_send_ms;
    uint8_t  channel;
    uint16_t len;
    uint8_t  data[NET_MAX_PACKET];
};

struct peer_internal{
    bool               used;
    enum net_peer_state state;
    struct sockaddr_in addr;
    uint16_t           next_send_seq;
    uint16_t           next_recv_seq;
    uint16_t           last_acked;
    uint32_t           last_recv_ms;
    uint32_t           rtt_ms;
    char               name[NET_MAX_NAME_LEN];
    struct queued_pkt  outbound[NET_SEND_QUEUE];
};

static bool               s_inited;
static bool               s_is_host;
static SOCKET             s_sock = INVALID_SOCKET;
static int                s_max_peers;
static struct peer_internal s_peers[NET_MAX_PEERS];
static uint8_t            s_recv_scratch[NET_MAX_PACKET + NET_HDR_SIZE];

static uint16_t seq_add(uint16_t a, uint16_t b)
{
    return (uint16_t)(a + b);
}

static int seq_cmp(uint16_t a, uint16_t b)
{
    return (int16_t)(a - b);
}

uint32_t Net_NowMs(void)
{
    return SDL_GetTicks();
}

bool Net_Init(void)
{
    if(s_inited)
        return true;
#if defined(_WIN32)
    WSADATA wsa;
    if(WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return false;
#endif
    s_inited = true;
    return true;
}

void Net_Shutdown(void)
{
    Net_Destroy();
#if defined(_WIN32)
    if(s_inited)
        WSACleanup();
#endif
    s_inited = false;
}

static bool set_nonblocking(SOCKET s)
{
#if defined(_WIN32)
    u_long mode = 1;
    return NET_IOCTL(s, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(s, F_GETFL, 0);
    if(flags < 0)
        return false;
    return fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

static void peer_clear(struct peer_internal *p)
{
    memset(p, 0, sizeof(*p));
}

static int find_peer_by_addr(const struct sockaddr_in *addr)
{
    for(int i = 0; i < s_max_peers; i++) {
        if(!s_peers[i].used)
            continue;
        if(s_peers[i].addr.sin_addr.s_addr == addr->sin_addr.s_addr
        && s_peers[i].addr.sin_port == addr->sin_port)
            return i;
    }
    return -1;
}

static int alloc_peer(const struct sockaddr_in *addr)
{
    for(int i = 0; i < s_max_peers; i++) {
        if(s_peers[i].used)
            continue;
        peer_clear(&s_peers[i]);
        s_peers[i].used = true;
        s_peers[i].state = NET_PEER_CONNECTING;
        s_peers[i].addr = *addr;
        s_peers[i].last_recv_ms = Net_NowMs();
        return i;
    }
    return -1;
}

static bool send_raw(const struct sockaddr_in *addr, const void *data, size_t len)
{
    int n = sendto(s_sock, (const char *)data, (int)len, 0,
                   (const struct sockaddr *)addr, sizeof(*addr));
    return n == (int)len;
}

static bool send_hdr(int peer_slot, uint8_t flags, uint8_t channel,
                     uint16_t seq, uint16_t ack,
                     const void *payload, uint16_t len)
{
    if(peer_slot < 0 || peer_slot >= s_max_peers || !s_peers[peer_slot].used)
        return false;
    if(len > NET_MAX_PACKET)
        return false;

    uint8_t buf[NET_HDR_SIZE + NET_MAX_PACKET];
    struct net_hdr *hdr = (struct net_hdr *)buf;
    hdr->magic = NET_MAGIC;
    hdr->flags = flags;
    hdr->channel = channel;
    hdr->seq = seq;
    hdr->ack = ack;
    hdr->len = len;
    hdr->send_time_ms = Net_NowMs();
    if(len && payload)
        memcpy(buf + NET_HDR_SIZE, payload, len);
    return send_raw(&s_peers[peer_slot].addr, buf, NET_HDR_SIZE + len);
}

static bool create_socket(uint16_t port)
{
    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(s_sock == INVALID_SOCKET)
        return false;
    if(!set_nonblocking(s_sock)) {
        NET_CLOSE(s_sock);
        s_sock = INVALID_SOCKET;
        return false;
    }
    int yes = 1;
    setsockopt(s_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));

    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind_addr.sin_port = htons(port);
    if(bind(s_sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) == SOCKET_ERROR) {
        NET_CLOSE(s_sock);
        s_sock = INVALID_SOCKET;
        return false;
    }
    return true;
}

bool Net_HostCreate(uint16_t port, int max_peers)
{
    if(!s_inited && !Net_Init())
        return false;
    Net_Destroy();
    if(max_peers < 1 || max_peers > NET_MAX_PEERS)
        max_peers = NET_MAX_PEERS;
    s_max_peers = max_peers;
    memset(s_peers, 0, sizeof(s_peers));
    if(!create_socket(port))
        return false;
    s_is_host = true;
    return true;
}

bool Net_ClientCreate(void)
{
    if(!s_inited && !Net_Init())
        return false;
    Net_Destroy();
    s_max_peers = NET_MAX_PEERS;
    memset(s_peers, 0, sizeof(s_peers));
    if(!create_socket(0))
        return false;
    s_is_host = false;
    return true;
}

void Net_Destroy(void)
{
    if(s_sock != INVALID_SOCKET) {
        NET_CLOSE(s_sock);
        s_sock = INVALID_SOCKET;
    }
    memset(s_peers, 0, sizeof(s_peers));
    s_max_peers = 0;
    s_is_host = false;
}

bool Net_Connect(struct net_address addr)
{
    if(s_sock == INVALID_SOCKET)
        return false;

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = addr.host;
    sa.sin_port = htons(addr.port);

    int slot = alloc_peer(&sa);
    if(slot < 0)
        return false;
    s_peers[slot].state = NET_PEER_CONNECTING;
    return send_hdr(slot, PKT_CONNECT, 0, 0, 0, NULL, 0);
}

void Net_DisconnectPeer(int peer_slot)
{
    if(peer_slot < 0 || peer_slot >= s_max_peers || !s_peers[peer_slot].used)
        return;
    send_hdr(peer_slot, PKT_DISCONNECT, 0, 0, 0, NULL, 0);
    peer_clear(&s_peers[peer_slot]);
}

void Net_DisconnectAll(void)
{
    for(int i = 0; i < s_max_peers; i++) {
        if(s_peers[i].used)
            Net_DisconnectPeer(i);
    }
}

static void queue_reliable(int peer_slot, uint8_t channel, const void *data, uint16_t len)
{
    struct peer_internal *p = &s_peers[peer_slot];
    for(int i = 0; i < NET_SEND_QUEUE; i++) {
        if(p->outbound[i].used)
            continue;
        p->outbound[i].used = true;
        p->outbound[i].seq = p->next_send_seq;
        p->outbound[i].channel = channel;
        p->outbound[i].len = len;
        p->outbound[i].last_send_ms = 0;
        memcpy(p->outbound[i].data, data, len);
        p->next_send_seq = seq_add(p->next_send_seq, 1);
        return;
    }
    fprintf(stderr, "[net] reliable queue full — packet dropped (peer=%d len=%u)\n",
            peer_slot, (unsigned)len);
}

static void flush_reliable(int peer_slot)
{
    struct peer_internal *p = &s_peers[peer_slot];
    uint32_t now = Net_NowMs();
    /* Retransmit in seq order, cap burst so we do not bury fresh CMD frames. */
    int sent = 0;
    for(;;) {
        int best = -1;
        uint16_t best_seq = 0;
        for(int i = 0; i < NET_SEND_QUEUE; i++) {
            struct queued_pkt *q = &p->outbound[i];
            if(!q->used)
                continue;
            if(q->last_send_ms != 0 && (now - q->last_send_ms) < 40)
                continue;
            if(best < 0 || seq_cmp(q->seq, best_seq) < 0) {
                best = i;
                best_seq = q->seq;
            }
        }
        if(best < 0 || sent >= 8)
            break;
        struct queued_pkt *q = &p->outbound[best];
        send_hdr(peer_slot, PKT_DATA | PKT_RELIABLE, q->channel, q->seq, p->next_recv_seq,
                 q->data, q->len);
        q->last_send_ms = now;
        sent++;
    }
}

bool Net_Send(int peer_slot, enum net_channel ch, const void *data, size_t len)
{
    if(peer_slot < 0 || peer_slot >= s_max_peers || !s_peers[peer_slot].used)
        return false;
    if(len > NET_MAX_PACKET)
        return false;
    if(ch == NET_CH_RELIABLE) {
        queue_reliable(peer_slot, (uint8_t)ch, data, (uint16_t)len);
        flush_reliable(peer_slot);
        return true;
    }
    struct peer_internal *p = &s_peers[peer_slot];
    return send_hdr(peer_slot, PKT_DATA, (uint8_t)ch, 0, p->next_recv_seq, data, (uint16_t)len);
}

bool Net_Broadcast(enum net_channel ch, const void *data, size_t len)
{
    bool ok = true;
    for(int i = 0; i < s_max_peers; i++) {
        if(s_peers[i].used && s_peers[i].state == NET_PEER_CONNECTED)
            ok = Net_Send(i, ch, data, len) && ok;
    }
    return ok;
}

static void ack_outbound(struct peer_internal *p, uint16_t ack)
{
    for(int i = 0; i < NET_SEND_QUEUE; i++) {
        if(!p->outbound[i].used)
            continue;
        if(seq_cmp(p->outbound[i].seq, ack) < 0) {
            p->outbound[i].used = false;
        }
    }
    p->last_acked = ack;
}

bool Net_Service(uint32_t timeout_ms, struct net_event *out_event)
{
    (void)timeout_ms;
    memset(out_event, 0, sizeof(*out_event));
    out_event->type = NET_EVENT_NONE;

    if(s_sock == INVALID_SOCKET)
        return false;

    for(int i = 0; i < s_max_peers; i++) {
        if(s_peers[i].used)
            flush_reliable(i);
    }

    /*
     * Drain until a deliverable event or the socket is empty.
     * Previously pure ACK / out-of-order reliable returns false and stopped
     * pump_net(), leaving CMD frames sitting in the UDP buffer — lockstep
     * then timed out empty frames and B never saw A's orders.
     */
    for(int drain = 0; drain < 64; drain++) {
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        int n = recvfrom(s_sock, (char *)s_recv_scratch, sizeof(s_recv_scratch), 0,
                         (struct sockaddr *)&from, &fromlen);
        if(n < (int)NET_HDR_SIZE)
            return false;

        struct net_hdr *hdr = (struct net_hdr *)s_recv_scratch;
        if(hdr->magic != NET_MAGIC || hdr->len + NET_HDR_SIZE > (uint16_t)n)
            continue;

        int slot = find_peer_by_addr(&from);
        uint8_t *payload = s_recv_scratch + NET_HDR_SIZE;

        if(hdr->flags & PKT_CONNECT) {
            if(slot < 0) {
                if(!s_is_host)
                    continue;
                slot = alloc_peer(&from);
                if(slot < 0)
                    continue;
            }
            s_peers[slot].state = NET_PEER_CONNECTED;
            s_peers[slot].last_recv_ms = Net_NowMs();
            send_hdr(slot, PKT_ACK | PKT_CONNECT, 0, 0, 0, NULL, 0);
            out_event->type = NET_EVENT_CONNECT;
            out_event->peer_slot = slot;
            return true;
        }

        if(slot < 0)
            continue;

        s_peers[slot].last_recv_ms = Net_NowMs();
        if(hdr->send_time_ms) {
            uint32_t rtt = Net_NowMs() - hdr->send_time_ms;
            if(rtt < 5000)
                s_peers[slot].rtt_ms = rtt;
        }
        ack_outbound(&s_peers[slot], hdr->ack);

        if(hdr->flags & PKT_DISCONNECT) {
            out_event->type = NET_EVENT_DISCONNECT;
            out_event->peer_slot = slot;
            peer_clear(&s_peers[slot]);
            return true;
        }

        if((hdr->flags & PKT_ACK) && (hdr->flags & PKT_CONNECT)) {
            s_peers[slot].state = NET_PEER_CONNECTED;
            out_event->type = NET_EVENT_CONNECT;
            out_event->peer_slot = slot;
            return true;
        }

        if(hdr->flags & PKT_DATA) {
            if(hdr->flags & PKT_RELIABLE) {
                if(seq_cmp(hdr->seq, s_peers[slot].next_recv_seq) < 0) {
                    /* Duplicate — ACK again, keep draining. */
                    send_hdr(slot, PKT_ACK, 0, 0, s_peers[slot].next_recv_seq, NULL, 0);
                    continue;
                }
                if(hdr->seq != s_peers[slot].next_recv_seq) {
                    /* Gap — nudge sender with dup ACK, keep draining. */
                    send_hdr(slot, PKT_ACK, 0, 0, s_peers[slot].next_recv_seq, NULL, 0);
                    continue;
                }
                s_peers[slot].next_recv_seq = seq_add(hdr->seq, 1);
                send_hdr(slot, PKT_ACK, 0, 0, s_peers[slot].next_recv_seq, NULL, 0);
            }
            out_event->type = NET_EVENT_RECEIVE;
            out_event->peer_slot = slot;
            out_event->channel = hdr->channel;
            out_event->data_len = hdr->len;
            out_event->data = PF_MALLOC(hdr->len ? hdr->len : 1);
            if(!out_event->data)
                return false;
            if(hdr->len)
                memcpy(out_event->data, payload, hdr->len);
            return true;
        }

        /* Pure ACK / other control — already applied via ack_outbound. */
    }

    return false;
}

void Net_FreeEvent(struct net_event *ev)
{
    if(ev && ev->data) {
        /* Must match PF_MALLOC in Net_Service (not libc free / mimalloc). */
        void *tmp = ev->data;
        PF_FREE(tmp);
        ev->data = NULL;
        ev->data_len = 0;
    }
}

int Net_PeerCount(void)
{
    int n = 0;
    for(int i = 0; i < s_max_peers; i++) {
        if(s_peers[i].used && s_peers[i].state == NET_PEER_CONNECTED)
            n++;
    }
    return n;
}

bool Net_GetPeer(int slot, struct net_peer *out)
{
    if(slot < 0 || slot >= s_max_peers || !s_peers[slot].used)
        return false;
    out->slot = slot;
    out->state = s_peers[slot].state;
    out->addr.host = s_peers[slot].addr.sin_addr.s_addr;
    out->addr.port = ntohs(s_peers[slot].addr.sin_port);
    out->last_recv_ms = s_peers[slot].last_recv_ms;
    out->rtt_ms = s_peers[slot].rtt_ms;
    pf_strlcpy(out->name, s_peers[slot].name, sizeof(out->name));
    return true;
}

bool Net_IsHost(void)
{
    return s_is_host;
}

bool Net_ParseAddress(const char *host, uint16_t port, struct net_address *out)
{
    if(!host || !out)
        return false;
    struct in_addr a;
    if(inet_pton(AF_INET, host, &a) != 1)
        return false;
    out->host = a.s_addr;
    out->port = port;
    return true;
}

void Net_AddressToString(struct net_address addr, char *buf, size_t buflen)
{
    struct in_addr a;
    a.s_addr = addr.host;
    char ip[64];
#if defined(_WIN32)
    strcpy(ip, inet_ntoa(a));
#else
    inet_ntop(AF_INET, &a, ip, sizeof(ip));
#endif
    pf_snprintf(buf, buflen, "%s:%u", ip, (unsigned)addr.port);
}
