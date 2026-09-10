#include "protocol.h"
#include <string.h>

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
    p[2] = (uint8_t)((v >> 16) & 0xff);
    p[3] = (uint8_t)((v >> 24) & 0xff);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

size_t ls_pkt_encode(const struct ls_packet *pkt, uint8_t *out, size_t maxout)
{
    size_t need = 13u + (size_t)pkt->payload_len;
    if(maxout < need || pkt->payload_len > LS_MAX_PAYLOAD)
        return 0;
    out[0] = LS_MAGIC;
    out[1] = pkt->msg_type;
    put_u32(out + 2, pkt->tick);
    out[6] = pkt->player_id;
    put_u16(out + 7, pkt->payload_len);
    put_u32(out + 9, pkt->checksum);
    if(pkt->payload_len)
        memcpy(out + 13, pkt->payload, pkt->payload_len);
    return need;
}

bool ls_pkt_decode(const uint8_t *in, size_t inlen, struct ls_packet *out)
{
    if(inlen < 13 || in[0] != LS_MAGIC)
        return false;
    out->msg_type = in[1];
    out->tick = get_u32(in + 2);
    out->player_id = in[6];
    out->payload_len = get_u16(in + 7);
    out->checksum = get_u32(in + 9);
    if(out->payload_len > LS_MAX_PAYLOAD || inlen < 13u + out->payload_len)
        return false;
    if(out->payload_len)
        memcpy(out->payload, in + 13, out->payload_len);
    return true;
}

size_t ls_cmds_encode(const struct ls_cmd *cmds, int n, uint8_t *out, size_t maxout)
{
    if(n < 0 || n > LS_MAX_CMDS)
        return 0;
    size_t need = 1u + (size_t)n * 18u;
    if(maxout < need)
        return 0;
    out[0] = (uint8_t)n;
    for(int i = 0; i < n; i++) {
        uint8_t *p = out + 1 + i * 18;
        p[0] = cmds[i].type;
        p[1] = cmds[i].attack;
        put_u32(p + 2, cmds[i].uid);
        put_u32(p + 6, cmds[i].target);
        memcpy(p + 10, &cmds[i].x, 4);
        memcpy(p + 14, &cmds[i].z, 4);
    }
    return need;
}

int ls_cmds_decode(const uint8_t *in, size_t inlen, struct ls_cmd *out, int maxn)
{
    if(inlen < 1)
        return -1;
    int n = in[0];
    if(n < 0 || n > LS_MAX_CMDS || n > maxn)
        return -1;
    if(inlen < 1u + (size_t)n * 18u)
        return -1;
    for(int i = 0; i < n; i++) {
        const uint8_t *p = in + 1 + i * 18;
        out[i].type = p[0];
        out[i].attack = p[1];
        out[i].uid = get_u32(p + 2);
        out[i].target = get_u32(p + 6);
        memcpy(&out[i].x, p + 10, 4);
        memcpy(&out[i].z, p + 14, 4);
    }
    return n;
}
