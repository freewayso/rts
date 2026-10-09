/*
 * Engine client — C API, protobuf wire (same schemas as server/proto).
 */
#include "public/online_client.h"

#include "../../server/common/pf_proto.h"
#include "../../server/common/pb_frame.h"
#include "../../server/proto/gen/pf_common.pb.h"
#include "../../server/proto/gen/pf_login.pb.h"
#include "../../server/proto/gen/pf_room.pb.h"
#include "../../server/common/tcp_net.h"

#include <memory>
#include <string>
#include <cstring>
#include <vector>
#include <chrono>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using socklen_t = int;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET (-1)
#define closesocket close
#endif

using namespace pf;
using namespace pf::net;

static std::unique_ptr<TcpStream> s_login;
static std::unique_ptr<TcpStream> s_room;
static char s_token[PF_OC_MAX_TOKEN];
static char s_username[32];
static int64_t s_uid;
static uint32_t s_room_id, s_kcp_conv;
static uint16_t s_udp_port;
static int s_local_slot = -1;

static RoomClient_OnListFn s_on_list;
static RoomClient_OnRoomFn s_on_room;
static RoomClient_OnDeletedFn s_on_deleted;
static void *s_ud;

static bool s_reg_pending, s_reg_ok; static int s_reg_err;
static bool s_login_pending, s_login_ok; static int s_login_err;
static bool s_auth_pending, s_auth_ok; static int s_auth_err;
static bool s_match_started;

static struct pf_room_info s_cur_room;
static bool s_have_cur_room;
static struct pf_match_info s_match;
static bool s_have_match;

static SOCKET s_udp = INVALID_SOCKET;
static sockaddr_in s_udp_peer{};
static bool s_in_match;
static uint32_t s_match_tick;
static struct pf_match_peer s_peers[PF_OC_MAX_PLAYERS];
static int s_peer_n;
static bool s_peer_entered_pending;
static uint64_t s_last_send_ms;

static uint64_t now_ms(void)
{
    using clock = std::chrono::steady_clock;
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
               clock::now().time_since_epoch())
        .count();
}

static void store_cur_room(const struct pf_room_info *r)
{
    if(!r) return;
    s_cur_room = *r;
    s_have_cur_room = true;
}

static void room_info_to_c(const RoomInfo &in, struct pf_room_info *out)
{
    memset(out, 0, sizeof(*out));
    out->id = in.room_id();
    strncpy(out->name, in.name().c_str(), sizeof(out->name) - 1);
    strncpy(out->map_path, in.map_path().c_str(), sizeof(out->map_path) - 1);
    out->max_players = in.max_players();
    out->player_count = in.player_count();
    out->owner_uid = in.owner_uid();
    out->state = (int)in.state();
    int n = in.players_size();
    if(n > PF_OC_MAX_PLAYERS) n = PF_OC_MAX_PLAYERS;
    for(int i = 0; i < n; i++) {
        out->players[i].uid = in.players(i).uid();
        strncpy(out->players[i].username, in.players(i).username().c_str(),
                sizeof(out->players[i].username) - 1);
        out->players[i].slot = in.players(i).slot();
    }
}

