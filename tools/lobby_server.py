#!/usr/bin/env python3
# Copyright (C) 2026 freewayso  https://github.com/freewayso
# Part of a modified Permafrost Engine. GPLv3: LICENSE.txt, NOTICE.md
#
# Room-server directory. Runs outside the game client.
#   GET  /           HTML room list (browser)
#   GET  /rooms      JSON
#   GET  /rooms.txt  TSV for the C client
#   POST /rooms      register: name=&host=&port=&max=&seed=
#   POST /hb         heartbeat: id=&players=
#   POST /unreg      id=

from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import parse_qs
import threading
import time
import json
import sys

PORT = 27000
TIMEOUT_SEC = 12

_lock = threading.Lock()
_next_id = 1
_rooms = {}  # id -> dict


def _now():
    return time.time()


def _purge():
    dead = [rid for rid, r in _rooms.items() if _now() - r["hb"] > TIMEOUT_SEC]
    for rid in dead:
        del _rooms[rid]


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        sys.stderr.write("[lobby] " + (fmt % args) + "\n")

    def _send(self, code, body, ctype="text/plain; charset=utf-8"):
        data = body.encode("utf-8") if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Connection", "close")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(data)

    def _form(self):
        n = int(self.headers.get("Content-Length", "0") or 0)
        raw = self.rfile.read(n).decode("utf-8", "replace")
        q = parse_qs(raw, keep_blank_values=True)
        return {k: (v[0] if v else "") for k, v in q.items()}

    def do_GET(self):
        with _lock:
            _purge()
            rooms = list(_rooms.values())
        path = self.path.split("?", 1)[0]
        if path in ("/", "/index.html"):
            rows = []
            for r in rooms:
                rows.append(
                    "<tr><td>{id}</td><td>{name}</td><td>{host}:{port}</td>"
                    "<td>{players}/{max}</td><td>{seed}</td></tr>".format(**r)
                )
            html = (
                "<!doctype html><meta charset=utf-8>"
                "<title>Permafrost rooms</title>"
                "<meta http-equiv=refresh content=3>"
                "<h1>房间服列表</h1>"
                "<p>大厅端口 {port} · 每 3 秒刷新 · 房间服心跳超时 {timeout}s</p>"
                "<table border=1 cellpadding=6>"
                "<tr><th>id</th><th>name</th><th>addr</th><th>players</th><th>seed</th></tr>"
                "{rows}"
                "</table>"
                "<p>JSON: <a href=/rooms>/rooms</a> · TSV: <a href=/rooms.txt>/rooms.txt</a></p>"
            ).format(port=PORT, timeout=TIMEOUT_SEC, rows="".join(rows) or "<tr><td colspan=5>暂无房间</td></tr>")
            self._send(200, html, "text/html; charset=utf-8")
            return
        if path == "/rooms.txt":
            lines = ["# id name host port players max seed"]
            for r in rooms:
                lines.append("{id} {name} {host} {port} {players} {max} {seed}".format(**r))
            self._send(200, "\n".join(lines) + "\n")
            return
        if path == "/rooms":
            self._send(200, json.dumps(rooms), "application/json; charset=utf-8")
            return
        self._send(404, "not found\n")

    def do_POST(self):
        global _next_id
        form = self._form()
        path = self.path.split("?", 1)[0]
        with _lock:
            _purge()
            if path == "/rooms":
                rid = _next_id
                _next_id += 1
                name = (form.get("name") or "room").replace(" ", "_")[:63]
                host = (form.get("host") or "127.0.0.1")[:63]
                port = int(form.get("port") or 40000)
                mx = int(form.get("max") or 2)
                seed = int(form.get("seed") or 1)
                _rooms[rid] = {
                    "id": rid,
                    "name": name,
                    "host": host,
                    "port": port,
                    "players": 0,
                    "max": mx,
                    "seed": seed,
                    "hb": _now(),
                }
                self._send(200, "OK id=%d\n" % rid)
                return
            if path == "/hb":
                rid = int(form.get("id") or 0)
                r = _rooms.get(rid)
                if not r:
                    self._send(404, "missing\n")
                    return
                r["players"] = int(form.get("players") or 0)
                r["hb"] = _now()
                self._send(200, "OK\n")
                return
            if path == "/unreg":
                rid = int(form.get("id") or 0)
                _rooms.pop(rid, None)
                self._send(200, "OK\n")
                return
        self._send(404, "not found\n")


def main():
    port = PORT
    if len(sys.argv) > 1:
        port = int(sys.argv[1])
    httpd = HTTPServer(("0.0.0.0", port), Handler)
    print("lobby: http://127.0.0.1:%d  (room list)" % port)
    httpd.serve_forever()


if __name__ == "__main__":
    main()
