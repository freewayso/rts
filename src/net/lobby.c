#include "lobby.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET lb_sock;
#define LB_INVALID INVALID_SOCKET
#define lb_close closesocket
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int lb_sock;
#define LB_INVALID (-1)
#define lb_close close
#endif

static bool s_ws;

static bool ws_ok(void)
{
#ifdef _WIN32
    if(!s_ws) {
        WSADATA wsa;
        if(WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
            return false;
        s_ws = true;
    }
#endif
    return true;
}

static lb_sock tcp_connect(const char *host, uint16_t port)
{
    if(!ws_ok())
        return LB_INVALID;
    lb_sock s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(s == LB_INVALID)
        return LB_INVALID;
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
#ifdef _WIN32
    a.sin_addr.s_addr = inet_addr(host);
    if(a.sin_addr.s_addr == INADDR_NONE) {
        lb_close(s);
        return LB_INVALID;
    }
#else
    if(inet_pton(AF_INET, host, &a.sin_addr) != 1) {
        lb_close(s);
        return LB_INVALID;
    }
#endif
#ifdef _WIN32
    DWORD ms = 3000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&ms, sizeof(ms));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&ms, sizeof(ms));
#else
    struct timeval tv = {3, 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    if(connect(s, (struct sockaddr*)&a, sizeof(a)) != 0) {
        lb_close(s);
        return LB_INVALID;
    }
    return s;
}

static int http_exchange(const char *host, uint16_t port, const char *req,
                         char *resp, int maxresp)
{
    lb_sock s = tcp_connect(host, port);
    if(s == LB_INVALID)
        return -1;
    int reqlen = (int)strlen(req);
    if(send(s, req, reqlen, 0) != reqlen) {
        lb_close(s);
        return -1;
    }
    int n = 0;
    while(n < maxresp - 1) {
        int r = recv(s, resp + n, maxresp - 1 - n, 0);
        if(r <= 0)
            break;
        n += r;
    }
    resp[n] = 0;
    lb_close(s);
    return n;
}

static const char *http_body(const char *resp)
{
    const char *p = strstr(resp, "\r\n\r\n");
    if(p)
        return p + 4;
    p = strstr(resp, "\n\n");
    if(p)
        return p + 2;
    return resp;
}

int Lobby_ListRooms(const char *lobby_host, uint16_t lobby_port,
                    struct lobby_room *out, int max_rooms)
{
    char req[256], resp[8192];
    snprintf(req, sizeof(req),
             "GET /rooms.txt HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n",
             lobby_host);
    if(http_exchange(lobby_host, lobby_port, req, resp, sizeof(resp)) < 0)
        return -1;
    const char *body = http_body(resp);
    int n = 0;
    const char *line = body;
    while(*line && n < max_rooms) {
        while(*line == '\r' || *line == '\n' || *line == ' ')
            line++;
        if(*line == 0)
            break;
        const char *nl = strchr(line, '\n');
        if(*line == '#') {
            if(!nl)
                break;
            line = nl + 1;
            continue;
        }
        struct lobby_room *r = &out[n];
        memset(r, 0, sizeof(*r));
        int port = 0;
        unsigned seed = 0;
        if(sscanf(line, "%d %63s %63s %d %d %d %u",
                  &r->id, r->name, r->host, &port,
                  &r->players, &r->max_players, &seed) >= 6) {
            r->port = (uint16_t)port;
            r->seed = seed;
            n++;
        }
        if(!nl)
            break;
        line = nl + 1;
    }
    return n;
}

int Lobby_RegisterRoom(const char *lobby_host, uint16_t lobby_port,
                       const char *name, const char *game_host, uint16_t game_port,
                       int max_players, uint32_t seed)
{
    char body[256], req[512], resp[1024];
    snprintf(body, sizeof(body),
             "name=%s&host=%s&port=%u&max=%d&seed=%u",
             name, game_host, (unsigned)game_port, max_players, seed);
    snprintf(req, sizeof(req),
             "POST /rooms HTTP/1.0\r\nHost: %s\r\n"
             "Content-Type: application/x-www-form-urlencoded\r\n"
             "Content-Length: %d\r\nConnection: close\r\n\r\n%s",
             lobby_host, (int)strlen(body), body);
    if(http_exchange(lobby_host, lobby_port, req, resp, sizeof(resp)) < 0)
        return -1;
    const char *b = http_body(resp);
    int id = -1;
    sscanf(b, "OK id=%d", &id);
    return id;
}

bool Lobby_Heartbeat(const char *lobby_host, uint16_t lobby_port,
                     int room_id, int players)
{
    char body[64], req[512], resp[512];
    snprintf(body, sizeof(body), "id=%d&players=%d", room_id, players);
    snprintf(req, sizeof(req),
             "POST /hb HTTP/1.0\r\nHost: %s\r\n"
             "Content-Type: application/x-www-form-urlencoded\r\n"
             "Content-Length: %d\r\nConnection: close\r\n\r\n%s",
             lobby_host, (int)strlen(body), body);
    if(http_exchange(lobby_host, lobby_port, req, resp, sizeof(resp)) < 0)
        return false;
    return strstr(http_body(resp), "OK") != NULL;
}

bool Lobby_Unregister(const char *lobby_host, uint16_t lobby_port, int room_id)
{
    char body[32], req[512], resp[512];
    snprintf(body, sizeof(body), "id=%d", room_id);
    snprintf(req, sizeof(req),
             "POST /unreg HTTP/1.0\r\nHost: %s\r\n"
             "Content-Type: application/x-www-form-urlencoded\r\n"
             "Content-Length: %d\r\nConnection: close\r\n\r\n%s",
             lobby_host, (int)strlen(body), body);
    if(http_exchange(lobby_host, lobby_port, req, resp, sizeof(resp)) < 0)
        return false;
    return strstr(http_body(resp), "OK") != NULL;
}
