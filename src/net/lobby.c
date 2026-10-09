/*
 *  Multiplayer lobby: host/join, player list, match start handshake.
 */

#define MEM_FILE_SYS MEM_SYS_NET
#define MEM_FILE_SUB 0

#include "public/net.h"
#include "../lib/public/pf_string.h"
#include "../mem.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <SDL.h>

#pragma pack(push, 1)
struct lobby_hdr{
    uint8_t  type;
    uint8_t  reserved;
    uint16_t payload_len;
};

struct lobby_hello{
    uint32_t protocol;
    int32_t  faction_id;
    char     name[NET_MAX_NAME_LEN];
};

struct lobby_welcome{
    int32_t  assigned_slot;
    int32_t  host_slot;
    uint32_t protocol;
};

struct lobby_player_wire{
    int32_t slot;
    int32_t faction_id;
    uint8_t ready;
    uint8_t connected;
    uint8_t pad[2];
    char    name[NET_MAX_NAME_LEN];
};

struct lobby_start_wire{
    uint32_t protocol_version;
    uint64_t rng_seed;
    uint32_t sim_hz;
    uint32_t command_delay;
    int32_t  local_player_id;
    int32_t  num_players;
    char     map_path[NET_MAX_MAP_PATH];
};
#pragma pack(pop)

static struct lobby_callbacks s_cb;
static bool                   s_active;
static bool                   s_is_host;
static int                    s_local_slot = -1;
static int                    s_host_peer_slot = -1; /* client: peer index of host */
static char                   s_local_name[NET_MAX_NAME_LEN];
static int                    s_local_faction;
static char                   s_map_path[NET_MAX_MAP_PATH];
static uint64_t               s_seed;
static struct lobby_player    s_players[NET_MAX_PEERS];
/* Host: net peer_slot -> lobby seat. Clients leave this unused. */
static int                    s_peer_to_slot[NET_MAX_PEERS];

static void notify_player_list(void)
{
    if(s_cb.on_player_list)
        s_cb.on_player_list(s_players, NET_MAX_PEERS, s_cb.user);
}

static void send_msg(int peer, uint8_t type, const void *payload, uint16_t len)
{
    uint8_t buf[NET_MAX_PACKET];
    if(sizeof(struct lobby_hdr) + len > sizeof(buf))
        return;
    struct lobby_hdr *h = (struct lobby_hdr *)buf;
    h->type = type;
    h->reserved = 0;
    h->payload_len = len;
    if(len && payload)
        memcpy(buf + sizeof(*h), payload, len);
    Net_Send(peer, NET_CH_RELIABLE, buf, sizeof(*h) + len);
}

static void broadcast_player_list(void)
{
    uint8_t buf[NET_MAX_PACKET];
    struct lobby_hdr *h = (struct lobby_hdr *)buf;
    h->type = LOBBY_MSG_PLAYER_LIST;
    h->reserved = 0;
    size_t off = sizeof(*h);
    for(int i = 0; i < NET_MAX_PEERS; i++) {
        struct lobby_player_wire w;
        memset(&w, 0, sizeof(w));
        w.slot = s_players[i].slot;
        w.faction_id = s_players[i].faction_id;
        w.ready = s_players[i].ready ? 1 : 0;
        w.connected = s_players[i].connected ? 1 : 0;
        pf_strlcpy(w.name, s_players[i].name, sizeof(w.name));
        if(off + sizeof(w) > sizeof(buf))
            break;
        memcpy(buf + off, &w, sizeof(w));
        off += sizeof(w);
    }
    h->payload_len = (uint16_t)(off - sizeof(*h));
    Net_Broadcast(NET_CH_RELIABLE, buf, off);
    notify_player_list();
}

static int alloc_player_slot(void)
{
    for(int i = 0; i < NET_MAX_PEERS; i++) {
        if(!s_players[i].connected)
            return i;
    }
    return -1;
}

static void clear_lobby_slot(int slot)
{
    if(slot < 0 || slot >= NET_MAX_PEERS)
        return;
    memset(&s_players[slot], 0, sizeof(s_players[slot]));
    s_players[slot].slot = slot;
}

