/*
 * Android multiplayer bridge (no Python): Lobby_* + LS_Init for LAN P2P.
 * All calls must run on the GL / engine thread (same as pf_android_frame).
 */
#ifndef ANDROID_MP_H
#define ANDROID_MP_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

bool pf_android_mp_host(uint16_t port, const char *name, int faction_id);
bool pf_android_mp_join(const char *host, uint16_t port, const char *name, int faction_id);
void pf_android_mp_shutdown(void);

bool pf_android_mp_set_map(const char *map_path);
bool pf_android_mp_set_seed(uint64_t seed);
bool pf_android_mp_set_ready(bool ready);
bool pf_android_mp_start_match(void);

bool pf_android_mp_is_host(void);
int  pf_android_mp_local_slot(void);
int  pf_android_mp_player_count(void);
bool pf_android_mp_is_lockstep(void);

/* Fill buf with "slot|name|fac|ready|connected\n..." (NUL-terminated). */
int  pf_android_mp_players_text(char *buf, size_t buflen);

/* True once if a START_MATCH was received (flag cleared). Auto LS_Init runs too. */
bool pf_android_mp_consume_match_started(void);

/* Optional: map path from last match start (empty if none). */
const char *pf_android_mp_match_map(void);

#ifdef __cplusplus
}
#endif

#endif
