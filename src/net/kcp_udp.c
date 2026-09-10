#include "kcp_udp.h"
#include "ikcp.h"

#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET ls_sock;
#define LS_INVALID INVALID_SOCKET
#define ls_close closesocket
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
typedef int ls_sock;
#define LS_INVALID (-1)
#define ls_close close
#endif

#define LS_MAX_PEERS 7
#define LS_CONV      0x50464B43u
#define LS_RECVQ     64
#define LS_PKTMAX    2048

struct peer{
    bool used;
    struct sockaddr_in addr;
    ikcpcb *kcp;
};

static ls_sock s_sock = LS_INVALID;
static bool s_is_host;
static bool s_ws_inited;
static struct peer s_peers[LS_MAX_PEERS];
static struct sockaddr_in s_host_addr;

static uint32_t ls_now_ms(void)
{
#ifdef _WIN32
    return (uint32_t)GetTickCount();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + (uint32_t)(ts.tv_nsec / 1000000u));
#endif
}

struct recvm{
    int peer;
    int len;
    uint8_t data[LS_PKTMAX];
};
static struct recvm s_q[LS_RECVQ];
static int s_qh, s_qt, s_qn;

static int udp_output(const char *buf, int len, ikcpcb *kcp, void *user)
{
    struct peer *p = user;
    (void)kcp;
    if(!p || s_sock == LS_INVALID)
        return -1;
    sendto(s_sock, buf, len, 0, (struct sockaddr*)&p->addr, sizeof(p->addr));
    return 0;
}

static void q_push(int peer, const uint8_t *d, int n)
{
    if(n <= 0 || n > LS_PKTMAX || s_qn >= LS_RECVQ)
        return;
    s_q[s_qt].peer = peer;
    memcpy(s_q[s_qt].data, d, (size_t)n);
    s_q[s_qt].len = n;
    s_qt = (s_qt + 1) % LS_RECVQ;
    s_qn++;
}

static struct peer *peer_by_addr(const struct sockaddr_in *a, bool create)
{
    for(int i = 0; i < LS_MAX_PEERS; i++) {
        if(s_peers[i].used
        && s_peers[i].addr.sin_addr.s_addr == a->sin_addr.s_addr
        && s_peers[i].addr.sin_port == a->sin_port)
            return &s_peers[i];
    }
    if(!create)
        return NULL;
    for(int i = 0; i < LS_MAX_PEERS; i++) {
        if(s_peers[i].used)
            continue;
        s_peers[i].used = true;
        s_peers[i].addr = *a;
        s_peers[i].kcp = ikcp_create(LS_CONV, &s_peers[i]);
        if(!s_peers[i].kcp) {
            s_peers[i].used = false;
            return NULL;
        }
        ikcp_nodelay(s_peers[i].kcp, 1, 10, 2, 1);
        ikcp_wndsize(s_peers[i].kcp, 128, 128);
        ikcp_setoutput(s_peers[i].kcp, udp_output);
        fprintf(stderr, "LS: peer connected %s:%d slot %d\n",
            inet_ntoa(a->sin_addr), ntohs(a->sin_port), i);
        return &s_peers[i];
    }
    return NULL;
}

