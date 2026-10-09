/*
 *  Player command encode/decode and application to game APIs.
 */

#define MEM_FILE_SYS MEM_SYS_LOCKSTEP
#define MEM_FILE_SUB 0

#include "public/lockstep.h"
#include "../game/public/game.h"
#include "../game/combat.h"
#include "../entity.h"

#include <string.h>
#include <assert.h>

size_t LS_CmdSerialize(const struct ls_player_frame *frame, uint8_t *out, size_t out_cap)
{
    if(!frame || !out)
        return 0;
    size_t need = sizeof(uint32_t) + sizeof(uint8_t) * 2 + sizeof(uint16_t)
                + frame->cmd_count * sizeof(struct ls_cmd);
    if(need > out_cap)
        return 0;
    size_t off = 0;
    memcpy(out + off, &frame->tick, sizeof(frame->tick)); off += sizeof(frame->tick);
    out[off++] = frame->player_id;
    out[off++] = frame->cmd_count;
    memcpy(out + off, &frame->reserved, sizeof(frame->reserved)); off += sizeof(frame->reserved);
    for(int i = 0; i < frame->cmd_count; i++) {
        memcpy(out + off, &frame->cmds[i], sizeof(struct ls_cmd));
        off += sizeof(struct ls_cmd);
    }
    return off;
}

bool LS_CmdDeserialize(const uint8_t *data, size_t len, struct ls_player_frame *out)
{
    if(!data || !out || len < 8)
        return false;
    memset(out, 0, sizeof(*out));
    size_t off = 0;
    memcpy(&out->tick, data + off, sizeof(out->tick)); off += sizeof(out->tick);
    out->player_id = data[off++];
    out->cmd_count = data[off++];
    if(out->cmd_count > LS_MAX_CMDS_PER_FRAME)
        return false;
    memcpy(&out->reserved, data + off, sizeof(out->reserved)); off += sizeof(out->reserved);
    for(int i = 0; i < out->cmd_count; i++) {
        if(off + sizeof(struct ls_cmd) > len)
            return false;
        memcpy(&out->cmds[i], data + off, sizeof(struct ls_cmd));
        off += sizeof(struct ls_cmd);
    }
    return true;
}

static void apply_one(const struct ls_cmd *cmd)
{
    int n = cmd->ent_count;
    if(n > LS_MAX_ENTITIES_PER_CMD)
        n = LS_MAX_ENTITIES_PER_CMD;

    switch(cmd->type) {
    case LS_CMD_MOVE:
        for(int i = 0; i < n; i++) {
            if(G_EntityExists(cmd->ents[i]))
                G_Move_SetDest(cmd->ents[i], (vec2_t){cmd->x, cmd->z}, false);
        }
        break;
    case LS_CMD_ATTACK:
        if(cmd->target_uid) {
            for(int i = 0; i < n; i++) {
                if(G_EntityExists(cmd->ents[i]))
                    G_Combat_AttackUnit(cmd->ents[i], cmd->target_uid);
            }
        }else{
            for(int i = 0; i < n; i++) {
                if(G_EntityExists(cmd->ents[i]))
                    G_Move_SetDest(cmd->ents[i], (vec2_t){cmd->x, cmd->z}, true);
            }
        }
        break;
    case LS_CMD_STOP:
        for(int i = 0; i < n; i++) {
            if(G_EntityExists(cmd->ents[i]))
                G_Combat_StopAttack(cmd->ents[i]);
        }
        break;
    case LS_CMD_SET_STANCE:
        for(int i = 0; i < n; i++) {
            if(G_EntityExists(cmd->ents[i]))
                G_Combat_SetStance(cmd->ents[i], (enum combat_stance)cmd->i0);
        }
        break;
    default:
        break;
    }
}

void LS_ApplyPlayerFrame(const struct ls_player_frame *frame)
{
    if(!frame)
        return;
    for(int i = 0; i < frame->cmd_count; i++)
        apply_one(&frame->cmds[i]);
}

bool LS_IssueMove(const uint32_t *ents, int n, float x, float z, bool attack)
{
    struct ls_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = attack ? LS_CMD_ATTACK : LS_CMD_MOVE;
    cmd.x = x;
    cmd.z = z;
    if(n > LS_MAX_ENTITIES_PER_CMD)
        n = LS_MAX_ENTITIES_PER_CMD;
    cmd.ent_count = (uint16_t)n;
    for(int i = 0; i < n; i++)
        cmd.ents[i] = ents[i];
    return LS_IssueCmd(&cmd);
}

bool LS_IssueAttack(const uint32_t *ents, int n, uint32_t target)
{
    struct ls_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = LS_CMD_ATTACK;
    cmd.target_uid = target;
    if(n > LS_MAX_ENTITIES_PER_CMD)
        n = LS_MAX_ENTITIES_PER_CMD;
    cmd.ent_count = (uint16_t)n;
    for(int i = 0; i < n; i++)
        cmd.ents[i] = ents[i];
    return LS_IssueCmd(&cmd);
}