static void handle_hello(int peer, const struct lobby_hello *hello)
{
    if(!s_is_host)
        return;
    if(peer < 0 || peer >= NET_MAX_PEERS)
        return;
    if(hello->protocol != NET_PROTOCOL_VERSION) {
        uint8_t reason = 1;
        send_msg(peer, LOBBY_MSG_REJECT, &reason, 1);
        Net_DisconnectPeer(peer);
        return;
    }
    /* Reconnect: reuse previous seat for this net peer if still mapped. */
    int slot = s_peer_to_slot[peer];
    if(slot < 0 || slot >= NET_MAX_PEERS || slot == s_local_slot)
        slot = alloc_player_slot();
    if(slot < 0) {
        uint8_t reason = 2;
        send_msg(peer, LOBBY_MSG_REJECT, &reason, 1);
        Net_DisconnectPeer(peer);
        return;
    }
    s_peer_to_slot[peer] = slot;
    s_players[slot].slot = slot;
    s_players[slot].faction_id = hello->faction_id;
    s_players[slot].ready = false;
    s_players[slot].connected = true;
    pf_strlcpy(s_players[slot].name, hello->name, sizeof(s_players[slot].name));

    struct lobby_welcome wel;
    wel.assigned_slot = slot;
    wel.host_slot = s_local_slot;
    wel.protocol = NET_PROTOCOL_VERSION;
    send_msg(peer, LOBBY_MSG_WELCOME, &wel, sizeof(wel));
    broadcast_player_list();
}

static void handle_packet(int peer, const uint8_t *data, size_t len)
{
    if(len < sizeof(struct lobby_hdr))
        return;
    const struct lobby_hdr *h = (const struct lobby_hdr *)data;
    const uint8_t *payload = data + sizeof(*h);
    if(sizeof(*h) + h->payload_len > len)
        return;

    switch(h->type) {
    case LOBBY_MSG_HELLO:
        if(h->payload_len >= sizeof(struct lobby_hello))
            handle_hello(peer, (const struct lobby_hello *)payload);
        break;
    case LOBBY_MSG_WELCOME:
        if(h->payload_len >= sizeof(struct lobby_welcome)) {
            const struct lobby_welcome *w = (const struct lobby_welcome *)payload;
            s_local_slot = w->assigned_slot;
            s_players[s_local_slot].slot = s_local_slot;
            s_players[s_local_slot].faction_id = s_local_faction;
            s_players[s_local_slot].connected = true;
            pf_strlcpy(s_players[s_local_slot].name, s_local_name,
                       sizeof(s_players[s_local_slot].name));
        }
        break;
    case LOBBY_MSG_PLAYER_LIST: {
        int count = h->payload_len / (int)sizeof(struct lobby_player_wire);
        memset(s_players, 0, sizeof(s_players));
        for(int i = 0; i < count && i < NET_MAX_PEERS; i++) {
            const struct lobby_player_wire *w =
                (const struct lobby_player_wire *)(payload + i * sizeof(*w));
            int slot = w->slot;
            if(slot < 0 || slot >= NET_MAX_PEERS)
                continue;
            s_players[slot].slot = slot;
            s_players[slot].faction_id = w->faction_id;
            s_players[slot].ready = w->ready != 0;
            s_players[slot].connected = w->connected != 0;
            pf_strlcpy(s_players[slot].name, w->name, sizeof(s_players[slot].name));
        }
        notify_player_list();
        break;
    }
    case LOBBY_MSG_START_MATCH:
        if(h->payload_len >= sizeof(struct lobby_start_wire)) {
            const struct lobby_start_wire *sw = (const struct lobby_start_wire *)payload;
            struct lobby_start_info info;
            memset(&info, 0, sizeof(info));
            info.protocol_version = sw->protocol_version;
            info.rng_seed = sw->rng_seed;
            info.sim_hz = sw->sim_hz;
            info.command_delay = sw->command_delay;
            /* Prefer seat from wire (host personalizes per peer). Fallback to WELCOME. */
            if(sw->local_player_id >= 0 && sw->local_player_id < NET_MAX_PEERS)
                s_local_slot = sw->local_player_id;
            info.local_player_id = s_local_slot;
            info.num_players = sw->num_players;
            pf_strlcpy(info.map_path, sw->map_path, sizeof(info.map_path));
            memcpy(info.players, s_players, sizeof(info.players));
            if(s_cb.on_match_start)
                s_cb.on_match_start(&info, s_cb.user);
        }
        break;
    case LOBBY_MSG_CHAT:
        if(h->payload_len > 0 && s_cb.on_chat) {
            char text[256];
            size_t n = h->payload_len < sizeof(text) - 1 ? h->payload_len : sizeof(text) - 1;
            memcpy(text, payload, n);
            text[n] = '\0';
            s_cb.on_chat(peer, text, s_cb.user);
        }
        break;
    case LOBBY_MSG_PING:
        send_msg(peer, LOBBY_MSG_PONG, payload, h->payload_len);
        break;
    case LOBBY_MSG_PONG:
        break;
    case LOBBY_MSG_REJECT:
        if(s_cb.on_disconnected)
            s_cb.on_disconnected(peer, s_cb.user);
        break;
    default:
        break;
    }
}

