/*
 * Login server — Logic thread + Storage thread; protobuf wire (pf.net.*).
 */
#include "../common/tcp_net.h"
#include "../common/pf_proto.h"
#include "../common/pf_log.h"
#include "../storage/storage_service.h"
#include "../proto/gen/pf_common.pb.h"
#include "../proto/gen/pf_login.pb.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

using namespace pf;
using namespace pf::net;

struct Pending {
    enum Kind { Reg, Login, Profile, TokenValidate } kind;
    TcpStream *stream = nullptr;
};

static StorageService g_store;
static std::unordered_map<uint64_t, Pending> g_pending;
static std::vector<std::unique_ptr<TcpStream>> g_clients;

static const char *err_str(int e)
{
    switch(e) {
    case PF_OK: return "OK";
    case PF_ERR_UNAUTHORIZED: return "UNAUTHORIZED";
    case PF_ERR_NOT_FOUND: return "NOT_FOUND(user/room)";
    case PF_ERR_FULL: return "FULL";
    case PF_ERR_NOT_OWNER: return "NOT_OWNER";
    case PF_ERR_BAD_STATE: return "BAD_STATE";
    case PF_ERR_ALREADY_IN_ROOM: return "ALREADY_IN_ROOM";
    case PF_ERR_EXISTS: return "EXISTS";
    case PF_ERR_BAD_PASS: return "BAD_PASS";
    case PF_ERR_INTERNAL: return "INTERNAL";
    case PF_ERR_BAD_ARGS: return "BAD_ARGS";
    default: return "?";
    }
}

static void on_msg(TcpStream &s, const PbPacket &pkt)
{
    PF_LOG_D("recv msg_id=%u from %s bytes=%zu", pkt.msg_id, s.peer_ip.c_str(), pkt.payload.size());
    switch(pkt.msg_id) {
    case MSG_REG_REQ: {
        RegReq req;
        if(!ParsePayload(pkt.payload, req) || req.username().empty()) {
            PF_LOG_W("reg_req bad args from %s", s.peer_ip.c_str());
            RegRsp rsp; rsp.set_ok(false); rsp.set_err(ERR_BAD_ARGS);
            s.send_pb(MSG_REG_RSP, rsp);
            return;
        }
        PF_LOG_I("reg_req user='%s' pass_len=%d from %s",
                 req.username().c_str(), (int)req.password().size(), s.peer_ip.c_str());
        StorageRequest r;
        r.req_id = g_store.next_req_id();
        r.op = StorageOp::UserCreate;
        r.username = req.username();
        r.password = req.password();
        g_pending[r.req_id] = { Pending::Reg, &s };
        g_store.req_queue().push(r);
        break;
    }
    case MSG_LOGIN_REQ: {
        LoginReq req;
        if(!ParsePayload(pkt.payload, req)) {
            PF_LOG_W("login_req parse fail from %s", s.peer_ip.c_str());
            LoginRsp rsp; rsp.set_ok(false); rsp.set_err(ERR_BAD_ARGS);
            s.send_pb(MSG_LOGIN_RSP, rsp);
            return;
        }
        PF_LOG_I("login_req user='%s' pass_len=%d from %s",
                 req.username().c_str(), (int)req.password().size(), s.peer_ip.c_str());
        StorageRequest r;
        r.req_id = g_store.next_req_id();
        r.op = StorageOp::UserAuth;
        r.username = req.username();
        r.password = req.password();
        g_pending[r.req_id] = { Pending::Login, &s };
        g_store.req_queue().push(r);
        break;
    }
    case MSG_HEARTBEAT_REQ: {
        HeartbeatRsp rsp; rsp.set_ok(true);
        s.send_pb(MSG_HEARTBEAT_RSP, rsp);
        break;
    }
    case MSG_PROFILE_REQ:
    case MSG_TOKEN_VALIDATE_REQ: {
        std::string token;
        Pending::Kind kind = Pending::Profile;
        if(pkt.msg_id == MSG_PROFILE_REQ) {
            ProfileReq req;
            if(!ParsePayload(pkt.payload, req)) return;
            token = req.token();
            kind = Pending::Profile;
        } else {
            TokenValidateReq tv;
            if(!ParsePayload(pkt.payload, tv)) return;
            token = tv.token();
            kind = Pending::TokenValidate;
        }
        PF_LOG_I("session_get token_len=%d kind=%d from %s",
                 (int)token.size(), (int)kind, s.peer_ip.c_str());
        StorageRequest r;
        r.req_id = g_store.next_req_id();
        r.op = StorageOp::SessionGet;
        r.token = token;
        g_pending[r.req_id] = { kind, &s };
        g_store.req_queue().push(r);
        break;
    }
    default:
        PF_LOG_W("unknown msg_id=%u from %s", pkt.msg_id, s.peer_ip.c_str());
        break;
    }
}

