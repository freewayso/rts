#ifndef LS_PROTOCOL_H
#define LS_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "public/lockstep.h"

#define LS_MAGIC           0x50
#define LS_PROTO_VER       1
#define LS_MAX_CMDS        48
#define LS_MAX_PAYLOAD     2048
#define LS_MAX_PACKET      (16 + LS_MAX_PAYLOAD)

enum ls_msg{
    LS_MSG_HELLO        = 1,
    LS_MSG_WELCOME      = 2,
    LS_MSG_START        = 3,
    LS_MSG_CMD_FRAME    = 4,
    LS_MSG_CMD_EMPTY    = 5,
    LS_MSG_HEARTBEAT    = 6,
    LS_MSG_HASH_REPORT  = 7,
    LS_MSG_DESYNC       = 8,
    LS_MSG_SNAP_REQ     = 9,
    LS_MSG_SNAP_BEGIN   = 10,
    LS_MSG_CMD_REPLAY   = 11,
    LS_MSG_CATCHUP_DONE = 12,
    LS_MSG_LEAVE        = 13,
    LS_MSG_PAUSE        = 14
};

enum ls_cmd_type{
    LS_CMD_MOVE   = 1,
    LS_CMD_ATTACK = 2,
    LS_CMD_STOP   = 3
};

struct ls_cmd{
    uint8_t  type;
    uint8_t  attack;
    uint32_t uid;
    uint32_t target;
    float    x, z;
};

struct ls_packet{
    uint8_t  msg_type;
    uint8_t  player_id;
    uint32_t tick;
    uint32_t checksum;
    uint16_t payload_len;
    uint8_t  payload[LS_MAX_PAYLOAD];
};

size_t ls_pkt_encode(const struct ls_packet *pkt, uint8_t *out, size_t maxout);
bool   ls_pkt_decode(const uint8_t *in, size_t inlen, struct ls_packet *out);

size_t ls_cmds_encode(const struct ls_cmd *cmds, int n, uint8_t *out, size_t maxout);
int    ls_cmds_decode(const uint8_t *in, size_t inlen, struct ls_cmd *out, int maxn);

#endif
