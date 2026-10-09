/*
 * Room relay — protobuf TCP control + UDP cmd fanout (KCP-ready).
 */
#include "../common/tcp_net.h"
#include "../common/pf_proto.h"
#include "../common/pf_log.h"
#include "../proto/gen/pf_common.pb.h"
#include "../proto/gen/pf_room.pb.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <direct.h>
using socklen_t = int;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET (-1)
#define closesocket close
#endif

using namespace pf;
using namespace pf::net;

struct Player {
    int64_t uid = 0;
    std::string username;
    int slot = -1;
    TcpStream *stream = nullptr;
    uint32_t kcp_conv = 0;
    bool kcp_ready = false;
    sockaddr_in udp_addr{};
    bool has_udp = false;
};

struct Room {
    uint32_t id = 0;
    std::string name;
    std::string map_path;
    int max_players = 8;
    int64_t owner_uid = 0;
    RoomState state = ROOM_OPEN;
    std::vector<Player> players;

    Player *find_uid(int64_t uid) {
        for(auto &p : players)
            if(p.uid == uid) return &p;
        return nullptr;
    }
    int player_count() const { return (int)players.size(); }
};

struct Session {
    int64_t uid = 0;
    std::string username;
    std::string token;
    uint32_t room_id = 0;
};

static std::vector<std::unique_ptr<TcpStream>> g_clients;
static std::map<TcpStream*, Session> g_sess;
static std::map<uint32_t, Room> g_rooms;
static uint32_t g_next_room = 1;
static uint32_t g_next_conv = 1000;
static SOCKET g_udp = INVALID_SOCKET;
static uint16_t g_udp_port = PF_ROOM_UDP_PORT;

static void fill_room_info(RoomInfo *out, const Room &r)
{
    out->set_room_id(r.id);
    out->set_name(r.name);
    out->set_map_path(r.map_path);
    out->set_max_players(r.max_players);
    out->set_player_count(r.player_count());
    out->set_owner_uid(r.owner_uid);
    out->set_state(r.state);
    out->clear_players();
    for(auto &p : r.players) {
        auto *pi = out->add_players();
        pi->set_uid(p.uid);
        pi->set_username(p.username);
        pi->set_slot(p.slot);
    }
}

/* Push room snapshot to every authenticated TCP session (lobby + in-room). */
static void broadcast_room_update(const Room &r)
{
    RoomUpdate up;
    fill_room_info(up.mutable_room(), r);
    for(auto &kv : g_sess) {
        TcpStream *st = kv.first;
        if(st && st->valid())
            st->send_pb(MSG_ROOM_UPDATE, up);
    }
}

static void broadcast_room_deleted(uint32_t room_id, const char *reason)
{
    RoomDeleteNtf ntf;
    ntf.set_room_id(room_id);
    ntf.set_reason(reason ? reason : "closed");
    for(auto &kv : g_sess) {
        TcpStream *st = kv.first;
        if(st && st->valid())
            st->send_pb(MSG_ROOM_DELETE_NTF, ntf);
    }
}

static Session *sess_of(TcpStream &s)
{
    auto it = g_sess.find(&s);
    return it == g_sess.end() ? nullptr : &it->second;
}

