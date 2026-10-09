#include "storage_service.h"
#include "../common/pf_proto.h"
#include "../common/pf_crypto.h"
#include "../common/pf_log.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <map>
#include <mutex>
#include <string>

#if defined(PF_HAS_SQLITE)
#include "sqlite3.h"
#endif

namespace pf {

namespace {

struct UserRow {
    int64_t uid = 0;
    std::string username;
    std::string salt_hex;
    std::string hash_hex;
    int64_t created_at = 0;
    int64_t last_login = 0;
};

struct SessionRow {
    std::string token;
    int64_t uid = 0;
    int64_t expire_at = 0;
    int64_t created_at = 0;
};

/* Simple file DB when SQLite amalgamation is not linked. Same queue semantics. */
class FileAccountDb {
public:
    bool open(const std::string &path) {
        path_ = path;
        load();
        return true;
    }
    void close() { save(); }

    int create_user(const std::string &user, const std::string &pass, int64_t *uid_out) {
        for(auto &kv : users_) {
            if(_stricmp_compat(kv.second.username.c_str(), user.c_str()) == 0)
                return PF_ERR_EXISTS;
        }
        UserRow u;
        u.uid = ++next_uid_;
        u.username = user;
        u.salt_hex = RandomTokenHex(8);
        std::string material = u.salt_hex + ":" + pass;
        u.hash_hex = Sha256Hex(material.data(), material.size());
        u.created_at = now();
        u.last_login = 0;
        users_[u.uid] = u;
        save();
        if(uid_out) *uid_out = u.uid;
        return PF_OK;
    }

    int auth(const std::string &user, const std::string &pass,
             int64_t *uid_out, std::string *token_out) {
        UserRow *found = nullptr;
        for(auto &kv : users_) {
            if(_stricmp_compat(kv.second.username.c_str(), user.c_str()) == 0) {
                found = &kv.second;
                break;
            }
        }
        if(!found)
            return PF_ERR_NOT_FOUND;
        std::string material = found->salt_hex + ":" + pass;
        std::string h = Sha256Hex(material.data(), material.size());
        if(h != found->hash_hex)
            return PF_ERR_BAD_PASS;
        found->last_login = now();
        std::string tok = RandomTokenHex(16);
        SessionRow s;
        s.token = tok;
        s.uid = found->uid;
        s.created_at = now();
        s.expire_at = s.created_at + 86400;
        sessions_[tok] = s;
        save();
        if(uid_out) *uid_out = found->uid;
        if(token_out) *token_out = tok;
        return PF_OK;
    }

    int session_get(const std::string &token, int64_t *uid_out, std::string *user_out) {
        auto it = sessions_.find(token);
        if(it == sessions_.end())
            return PF_ERR_UNAUTHORIZED;
        if(it->second.expire_at < now()) {
            sessions_.erase(it);
            save();
            return PF_ERR_UNAUTHORIZED;
        }
        auto uit = users_.find(it->second.uid);
        if(uit == users_.end())
            return PF_ERR_UNAUTHORIZED;
        if(uid_out) *uid_out = uit->second.uid;
        if(user_out) *user_out = uit->second.username;
        return PF_OK;
    }

    int session_del(const std::string &token) {
        sessions_.erase(token);
        save();
        return PF_OK;
    }

    int user_get(int64_t uid, UserRow *out) {
        auto it = users_.find(uid);
        if(it == users_.end())
            return PF_ERR_NOT_FOUND;
        *out = it->second;
        return PF_OK;
    }

private:
    static int64_t now() { return (int64_t)time(nullptr); }
    static int _stricmp_compat(const char *a, const char *b) {
#ifdef _WIN32
        return _stricmp(a, b);
#else
        return strcasecmp(a, b);
#endif
    }

