# -*- coding: ascii -*-
# Online lobby -> engine entry after match_start.
# Launched by online_client_ui:
#   pf.exe <root> scripts/rts/match_online.py
#     --PF_MATCH_MAP=... --PF_MATCH_SLOT=...
#     --PF_MATCH_USER=... --PF_MATCH_ROSTER=slot:name;...
#

import traceback

import pf
import rts.globals

import views.demo_window as dw
import views.action_pad_window as apw

import view_controllers.action_pad_vc as apvc
import view_controllers.demo_vc as dvc

from constants import *
from units import *

############################################################
# Resolve map / seat / roster from lobby                   #
############################################################

def _split_map_hint(hint):
    hint = (hint or "").replace("\\", "/").strip()
    if not hint:
        return "assets/maps", "demo.pfmap"
    if hint.startswith("maps/") and not hint.startswith("assets/"):
        hint = "assets/" + hint
    if "/" in hint:
        d, f = hint.rsplit("/", 1)
        if not d:
            d = "assets/maps"
        return d, f
    return "assets/maps", hint

def _arg(name, default=""):
    try:
        v = pf.get_arg(name)
        if v is None:
            return default
        return v
    except Exception:
        traceback.print_exc()
        return default

def _parse_roster(text):
    """Parse 'slot:name;slot:name' -> {faction_id: display_name}.
    Seat 0/1/2 -> player factions 1/2/3.
    """
    out = {}
    if not text:
        return out
    for part in str(text).split(";"):
        part = part.strip()
        if not part or ":" not in part:
            continue
        slot_s, name = part.split(":", 1)
        try:
            s = int(slot_s)
        except Exception:
            continue
        name = (name or "").strip()
        if not name:
            continue
        fac = 1 + (s % 3)
        # First claim for a faction wins (unique seats expected)
        if fac not in out:
            out[fac] = name[:31]
    return out

map_hint = _arg("PF_MATCH_MAP", "assets/maps/demo.pfmap")
slot = 0
try:
    v = _arg("PF_MATCH_SLOT", "0")
    if v is not None and str(v).strip() != "":
        slot = int(v)
except Exception:
    traceback.print_exc()
    slot = 0

local_user = _arg("PF_MATCH_USER", "Player") or "Player"
local_user = str(local_user).strip()[:31] or "Player"
roster_fac = _parse_roster(_arg("PF_MATCH_ROSTER", ""))
# Ensure local seat is represented
player_faction = 1 + (int(slot) % 3)
if player_faction not in roster_fac:
    roster_fac[player_faction] = local_user

map_dir, map_file = _split_map_hint(map_hint)
scene_path = map_dir + "/" + map_file
if scene_path.lower().endswith(".pfmap"):
    scene_path = scene_path[:-6] + ".pfscene"

############################################################
# Global configs                                           #
############################################################

pf.set_ambient_light_color((1.0, 1.0, 1.0))
pf.set_emit_light_color((1.0, 1.0, 1.0))
pf.set_emit_light_pos((1664.0, 1024.0, 384.0))
pf.set_active_font("OptimusPrinceps.ttf")

############################################################
# Setup map/scene                                          #
############################################################

pf.load_map(map_dir, map_file)
try:
    rts.globals.scene_objs, rts.globals.scene_regions, _ = pf.load_scene(scene_path)
except Exception:
    traceback.print_exc()
    rts.globals.scene_objs = []
    rts.globals.scene_regions = []

pf.disable_fog_of_war()
try:
    for _fac in xrange(4):
        pf.explore_map(_fac)
except Exception:
    traceback.print_exc()

pf.set_skybox("assets/skyboxes/clouds_blue", "jpg")

# Player factions fight each other (PvP). Faction 0 = neutral/gaia.
pf.set_diplomacy_state(1, 2, pf.DIPLOMACY_STATE_WAR)
pf.set_diplomacy_state(1, 3, pf.DIPLOMACY_STATE_WAR)
pf.set_diplomacy_state(2, 3, pf.DIPLOMACY_STATE_WAR)

pf.set_faction_controllable(0, False)
for f in (1, 2, 3):
    pf.set_faction_controllable(f, f == player_faction)

# Rename player factions to login names (shown in UI + nameplates).
# Colors are relative to THIS client: own army = blue, other players = deep red
# (war diplomacy already set above so they can fight).
DEFAULT_FAC_NAMES = {
    1: "Player1",
    2: "Player2",
    3: "Player3",
}
# P1/P2 view: own always blue; enemies always deep red (never faction-default green).
OWN_COLOR = (40, 110, 255, 255)
ENEMY_COLOR = (210, 28, 28, 255)
try:
    for fac_id in (1, 2, 3):
        name = roster_fac.get(fac_id) or DEFAULT_FAC_NAMES[fac_id]
        if fac_id == player_faction:
            color = OWN_COLOR
        else:
            color = ENEMY_COLOR
        pf.update_faction(fac_id, name, color)