static void on_msg(TcpStream &s, const PbPacket &pkt)
{
    PF_LOG_D("recv msg_id=%u from %s", pkt.msg_id, s.peer_ip.c_str());
    if(pkt.msg_id == MSG_AUTH_REQ) {
        AuthReq req;
        if(!ParsePayload(pkt.payload, req) || req.token().empty()) {
            AuthRsp rsp; rsp.set_ok(false); rsp.set_err(ERR_BAD_ARGS);
            s.send_pb(MSG_AUTH_RSP, rsp);
            return;
        }
        Session se;
        se.token = req.token();
        se.uid = req.uid();
        se.username = req.username();
        g_sess[&s] = se;
        AuthRsp rsp;
        rsp.set_ok(true);
        rsp.set_err(ERR_OK);
        rsp.set_uid(se.uid);
        rsp.set_username(se.username);
        s.send_pb(MSG_AUTH_RSP, rsp);
        return;
    }

    Session *se = sess_of(s);
    if(!se) {
        AuthRsp rsp; rsp.set_ok(false); rsp.set_err(ERR_UNAUTHORIZED);
        s.send_pb(MSG_AUTH_RSP, rsp);
        return;
    }

    switch(pkt.msg_id) {
    case MSG_ROOM_LIST_REQ: {
        RoomListReq req;
        ParsePayload(pkt.payload, req);
        RoomListRsp rsp;
        rsp.set_ok(true);
        rsp.set_err(ERR_OK);
        for(auto &kv : g_rooms) {
            if(req.open_only() && kv.second.state != ROOM_OPEN)
                continue;
            if(kv.second.state == ROOM_CLOSED)
                continue;
            fill_room_info(rsp.add_rooms(), kv.second);
        }
        s.send_pb(MSG_ROOM_LIST_RSP, rsp);
        break;
    }
    case MSG_ROOM_QUERY_REQ: {
        RoomQueryReq req;
        if(!ParsePayload(pkt.payload, req)) return;
        auto it = g_rooms.find(req.room_id());
        RoomQueryRsp rsp;
        if(it == g_rooms.end()) {
            rsp.set_ok(false); rsp.set_err(ERR_NOT_FOUND);
        } else {
            rsp.set_ok(true); rsp.set_err(ERR_OK);
            fill_room_info(rsp.mutable_room(), it->second);
        }
        s.send_pb(MSG_ROOM_QUERY_RSP, rsp);
        break;
    }
    case MSG_ROOM_CREATE_REQ: {
        RoomCreateReq req;
        if(!ParsePayload(pkt.payload, req)) return;
        RoomCreateRsp rsp;
        if(se->room_id) {
            rsp.set_ok(false); rsp.set_err(ERR_ALREADY_IN_ROOM);
            s.send_pb(MSG_ROOM_CREATE_RSP, rsp);
            break;
        }
        Room r;
        r.id = g_next_room++;
        r.name = req.name().empty() ? "room" : req.name();
        r.map_path = req.map_path().empty() ? "maps/demo.pfmap" : req.map_path();
        r.max_players = req.max_players() > 0 ? req.max_players() : 8;
        if(r.max_players > PF_MAX_PLAYERS_ROOM) r.max_players = PF_MAX_PLAYERS_ROOM;
        r.owner_uid = se->uid;
        r.state = ROOM_OPEN;
        Player pl;
        pl.uid = se->uid;
        pl.username = se->username;
        pl.slot = 0;
        pl.stream = &s;
        pl.kcp_conv = g_next_conv++;
        r.players.push_back(pl);
        se->room_id = r.id;
        g_rooms[r.id] = r;
        rsp.set_ok(true); rsp.set_err(ERR_OK);
        fill_room_info(rsp.mutable_room(), g_rooms[r.id]);
        s.send_pb(MSG_ROOM_CREATE_RSP, rsp);
        /* Notify all lobby clients so the room appears without manual refresh. */
        broadcast_room_update(g_rooms[r.id]);
        printf("room created %u by %lld (%s) players=%d\n",
               r.id, (long long)se->uid, se->username.c_str(),
               g_rooms[r.id].player_count());
        break;
    }
    case MSG_ROOM_DELETE_REQ: {
        RoomDeleteReq req;
        if(!ParsePayload(pkt.payload, req)) return;
        RoomDeleteRsp rsp;
        auto it = g_rooms.find(req.room_id());
        if(it == g_rooms.end()) {
            rsp.set_ok(false); rsp.set_err(ERR_NOT_FOUND);
        } else if(it->second.owner_uid != se->uid) {
            rsp.set_ok(false); rsp.set_err(ERR_NOT_OWNER);
        } else if(it->second.state == ROOM_IN_MATCH) {
            rsp.set_ok(false); rsp.set_err(ERR_BAD_STATE);
        } else {
            RoomDeleteNtf ntf;
            ntf.set_room_id(it->second.id);
            ntf.set_reason("owner_deleted");
            for(auto &p : it->second.players) {
                auto *ps = p.stream ? sess_of(*p.stream) : nullptr;
                if(ps) ps->room_id = 0;
            }
            uint32_t deleted_id = it->second.id;
            g_rooms.erase(it);
            rsp.set_ok(true); rsp.set_err(ERR_OK);
            broadcast_room_deleted(deleted_id, "owner_deleted");
        }
        s.send_pb(MSG_ROOM_DELETE_RSP, rsp);
        break;
    }
    case MSG_ROOM_JOIN_REQ: {
        RoomJoinReq req;
        if(!ParsePayload(pkt.payload, req)) return;
        RoomJoinRsp rsp;
        if(se->room_id) {
            rsp.set_ok(false); rsp.set_err(ERR_ALREADY_IN_ROOM);
            s.send_pb(MSG_ROOM_JOIN_RSP, rsp);
            break;
        }
        auto it = g_rooms.find(req.room_id());
        if(it == g_rooms.end()) {
            printf("join room %u failed NOT_FOUND (have %zu rooms) uid=%lld\n",
                   req.room_id(), g_rooms.size(), (long long)se->uid);
            rsp.set_ok(false); rsp.set_err(ERR_NOT_FOUND);
            s.send_pb(MSG_ROOM_JOIN_RSP, rsp);
            break;
        }
        if(it->second.state != ROOM_OPEN) {
            printf("join room %u failed BAD_STATE=%d uid=%lld\n",
                   req.room_id(), (int)it->second.state, (long long)se->uid);
            rsp.set_ok(false); rsp.set_err(ERR_BAD_STATE);
            s.send_pb(MSG_ROOM_JOIN_RSP, rsp);
            break;
        }
        Room &r = it->second;
        if(r.player_count() >= r.max_players) {
            rsp.set_ok(false); rsp.set_err(ERR_FULL);
            s.send_pb(MSG_ROOM_JOIN_RSP, rsp);
            break;
        }
        bool used[PF_MAX_PLAYERS_ROOM] = {};
        for(auto &p : r.players)
            if(p.slot >= 0 && p.slot < PF_MAX_PLAYERS_ROOM) used[p.slot] = true;
        int slot = 0;
        while(slot < r.max_players && used[slot]) slot++;
        Player pl;
        pl.uid = se->uid; pl.username = se->username; pl.slot = slot;
        pl.stream = &s; pl.kcp_conv = g_next_conv++;
        r.players.push_back(pl);
        se->room_id = r.id;
        rsp.set_ok(true); rsp.set_err(ERR_OK);
        fill_room_info(rsp.mutable_room(), r);
        s.send_pb(MSG_ROOM_JOIN_RSP, rsp);
        broadcast_room_update(r);
        break;
    }
    case MSG_ROOM_LEAVE_REQ: {
        RoomLeaveReq req;
        ParsePayload(pkt.payload, req);
        uint32_t id = se->room_id ? se->room_id : req.room_id();
        RoomLeaveRsp rsp;
        auto it = g_rooms.find(id);
        if(it == g_rooms.end()) {
            rsp.set_ok(false); rsp.set_err(ERR_NOT_FOUND);
            s.send_pb(MSG_ROOM_LEAVE_RSP, rsp);
            break;
        }
        Room &r = it->second;
        for(auto pit = r.players.begin(); pit != r.players.end(); ++pit) {
            if(pit->uid == se->uid) { r.players.erase(pit); break; }
        }
        se->room_id = 0;
        rsp.set_ok(true); rsp.set_err(ERR_OK);
        s.send_pb(MSG_ROOM_LEAVE_RSP, rsp);
        if(r.players.empty()) {
            uint32_t deleted_id = r.id;
            g_rooms.erase(it);
            broadcast_room_deleted(deleted_id, "empty");
        } else {
            if(r.owner_uid == se->uid)
                r.owner_uid = r.players[0].uid;
            broadcast_room_update(r);
        }
        break;
    }
    case MSG_MATCH_START_REQ: {
        MatchStartReq req;
        ParsePayload(pkt.payload, req);
        auto it = g_rooms.find(se->room_id);
        PF_LOG_I("match_start from uid=%lld room_id=%u in_room=%d",
                 (long long)se->uid, se->room_id, it != g_rooms.end());
        if(it == g_rooms.end() || it->second.owner_uid != se->uid) {
            MatchStartNtf ntf; ntf.set_ok(false); ntf.set_err(ERR_NOT_OWNER);
            s.send_pb(MSG_MATCH_START_NTF, ntf);
            PF_LOG_W("match_start rejected NOT_OWNER uid=%lld room=%u",
                     (long long)se->uid, se->room_id);
            break;
        }
        Room &r = it->second;
        if(r.state != ROOM_OPEN) {
            MatchStartNtf ntf; ntf.set_ok(false); ntf.set_err(ERR_BAD_STATE);
            s.send_pb(MSG_MATCH_START_NTF, ntf);
            PF_LOG_W("match_start rejected BAD_STATE=%d room=%u",
                     (int)r.state, r.id);
            break;
        }
        /* Need a second player — solo start left the room IN_MATCH and blocked joins. */
        if(r.player_count() < 2) {
            MatchStartNtf ntf; ntf.set_ok(false); ntf.set_err(ERR_BAD_STATE);
            s.send_pb(MSG_MATCH_START_NTF, ntf);
            PF_LOG_W("match_start rejected need>=2 players room=%u have=%d",
                     r.id, r.player_count());
            break;
        }
        r.state = ROOM_STARTING;
        uint64_t seed = req.rng_seed() ? req.rng_seed() : (uint64_t)time(nullptr);
        uint32_t hz = req.sim_hz() ? req.sim_hz() : 10;
        uint32_t delay = req.command_delay() ? req.command_delay() : 2;
        for(auto &p : r.players) {
            if(!p.stream) continue;
            MatchStartNtf ntf;
            ntf.set_ok(true); ntf.set_err(ERR_OK);
            ntf.set_room_id(r.id);
            ntf.set_rng_seed(seed);
            ntf.set_sim_hz(hz);
            ntf.set_command_delay(delay);
            ntf.set_map_path(r.map_path);
            ntf.set_local_slot(p.slot);
            ntf.set_kcp_conv(p.kcp_conv);
            ntf.set_udp_port(g_udp_port);
            p.stream->send_pb(MSG_MATCH_START_NTF, ntf);
        }
        r.state = ROOM_IN_MATCH;
        /* Lobby clients must drop this from "joinable" and stop showing OPEN. */
        broadcast_room_update(r);
        printf("match started room=%u players=%d seed=%llu\n",
               r.id, r.player_count(), (unsigned long long)seed);
        break;
    }
    case MSG_KCP_READY: {
        KcpReady req;
        ParsePayload(pkt.payload, req);
        auto it = g_rooms.find(se->room_id);
        if(it != g_rooms.end())
            if(Player *p = it->second.find_uid(se->uid))
                p->kcp_ready = true;
        break;
    }
    default:
        break;
    }
}

