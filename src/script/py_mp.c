/*
 *  Python bindings for P2P lobby + lockstep (same-room shared match).
 */
#include <Python.h>

#include "../lockstep/public/lockstep.h"
#include "../net/public/net.h"
#include "../lib/public/pf_string.h"

#include <string.h>
#include <stdio.h>
#include <stdint.h>

static struct lobby_start_info s_pending_start;
static int                     s_have_match_start;
static int                     s_mp_expected_players;

static void on_match_start_cb(const struct lobby_start_info *info, void *user)
{
    (void)user;
    if(!info)
        return;
    s_pending_start = *info;
    s_have_match_start = 1;
    fprintf(stderr, "[mp] match_start seed=%llu players=%d local=%d\n",
            (unsigned long long)info->rng_seed, info->num_players,
            info->local_player_id);
}

static void on_player_list_cb(const struct lobby_player *players, int count, void *user)
{
    (void)user;
    fprintf(stderr, "[mp] players=%d\n", count);
    for(int i = 0; i < count; i++) {
        if(players[i].connected)
            fprintf(stderr, "  slot=%d fac=%d name=%s ready=%d\n",
                    players[i].slot, players[i].faction_id,
                    players[i].name, (int)players[i].ready);
    }
}

static void on_disconnected_cb(int peer_slot, void *user)
{
    (void)user;
    fprintf(stderr, "[mp] peer disconnected slot=%d\n", peer_slot);
    LS_NotifyPeerDisconnected(peer_slot);
}

static struct lobby_callbacks make_cbs(void)
{
    struct lobby_callbacks cb;
    memset(&cb, 0, sizeof(cb));
    cb.on_match_start = on_match_start_cb;
    cb.on_player_list = on_player_list_cb;
    cb.on_disconnected = on_disconnected_cb;
    return cb;
}

PyObject *PyPf_mp_host(PyObject *self, PyObject *args)
{
    int port = NET_DEFAULT_PORT;
    const char *name = "Player";
    int faction = 1;
    if(!PyArg_ParseTuple(args, "|isi", &port, &name, &faction))
        return NULL;
    s_have_match_start = 0;
    struct lobby_callbacks cb = make_cbs();
    if(!Lobby_Host((uint16_t)port, name, faction, &cb)) {
        PyErr_SetString(PyExc_RuntimeError, "Lobby_Host failed");
        return NULL;
    }
    fprintf(stderr, "[mp] hosting on port %d as %s fac=%d\n", port, name, faction);
    Py_RETURN_TRUE;
}

PyObject *PyPf_mp_join(PyObject *self, PyObject *args)
{
    const char *host = "127.0.0.1";
    int port = NET_DEFAULT_PORT;
    const char *name = "Player";
    int faction = 2;
    if(!PyArg_ParseTuple(args, "|sisi", &host, &port, &name, &faction))
        return NULL;
    s_have_match_start = 0;
    struct net_address addr;
    if(!Net_ParseAddress(host, (uint16_t)port, &addr)) {
        PyErr_SetString(PyExc_RuntimeError, "bad host address");
        return NULL;
    }
    struct lobby_callbacks cb = make_cbs();
    if(!Lobby_Join(addr, name, faction, &cb)) {
        /* Soft fail — caller may retry */
        Py_RETURN_FALSE;
    }
    fprintf(stderr, "[mp] joining %s:%d as %s fac=%d\n", host, port, name, faction);
    Py_RETURN_TRUE;
}

PyObject *PyPf_mp_set_map(PyObject *self, PyObject *args)
{
    const char *path;
    if(!PyArg_ParseTuple(args, "s", &path))
        return NULL;
    if(!Lobby_SetMap(path))
        Py_RETURN_FALSE;
    Py_RETURN_TRUE;
}

PyObject *PyPf_mp_set_seed(PyObject *self, PyObject *args)
{
    unsigned long long seed;
    if(!PyArg_ParseTuple(args, "K", &seed))
        return NULL;
    if(!Lobby_SetSeed((uint64_t)seed))
        Py_RETURN_FALSE;
    Py_RETURN_TRUE;
}

PyObject *PyPf_mp_set_expected_players(PyObject *self, PyObject *args)
{
    int n;
    if(!PyArg_ParseTuple(args, "i", &n))
        return NULL;
    s_mp_expected_players = n;
    Py_RETURN_NONE;
}

PyObject *PyPf_mp_player_count(PyObject *self)
{
    return PyInt_FromLong(Lobby_PlayerCount());
}

