#pragma once
/*
 * Thread-safe request/response queues between Logic and Storage threads.
 */
#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace pf {

enum class StorageOp {
    UserCreate,
    UserAuth,
    SessionGet,
    SessionDel,
    UserGet,
    Shutdown
};

struct StorageRequest {
    uint64_t    req_id = 0;
    StorageOp   op = StorageOp::UserGet;
    std::string username;
    std::string password;
    std::string token;
    int64_t     uid = 0;
};

struct StorageResponse {
    uint64_t    req_id = 0;
    int         errcode = 0;
    int64_t     uid = 0;
    std::string username;
    std::string token;
    int64_t     created_at = 0;
    int64_t     last_login = 0;
};

class MpscQueueReq {
public:
    void push(StorageRequest r) {
        std::lock_guard<std::mutex> lk(mu_);
        q_.push_back(std::move(r));
        cv_.notify_one();
    }
    bool pop_wait(StorageRequest &out, int timeout_ms) {
        std::unique_lock<std::mutex> lk(mu_);
        if(!cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                         [&]{ return !q_.empty(); }))
            return false;
        out = std::move(q_.front());
        q_.pop_front();
        return true;
    }
    void wake() { cv_.notify_all(); }
private:
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<StorageRequest> q_;
};

class MpscQueueRsp {
public:
    void push(StorageResponse r) {
        std::lock_guard<std::mutex> lk(mu_);
        q_.push_back(std::move(r));
    }
    bool try_pop(StorageResponse &out) {
        std::lock_guard<std::mutex> lk(mu_);
        if(q_.empty())
            return false;
        out = std::move(q_.front());
        q_.pop_front();
        return true;
    }
private:
    std::mutex mu_;
    std::deque<StorageResponse> q_;
};

} // namespace pf