static void poll_udp()
{
    if(g_udp == INVALID_SOCKET)
        return;
    char buf[4096];
    sockaddr_in from{};
    socklen_t flen = sizeof(from);
    for(;;) {
        int n = recvfrom(g_udp, buf, sizeof(buf), 0, (sockaddr*)&from, &flen);
        if(n <= (int)sizeof(PbFrameHdr))
            break;
        uint16_t mid = 0;
        std::string payload;
        size_t consumed = 0;
        if(!TryDecodePbFrame((const uint8_t*)buf, (size_t)n, mid, payload, consumed))
            continue;
        uint32_t room_id = 0, conv = 0;
        if(mid == MSG_CMD_FRAME) {
            CmdFrame cf;
            if(!ParsePayload(payload, cf)) continue;
            room_id = cf.room_id();
            conv = cf.kcp_conv();
        } else if(mid == MSG_HASH) {
            HashReport hr;
            if(!ParsePayload(payload, hr)) continue;
            room_id = hr.room_id();
            conv = hr.kcp_conv();
        } else
            continue;

        auto it = g_rooms.find(room_id);
        if(it == g_rooms.end())
            continue;
        Room &r = it->second;
        for(auto &p : r.players) {
            if(p.kcp_conv == conv) {
                p.udp_addr = from;
                p.has_udp = true;
                PF_LOG_I("udp enter room=%u conv=%u from %s", room_id, conv,
                         inet_ntoa(from.sin_addr));
            }
        }
        for(auto &p : r.players) {
            if(!p.has_udp) continue;
            /* fan-out to all registered peers (incl. sender echo) */
            sendto(g_udp, buf, n, 0, (sockaddr*)&p.udp_addr, sizeof(p.udp_addr));
        }
    }
}