except Exception:
    traceback.print_exc()

# Re-assert war so selection rings use enemy-red, not allied-green.
try:
    pf.set_diplomacy_state(1, 2, pf.DIPLOMACY_STATE_WAR)
    pf.set_diplomacy_state(1, 3, pf.DIPLOMACY_STATE_WAR)
    pf.set_diplomacy_state(2, 3, pf.DIPLOMACY_STATE_WAR)
except Exception:
    traceback.print_exc()

# Cache faction_id -> display name for overhead labels
_fac_names = {}
try:
    for f in pf.get_factions_list():
        _fac_names[f["id"]] = f["name"]
except Exception:
    traceback.print_exc()

_fac_colors = {}
for _f in (1, 2, 3):
    if _f == player_faction:
        _fac_colors[_f] = OWN_COLOR
    else:
        _fac_colors[_f] = ENEMY_COLOR

############################################################
# Overhead player nameplates + floating damage numbers     #
############################################################

_hp_prev = {}
_dmg_floaters = []  # dicts: text, sx, sy0, birth_ms, color
_DMG_FLOAT_MS = 3600
_FONT_NORMAL = "OptimusPrinceps.ttf"
_FONT_DMG = "OptimusPrinceps.ttf.32"

def _now_ms():
    try:
        return int(pf.get_ticks())
    except Exception:
        return 0

def _draw_unit_nameplates(user, event):
    objs = rts.globals.scene_objs or []
    now = _now_ms()
    for ent in objs:
        try:
            if not isinstance(ent, pf.CombatableEntity):
                continue
            fac = ent.faction_id
            if fac not in (1, 2, 3):
                continue
            # Skip dead / depleted units
            try:
                hp = int(ent.hp)
            except Exception:
                continue
            uid = int(ent.uid)
            prev = _hp_prev.get(uid)
            if prev is None:
                _hp_prev[uid] = hp
            elif hp < prev:
                dmg = int(prev - hp)
                if dmg > 0:
                    try:
                        sx, sy = ent.top_screen_pos
                        _dmg_floaters.append({
                            "text": "-%d" % dmg,
                            "sx": float(sx),
                            "sy0": float(sy) - 36.0,
                            "birth": now,
                            "color": (255, 48, 48, 255),
                        })
                    except Exception:
                        pass
                _hp_prev[uid] = hp
            else:
                _hp_prev[uid] = hp
            if hp <= 0:
                continue
            name = _fac_names.get(fac)
            if not name:
                continue
            sx, sy = ent.top_screen_pos
            # Slightly above healthbar
            w, h = 120, 18
            bounds = (int(sx - w / 2), int(sy - 28), w, h)
            color = _fac_colors.get(fac, (255, 255, 255, 255))
            pf.draw_text(name, bounds, color)
        except Exception:
            pass

    # Rising damage numbers (large font + outline so they read in combat)
    alive = []
    try:
        pf.set_active_font(_FONT_DMG)
    except Exception:
        pass
    for f in _dmg_floaters:
        age = now - int(f.get("birth", now))
        if age < 0 or age > _DMG_FLOAT_MS:
            continue
        # Slow drift upward; hold opaque longer, then fade.
        rise = age * 0.010
        fade_start = _DMG_FLOAT_MS - 1100
        alpha = 255
        if age > fade_start:
            alpha = max(0, int(255 * (1.0 - float(age - fade_start) / 1100.0)))
        col = f.get("color", (255, 48, 48, 255))
        color = (col[0], col[1], col[2], alpha)
        outline = (0, 0, 0, alpha)
        sx = int(f["sx"] - 36)
        sy = int(f["sy0"] - rise - 8)
        bounds = (sx, sy, 96, 40)
        try:
            for ox, oy in ((-2, 0), (2, 0), (0, -2), (0, 2)):
                pf.draw_text(f["text"], (sx + ox, sy + oy, 96, 40), outline)
            pf.draw_text(f["text"], bounds, color)
        except Exception:
            pass
        alive.append(f)
    del _dmg_floaters[:]
    _dmg_floaters.extend(alive)
    try:
        pf.set_active_font(_FONT_NORMAL)
    except Exception:
        pass

pf.register_event_handler(pf.EVENT_UPDATE_START, _draw_unit_nameplates, None)

############################################################
# Camera                                                   #
############################################################

