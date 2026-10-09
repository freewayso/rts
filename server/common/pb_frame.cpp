#include "pb_frame.h"
#include <cstring>

namespace pf {

bool EncodePbFrame(uint16_t msg_id, const google::protobuf::MessageLite &msg,
                   std::vector<uint8_t> &out)
{
    std::string body;
    if(!msg.SerializeToString(&body))
        return false;
    if(body.size() > 0x100000)
        return false;
    out.resize(sizeof(PbFrameHdr) + body.size());
    PbFrameHdr hdr{};
    hdr.magic = PF_PB_MAGIC;
    hdr.version = PF_MSG_VERSION;
    hdr.msg_id = msg_id;
    hdr.payload_len = (uint32_t)body.size();
    memcpy(out.data(), &hdr, sizeof(hdr));
    if(!body.empty())
        memcpy(out.data() + sizeof(hdr), body.data(), body.size());
    return true;
}

bool TryDecodePbFrame(const uint8_t *data, size_t len,
                      uint16_t &msg_id, std::string &payload, size_t &consumed)
{
    consumed = 0;
    if(len < sizeof(PbFrameHdr))
        return false;
    PbFrameHdr hdr;
    memcpy(&hdr, data, sizeof(hdr));
    if(hdr.magic != PF_PB_MAGIC || hdr.version != PF_MSG_VERSION)
        return false;
    size_t need = sizeof(PbFrameHdr) + hdr.payload_len;
    if(len < need)
        return false;
    msg_id = hdr.msg_id;
    payload.assign((const char*)data + sizeof(PbFrameHdr), hdr.payload_len);
    consumed = need;
    return true;
}

} // namespace pf