static bool sock_init(uint16_t bind_port)
{
#ifdef _WIN32
    if(!s_ws_inited) {
        WSADATA wsa;
        if(WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
            return false;
        s_ws_inited = true;
    }
#endif
    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(s_sock == LS_INVALID)
        return false;
#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket(s_sock, FIONBIO, &nb);
#else
    fcntl(s_sock, F_SETFL, fcntl(s_sock, F_GETFL, 0) | O_NONBLOCK);
#endif
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(bind_port);
    if(bind(s_sock, (struct sockaddr*)&a, sizeof(a)) != 0) {
        ls_close(s_sock);
        s_sock = LS_INVALID;
        return false;
    }
    return true;
}

bool KcpUdp_Listen(uint16_t port)
{
    KcpUdp_Shutdown();
    s_is_host = true;
    return sock_init(port);
}

bool KcpUdp_Connect(const char *ip, uint16_t port)
{
    KcpUdp_Shutdown();
    s_is_host = false;
    if(!sock_init(0))
        return false;
    memset(&s_host_addr, 0, sizeof(s_host_addr));
    s_host_addr.sin_family = AF_INET;
    s_host_addr.sin_port = htons(port);
#ifdef _WIN32
    s_host_addr.sin_addr.s_addr = inet_addr(ip);
    if(s_host_addr.sin_addr.s_addr == INADDR_NONE)
        return false;
#else
    if(inet_pton(AF_INET, ip, &s_host_addr.sin_addr) != 1)
        return false;
#endif
    struct peer *p = peer_by_addr(&s_host_addr, true);
    return p != NULL;
}

void KcpUdp_Shutdown(void)
{
    for(int i = 0; i < LS_MAX_PEERS; i++) {
        if(s_peers[i].kcp)
            ikcp_release(s_peers[i].kcp);
        s_peers[i].kcp = NULL;
        s_peers[i].used = false;
    }
    if(s_sock != LS_INVALID) {
        ls_close(s_sock);
        s_sock = LS_INVALID;
    }
    s_qh = s_qt = s_qn = 0;
    s_is_host = false;
}

void KcpUdp_Poll(void)
{
    if(s_sock == LS_INVALID)
        return;
    for(;;) {
        uint8_t buf[2048];
        struct sockaddr_in from;
#ifdef _WIN32
        int flen = sizeof(from);
#else
        socklen_t flen = sizeof(from);
#endif
        int n = recvfrom(s_sock, (char*)buf, sizeof(buf), 0, (struct sockaddr*)&from, &flen);
        if(n <= 0)
            break;
        struct peer *p = peer_by_addr(&from, true);
        if(p && p->kcp)
            ikcp_input(p->kcp, (const char*)buf, n);
    }
    IUINT32 now = ls_now_ms();
    char msg[LS_PKTMAX];
    for(int i = 0; i < LS_MAX_PEERS; i++) {
        if(!s_peers[i].used || !s_peers[i].kcp)
            continue;
        ikcp_update(s_peers[i].kcp, now);
        for(;;) {
            int n = ikcp_recv(s_peers[i].kcp, msg, LS_PKTMAX);
            if(n <= 0)
                break;
            q_push(i, (const uint8_t*)msg, n);
        }
    }
}

int KcpUdp_RecvFrom(int *peer, uint8_t *buf, int max)
{
    if(s_qn <= 0)
        return 0;
    if(peer)
        *peer = s_q[s_qh].peer;
    int n = s_q[s_qh].len;
    if(n > max)
        n = max;
    memcpy(buf, s_q[s_qh].data, (size_t)n);
    s_qh = (s_qh + 1) % LS_RECVQ;
    s_qn--;
    return n;
}

int KcpUdp_Recv(uint8_t *buf, int max)
{
    int peer;
    return KcpUdp_RecvFrom(&peer, buf, max);
}

bool KcpUdp_SendTo(int peer, const uint8_t *buf, int len)
{
    if(peer < 0 || peer >= LS_MAX_PEERS || !s_peers[peer].used || !s_peers[peer].kcp)
        return false;
    return ikcp_send(s_peers[peer].kcp, (const char*)buf, len) >= 0;
}

bool KcpUdp_Broadcast(const uint8_t *buf, int len)
{
    bool any = false;
    for(int i = 0; i < LS_MAX_PEERS; i++) {
        if(s_peers[i].used && s_peers[i].kcp) {
            if(ikcp_send(s_peers[i].kcp, (const char*)buf, len) >= 0)
                any = true;
        }
    }
    return any;
}

int KcpUdp_PeerCount(void)
{
    int n = 0;
    for(int i = 0; i < LS_MAX_PEERS; i++) {
        if(s_peers[i].used)
            n++;
    }
    return n;
}

bool KcpUdp_IsHost(void) { return s_is_host; }
