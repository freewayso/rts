#pragma once
#include "pf_proto.h"
#include <string>
#include <vector>
#include <cstdint>

namespace pf {

struct Message {
    std::string name;
    std::vector<std::string> args;
};

/* Encode/decode length-prefixed named messages (luabus-inspired). */
bool EncodeMessage(const Message &msg, std::vector<uint8_t> &out);
bool DecodeMessage(const uint8_t *data, size_t len, Message &out);

int ArgAsInt(const Message &m, size_t i, int def = 0);
uint32_t ArgAsU32(const Message &m, size_t i, uint32_t def = 0);
const std::string &ArgAsStr(const Message &m, size_t i);

} // namespace pf