PyObject *PyPf_mp_peer_count(PyObject *self)
{
    return PyInt_FromLong(Net_PeerCount());
}

PyObject *PyPf_mp_local_slot(PyObject *self)
{
    return PyInt_FromLong(Lobby_LocalSlot());
}

PyObject *PyPf_mp_is_host(PyObject *self)
{
    if(Lobby_IsHost())
        Py_RETURN_TRUE;
    Py_RETURN_FALSE;
}

PyObject *PyPf_mp_start_match(PyObject *self)
{
    if(!Lobby_StartMatch())
        Py_RETURN_FALSE;
    Py_RETURN_TRUE;
}

PyObject *PyPf_mp_take_match_start(PyObject *self)
{
    if(!s_have_match_start)
        Py_RETURN_NONE;
    s_have_match_start = 0;
    PyObject *d = PyDict_New();
    if(!d)
        return NULL;
    PyDict_SetItemString(d, "rng_seed", PyLong_FromUnsignedLongLong(s_pending_start.rng_seed));
    PyDict_SetItemString(d, "sim_hz", PyInt_FromLong(s_pending_start.sim_hz));
    PyDict_SetItemString(d, "command_delay", PyInt_FromLong(s_pending_start.command_delay));
    PyDict_SetItemString(d, "local_player_id", PyInt_FromLong(s_pending_start.local_player_id));
    PyDict_SetItemString(d, "num_players", PyInt_FromLong(s_pending_start.num_players));
    PyDict_SetItemString(d, "map_path", PyString_FromString(s_pending_start.map_path));
    return d;
}

PyObject *PyPf_lockstep_init(PyObject *self, PyObject *args)
{
    unsigned long long seed = 1;
    int hz = LS_DEFAULT_HZ;
    int delay = LS_DEFAULT_DELAY;
    int local_id = 0;
    int num_players = 2;
    int is_host = 0;
    int local_only = 0;
    PyObject *factions = NULL;

    if(!PyArg_ParseTuple(args, "Kiiiiii|O",
                         &seed, &hz, &delay, &local_id, &num_players,
                         &is_host, &local_only, &factions))
        return NULL;

    struct ls_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.rng_seed = (uint64_t)seed;
    cfg.sim_hz = (uint32_t)(hz > 0 ? hz : LS_DEFAULT_HZ);
    cfg.command_delay = (uint32_t)(delay > 0 ? delay : LS_DEFAULT_DELAY);
    cfg.local_player_id = local_id;
    cfg.num_players = num_players;
    cfg.is_host = is_host != 0;
    cfg.local_only = local_only != 0;

    if(factions && PySequence_Check(factions)) {
        Py_ssize_t n = PySequence_Size(factions);
        for(Py_ssize_t i = 0; i < n && i < LS_MAX_PLAYERS; i++) {
            PyObject *item = PySequence_GetItem(factions, i);
            if(item && PyInt_Check(item))
                cfg.faction_ids[i] = (int)PyInt_AS_LONG(item);
            Py_XDECREF(item);
        }
    } else {
        /* Online default: seat S -> faction 1+(S%3) */
        for(int i = 0; i < LS_MAX_PLAYERS; i++)
            cfg.faction_ids[i] = 1 + (i % 3);
    }

    if(local_id < 0 || local_id >= LS_MAX_PLAYERS) {
        PyErr_Format(PyExc_RuntimeError,
                     "LS_Init bad local_player_id=%d (lobby seat not assigned yet)",
                     local_id);
        return NULL;
    }
    if(!LS_Init(&cfg)) {
        PyErr_SetString(PyExc_RuntimeError, "LS_Init failed");
        return NULL;
    }
    fprintf(stderr, "[mp] lockstep active seed=%llu local=%d players=%d host=%d\n",
            (unsigned long long)seed, local_id, num_players, is_host);
    Py_RETURN_TRUE;
}

PyObject *PyPf_lockstep_active(PyObject *self)
{
    if(LS_IsLockstep())
        Py_RETURN_TRUE;
    Py_RETURN_FALSE;
}

PyObject *PyPf_mp_service(PyObject *self)
{
    /* Handshake pump only — full LS_Service runs each engine frame.
     * Once lockstep owns the socket, do not drain lobby (overlapping type ids
     * used to swallow command frames). */
    if(!LS_IsLockstep())
        Lobby_Service();
    Py_RETURN_NONE;
}