static bool lobby_common_init(const char *local_name, int faction_id,
                              const struct lobby_callbacks *cb)
{
    if(!cb)
        return false;
    s_cb = *cb;
    s_active = true;
    s_local_faction = faction_id;
    s_seed = (uint64_t)SDL_GetTicks() ^ 0xC0FFEEULL;
    pf_strlcpy(s_local_name, local_name ? local_name : "Player", sizeof(s_local_name));
    pf_strlcpy(s_map_path, "assets/maps/demo.pfmap", sizeof(s_map_path));
    memset(s_players, 0, sizeof(s_players));
    for(int i = 0; i < NET_MAX_PEERS; i++)
        s_peer_to_slot[i] = -1;
    return true;
}

bool Lobby_Host(uint16_t port, const char *local_name, int faction_id,
                const struct lobby_callbacks *cb)
{
    Lobby_Shutdown();
    if(!lobby_common_init(local_name, faction_id, cb))
        return false;
    if(!Net_HostCreate(port ? port : NET_DEFAULT_PORT, NET_MAX_PEERS))
        return false;
    s_is_host = true;
    s_local_slot = 0;
    s_players[0].slot = 0;
    s_players[0].faction_id = faction_id;
    s_players[0].ready = true;
    s_players[0].connected = true;
    pf_strlcpy(s_players[0].name, s_local_name, sizeof(s_players[0].name));
    notify_player_list();
    return true;
}

bool Lobby_Join(struct net_address addr, const char *local_name, int faction_id,
                const struct lobby_callbacks *cb)
{
    Lobby_Shutdown();
    if(!lobby_common_init(local_name, faction_id, cb))
        return false;
    if(!Net_ClientCreate())
        return false;
    s_is_host = false;
    s_local_slot = -1;
    if(!Net_Connect(addr))
        return false;
    s_host_peer_slot = 0;
    return true;
}

void Lobby_Shutdown(void)
{
    if(s_active) {
        Net_DisconnectAll();
        Net_Destroy();
    }
    memset(&s_cb, 0, sizeof(s_cb));
    s_active = false;
    s_is_host = false;
    s_local_slot = -1;
    s_host_peer_slot = -1;
}

void Lobby_Service(void)
{
    if(!s_active)
        return;

    struct net_event ev;
    while(Net_Service(0, &ev)) {
        switch(ev.type) {
        case NET_EVENT_CONNECT:
            if(!s_is_host && s_local_slot < 0) {
                struct lobby_hello hello;
                memset(&hello, 0, sizeof(hello));
                hello.protocol = NET_PROTOCOL_VERSION;
                hello.faction_id = s_local_faction;
                pf_strlcpy(hello.name, s_local_name, sizeof(hello.name));
                send_msg(ev.peer_slot, LOBBY_MSG_HELLO, &hello, sizeof(hello));
                s_host_peer_slot = ev.peer_slot;
            }
            break;
        case NET_EVENT_DISCONNECT:
            if(s_is_host && ev.peer_slot >= 0 && ev.peer_slot < NET_MAX_PEERS) {
                int slot = s_peer_to_slot[ev.peer_slot];
                s_peer_to_slot[ev.peer_slot] = -1;
                if(slot > 0) {
                    clear_lobby_slot(slot);
                    broadcast_player_list();
                }
            }
            if(s_cb.on_disconnected)
                s_cb.on_disconnected(ev.peer_slot, s_cb.user);
            break;
        case NET_EVENT_RECEIVE:
            handle_packet(ev.peer_slot, ev.data, ev.data_len);
            break;
        default:
            break;
        }
        Net_FreeEvent(&ev);
    }
}

