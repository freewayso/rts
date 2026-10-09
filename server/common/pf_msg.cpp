#include "pf_msg.h"
#include <cstring>

namespace pf {

bool EncodeMessage(const Message &msg, std::vector<uint8_t> &out)
{
    if(msg.name.size() > PF_MAX_MSG_NAME || msg.args.size() > PF_MAX_ARGS)
        return false;

    size_t payload = msg.name.size();
    for(const auto &a : msg.args) {
        if(a.size() > PF_MAX_ARG_LEN)
            return false;
        payload += 4 + a.size();
    }

    out.resize(sizeof(pf_msg_hdr) + payload);
    auto *hdr = reinterpret_cast<pf_msg_hdr*>(out.data());
    hdr->magic = PF_MSG_MAGIC;
    hdr->version = PF_MSG_VERSION;
    hdr->name_len = (uint16_t)msg.name.size();
    hdr->argc = (uint16_t)msg.args.size();
    hdr->reserved = 0;

    uint8_t *p = out.data() + sizeof(pf_msg_hdr);
    memcpy(p, msg.name.data(), msg.name.size());
    p += msg.name.size();
    for(const auto &a : msg.args) {
        uint32_t n = (uint32_t)a.size();
        memcpy(p, &n, 4);
        p += 4;
        if(n) {
            memcpy(p, a.data(), n);
            p += n;
        }
    }
    return true;
}

bool DecodeMessage(const uint8_t *data, size_t len, Message &out)
{
    out.name.clear();
    out.args.clear();
    if(len < sizeof(pf_msg_hdr))
        return false;
    const auto *hdr = reinterpret_cast<const pf_msg_hdr*>(data);
    if(hdr->magic != PF_MSG_MAGIC || hdr->version != PF_MSG_VERSION)
        return false;
    if(hdr->name_len > PF_MAX_MSG_NAME || hdr->argc > PF_MAX_ARGS)
        return false;
    size_t need = sizeof(pf_msg_hdr) + hdr->name_len;
    if(len < need)
        return false;
    out.name.assign((const char*)data + sizeof(pf_msg_hdr), hdr->name_len);
    const uint8_t *p = data + sizeof(pf_msg_hdr) + hdr->name_len;
    size_t left = len - need;
    for(uint16_t i = 0; i < hdr->argc; ++i) {
        if(left < 4)
            return false;
        uint32_t n = 0;
        memcpy(&n, p, 4);
        p += 4;
        left -= 4;
        if(n > PF_MAX_ARG_LEN || left < n)
            return false;
        out.args.emplace_back((const char*)p, n);
        p += n;
        left -= n;
    }
    return true;
}

int ArgAsInt(const Message &m, size_t i, int def)
{
    if(i >= m.args.size() || m.args[i].empty())
        return def;
    return atoi(m.args[i].c_str());
}

uint32_t ArgAsU32(const Message &m, size_t i, uint32_t def)
{
    if(i >= m.args.size() || m.args[i].empty())
        return def;
    return (uint32_t)strtoul(m.args[i].c_str(), nullptr, 10);
}

const std::string &ArgAsStr(const Message &m, size_t i)
{
    static const std::string empty;
    if(i >= m.args.size())
        return empty;
    return m.args[i];
}

} // namespace pf
