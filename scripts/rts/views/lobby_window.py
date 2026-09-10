# -*- coding: utf-8 -*-

import pf

class LobbyWindow(pf.Window):

    WIDTH = 420
    HEIGHT = 520

    def __init__(self):
        super(LobbyWindow, self).__init__("房间列表",
            (300, 25, LobbyWindow.WIDTH, LobbyWindow.HEIGHT),
            pf.NK_WINDOW_BORDER | pf.NK_WINDOW_MOVABLE | pf.NK_WINDOW_MINIMIZABLE | pf.NK_WINDOW_TITLE | pf.NK_WINDOW_CLOSABLE,
            (1920, 1080))
        self.lobby_addr = "127.0.0.1:27000"
        self.rooms = []
        self.error = ""
        self.selected = -1
        self.status = "off"

    def update(self):
        if hasattr(pf, "lockstep_status"):
            self.status = pf.lockstep_status()

        self.layout_row_dynamic(18, 1)
        self.label_colored_wrap("大厅 (客户端外): http://%s" % self.lobby_addr, (180, 180, 180))

        self.layout_row_dynamic(28, 1)
        self.lobby_addr = self.edit_string(pf.NK_EDIT_SIMPLE, self.lobby_addr)

        def on_refresh():
            pf.global_event(0x20006, None)

        def on_join():
            if self.selected >= 0 and self.selected < len(self.rooms):
                pf.global_event(0x20007, self.rooms[self.selected])

        self.layout_row_dynamic(28, 2)
        self.button_label("刷新列表", on_refresh)
        self.button_label("加入选中", on_join)

        self.layout_row_dynamic(18, 1)
        self.label_colored_wrap("状态: %s  tick=%s  id=%s" % (
            self.status,
            str(pf.lockstep_tick()) if hasattr(pf, "lockstep_tick") else "-",
            str(pf.lockstep_player_id()) if hasattr(pf, "lockstep_player_id") else "-"),
            (220, 220, 120))

        if self.error:
            self.layout_row_dynamic(36, 1)
            self.label_colored_wrap(self.error, (255, 120, 120))

        def room_group():
            self.layout_row_dynamic(22, 1)
            if not self.rooms:
                self.label_colored_wrap("没有房间。先开大厅和房间服。", (160, 160, 160))
                return
            for i in range(len(self.rooms)):
                r = self.rooms[i]
                label = "#%s %s  %s:%s  %s/%s" % (
                    r.get("id", "?"), r.get("name", "?"),
                    r.get("host", "?"), r.get("port", "?"),
                    r.get("players", 0), r.get("max_players", 0))
                on = self.selectable_label(label, pf.NK_TEXT_ALIGN_LEFT, i == self.selected)
                if on:
                    self.selected = i

        self.layout_row_dynamic(280, 1)
        self.group("Rooms", pf.NK_WINDOW_BORDER, room_group)