bool Lobby_SetReady(bool ready)
{
    if(s_local_slot < 0)
        return false;
    s_players[s_local_slot].ready = ready;
    if(s_is_host)
        broadcast_player_list();
    return true;
}

bool Lobby_SetMap(const char *map_path)
{
    if(!s_is_host || !map_path)
        return false;
    pf_strlcpy(s_map_path, map_path, sizeof(s_map_path));
    return true;
}

bool Lobby_SetSeed(uint64_t seed)
{
    if(!s_is_host)
        return false;
    s_seed = seed;
    return true;
}

bool Lobby_StartMatch(void)
{
    if(!s_is_host)
        return false;

    struct lobby_start_wire sw;
    memset(&sw, 0, sizeof(sw));
    sw.protocol_version = NET_PROTOCOL_VERSION;
    sw.rng_seed = s_seed;
    sw.sim_hz = 10;
    sw.command_delay = 2;
    sw.num_players = 0;
    for(int i = 0; i < NET_MAX_PEERS; i++) {
        if(s_players[i].connected)
            sw.num_players++;
    }
    if(sw.num_players < 1)
        sw.num_players = 1;
    pf_strlcpy(sw.map_path, s_map_path, sizeof(sw.map_path));

    /* Personalized seat id per peer — clients must not depend on WELCOME alone
     * (join retry / packet reorder previously left local_player_id = -1). */
    for(int peer = 0; peer < NET_MAX_PEERS; peer++) {
        int slot = s_peer_to_slot[peer];
        if(slot < 0 || !s_players[slot].connected)
            continue;
        sw.local_player_id = slot;
        uint8_t buf[NET_MAX_PACKET];
        struct lobby_hdr *h = (struct lobby_hdr *)buf;
        h->type = LOBBY_MSG_START_MATCH;
        h->reserved = 0;
        h->payload_len = sizeof(sw);
        memcpy(buf + sizeof(*h), &sw, sizeof(sw));
        Net_Send(peer, NET_CH_RELIABLE, buf, sizeof(*h) + sizeof(sw));
    }

    struct lobby_start_info info;
    memset(&info, 0, sizeof(info));
    info.protocol_version = sw.protocol_version;
    info.rng_seed = sw.rng_seed;
    info.sim_hz = sw.sim_hz;
    info.command_delay = sw.command_delay;
    info.local_player_id = s_local_slot;
    info.num_players = sw.num_players;
    pf_strlcpy(info.map_path, sw.map_path, sizeof(info.map_path));
    memcpy(info.players, s_players, sizeof(info.players));
    if(s_cb.on_match_start)
        s_cb.on_match_start(&info, s_cb.user);
    return true;
}

bool Lobby_SendChat(const char *text)
{
    if(!text)
        return false;
    size_t len = strlen(text);
    if(len > 200)
        len = 200;
    uint8_t buf[NET_MAX_PACKET];
    struct lobby_hdr *h = (struct lobby_hdr *)buf;
    h->type = LOBBY_MSG_CHAT;
    h->reserved = 0;
    h->payload_len = (uint16_t)len;
    memcpy(buf + sizeof(*h), text, len);
    if(s_is_host)
        return Net_Broadcast(NET_CH_RELIABLE, buf, sizeof(*h) + len);
    if(s_host_peer_slot >= 0)
        return Net_Send(s_host_peer_slot, NET_CH_RELIABLE, buf, sizeof(*h) + len);
    return false;
}

bool Lobby_IsHost(void)
{
    return s_is_host;
}

int Lobby_LocalSlot(void)
{
    return s_local_slot;
}

int Lobby_PlayerCount(void)
{
    int n = 0;
    for(int i = 0; i < NET_MAX_PEERS; i++) {
        if(s_players[i].connected)
            n++;
    }
    return n;
}

bool Lobby_GetPlayers(struct lobby_player *out, int *inout_count)
{
    if(!out || !inout_count)
        return false;
    int max = *inout_count;
    int n = 0;
    for(int i = 0; i < NET_MAX_PEERS && n < max; i++) {
        if(!s_players[i].connected)
            continue;
        out[n++] = s_players[i];
    }
    *inout_count = n;
    return true;
}
