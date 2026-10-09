#pragma once
/* Minimal TCP with protobuf frames (msg_id + payload). */
#include "pb_frame.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace pf {

struct PbPacket {
    uint16_t msg_id = 0;
    std::string payload;
};

class TcpStream {
public:
    explicit TcpStream(uintptr_t sock = 0);
    ~TcpStream();
    TcpStream(const TcpStream&) = delete;
    TcpStream& operator=(const TcpStream&) = delete;

    uintptr_t fd() const { return sock_; }
    bool valid() const { return sock_ != 0 && sock_ != (uintptr_t)-1; }
    void close();

    bool send_pb(uint16_t msg_id, const google::protobuf::MessageLite &msg);
    bool send_raw(const std::vector<uint8_t> &frame);
    bool feed(const uint8_t *data, size_t n, PbPacket &out, bool &got);

    std::string peer_ip;
    uint64_t    user_data = 0;

private:
    uintptr_t sock_ = 0;
    std::vector<uint8_t> rx_;
};

class TcpServer {
public:
    using AcceptFn = std::function<void(std::unique_ptr<TcpStream>)>;

    bool listen_on(uint16_t port, int backlog = 32);
    void close();
    void poll(int timeout_ms,
              const AcceptFn &on_accept,
              const std::function<void(TcpStream&, const PbPacket&)> &on_msg,
              const std::function<void(TcpStream&)> &on_close,
              std::vector<std::unique_ptr<TcpStream>> &clients);

    static bool init_sockets();
    static void shutdown_sockets();

private:
    uintptr_t listen_ = 0;
};

bool TcpConnect(const char *host, uint16_t port, std::unique_ptr<TcpStream> &out);

} // namespace pf