static bool pump_stream(TcpStream &st, bool login_side)
{
    char buf[8192];
    bool any = false;
    for(;;) {
        int n = recv((SOCKET)st.fd(), buf, sizeof(buf), 0);
        if(n <= 0)
            break;
        PbPacket pkt;
        bool got = false;
        if(!st.feed((const uint8_t*)buf, (size_t)n, pkt, got))
            return any;
        while(got) {
            any = true;
            if(login_side) {
                if(pkt.msg_id == MSG_LOGIN_RSP) {
                    LoginRsp rsp;
                    if(ParsePayload(pkt.payload, rsp)) {
                        s_login_pending = true;
                        s_login_ok = rsp.ok();
                        s_login_err = (int)rsp.err();
                        if(rsp.ok()) {
                            strncpy(s_token, rsp.token().c_str(), sizeof(s_token) - 1);
                            s_uid = rsp.uid();
                            strncpy(s_username, rsp.username().c_str(), sizeof(s_username) - 1);
                        }
                    }
                } else if(pkt.msg_id == MSG_REG_RSP) {
                    RegRsp rsp;
                    if(ParsePayload(pkt.payload, rsp)) {
                        s_reg_pending = true;
                        s_reg_ok = rsp.ok();
                        s_reg_err = (int)rsp.err();
                    }
                }
            } else {
                if(pkt.msg_id == MSG_AUTH_RSP) {
                    AuthRsp rsp;
                    if(ParsePayload(pkt.payload, rsp)) {
                        s_auth_pending = true;
                        s_auth_ok = rsp.ok();
                        s_auth_err = (int)rsp.err();
                    }
                } else if(pkt.msg_id == MSG_ROOM_LIST_RSP && s_on_list) {
                    RoomListRsp rsp;
                    if(ParsePayload(pkt.payload, rsp)) {
                        struct pf_room_info rooms[PF_OC_MAX_ROOMS];
                        int nrooms = rsp.rooms_size();
                        if(nrooms > PF_OC_MAX_ROOMS) nrooms = PF_OC_MAX_ROOMS;
                        for(int i = 0; i < nrooms; i++)
                            room_info_to_c(rsp.rooms(i), &rooms[i]);
                        s_on_list(rooms, nrooms, s_ud);
                    }
                } else if(pkt.msg_id == MSG_ROOM_CREATE_RSP ||
                          pkt.msg_id == MSG_ROOM_QUERY_RSP ||
                          pkt.msg_id == MSG_ROOM_JOIN_RSP) {
                    RoomInfo info;
                    int err = 0;
                    bool ok = false;
                    if(pkt.msg_id == MSG_ROOM_CREATE_RSP) {
                        RoomCreateRsp rsp; ParsePayload(pkt.payload, rsp);
                        ok = rsp.ok(); err = (int)rsp.err(); if(ok) info = rsp.room();
                    } else if(pkt.msg_id == MSG_ROOM_QUERY_RSP) {
                        RoomQueryRsp rsp; ParsePayload(pkt.payload, rsp);
                        ok = rsp.ok(); err = (int)rsp.err(); if(ok) info = rsp.room();
                    } else {
                        RoomJoinRsp rsp; ParsePayload(pkt.payload, rsp);
                        ok = rsp.ok(); err = (int)rsp.err(); if(ok) info = rsp.room();
                    }
                    struct pf_room_info cinfo;
                    if(ok) {
                        room_info_to_c(info, &cinfo);
                        s_room_id = cinfo.id;
                        store_cur_room(&cinfo);
                    }
                    if(s_on_room)
                        s_on_room(ok ? &cinfo : nullptr, ok ? 0 : err, s_ud);
                } else if(pkt.msg_id == MSG_ROOM_UPDATE && s_on_room) {
                    RoomUpdate up;
                    if(ParsePayload(pkt.payload, up)) {
                        struct pf_room_info cinfo;
                        room_info_to_c(up.room(), &cinfo);
                        /* Lobby-wide notify: only treat as "my room" if we are a member. */
                        bool i_am_in = false;
                        for(int i = 0; i < cinfo.player_count && i < PF_OC_MAX_PLAYERS; ++i) {
                            if(cinfo.players[i].uid == s_uid) {
                                i_am_in = true;
                                break;
                            }
                        }
                        if(i_am_in) {
                            s_room_id = cinfo.id;
                            store_cur_room(&cinfo);
                        }
                        s_on_room(&cinfo, i_am_in ? 0 : -1, s_ud);
                    }
                } else if(pkt.msg_id == MSG_ROOM_LEAVE_RSP) {
                    RoomLeaveRsp rsp;
                    if(ParsePayload(pkt.payload, rsp) && rsp.ok()) {
                        s_room_id = 0;
                        s_local_slot = -1;
                        s_kcp_conv = 0;
                        s_have_cur_room = false;
                    }
                } else if(pkt.msg_id == MSG_ROOM_DELETE_NTF) {
                    RoomDeleteNtf ntf;
                    if(ParsePayload(pkt.payload, ntf)) {
                        if(s_on_deleted)
                            s_on_deleted(ntf.room_id(), 0, s_ud);
                        if(s_room_id == ntf.room_id()) {
                            s_room_id = 0;
                            s_local_slot = -1;
                            s_have_cur_room = false;
                        }
                    }
                } else if(pkt.msg_id == MSG_MATCH_START_NTF) {
                    MatchStartNtf ntf;
                    if(ParsePayload(pkt.payload, ntf)) {
                        if(ntf.ok()) {
                            memset(&s_match, 0, sizeof(s_match));
                            s_room_id = ntf.room_id();
                            s_local_slot = ntf.local_slot();
                            s_kcp_conv = ntf.kcp_conv();
                            s_udp_port = (uint16_t)ntf.udp_port();
                            s_match.room_id = ntf.room_id();
                            s_match.rng_seed = ntf.rng_seed();
                            s_match.sim_hz = ntf.sim_hz() ? ntf.sim_hz() : 10;
                            s_match.command_delay = ntf.command_delay();
                            strncpy(s_match.map_path, ntf.map_path().c_str(),
                                    sizeof(s_match.map_path) - 1);
                            s_match.local_slot = ntf.local_slot();
                            s_match.kcp_conv = ntf.kcp_conv();
                            s_match.udp_port = (uint16_t)ntf.udp_port();
                            if(s_have_cur_room) {
                                s_match.player_count = s_cur_room.player_count;
                                for(int i = 0; i < s_cur_room.player_count && i < PF_OC_MAX_PLAYERS; ++i)
                                    s_match.players[i] = s_cur_room.players[i];
                            }
                            s_have_match = true;
                            s_match_started = true;
                        } else {
                            s_match_started = true;
                            s_local_slot = -((int)ntf.err() + 1);
                            s_have_match = false;
                        }
                    }
                }
            }
            got = false;
            if(!st.feed(nullptr, 0, pkt, got))
                break;
        }
    }
    return any;
}

