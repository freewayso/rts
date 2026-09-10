# -*- coding: utf-8 -*-
#
# Lockstep client entry: same demo map, no unsynced random mobs.
# Use with the external lobby + dedicated room server.
#

import pf
import rts.globals

import views.demo_window as dw
import views.action_pad_window as apw

import view_controllers.action_pad_vc as apvc
import view_controllers.demo_vc as dvc

from constants import *
from units import *

pf.set_ambient_light_color((1.0, 1.0, 1.0))
pf.set_emit_light_color((1.0, 1.0, 1.0))
pf.set_emit_light_pos((1664.0, 1024.0, 384.0))
pf.set_active_font("simhei.ttf")

pf.load_map("assets/maps", "demo.pfmap")
rts.globals.scene_objs, rts.globals.scene_regions, _ = pf.load_scene("assets/maps/demo.pfscene")
pf.set_skybox("assets/skyboxes/clouds_blue", "jpg")

pf.set_diplomacy_state(1, 2, pf.DIPLOMACY_STATE_WAR)
pf.set_diplomacy_state(1, 3, pf.DIPLOMACY_STATE_WAR)
pf.set_diplomacy_state(2, 3, pf.DIPLOMACY_STATE_WAR)

pf.set_faction_controllable(0, False)
pf.set_faction_controllable(2, False)
pf.set_faction_controllable(3, False)

main_cam = pf.get_active_camera()
debug_cam = pf.Camera(mode=pf.CAM_MODE_FPS, position=(0.0, 175.0, 0.0), pitch=-65.0, yaw=135.0)
active_cam = main_cam

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

demo_vc = dvc.DemoVC(dw.DemoWindow())
demo_vc.activate()

action_pad_vc = apvc.ActionPadVC(apw.ActionPadWindow())
action_pad_vc.activate()

pf.global_event(EVENT_LOBBY_SHOW, None)