    void load() {
        users_.clear();
        sessions_.clear();
        next_uid_ = 1;
        std::ifstream in(path_);
        if(!in)
            return;
        std::string line;
        while(std::getline(in, line)) {
            if(line.empty()) continue;
            std::istringstream ss(line);
            std::string kind;
            ss >> kind;
            if(kind == "U") {
                UserRow u;
                ss >> u.uid >> u.username >> u.salt_hex >> u.hash_hex >> u.created_at >> u.last_login;
                users_[u.uid] = u;
                if(u.uid >= next_uid_) next_uid_ = u.uid + 1;
            } else if(kind == "S") {
                SessionRow s;
                ss >> s.token >> s.uid >> s.expire_at >> s.created_at;
                sessions_[s.token] = s;
            }
        }
    }

    void save() {
        std::ofstream out(path_, std::ios::trunc);
        for(auto &kv : users_) {
            auto &u = kv.second;
            out << "U " << u.uid << " " << u.username << " " << u.salt_hex << " "
                << u.hash_hex << " " << u.created_at << " " << u.last_login << "\n";
        }
        for(auto &kv : sessions_) {
            auto &s = kv.second;
            out << "S " << s.token << " " << s.uid << " " << s.expire_at << " " << s.created_at << "\n";
        }
    }

    std::string path_;
    std::map<int64_t, UserRow> users_;
    std::map<std::string, SessionRow> sessions_;
    int64_t next_uid_ = 1;
};

#if defined(PF_HAS_SQLITE)
struct SqliteDb {
    sqlite3 *db = nullptr;
    bool open(const std::string &path) {
        if(sqlite3_open(path.c_str(), &db) != SQLITE_OK)
            return false;
        const char *sql =
            "CREATE TABLE IF NOT EXISTS users ("
            " uid INTEGER PRIMARY KEY AUTOINCREMENT,"
            " username TEXT NOT NULL UNIQUE COLLATE NOCASE,"
            " pass_salt TEXT NOT NULL,"
            " pass_hash TEXT NOT NULL,"
            " created_at INTEGER NOT NULL,"
            " last_login INTEGER NOT NULL DEFAULT 0);"
            "CREATE TABLE IF NOT EXISTS sessions ("
            " token TEXT PRIMARY KEY,"
            " uid INTEGER NOT NULL,"
            " expire_at INTEGER NOT NULL,"
            " created_at INTEGER NOT NULL);"
            "PRAGMA journal_mode=WAL;";
        char *err = nullptr;
        if(sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
            if(err) sqlite3_free(err);
            return false;
        }
        return true;
    }
    void close() {
        if(db) { sqlite3_close(db); db = nullptr; }
    }

    static int64_t now() { return (int64_t)time(nullptr); }

    int create_user(const std::string &user, const std::string &pass, int64_t *uid_out) {
        std::string salt = RandomTokenHex(8);
        std::string material = salt + ":" + pass;
        std::string hash = Sha256Hex(material.data(), material.size());
        sqlite3_stmt *st = nullptr;
        const char *q = "INSERT INTO users(username,pass_salt,pass_hash,created_at,last_login) VALUES(?,?,?,?,0)";
        if(sqlite3_prepare_v2(db, q, -1, &st, nullptr) != SQLITE_OK)
            return PF_ERR_INTERNAL;
        sqlite3_bind_text(st, 1, user.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, salt.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 3, hash.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 4, now());
        int rc = sqlite3_step(st);
        sqlite3_finalize(st);
        if(rc == SQLITE_CONSTRAINT)
            return PF_ERR_EXISTS;
        if(rc != SQLITE_DONE)
            return PF_ERR_INTERNAL;
        if(uid_out) *uid_out = (int64_t)sqlite3_last_insert_rowid(db);
        return PF_OK;
    }

