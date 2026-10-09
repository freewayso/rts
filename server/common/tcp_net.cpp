#include "tcp_net.h"
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socklen_t = int;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#define closesocket close
#define INVALID_SOCKET ((uintptr_t)-1)
#endif

namespace pf {

static void set_nonblock(uintptr_t s)
{
#ifdef _WIN32
    u_long on = 1;
    ioctlsocket((SOCKET)s, FIONBIO, &on);
#else
    int fl = fcntl((int)s, F_GETFL, 0);
    fcntl((int)s, F_SETFL, fl | O_NONBLOCK);
#endif
}

bool TcpServer::init_sockets()
{
#ifdef _WIN32
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#else
    return true;
#endif
}

void TcpServer::shutdown_sockets()
{
#ifdef _WIN32
    WSACleanup();
#endif
}

TcpStream::TcpStream(uintptr_t sock) : sock_(sock)
{
    if(valid())
        set_nonblock(sock_);
}

TcpStream::~TcpStream() { close(); }

void TcpStream::close()
{
    if(valid()) {
        closesocket((SOCKET)sock_);
        sock_ = 0;
    }
}

bool TcpStream::send_raw(const std::vector<uint8_t> &frame)
{
    if(!valid() || frame.empty())
        return false;
    size_t off = 0;
    while(off < frame.size()) {
        int n = ::send((SOCKET)sock_, (const char*)frame.data() + off, (int)(frame.size() - off), 0);
        if(n <= 0)
            return false;
        off += (size_t)n;
    }
    return true;
}

bool TcpStream::send_pb(uint16_t msg_id, const google::protobuf::MessageLite &msg)
{
    std::vector<uint8_t> frame;
    if(!EncodePbFrame(msg_id, msg, frame))
        return false;
    return send_raw(frame);
}

bool TcpStream::feed(const uint8_t *data, size_t n, PbPacket &out, bool &got)
{
    got = false;
    if(data && n)
        rx_.insert(rx_.end(), data, data + n);
    uint16_t mid = 0;
    std::string payload;
    size_t consumed = 0;
    if(!TryDecodePbFrame(rx_.data(), rx_.size(), mid, payload, consumed)) {
        if(rx_.size() >= sizeof(PbFrameHdr)) {
            PbFrameHdr hdr;
            memcpy(&hdr, rx_.data(), sizeof(hdr));
            if(hdr.magic != PF_PB_MAGIC)
                return false;
        }
        return true; /* need more */
    }
    out.msg_id = mid;
    out.payload = std::move(payload);
    rx_.erase(rx_.begin(), rx_.begin() + (ptrdiff_t)consumed);
    got = true;
    return true;
}

bool TcpServer::listen_on(uint16_t port, int backlog)
{
    listen_ = (uintptr_t)::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(listen_ == 0 || listen_ == (uintptr_t)INVALID_SOCKET)
        return false;
    int yes = 1;
    setsockopt((SOCKET)listen_, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if(bind((SOCKET)listen_, (sockaddr*)&addr, sizeof(addr)) != 0) {
        close();
        return false;
    }
    if(listen((SOCKET)listen_, backlog) != 0) {
        close();
        return false;
    }
    set_nonblock(listen_);
    return true;
}

void TcpServer::close()
{
    if(listen_ && listen_ != (uintptr_t)INVALID_SOCKET) {
        closesocket((SOCKET)listen_);
        listen_ = 0;
    }
}

void TcpServer::poll(int timeout_ms,
                     const AcceptFn &on_accept,
                     const std::function<void(TcpStream&, const PbPacket&)> &on_msg,
                     const std::function<void(TcpStream&)> &on_close,
                     std::vector<std::unique_ptr<TcpStream>> &clients)
{
    fd_set rfds;
    FD_ZERO(&rfds);
    SOCKET maxfd = (SOCKET)listen_;
    FD_SET((SOCKET)listen_, &rfds);
    for(auto &c : clients) {
        if(c && c->valid()) {
            FD_SET((SOCKET)c->fd(), &rfds);
            if((SOCKET)c->fd() > maxfd)
                maxfd = (SOCKET)c->fd();
        }
    }
    timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int r = select((int)maxfd + 1, &rfds, nullptr, nullptr, &tv);
    if(r <= 0)
        return;

    if(FD_ISSET((SOCKET)listen_, &rfds)) {
        sockaddr_in peer{};
        socklen_t plen = sizeof(peer);
        SOCKET s = accept((SOCKET)listen_, (sockaddr*)&peer, &plen);
        if(s != INVALID_SOCKET) {
            auto st = std::make_unique<TcpStream>((uintptr_t)s);
            char ip[64];
            inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
            st->peer_ip = ip;
            if(on_accept)
                on_accept(std::move(st));
            else
                clients.push_back(std::move(st));
        }
    }

    char buf[8192];
    for(size_t i = 0; i < clients.size(); ) {
        auto &c = clients[i];
        if(!c || !c->valid()) {
            clients.erase(clients.begin() + (ptrdiff_t)i);
            continue;
        }
        if(!FD_ISSET((SOCKET)c->fd(), &rfds)) {
            ++i;
            continue;
        }
        int n = recv((SOCKET)c->fd(), buf, sizeof(buf), 0);
        if(n <= 0) {
            if(on_close)
                on_close(*c);
            c->close();
            clients.erase(clients.begin() + (ptrdiff_t)i);
            continue;
        }
        PbPacket pkt;
        bool got = false;
        if(!c->feed((const uint8_t*)buf, (size_t)n, pkt, got)) {
            if(on_close)
                on_close(*c);
            c->close();
            clients.erase(clients.begin() + (ptrdiff_t)i);
            continue;
        }
        while(got) {
            if(on_msg)
                on_msg(*c, pkt);
            got = false;
            if(!c->feed(nullptr, 0, pkt, got)) {
                if(on_close)
                    on_close(*c);
                c->close();
                break;
            }
        }
        if(!c->valid()) {
            clients.erase(clients.begin() + (ptrdiff_t)i);
            continue;
        }
        ++i;
    }
}

bool TcpConnect(const char *host, uint16_t port, std::unique_ptr<TcpStream> &out)
{
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(s == INVALID_SOCKET)
        return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if(inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        closesocket(s);
        return false;
    }
    if(connect(s, (sockaddr*)&addr, sizeof(addr)) != 0) {
        closesocket(s);
        return false;
    }
    out = std::make_unique<TcpStream>((uintptr_t)s);
    return true;
}

} // namespace pf