main_cam = pf.get_active_camera()
debug_cam = pf.Camera(mode=pf.CAM_MODE_FPS, position=(0.0, 175.0, 0.0), pitch=-65.0, yaw=135.0)
active_cam = main_cam

def _focus_camera_on_faction(fac_id):
    """Center RTS camera on that faction's army (player2 starts far from p1)."""
    xs = []
    zs = []
    for ent in (rts.globals.scene_objs or []):
        try:
            if not isinstance(ent, pf.CombatableEntity):
                continue
            if int(ent.faction_id) != int(fac_id):
                continue
            try:
                if ent.hp <= 0:
                    continue
            except Exception:
                pass
            p = ent.pos
            xs.append(float(p[0]))
            zs.append(float(p[2]))
        except Exception:
            pass
    if not xs:
        print("[match_online] no units for faction %s to focus" % fac_id)
        return
    cx = sum(xs) / float(len(xs))
    cz = sum(zs) / float(len(zs))
    try:
        pf.get_active_camera().center_over_location((cx, cz))
        print("[match_online] camera -> faction %s at (%.1f, %.1f) n=%d" % (
            fac_id, cx, cz, len(xs)))
    except Exception:
        traceback.print_exc()

_focus_camera_on_faction(player_faction)

def toggle_camera(user, event):
    if event[0] == pf.SDL_SCANCODE_C and not pf.ui_text_edit_has_focus():
        global active_cam, main_cam, debug_cam
        if active_cam == main_cam:
            active_cam = debug_cam
        else:
            active_cam = main_cam
        pf.set_active_camera(active_cam)

def toggle_pause(user, event):
    if event[0] == pf.SDL_SCANCODE_P and not pf.ui_text_edit_has_focus():
        ss = pf.get_simstate()
        if ss == pf.G_RUNNING:
            pf.set_simstate(pf.G_PAUSED_UI_RUNNING)
        else:
            pf.set_simstate(pf.G_RUNNING)

pf.register_ui_event_handler(pf.SDL_KEYDOWN, toggle_camera, None)
pf.register_ui_event_handler(pf.SDL_KEYDOWN, toggle_pause, None)

############################################################
# Setup UI                                                 #
############################################################

demo_window = dw.DemoWindow()
demo_vc = dvc.DemoVC(demo_window)
demo_vc.activate()
# Lock controllable faction to this seat -- Demo panel faction clicks used to
# flip control, making selected units SELECTION_TYPE_ENEMY so RMB orders no-op.
def _reassert_local_faction(user, event):
    try:
        for _f in xrange(len(pf.get_factions_list())):
            pf.set_faction_controllable(_f, False)
        pf.set_faction_controllable(player_faction, True)
        demo_window.active_fac_idx = player_faction
        # Keep PvP war + relative colors (own blue / enemy deep red).
        pf.set_diplomacy_state(1, 2, pf.DIPLOMACY_STATE_WAR)
        pf.set_diplomacy_state(1, 3, pf.DIPLOMACY_STATE_WAR)
        pf.set_diplomacy_state(2, 3, pf.DIPLOMACY_STATE_WAR)
        for fac_id in (1, 2, 3):
            name = _fac_names.get(fac_id) or DEFAULT_FAC_NAMES.get(fac_id, "Player")
            color = OWN_COLOR if fac_id == player_faction else ENEMY_COLOR
            pf.update_faction(fac_id, name, color)
            _fac_colors[fac_id] = color
    except Exception:
        traceback.print_exc()

try:
    _reassert_local_faction(None, None)
    pf.register_ui_event_handler(EVENT_CONTROLLED_FACTION_CHANGED, _reassert_local_faction, None)
except Exception:
    traceback.print_exc()

action_pad_window = apw.ActionPadWindow()
action_pad_vc = apvc.ActionPadVC(action_pad_window)
action_pad_vc.activate()

def _select_own_army():
    """Select own units so the right ActionPad fills with command icons.
    Empty dark grid = ActionPad with no selection (easy to mistake for 'missing')."""
    own = []
    for ent in (rts.globals.scene_objs or []):
        try:
            if not isinstance(ent, pf.CombatableEntity):
                continue
            if int(ent.faction_id) != int(player_faction):
                continue
            try:
                if ent.hp <= 0:
                    continue
            except Exception:
                pass
            own.append(ent)
        except Exception:
            pass
    if not own:
        print("[match_online] no own units to select (fac=%s)" % player_faction)
        return
    try:
        pf.set_unit_selection(own[:24])
        print("[match_online] selected %d/%d own units for ActionPad" % (
            min(24, len(own)), len(own)))
    except Exception:
        traceback.print_exc()

_select_own_army()

