#
# Extra combat units for the RTS demo: random packs near the player
# that refill as they die.
#

import math
import pf
import rts.globals

from units.goblin import Goblin
from units.berzerker import Berzerker


TARGET_ALIVE = 28
REFILL_BELOW = 16
PACK_SIZE = 8
MIN_DIST = 45.0
MAX_DIST = 110.0

MAP_CHUNKS = 4
HALF_X = (MAP_CHUNKS * pf.TILES_PER_CHUNK_WIDTH * pf.X_COORDS_PER_TILE) * 0.5 - 24.0
HALF_Z = (MAP_CHUNKS * pf.TILES_PER_CHUNK_HEIGHT * pf.Z_COORDS_PER_TILE) * 0.5 - 24.0

_KINDS = (
    (Goblin, "assets/models/goblin", "goblin.pfobj", "Goblin", 2, 0.9, 3.0),
    (Berzerker, "assets/models/berzerker", "berzerker.pfobj", "Berzerker", 3, 0.7, 3.0),
)

_extra = []
_tick = [0]
_rng = [0xC0FFEE]


def _rand():
    _rng[0] = (1664525 * _rng[0] + 1013904223) & 0xffffffff
    return _rng[0]


def _u01():
    return _rand() / 4294967296.0


def _uniform(a, b):
    return a + (b - a) * _u01()


def _choice(seq):
    return seq[_rand() % len(seq)]


def _player_xz():
    n = 0
    sx = 0.0
    sz = 0.0
    for ent in rts.globals.scene_objs:
        try:
            if getattr(ent, "faction_id", None) != 1:
                continue
            p = ent.pos
            sx += p[0]
            sz += p[2]
            n += 1
        except Exception:
            pass
    if n:
        return (sx / n, sz / n)
    return (70.0, -90.0)


def _alive(ent):
    return ent in rts.globals.scene_objs


def _prune():
    alive = [e for e in _extra if _alive(e)]
    del _extra[:]
    _extra.extend(alive)


def _place_xz(origin, min_d, max_d):
    for _try in range(24):
        ang = _uniform(0.0, 6.28318530718)
        dist = _uniform(min_d, max_d)
        x = origin[0] + math.cos(ang) * dist
        z = origin[1] + math.sin(ang) * dist
        x = max(-HALF_X, min(HALF_X, x))
        z = max(-HALF_Z, min(HALF_Z, z))
        if not pf.map_pos_over_land(x, z):
            continue
        y = pf.map_height_at_point(x, z)
        if y is None:
            continue
        snapped = pf.map_nearest_pathable((x, z), radius=3.0)
        if snapped is None:
            continue
        x, z = snapped
        y = pf.map_height_at_point(x, z)
        if y is None:
            continue
        return (x, y, z)
    return None


def _spawn_one(origin, min_d, max_d, hunt):
    pos = _place_xz(origin, min_d, max_d)
    if pos is None:
        return None
    cls, path, pfobj, name, fac, scale, srad = _choice(_KINDS)
    unit = cls(path, pfobj, name)
    unit.pos = pos
    unit.scale = (scale, scale, scale)
    unit.faction_id = fac
    unit.selection_radius = srad
    unit.selectable = True
    unit.vision_range = 35.0
    rts.globals.scene_objs.append(unit)
    _extra.append(unit)
    if hunt:
        try:
            unit.attack(hunt)
        except Exception:
            pass
    return unit


def _spawn_pack(n, min_d, max_d):
    origin = _player_xz()
    hunt = origin
    for _i in range(n):
        _spawn_one(origin, min_d, max_d, hunt)


def on_update(user, event):
    _tick[0] += 1
    if _tick[0] % 90 != 0:
        return
    _prune()
    if len(_extra) < REFILL_BELOW:
        _spawn_pack(PACK_SIZE, MIN_DIST, MAX_DIST)


def setup():
    _spawn_pack(20, 50.0, 95.0)
    _spawn_pack(12, 100.0, 160.0)
    pf.register_event_handler(pf.EVENT_UPDATE_START, on_update, None)
