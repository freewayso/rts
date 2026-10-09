#pragma once
/* Launch the Permafrost single-player engine (lib/pf.exe) from the lobby UI. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct engine_launch_opts {
    const char *map_path;     /* e.g. assets/maps/demo.pfmap */
    int         slot;         /* local player seat */
    const char *local_user;   /* login name */
    const char *roster;       /* "slot:name;..." */
    /* Shared-match / lockstep (0 players or NULL host => solo, no P2P) */
    uint64_t    rng_seed;
    int         num_players;
    uint32_t    room_id;
    const char *mp_host;      /* e.g. 127.0.0.1; NULL/empty = solo */
};

/* Returns true if CreateProcess succeeded. On failure, err_out (wide) is filled. */
bool EngineLaunch_StartMatch(const struct engine_launch_opts *opts,
                             wchar_t *err_out, int err_n);

/* True if a previous launch is still running. */
bool EngineLaunch_IsRunning(void);

/* Resolve engine root (directory with assets/ + scripts/). Empty if not found. */
bool EngineLaunch_FindRoot(wchar_t *out, int out_n);

#ifdef __cplusplus
}
#endif