int main(int argc, char **argv)
{
    uint16_t tcp_port = PF_ROOM_TCP_PORT;
    if(argc > 1) tcp_port = (uint16_t)atoi(argv[1]);
    if(argc > 2) g_udp_port = (uint16_t)atoi(argv[2]);

    Logger::instance().set_name("room");
    Logger::instance().set_file("logs/room_server.log");
#ifdef _WIN32
    _mkdir("logs");
#endif

    TcpServer::init_sockets();
    g_udp = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in ua{};
    ua.sin_family = AF_INET;
    ua.sin_addr.s_addr = htonl(INADDR_ANY);
    ua.sin_port = htons(g_udp_port);
    if(bind(g_udp, (sockaddr*)&ua, sizeof(ua)) != 0) {
        fprintf(stderr, "udp bind %u failed\n", g_udp_port);
        return 1;
    }
#ifdef _WIN32
    u_long non = 1;
    ioctlsocket(g_udp, FIONBIO, &non);
#endif

    TcpServer srv;
    if(!srv.listen_on(tcp_port)) {
        fprintf(stderr, "tcp listen %u failed\n", tcp_port);
        return 1;
    }
    printf("room_server (protobuf) tcp=%u udp=%u\n", tcp_port, g_udp_port);
    PF_LOG_I("room_server listening tcp=%u udp=%u", tcp_port, g_udp_port);

    for(;;) {
        srv.poll(20,
            [&](std::unique_ptr<TcpStream> st) {
                printf("accept %s\n", st->peer_ip.c_str());
                g_clients.push_back(std::move(st));
            },
            on_msg,
            [](TcpStream &s) {
                auto it = g_sess.find(&s);
                if(it != g_sess.end()) {
                    uint32_t rid = it->second.room_id;
                    int64_t uid = it->second.uid;
                    g_sess.erase(it);
                    auto rit = g_rooms.find(rid);
                    if(rit != g_rooms.end()) {
                        Room &r = rit->second;
                        for(auto pit = r.players.begin(); pit != r.players.end(); ++pit) {
                            if(pit->uid == uid) { r.players.erase(pit); break; }
                        }
                        if(r.players.empty()) {
                            uint32_t deleted_id = r.id;
                            g_rooms.erase(rit);
                            broadcast_room_deleted(deleted_id, "disconnect_empty");
                            printf("room %u removed (empty after disconnect uid=%lld)\n",
                                   deleted_id, (long long)uid);
                        } else {
                            if(r.owner_uid == uid)
                                r.owner_uid = r.players[0].uid;
                            broadcast_room_update(r);
                        }
                    }
                }
            },
            g_clients);
        poll_udp();
    }
    return 0;
}
