# -*- coding: utf-8 -*-

import pf
from constants import *
import common.view_controllers.view_controller as vc

EVENT_LOBBY_REFRESH = 0x20006
EVENT_LOBBY_JOIN    = 0x20007

class LobbyVC(vc.ViewController):

    def __init__(self, view):
        self.__view = view

    def __parse_addr(self, s):
        host = "127.0.0.1"
        port = 27000
        if not s:
            return host, port
        parts = s.strip().split(":")
        if parts[0]:
            host = parts[0]
        if len(parts) > 1:
            try:
                port = int(parts[1])
            except ValueError:
                port = 27000
        return host, port

    def __on_show(self, event):
        if self.__view.hidden:
            self.__view.show()
        self.__on_refresh(None)

    def __on_refresh(self, event):
        host, port = self.__parse_addr(self.__view.lobby_addr)
        try:
            self.__view.rooms = pf.lobby_list_rooms(host, port)
            self.__view.error = ""
            if self.__view.selected >= len(self.__view.rooms):
                self.__view.selected = -1
        except Exception, e:
            self.__view.rooms = []
            self.__view.error = str(e)

    def __on_join(self, event):
        if not event:
            return
        ip = event.get("host")
        port = event.get("port")
        if not ip:
            self.__view.error = "房间没有地址"
            return
        try:
            pf.lockstep_join(str(ip), int(port))
            self.__view.error = ""
            self.__view.status = pf.lockstep_status()
        except Exception, e:
            self.__view.error = str(e)

    def activate(self):
        pf.register_ui_event_handler(EVENT_LOBBY_SHOW, LobbyVC.__on_show, self)
        pf.register_ui_event_handler(EVENT_LOBBY_REFRESH, LobbyVC.__on_refresh, self)
        pf.register_ui_event_handler(EVENT_LOBBY_JOIN, LobbyVC.__on_join, self)

    def deactivate(self):
        self.__view.hide()
        pf.unregister_event_handler(EVENT_LOBBY_JOIN, LobbyVC.__on_join)
        pf.unregister_event_handler(EVENT_LOBBY_REFRESH, LobbyVC.__on_refresh)
        pf.unregister_event_handler(EVENT_LOBBY_SHOW, LobbyVC.__on_show)