    int auth(const std::string &user, const std::string &pass, int64_t *uid_out, std::string *token_out) {
        sqlite3_stmt *st = nullptr;
        const char *q = "SELECT uid,pass_salt,pass_hash FROM users WHERE username = ? COLLATE NOCASE";
        if(sqlite3_prepare_v2(db, q, -1, &st, nullptr) != SQLITE_OK)
            return PF_ERR_INTERNAL;
        sqlite3_bind_text(st, 1, user.c_str(), -1, SQLITE_TRANSIENT);
        if(sqlite3_step(st) != SQLITE_ROW) {
            sqlite3_finalize(st);
            return PF_ERR_NOT_FOUND; /* user missing — was wrongly BAD_PASS */
        }
        int64_t uid = sqlite3_column_int64(st, 0);
        const char *salt_c = (const char*)sqlite3_column_text(st, 1);
        const char *hash_c = (const char*)sqlite3_column_text(st, 2);
        std::string salt = salt_c ? salt_c : "";
        std::string hash = hash_c ? hash_c : "";
        sqlite3_finalize(st);
        std::string material = salt + ":" + pass;
        std::string got = Sha256Hex(material.data(), material.size());
        if(got != hash)
            return PF_ERR_BAD_PASS;

        sqlite3_exec(db, ("UPDATE users SET last_login=" + std::to_string(now()) +
                          " WHERE uid=" + std::to_string(uid)).c_str(), nullptr, nullptr, nullptr);

        std::string tok = RandomTokenHex(16);
        const char *ins = "INSERT INTO sessions(token,uid,expire_at,created_at) VALUES(?,?,?,?)";
        if(sqlite3_prepare_v2(db, ins, -1, &st, nullptr) != SQLITE_OK)
            return PF_ERR_INTERNAL;
        int64_t t = now();
        sqlite3_bind_text(st, 1, tok.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 2, uid);
        sqlite3_bind_int64(st, 3, t + 86400);
        sqlite3_bind_int64(st, 4, t);
        int rc = sqlite3_step(st);
        sqlite3_finalize(st);
        if(rc != SQLITE_DONE)
            return PF_ERR_INTERNAL;
        if(uid_out) *uid_out = uid;
        if(token_out) *token_out = tok;
        return PF_OK;
    }

    int session_get(const std::string &token, int64_t *uid_out, std::string *user_out) {
        sqlite3_stmt *st = nullptr;
        const char *q =
            "SELECT s.uid,u.username,s.expire_at FROM sessions s "
            "JOIN users u ON u.uid=s.uid WHERE s.token=?";
        if(sqlite3_prepare_v2(db, q, -1, &st, nullptr) != SQLITE_OK)
            return PF_ERR_INTERNAL;
        sqlite3_bind_text(st, 1, token.c_str(), -1, SQLITE_TRANSIENT);
        if(sqlite3_step(st) != SQLITE_ROW) {
            sqlite3_finalize(st);
            return PF_ERR_UNAUTHORIZED;
        }
        int64_t uid = sqlite3_column_int64(st, 0);
        std::string user = (const char*)sqlite3_column_text(st, 1);
        int64_t exp = sqlite3_column_int64(st, 2);
        sqlite3_finalize(st);
        if(exp < now()) {
            session_del(token);
            return PF_ERR_UNAUTHORIZED;
        }
        if(uid_out) *uid_out = uid;
        if(user_out) *user_out = user;
        return PF_OK;
    }

