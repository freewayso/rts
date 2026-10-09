#pragma once
#include "pf_proto.h"
#include <google/protobuf/message_lite.h>
#include <cstdint>
#include <string>
#include <vector>

namespace pf {

#pragma pack(push, 1)
struct PbFrameHdr {
    uint32_t magic;
    uint16_t version;
    uint16_t msg_id;
    uint32_t payload_len;
};
#pragma pack(pop)

bool EncodePbFrame(uint16_t msg_id, const google::protobuf::MessageLite &msg,
                   std::vector<uint8_t> &out);

bool TryDecodePbFrame(const uint8_t *data, size_t len,
                      uint16_t &msg_id, std::string &payload, size_t &consumed);

template<typename T>
bool ParsePayload(const std::string &payload, T &out)
{
    return out.ParseFromArray(payload.data(), (int)payload.size());
}

} // namespace pf