static void drain_storage_rsp()
{
    StorageResponse rsp;
    while(g_store.rsp_queue().try_pop(rsp)) {
        auto it = g_pending.find(rsp.req_id);
        if(it == g_pending.end()) {
            PF_LOG_W("orphan storage rsp id=%llu err=%d",
                     (unsigned long long)rsp.req_id, rsp.errcode);
            continue;
        }
        Pending p = it->second;
        g_pending.erase(it);
        if(!p.stream || !p.stream->valid()) {
            PF_LOG_W("storage rsp id=%llu but client gone err=%d",
                     (unsigned long long)rsp.req_id, rsp.errcode);
            continue;
        }

        if(p.kind == Pending::Reg) {
            PF_LOG_I("reg_rsp user=%s err=%d(%s) uid=%lld -> %s",
                     rsp.username.c_str(), rsp.errcode, err_str(rsp.errcode),
                     (long long)rsp.uid, p.stream->peer_ip.c_str());
            RegRsp m;
            m.set_ok(rsp.errcode == PF_OK);
            m.set_err((ErrCode)rsp.errcode);
            m.set_uid(rsp.uid);
            p.stream->send_pb(MSG_REG_RSP, m);
        } else if(p.kind == Pending::Login) {
            PF_LOG_I("login_rsp user=%s err=%d(%s) uid=%lld token_len=%d -> %s",
                     rsp.username.c_str(), rsp.errcode, err_str(rsp.errcode),
                     (long long)rsp.uid, (int)rsp.token.size(), p.stream->peer_ip.c_str());
            LoginRsp m;
            m.set_ok(rsp.errcode == PF_OK);
            m.set_err((ErrCode)rsp.errcode);
            m.set_token(rsp.token);
            m.set_uid(rsp.uid);
            m.set_username(rsp.username);
            p.stream->send_pb(MSG_LOGIN_RSP, m);
        } else if(p.kind == Pending::Profile) {
            ProfileRsp m;
            m.set_ok(rsp.errcode == PF_OK);
            m.set_err((ErrCode)rsp.errcode);
            m.set_uid(rsp.uid);
            m.set_username(rsp.username);
            p.stream->send_pb(MSG_PROFILE_RSP, m);
        } else if(p.kind == Pending::TokenValidate) {
            TokenValidateRsp tv;
            tv.set_ok(rsp.errcode == PF_OK);
            tv.set_err((ErrCode)rsp.errcode);
            tv.set_uid(rsp.uid);
            tv.set_username(rsp.username);
            p.stream->send_pb(MSG_TOKEN_VALIDATE_RSP, tv);
        }
    }
}

int main(int argc, char **argv)
{
    uint16_t port = PF_LOGIN_DEFAULT_PORT;
    std::string db = "data/account.db";
    if(argc > 1) port = (uint16_t)atoi(argv[1]);
    if(argc > 2) db = argv[2];

    Logger::instance().set_name("login");
    Logger::instance().set_file("logs/login_server.log");

    TcpServer::init_sockets();
#ifdef _WIN32
    _mkdir("data");
    _mkdir("logs");
#else
    mkdir("data", 0755);
    mkdir("logs", 0755);
#endif

    if(!g_store.start(db)) {
        PF_LOG_E("storage start failed: %s", db.c_str());
        return 1;
    }

    TcpServer srv;
    if(!srv.listen_on(port)) {
        PF_LOG_E("listen %u failed", port);
        return 1;
    }
    PF_LOG_I("login_server listening port=%u db=%s", port, db.c_str());

    for(;;) {
        srv.poll(50,
            [&](std::unique_ptr<TcpStream> st) {
                PF_LOG_I("accept %s", st->peer_ip.c_str());
                g_clients.push_back(std::move(st));
            },
            on_msg,
            [](TcpStream &s) { PF_LOG_I("close %s", s.peer_ip.c_str()); },
            g_clients);
        drain_storage_rsp();
    }
    return 0;
}