    int session_del(const std::string &token) {
        sqlite3_stmt *st = nullptr;
        if(sqlite3_prepare_v2(db, "DELETE FROM sessions WHERE token=?", -1, &st, nullptr) != SQLITE_OK)
            return PF_ERR_INTERNAL;
        sqlite3_bind_text(st, 1, token.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(st);
        sqlite3_finalize(st);
        return PF_OK;
    }

    int user_get(int64_t uid, UserRow *out) {
        sqlite3_stmt *st = nullptr;
        if(sqlite3_prepare_v2(db,
            "SELECT uid,username,created_at,last_login FROM users WHERE uid=?", -1, &st, nullptr) != SQLITE_OK)
            return PF_ERR_INTERNAL;
        sqlite3_bind_int64(st, 1, uid);
        if(sqlite3_step(st) != SQLITE_ROW) {
            sqlite3_finalize(st);
            return PF_ERR_NOT_FOUND;
        }
        out->uid = sqlite3_column_int64(st, 0);
        out->username = (const char*)sqlite3_column_text(st, 1);
        out->created_at = sqlite3_column_int64(st, 2);
        out->last_login = sqlite3_column_int64(st, 3);
        sqlite3_finalize(st);
        return PF_OK;
    }
};
#endif

} // namespace

StorageService::StorageService() = default;

StorageService::~StorageService()
{
    stop();
}

bool StorageService::start(const std::string &db_path)
{
    if(running_)
        return true;
    if(!open_db(db_path)) {
        PF_LOG_E("storage open_db failed path=%s", db_path.c_str());
        return false;
    }
#if defined(PF_HAS_SQLITE)
    PF_LOG_I("storage started SQLITE path=%s", db_path.c_str());
#else
    PF_LOG_I("storage started FileDB path=%s", db_path.c_str());
#endif
    running_ = true;
    thr_ = std::thread([this]{ thread_main(); });
    return true;
}

void StorageService::stop()
{
    if(!running_)
        return;
    StorageRequest r;
    r.op = StorageOp::Shutdown;
    req_.push(r);
    req_.wake();
    if(thr_.joinable())
        thr_.join();
    running_ = false;
    close_db();
}

bool StorageService::open_db(const std::string &path)
{
#if defined(PF_HAS_SQLITE)
    auto *s = new SqliteDb();
    if(!s->open(path)) { delete s; return false; }
    db_ = s;
#else
    auto *s = new FileAccountDb();
    if(!s->open(path)) { delete s; return false; }
    db_ = s;
#endif
    return true;
}

void StorageService::close_db()
{
    if(!db_)
        return;
#if defined(PF_HAS_SQLITE)
    auto *s = (SqliteDb*)db_;
    s->close();
    delete s;
#else
    auto *s = (FileAccountDb*)db_;
    s->close();
    delete s;
#endif
    db_ = nullptr;
}

void StorageService::thread_main()
{
    while(running_) {
        StorageRequest req;
        if(!req_.pop_wait(req, 200))
            continue;
        if(req.op == StorageOp::Shutdown)
            break;
        handle(req);
    }
}

void StorageService::handle(const StorageRequest &req)
{
    StorageResponse rsp;
    rsp.req_id = req.req_id;
    rsp.errcode = PF_ERR_INTERNAL;

#if defined(PF_HAS_SQLITE)
    auto *db = (SqliteDb*)db_;
#else
    auto *db = (FileAccountDb*)db_;
#endif
    switch(req.op) {
    case StorageOp::UserCreate: {
        int64_t uid = 0;
        rsp.errcode = db->create_user(req.username, req.password, &uid);
        rsp.uid = uid;
        rsp.username = req.username;
        break;
    }
    case StorageOp::UserAuth: {
        int64_t uid = 0;
        std::string tok;
        rsp.errcode = db->auth(req.username, req.password, &uid, &tok);
        rsp.uid = uid;
        rsp.username = req.username;
        rsp.token = tok;
        break;
    }
    case StorageOp::SessionGet: {
        int64_t uid = 0;
        std::string user;
        rsp.errcode = db->session_get(req.token, &uid, &user);
        rsp.uid = uid;
        rsp.username = user;
        rsp.token = req.token;
        break;
    }
    case StorageOp::SessionDel:
        rsp.errcode = db->session_del(req.token);
        break;
    case StorageOp::UserGet: {
        UserRow u;
        rsp.errcode = db->user_get(req.uid, &u);
        if(rsp.errcode == PF_OK) {
            rsp.uid = u.uid;
            rsp.username = u.username;
            rsp.created_at = u.created_at;
            rsp.last_login = u.last_login;
        }
        break;
    }
    default:
        rsp.errcode = PF_ERR_BAD_ARGS;
        break;
    }
    const char *opname = "?";
    switch(req.op) {
    case StorageOp::UserCreate: opname = "UserCreate"; break;
    case StorageOp::UserAuth:   opname = "UserAuth"; break;
    case StorageOp::SessionGet: opname = "SessionGet"; break;
    case StorageOp::SessionDel: opname = "SessionDel"; break;
    case StorageOp::UserGet:    opname = "UserGet"; break;
    default: break;
    }
    PF_LOG_I("storage op=%s user=%s req_id=%llu err=%d uid=%lld",
             opname, req.username.c_str(),
             (unsigned long long)req.req_id, rsp.errcode, (long long)rsp.uid);
    rsp_queue().push(rsp);
}

} // namespace pf
