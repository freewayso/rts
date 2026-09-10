/*
 *  Copyright (C) 2026 freewayso  https://github.com/freewayso
 *  This file is part of a modified Permafrost Engine and is released
 *  under GPLv3 (see LICENSE.txt and NOTICE.md).
 *
 * Dedicated lockstep room server: UDP/KCP relay, no game simulation.
 * Registers itself with the lobby HTTP directory so clients can list rooms.
 *
 *   room_server --port 40000 --name demo --max 2 --seed 1 \
 *               --lobby 127.0.0.1:27000 --host 127.0.0.1
 */
#include "../src/net/kcp_udp.h"
#include "../src/net/protocol.h"
#include "../src/net/cmd_buffer.h"
#include "../src/net/lobby.h"
#include "../src/net/public/lockstep.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <unistd.h>
#include <time.h>
#endif

#define MAX_NAME 64

static char     s_name[MAX_NAME] = "demo";
static char     s_adv_host[64] = "127.0.0.1";
static char     s_lobby_host[64] = "127.0.0.1";
static uint16_t s_lobby_port = LOBBY_DEFAULT_PORT;
static uint16_t s_port = LS_DEFAULT_PORT;
static int      s_max = 2;
static uint32_t s_seed = 1;
static int      s_lobby_id = -1;

static int      s_njoined;
static int      s_peer_player[LS_MAX_PLAYERS];
static bool     s_started;
static uint32_t s_hashes[LS_BUF_TICKS][LS_MAX_PLAYERS];
static bool     s_hash_has[LS_BUF_TICKS][LS_MAX_PLAYERS];

static uint32_t now_ms(void)
{
#ifdef _WIN32
    return (uint32_t)GetTickCount();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + (uint32_t)(ts.tv_nsec / 1000000u));
#endif
}