def _ui_reshow(user, event):
    try:
        demo_window.show()
        action_pad_window.show()
        _select_own_army()
    except Exception:
        traceback.print_exc()
    try:
        pf.unregister_event_handler(pf.EVENT_UPDATE_START, _ui_reshow)
    except Exception:
        pass
pf.register_event_handler(pf.EVENT_UPDATE_START, _ui_reshow, None)

############################################################
# P2P lockstep - same room => one shared match             #
############################################################

_mp_role = str(_arg("PF_MP_ROLE", "solo") or "solo").lower()
_mp_host = str(_arg("PF_MP_HOST", "127.0.0.1") or "127.0.0.1")
_mp_pending_start = None
_mp_port = 27750
_mp_players = 1
_mp_seed = 1
try:
    _mp_port = int(_arg("PF_MP_PORT", "27750") or "27750")
except Exception:
    _mp_port = 27750
try:
    _mp_players = int(_arg("PF_MATCH_PLAYERS", "1") or "1")
except Exception:
    _mp_players = 1
try:
    _mp_seed = int(_arg("PF_MATCH_SEED", "1") or "1")
except Exception:
    _mp_seed = 1

_mp_started = False
_mp_join_attempts = 0
_mp_factions = tuple([1 + (i % 3) for i in xrange(8)])

def _begin_lockstep(info):
    global _mp_started, _mp_pending_start
    if _mp_started or not info:
        return False
    seed = int(info.get("rng_seed", _mp_seed))
    hz = int(info.get("sim_hz", 10))
    delay = int(info.get("command_delay", 2))
    local_id = int(info.get("local_player_id", slot))
    if local_id < 0:
        try:
            local_id = int(pf.mp_local_slot())
        except Exception:
            local_id = -1
    if local_id < 0:
        print("[match_online] defer lockstep_init: seat not assigned yet")
        _mp_pending_start = info
        return False
    nplayers = int(info.get("num_players", _mp_players))
    is_host = 1 if pf.mp_is_host() else 0
    print("[match_online] lockstep_init seed=%s local=%s players=%s host=%s" % (
        seed, local_id, nplayers, is_host))
    pf.lockstep_init(seed, hz, delay, local_id, nplayers, is_host, 0, _mp_factions)
    _mp_started = True
    _mp_pending_start = None
    return True

def _mp_tick(user, event):
    global _mp_join_attempts, _mp_started, _mp_pending_start
    if _mp_started or _mp_role not in ("host", "join"):
        return
    try:
        pf.mp_service()
    except Exception:
        traceback.print_exc()
        return

    info = pf.mp_take_match_start()
    if info:
        _mp_pending_start = info
    if _mp_pending_start and _begin_lockstep(_mp_pending_start):
        return

    if _mp_role == "host":
        # Wait until expected peers are in lobby, then start once.
        try:
            n = int(pf.mp_player_count())
        except Exception:
            n = 0
        if n >= max(2, int(_mp_players)):
            print("[match_online] host starting match (%d players)" % n)
            pf.mp_start_match()
            info2 = pf.mp_take_match_start()
            if info2:
                _mp_pending_start = info2
            if _mp_pending_start:
                _begin_lockstep(_mp_pending_start)
    elif _mp_role == "join":
        # Wait for WELCOME seat. Only re-join when the socket has no peers
        # (re-join while handshaking wiped WELCOME and left local_id=-1).
        if pf.mp_local_slot() < 0:
            _mp_join_attempts += 1
            peers = 0
            try:
                peers = int(pf.mp_peer_count())
            except Exception:
                peers = 0
            if peers <= 0 and (_mp_join_attempts % 20) == 1:
                print("[match_online] joining %s:%d ..." % (_mp_host, _mp_port))
                try:
                    pf.mp_join(_mp_host, _mp_port, local_user, player_faction)
                except Exception:
                    traceback.print_exc()

if _mp_role in ("host", "join") and _mp_players >= 2:
    print("[match_online] multiplayer role=%s port=%d players=%d seed=%d fac=%d" % (
        _mp_role, _mp_port, _mp_players, _mp_seed, player_faction))
    try:
        pf.mp_set_expected_players(_mp_players)
        if _mp_role == "host":
            pf.mp_host(_mp_port, local_user, player_faction)
            pf.mp_set_map(map_dir + "/" + map_file)
            pf.mp_set_seed(_mp_seed)
        else:
            pf.mp_join(_mp_host, _mp_port, local_user, player_faction)
        pf.register_event_handler(pf.EVENT_UPDATE_START, _mp_tick, None)
    except Exception:
        traceback.print_exc()
else:
    print("[match_online] solo mode (no lockstep)")
