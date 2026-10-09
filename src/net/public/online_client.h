#pragma once
/*
 * Client-side Login + Room APIs (symmetric to server messages).
 */
#ifndef PF_ONLINE_CLIENT_H
#define PF_ONLINE_CLIENT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PF_OC_MAX_NAME 64
#define PF_OC_MAX_MAP  256
#define PF_OC_MAX_TOKEN 64
#define PF_OC_MAX_ROOMS 64
#define PF_OC_MAX_PLAYERS 8

struct pf_room_player {
    int64_t uid;
    char    username[32];
    int     slot;
};

struct pf_room_info {
    uint32_t id;
    char     name[PF_OC_MAX_NAME];
    char     map_path[PF_OC_MAX_MAP];
    int      max_players;
    int      player_count;
    int64_t  owner_uid;
    int      state; /* pf_room_state */
    struct pf_room_player players[PF_OC_MAX_PLAYERS];
};

/* Match session after match_start (in-game handshake over UDP relay) */
struct pf_match_info {
    uint32_t room_id;
    uint64_t rng_seed;
    uint32_t sim_hz;
    uint32_t command_delay;
    char     map_path[PF_OC_MAX_MAP];
    int      local_slot;
    uint32_t kcp_conv;
    uint16_t udp_port;
    int      player_count;
    struct pf_room_player players[PF_OC_MAX_PLAYERS];
};

struct pf_match_peer {
    int      slot;
    char     username[32];
    bool     in_game;      /* heard UDP ENTER/tick from this peer */
    uint32_t last_tick;
};

/* ---- Login ---- */
bool LoginClient_Connect(const char *host, uint16_t port);
void LoginClient_Disconnect(void);
bool LoginClient_Register(const char *user, const char *pass);
bool LoginClient_Login(const char *user, const char *pass);
const char *LoginClient_Token(void);
int64_t     LoginClient_Uid(void);
const char *LoginClient_Username(void);
bool LoginClient_Service(void);

/* ---- Room ---- */
bool RoomClient_Connect(const char *host, uint16_t tcp_port);
void RoomClient_Disconnect(void);
bool RoomClient_Auth(const char *token, int64_t uid, const char *username);

bool RoomClient_ListRooms(void);
bool RoomClient_QueryRoom(uint32_t room_id);
bool RoomClient_CreateRoom(const char *name, const char *map_path, int max_players);
bool RoomClient_DeleteRoom(uint32_t room_id);
bool RoomClient_JoinRoom(uint32_t room_id);
bool RoomClient_LeaveRoom(uint32_t room_id);
bool RoomClient_StartMatch(uint32_t seed, int sim_hz, int command_delay);

typedef void (*RoomClient_OnListFn)(const struct pf_room_info *rooms, int n, void *ud);
typedef void (*RoomClient_OnRoomFn)(const struct pf_room_info *room, int err, void *ud);
typedef void (*RoomClient_OnDeletedFn)(uint32_t room_id, int reason, void *ud);

void RoomClient_SetCallbacks(RoomClient_OnListFn on_list,
                             RoomClient_OnRoomFn on_room,
                             RoomClient_OnDeletedFn on_deleted,
                             void *userdata);

bool RoomClient_Service(void);

uint32_t RoomClient_KcpConv(void);
uint16_t RoomClient_UdpPort(void);
int      RoomClient_LocalSlot(void);
uint32_t RoomClient_CurrentRoomId(void);

/* Snapshot of last room info (create/join/update) */
bool RoomClient_GetCurrentRoom(struct pf_room_info *out);

/* After match_start_ntf */
bool RoomClient_TakeMatchStarted(void);
bool RoomClient_GetMatchInfo(struct pf_match_info *out);

/*
 * Enter in-game session: TCP KCP_READY + UDP to room relay.
 * host = room server IP (same as TCP). Call after TakeMatchStarted ok.
 */
bool RoomClient_EnterMatch(const char *host);
void RoomClient_LeaveMatch(void);
bool RoomClient_InMatch(void);

/* Pump UDP + advance local tick; call every frame while in match */
void RoomClient_MatchService(void);

uint32_t RoomClient_MatchTick(void);
int      RoomClient_MatchPeerCount(void);
bool     RoomClient_MatchGetPeers(struct pf_match_peer *out, int *inout_n);
bool     RoomClient_MatchPeerEntered(void); /* one-shot: a peer newly marked in_game */

/* UI helpers */
bool LoginClient_TakeRegResult(bool *ok, int *err);
bool LoginClient_TakeLoginResult(bool *ok, int *err);
bool RoomClient_TakeAuthResult(bool *ok, int *err);

#ifdef __cplusplus
}
#endif

#endif