static void sleep_ms(unsigned ms)
{
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

static bool send_pkt(int peer, uint8_t type, uint32_t tick, uint8_t pid,
                     const uint8_t *pay, uint16_t plen, uint32_t checksum)
{
    struct ls_packet pkt;
    uint8_t buf[LS_MAX_PACKET];
    size_t n;

    memset(&pkt, 0, sizeof(pkt));
    pkt.msg_type = type;
    pkt.tick = tick;
    pkt.player_id = pid;
    pkt.checksum = checksum;
    pkt.payload_len = plen;
    if(plen)
        memcpy(pkt.payload, pay, plen);
    n = ls_pkt_encode(&pkt, buf, sizeof(buf));
    if(!n)
        return false;
    if(peer < 0)
        return KcpUdp_Broadcast(buf, (int)n);
    return KcpUdp_SendTo(peer, buf, (int)n);
}

static void send_welcome(int peer, int pid)
{
    uint8_t pay[5];
    pay[0] = (uint8_t)(s_seed);
    pay[1] = (uint8_t)(s_seed >> 8);
    pay[2] = (uint8_t)(s_seed >> 16);
    pay[3] = (uint8_t)(s_seed >> 24);
    pay[4] = (uint8_t)s_max;
    send_pkt(peer, LS_MSG_WELCOME, 0, (uint8_t)pid, pay, 5, 0);
}

static void maybe_start(void)
{
    if(s_started || s_njoined < s_max)
        return;
    s_started = true;
    send_pkt(-1, LS_MSG_START, 0, 0, NULL, 0, 0);
    fprintf(stderr, "room: START %d players seed=%u\n", s_njoined, s_seed);
}

static void handle_hello(int peer)
{
    int pid;
    if(peer < 0 || peer >= LS_MAX_PLAYERS)
        return;
    if(s_peer_player[peer] >= 0) {
        send_welcome(peer, s_peer_player[peer]);
        if(s_started)
            send_pkt(peer, LS_MSG_START, 0, 0, NULL, 0, 0);
        return;
    }
    if(s_njoined >= s_max) {
        fprintf(stderr, "room: peer %d rejected (full)\n", peer);
        return;
    }
    pid = s_njoined++;
    s_peer_player[peer] = pid;
    send_welcome(peer, pid);
    fprintf(stderr, "room: player %d joined (peer %d) %d/%d\n",
            pid, peer, s_njoined, s_max);
    maybe_start();
}

static void handle_cmd(const struct ls_packet *pkt)
{
    struct ls_cmd cmds[LS_MAX_CMDS];
    int n = 0;
    if(pkt->player_id >= s_max)
        return;
    if(pkt->payload_len > 0) {
        n = ls_cmds_decode(pkt->payload, pkt->payload_len, cmds, LS_MAX_CMDS);
        if(n < 0)
            n = 0;
    }
    CmdBuf_Put(pkt->tick, pkt->player_id, cmds, n);
    send_pkt(-1, LS_MSG_CMD_FRAME, pkt->tick, pkt->player_id,
             pkt->payload, pkt->payload_len, pkt->checksum);
}

static void handle_snap_req(int peer, const struct ls_packet *pkt)
{
    uint32_t from = pkt->tick + 1;
    uint32_t newest = CmdBuf_LogNewest();
    uint32_t oldest = CmdBuf_LogOldest();
    uint8_t meta[8];
    uint32_t t;
    int p;

    if(from < oldest)
        from = oldest;
    meta[0] = (uint8_t)from;
    meta[1] = (uint8_t)(from >> 8);
    meta[2] = (uint8_t)(from >> 16);
    meta[3] = (uint8_t)(from >> 24);
    meta[4] = (uint8_t)newest;
    meta[5] = (uint8_t)(newest >> 8);
    meta[6] = (uint8_t)(newest >> 16);
    meta[7] = (uint8_t)(newest >> 24);
    send_pkt(peer, LS_MSG_SNAP_BEGIN, from, 0, meta, 8, 0);

    for(t = from; t <= newest; t++) {
        for(p = 0; p < s_max; p++) {
            struct ls_cmd cmds[LS_MAX_CMDS];
            uint8_t payload[LS_MAX_PAYLOAD];
            int n = CmdBuf_LogGet(t, p, cmds, LS_MAX_CMDS);
            size_t enc;
            if(n < 0)
                n = 0;
            enc = ls_cmds_encode(cmds, n, payload, sizeof(payload));
            send_pkt(peer, LS_MSG_CMD_REPLAY, t, (uint8_t)p,
                     payload, (uint16_t)enc, 0);
        }
    }
}

static void handle_hash(const struct ls_packet *pkt)
{
    uint32_t slot;
    int p;
    uint32_t h = pkt->checksum;
    if(pkt->player_id >= s_max)
        return;
    if(pkt->payload_len >= 4) {
        h = (uint32_t)pkt->payload[0]
          | ((uint32_t)pkt->payload[1] << 8)
          | ((uint32_t)pkt->payload[2] << 16)
          | ((uint32_t)pkt->payload[3] << 24);
    }
    slot = pkt->tick % LS_BUF_TICKS;
    s_hashes[slot][pkt->player_id] = h;
    s_hash_has[slot][pkt->player_id] = true;
    for(p = 0; p < s_max; p++) {
        if(!s_hash_has[slot][p])
            return;
        if(s_hashes[slot][p] != h) {
            fprintf(stderr, "room: DESYNC tick %u p0=%08x p%d=%08x\n",
                    pkt->tick, s_hashes[slot][0], p, s_hashes[slot][p]);
            send_pkt(-1, LS_MSG_DESYNC, pkt->tick, 0, NULL, 0, 0);
            return;
        }
    }
}

static void on_packet(int peer, const uint8_t *buf, int len)
{
    struct ls_packet pkt;
    if(!ls_pkt_decode(buf, (size_t)len, &pkt))
        return;
    switch(pkt.msg_type) {
    case LS_MSG_HELLO:
        handle_hello(peer);
        break;
    case LS_MSG_CMD_FRAME:
    case LS_MSG_CMD_EMPTY:
        handle_cmd(&pkt);
        break;
    case LS_MSG_SNAP_REQ:
        handle_snap_req(peer, &pkt);
        break;
    case LS_MSG_HASH_REPORT:
        handle_hash(&pkt);
        break;
    case LS_MSG_LEAVE:
        fprintf(stderr, "room: leave peer %d\n", peer);
        break;
    default:
        break;
    }
}

static const char *arg_val(int argc, char **argv, const char *key, const char *def)
{
    int i;
    size_t klen = strlen(key);
    for(i = 1; i < argc; i++) {
        if(strncmp(argv[i], key, klen) == 0 && argv[i][klen] == '=')
            return argv[i] + klen + 1;
        if(strcmp(argv[i], key) == 0 && i + 1 < argc)
            return argv[++i];
    }
    return def;
}

static void parse_hostport(const char *s, char *host, size_t hostsz, uint16_t *port)
{
    const char *colon;
    if(!s)
        return;
    colon = strrchr(s, ':');
    if(!colon) {
        snprintf(host, hostsz, "%s", s);
        return;
    }
    if((size_t)(colon - s) >= hostsz)
        return;
    memcpy(host, s, (size_t)(colon - s));
    host[colon - s] = 0;
    *port = (uint16_t)atoi(colon + 1);
}

int main(int argc, char **argv)
{
    uint32_t last_hb = 0;
    uint32_t last_reg = 0;
    int i;

    snprintf(s_name, sizeof(s_name), "%s", arg_val(argc, argv, "--name", "demo"));
    snprintf(s_adv_host, sizeof(s_adv_host), "%s", arg_val(argc, argv, "--host", "127.0.0.1"));
    parse_hostport(arg_val(argc, argv, "--lobby", "127.0.0.1:27000"),
                   s_lobby_host, sizeof(s_lobby_host), &s_lobby_port);
    s_port = (uint16_t)atoi(arg_val(argc, argv, "--port", "40000"));
    s_max = atoi(arg_val(argc, argv, "--max", "2"));
    s_seed = (uint32_t)strtoul(arg_val(argc, argv, "--seed", "1"), NULL, 10);
    if(s_port == 0)
        s_port = LS_DEFAULT_PORT;
    if(s_max < 1)
        s_max = 1;
    if(s_max > LS_MAX_PLAYERS)
        s_max = LS_MAX_PLAYERS;

    for(i = 0; i < LS_MAX_PLAYERS; i++)
        s_peer_player[i] = -1;
    CmdBuf_Reset(s_max);

    if(!KcpUdp_Listen(s_port)) {
        fprintf(stderr, "room: listen %u failed\n", (unsigned)s_port);
        return 1;
    }
    fprintf(stderr, "room: '%s' UDP/KCP :%u max=%d seed=%u lobby=%s:%u advertise=%s\n",
            s_name, (unsigned)s_port, s_max, s_seed,
            s_lobby_host, (unsigned)s_lobby_port, s_adv_host);

    for(;;) {
        uint8_t buf[LS_MAX_PACKET];
        int peer, n;
        uint32_t now;

        KcpUdp_Poll();
        for(;;) {
            peer = 0;
            n = KcpUdp_RecvFrom(&peer, buf, (int)sizeof(buf));
            if(n <= 0)
                break;
            on_packet(peer, buf, n);
        }

        now = now_ms();
        if(s_lobby_id < 0 && (last_reg == 0 || (now - last_reg) >= 3000u)) {
            s_lobby_id = Lobby_RegisterRoom(s_lobby_host, s_lobby_port,
                                            s_name, s_adv_host, s_port,
                                            s_max, s_seed);
            last_reg = now;
            if(s_lobby_id >= 0)
                fprintf(stderr, "room: registered lobby id=%d\n", s_lobby_id);
            else
                fprintf(stderr, "room: lobby register failed (is lobby_server running?)\n");
        }
        if(s_lobby_id >= 0 && (now - last_hb) >= 3000u) {
            if(!Lobby_Heartbeat(s_lobby_host, s_lobby_port, s_lobby_id, s_njoined)) {
                fprintf(stderr, "room: heartbeat failed, will re-register\n");
                s_lobby_id = -1;
            }
            last_hb = now;
        }
        sleep_ms(1);
    }
}