extern "C" {

void RoomClient_LeaveMatch(void); /* defined below; used by Connect */

bool LoginClient_Connect(const char *host, uint16_t port)
{
    TcpServer::init_sockets();
    s_login.reset();
    return TcpConnect(host, port, s_login);
}

void LoginClient_Disconnect(void)
{
    s_login.reset();
    s_token[0] = 0;
    s_uid = 0;
}

bool LoginClient_Register(const char *user, const char *pass)
{
    if(!s_login || !s_login->valid()) return false;
    RegReq req;
    req.set_username(user ? user : "");
    req.set_password(pass ? pass : "");
    return s_login->send_pb(MSG_REG_REQ, req);
}

bool LoginClient_Login(const char *user, const char *pass)
{
    if(!s_login || !s_login->valid()) return false;
    LoginReq req;
    req.set_username(user ? user : "");
    req.set_password(pass ? pass : "");
    return s_login->send_pb(MSG_LOGIN_REQ, req);
}

const char *LoginClient_Token(void) { return s_token; }
int64_t LoginClient_Uid(void) { return s_uid; }
const char *LoginClient_Username(void) { return s_username; }

bool LoginClient_Service(void)
{
    if(!s_login || !s_login->valid()) return false;
    return pump_stream(*s_login, true);
}

bool RoomClient_Connect(const char *host, uint16_t tcp_port)
{
    TcpServer::init_sockets();
    RoomClient_LeaveMatch();
    s_room.reset();
    s_room_id = 0;
    s_kcp_conv = 0;
    s_udp_port = 0;
    s_local_slot = -1;
    s_match_started = false;
    s_have_match = false;
    s_have_cur_room = false;
    return TcpConnect(host, tcp_port, s_room);
}

void RoomClient_Disconnect(void)
{
    RoomClient_LeaveMatch();
    s_room.reset();
    s_room_id = 0;
    s_local_slot = -1;
}

bool RoomClient_Auth(const char *token, int64_t uid, const char *username)
{
    if(!s_room || !s_room->valid()) return false;
    AuthReq req;
    req.set_token(token ? token : "");
    req.set_uid(uid);
    req.set_username(username ? username : "");
    return s_room->send_pb(MSG_AUTH_REQ, req);
}

bool RoomClient_ListRooms(void)
{
    if(!s_room || !s_room->valid()) return false;
    RoomListReq req;
    req.set_open_only(true);
    return s_room->send_pb(MSG_ROOM_LIST_REQ, req);
}

bool RoomClient_QueryRoom(uint32_t room_id)
{
    if(!s_room || !s_room->valid()) return false;
    RoomQueryReq req;
    req.set_room_id(room_id);
    return s_room->send_pb(MSG_ROOM_QUERY_REQ, req);
}

bool RoomClient_CreateRoom(const char *name, const char *map_path, int max_players)
{
    if(!s_room || !s_room->valid()) return false;
    RoomCreateReq req;
    req.set_name(name ? name : "room");
    req.set_map_path(map_path ? map_path : "maps/demo.pfmap");
    req.set_max_players(max_players);
    return s_room->send_pb(MSG_ROOM_CREATE_REQ, req);
}

bool RoomClient_DeleteRoom(uint32_t room_id)
{
    if(!s_room || !s_room->valid()) return false;
    RoomDeleteReq req;
    req.set_room_id(room_id);
    return s_room->send_pb(MSG_ROOM_DELETE_REQ, req);
}

bool RoomClient_JoinRoom(uint32_t room_id)
{
    if(!s_room || !s_room->valid()) return false;
    RoomJoinReq req;
    req.set_room_id(room_id);
    return s_room->send_pb(MSG_ROOM_JOIN_REQ, req);
}

bool RoomClient_LeaveRoom(uint32_t room_id)
{
    if(!s_room || !s_room->valid()) return false;
    RoomLeaveReq req;
    req.set_room_id(room_id);
    return s_room->send_pb(MSG_ROOM_LEAVE_REQ, req);
}

bool RoomClient_StartMatch(uint32_t seed, int sim_hz, int command_delay)
{
    if(!s_room || !s_room->valid()) return false;
    MatchStartReq req;
    req.set_rng_seed(seed);
    req.set_sim_hz((uint32_t)sim_hz);
    req.set_command_delay((uint32_t)command_delay);
    return s_room->send_pb(MSG_MATCH_START_REQ, req);
}

void RoomClient_SetCallbacks(RoomClient_OnListFn on_list,
                             RoomClient_OnRoomFn on_room,
                             RoomClient_OnDeletedFn on_deleted,
                             void *userdata)
{
    s_on_list = on_list;
    s_on_room = on_room;
    s_on_deleted = on_deleted;
    s_ud = userdata;
}

bool RoomClient_Service(void)
{
    if(!s_room || !s_room->valid()) return false;
    return pump_stream(*s_room, false);
}

uint32_t RoomClient_KcpConv(void) { return s_kcp_conv; }
uint16_t RoomClient_UdpPort(void) { return s_udp_port; }
int RoomClient_LocalSlot(void) { return s_local_slot; }
uint32_t RoomClient_CurrentRoomId(void) { return s_room_id; }

bool RoomClient_GetCurrentRoom(struct pf_room_info *out)
{
    if(!out || !s_have_cur_room) return false;
    *out = s_cur_room;
    return true;
}

bool LoginClient_TakeRegResult(bool *ok, int *err)
{
    if(!s_reg_pending) return false;
    s_reg_pending = false;
    if(ok) *ok = s_reg_ok;
    if(err) *err = s_reg_err;
    return true;
}

bool LoginClient_TakeLoginResult(bool *ok, int *err)
{
    if(!s_login_pending) return false;
    s_login_pending = false;
    if(ok) *ok = s_login_ok;
    if(err) *err = s_login_err;
    return true;
}

bool RoomClient_TakeAuthResult(bool *ok, int *err)
{
    if(!s_auth_pending) return false;
    s_auth_pending = false;
    if(ok) *ok = s_auth_ok;
    if(err) *err = s_auth_err;
    return true;
}

bool RoomClient_TakeMatchStarted(void)
{
    if(!s_match_started) return false;
    s_match_started = false;
    return true;
}

bool RoomClient_GetMatchInfo(struct pf_match_info *out)
{
    if(!out || !s_have_match) return false;
    *out = s_match;
    return true;
}

static bool udp_send_cmd(uint32_t tick, const char *payload)
{
    if(s_udp == INVALID_SOCKET || !s_in_match) return false;
    CmdFrame cf;
    cf.set_room_id(s_match.room_id);
    cf.set_kcp_conv(s_match.kcp_conv);
    cf.set_tick(tick);
    cf.set_player_id((uint32_t)s_match.local_slot);
    cf.set_payload(payload ? payload : "");
    std::vector<uint8_t> frame;
    if(!EncodePbFrame(MSG_CMD_FRAME, cf, frame))
        return false;
    int n = sendto(s_udp, (const char*)frame.data(), (int)frame.size(), 0,
                   (sockaddr*)&s_udp_peer, sizeof(s_udp_peer));
    return n == (int)frame.size();
}

static void mark_peer_in_game(int slot, const char *name, uint32_t tick)
{
    if(slot < 0) return;
    for(int i = 0; i < s_peer_n; ++i) {
        if(s_peers[i].slot == slot) {
            if(!s_peers[i].in_game)
                s_peer_entered_pending = true;
            s_peers[i].in_game = true;
            s_peers[i].last_tick = tick;
            if(name && name[0])
                strncpy(s_peers[i].username, name, sizeof(s_peers[i].username) - 1);
            return;
        }
    }
    if(s_peer_n >= PF_OC_MAX_PLAYERS) return;
    struct pf_match_peer &p = s_peers[s_peer_n++];
    memset(&p, 0, sizeof(p));
    p.slot = slot;
    p.in_game = true;
    p.last_tick = tick;
    if(name && name[0])
        strncpy(p.username, name, sizeof(p.username) - 1);
    else
        snprintf(p.username, sizeof(p.username), "P%d", slot);
    s_peer_entered_pending = true;
}

bool RoomClient_EnterMatch(const char *host)
{
    if(!s_have_match || s_match.local_slot < 0) return false;
    RoomClient_LeaveMatch();

    if(s_room && s_room->valid()) {
        KcpReady kr;
        kr.set_room_id(s_match.room_id);
        s_room->send_pb(MSG_KCP_READY, kr);
    }

    s_udp = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(s_udp == INVALID_SOCKET) return false;
#ifdef _WIN32
    u_long non = 1;
    ioctlsocket(s_udp, FIONBIO, &non);
#else
    int fl = fcntl(s_udp, F_GETFL, 0);
    fcntl(s_udp, F_SETFL, fl | O_NONBLOCK);
#endif

    memset(&s_udp_peer, 0, sizeof(s_udp_peer));
    s_udp_peer.sin_family = AF_INET;
    s_udp_peer.sin_port = htons(s_match.udp_port ? s_match.udp_port : PF_ROOM_UDP_PORT);
    const char *h = (host && host[0]) ? host : "127.0.0.1";
    if(inet_pton(AF_INET, h, &s_udp_peer.sin_addr) != 1) {
        closesocket(s_udp);
        s_udp = INVALID_SOCKET;
        return false;
    }

    s_peer_n = 0;
    memset(s_peers, 0, sizeof(s_peers));
    for(int i = 0; i < s_match.player_count && i < PF_OC_MAX_PLAYERS; ++i) {
        s_peers[s_peer_n].slot = s_match.players[i].slot;
        strncpy(s_peers[s_peer_n].username, s_match.players[i].username,
                sizeof(s_peers[s_peer_n].username) - 1);
        s_peers[s_peer_n].in_game = (s_match.players[i].slot == s_match.local_slot);
        s_peers[s_peer_n].last_tick = 0;
        s_peer_n++;
    }
    if(s_peer_n == 0) {
        s_peers[0].slot = s_match.local_slot;
        strncpy(s_peers[0].username, s_username, sizeof(s_peers[0].username) - 1);
        s_peers[0].in_game = true;
        s_peer_n = 1;
    }

    s_in_match = true;
    s_match_tick = 0;
    s_last_send_ms = 0;
    s_peer_entered_pending = false;

    char enter[96];
    snprintf(enter, sizeof(enter), "ENTER|%s|%d", s_username, s_match.local_slot);
    for(int i = 0; i < 3; ++i)
        udp_send_cmd(0, enter);
    return true;
}

void RoomClient_LeaveMatch(void)
{
    if(s_udp != INVALID_SOCKET) {
        closesocket(s_udp);
        s_udp = INVALID_SOCKET;
    }
    s_in_match = false;
    s_match_tick = 0;
    s_peer_n = 0;
}

bool RoomClient_InMatch(void) { return s_in_match; }

void RoomClient_MatchService(void)
{
    if(!s_in_match || s_udp == INVALID_SOCKET) return;

    char buf[4096];
    for(;;) {
        sockaddr_in from{};
        socklen_t flen = sizeof(from);
        int n = recvfrom(s_udp, buf, sizeof(buf), 0, (sockaddr*)&from, &flen);
        if(n <= 0) break;
        uint16_t mid = 0;
        std::string payload;
        size_t consumed = 0;
        if(!TryDecodePbFrame((const uint8_t*)buf, (size_t)n, mid, payload, consumed))
            continue;
        if(mid != MSG_CMD_FRAME) continue;
        CmdFrame cf;
        if(!ParsePayload(payload, cf)) continue;
        if(cf.room_id() != s_match.room_id) continue;
        int slot = (int)cf.player_id();
        if(slot == s_match.local_slot) continue; /* ignore echo */
        const std::string &pl = cf.payload();
        char name[32] = {};
        if(pl.rfind("ENTER|", 0) == 0) {
            /* ENTER|user|slot */
            const char *p = pl.c_str() + 6;
            const char *bar = strchr(p, '|');
            if(bar) {
                size_t len = (size_t)(bar - p);
                if(len >= sizeof(name)) len = sizeof(name) - 1;
                memcpy(name, p, len);
            }
        }
        mark_peer_in_game(slot, name[0] ? name : nullptr, cf.tick());
    }

    uint64_t t = now_ms();
    uint32_t hz = s_match.sim_hz ? s_match.sim_hz : 10;
    uint64_t interval = 1000 / hz;
    if(interval < 50) interval = 50;
    if(s_last_send_ms == 0 || t - s_last_send_ms >= interval) {
        s_match_tick++;
        char tickpl[96];
        snprintf(tickpl, sizeof(tickpl), "TICK|%s|%d|%u",
                 s_username, s_match.local_slot, s_match_tick);
        udp_send_cmd(s_match_tick, tickpl);
        /* refresh local peer tick */
        for(int i = 0; i < s_peer_n; ++i) {
            if(s_peers[i].slot == s_match.local_slot) {
                s_peers[i].in_game = true;
                s_peers[i].last_tick = s_match_tick;
            }
        }
        s_last_send_ms = t;
    }
}

uint32_t RoomClient_MatchTick(void) { return s_match_tick; }

int RoomClient_MatchPeerCount(void) { return s_peer_n; }

bool RoomClient_MatchGetPeers(struct pf_match_peer *out, int *inout_n)
{
    if(!out || !inout_n) return false;
    int n = *inout_n;
    if(n > s_peer_n) n = s_peer_n;
    for(int i = 0; i < n; ++i)
        out[i] = s_peers[i];
    *inout_n = n;
    return true;
}

bool RoomClient_MatchPeerEntered(void)
{
    if(!s_peer_entered_pending) return false;
    s_peer_entered_pending = false;
    return true;
}

} /* extern C */
