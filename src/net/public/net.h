/*
 *  This file is part of Permafrost Engine.
 *  Copyright (C) 2017-2026 Eduard Permyakov / Lockstep contributors
 *
 *  Reliable UDP transport and multiplayer lobby (ENet-style API, vendored
 *  implementation so the engine does not require an external ENet build).
 */

#ifndef NET_H
#define NET_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define NET_MAX_PEERS           8
#define NET_MAX_NAME_LEN        32
#define NET_MAX_MAP_PATH        256
#define NET_DEFAULT_PORT        27750
#define NET_PROTOCOL_VERSION    1
#define NET_MAX_PACKET          1200

enum net_channel{
    NET_CH_RELIABLE = 0,
    NET_CH_UNRELIABLE = 1,
    NET_CH_COUNT
};

enum net_peer_state{
    NET_PEER_DISCONNECTED = 0,
    NET_PEER_CONNECTING,
    NET_PEER_CONNECTED
};

enum net_event_type{
    NET_EVENT_NONE = 0,
    NET_EVENT_CONNECT,
    NET_EVENT_DISCONNECT,
    NET_EVENT_RECEIVE
};

struct net_address{
    uint32_t host; /* network byte order */
    uint16_t port; /* host byte order */
};

struct net_peer{
    int                slot;
    enum net_peer_state state;
    struct net_address addr;
    uint32_t           last_recv_ms;
    uint32_t           rtt_ms;
    char               name[NET_MAX_NAME_LEN];
};

struct net_event{
    enum net_event_type type;
    int                 peer_slot;
    uint8_t             channel;
    uint8_t            *data;
    size_t              data_len;
};

bool     Net_Init(void);
void     Net_Shutdown(void);

/* Create host (server) listening on port, or client (port 0 = ephemeral). */
bool     Net_HostCreate(uint16_t port, int max_peers);
bool     Net_ClientCreate(void);
void     Net_Destroy(void);

bool     Net_Connect(struct net_address addr);
void     Net_DisconnectPeer(int peer_slot);
void     Net_DisconnectAll(void);

/* Pump sockets; fills out_event when true. Caller must Net_FreeEvent. */
bool     Net_Service(uint32_t timeout_ms, struct net_event *out_event);
void     Net_FreeEvent(struct net_event *ev);

bool     Net_Send(int peer_slot, enum net_channel ch, const void *data, size_t len);
bool     Net_Broadcast(enum net_channel ch, const void *data, size_t len);

int      Net_PeerCount(void);
bool     Net_GetPeer(int slot, struct net_peer *out);
bool     Net_IsHost(void);
uint32_t Net_NowMs(void);

bool     Net_ParseAddress(const char *host, uint16_t port, struct net_address *out);
void     Net_AddressToString(struct net_address addr, char *buf, size_t buflen);

/* Lobby / session control plane ------------------------------------------- */

enum lobby_msg_type{
    LOBBY_MSG_HELLO = 1,
    LOBBY_MSG_WELCOME,
    LOBBY_MSG_PLAYER_LIST,
    LOBBY_MSG_CHAT,
    LOBBY_MSG_START_MATCH,
    LOBBY_MSG_PING,
    LOBBY_MSG_PONG,
    LOBBY_MSG_KICK,
    LOBBY_MSG_REJECT
};

struct lobby_player{
    int      slot;
    int      faction_id;
    bool     ready;
    bool     connected;
    char     name[NET_MAX_NAME_LEN];
};

struct lobby_start_info{
    uint32_t protocol_version;
    uint64_t rng_seed;
    uint32_t sim_hz;
    uint32_t command_delay;
    int      local_player_id;
    int      num_players;
    char     map_path[NET_MAX_MAP_PATH];
    struct lobby_player players[NET_MAX_PEERS];
};

struct lobby_callbacks{
    void (*on_player_list)(const struct lobby_player *players, int count, void *user);
    void (*on_match_start)(const struct lobby_start_info *info, void *user);
    void (*on_chat)(int from_slot, const char *text, void *user);
    void (*on_disconnected)(int peer_slot, void *user);
    void *user;
};

bool Lobby_Host(uint16_t port, const char *local_name, int faction_id,
                const struct lobby_callbacks *cb);
bool Lobby_Join(struct net_address addr, const char *local_name, int faction_id,
                const struct lobby_callbacks *cb);
void Lobby_Shutdown(void);
void Lobby_Service(void);

bool Lobby_SetReady(bool ready);
bool Lobby_SetMap(const char *map_path);
bool Lobby_SetSeed(uint64_t seed);
bool Lobby_StartMatch(void); /* host only */
bool Lobby_SendChat(const char *text);
bool Lobby_IsHost(void);
int  Lobby_LocalSlot(void);
int  Lobby_PlayerCount(void);
bool Lobby_GetPlayers(struct lobby_player *out, int *inout_count);

#endif
