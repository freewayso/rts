#pragma once
/*
 * Shared protocol constants for Login / Room — protobuf over framed TCP/UDP.
 * Message schemas: server/proto/*.proto  (pf.net.MsgId, etc.)
 */
#include <stdint.h>
#include <stddef.h>

#define PF_PB_MAGIC         0x50465042u /* 'PFPB' */
#define PF_MSG_MAGIC        PF_PB_MAGIC /* legacy alias */
#define PF_MSG_VERSION      1
#define PF_MAX_USERNAME     32
#define PF_MAX_TOKEN        64
#define PF_MAX_ROOM_NAME    64
#define PF_MAX_MAP_PATH     256
#define PF_MAX_PLAYERS_ROOM 8

#define PF_LOGIN_DEFAULT_PORT  27000
#define PF_ROOM_TCP_PORT       27001
#define PF_ROOM_UDP_PORT       27002

enum pf_errcode {
    PF_OK = 0,
    PF_ERR_UNAUTHORIZED = 1,
    PF_ERR_NOT_FOUND = 2,
    PF_ERR_FULL = 3,
    PF_ERR_NOT_OWNER = 4,
    PF_ERR_BAD_STATE = 5,
    PF_ERR_ALREADY_IN_ROOM = 6,
    PF_ERR_EXISTS = 7,
    PF_ERR_BAD_PASS = 8,
    PF_ERR_INTERNAL = 9,
    PF_ERR_BAD_ARGS = 10
};

enum pf_room_state {
    PF_ROOM_OPEN = 0,
    PF_ROOM_STARTING = 1,
    PF_ROOM_IN_MATCH = 2,
    PF_ROOM_CLOSED = 3
};

/* Wire frame header before protobuf payload (see pb_frame.h). */
#pragma pack(push, 1)
struct pf_pb_hdr {
    uint32_t magic;
    uint16_t version;
    uint16_t msg_id;
    uint32_t payload_len;
};
#pragma pack(pop)
