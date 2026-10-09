#pragma once
#include "storage_queue.h"
#include <atomic>
#include <thread>
#include <string>

namespace pf {

class StorageService {
public:
    StorageService();
    ~StorageService();

    bool start(const std::string &db_path);
    void stop();

    MpscQueueReq &req_queue() { return req_; }
    MpscQueueRsp &rsp_queue() { return rsp_; }

    uint64_t next_req_id() { return ++req_id_; }

private:
    void thread_main();
    bool open_db(const std::string &path);
    void close_db();
    void handle(const StorageRequest &req);

    MpscQueueReq req_;
    MpscQueueRsp rsp_;
    std::thread  thr_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> req_id_{1};
    void *db_ = nullptr; /* sqlite3* */
};

} // namespace pf
