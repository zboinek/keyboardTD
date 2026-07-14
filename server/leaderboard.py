#!/usr/bin/env python3
"""keyboardTD hall of fame — a tiny same-origin API next to the static game.

Runs behind the container's nginx (location /api/ -> 127.0.0.1:8081):

    GET  /api/top10   -> JSON list of the 10 best runs
    POST /api/score   -> submit {nick, score, wpm, level, duration}
                         responds {"ok": true, "top10": [...]}

Scores are client-reported, so this can't be tamper-proof — the checks
below only reject the lazy forgeries (see REPLAY_PLAN.md for the real fix):
  - nick must match [a-z0-9]{3,10}
  - duration 60s..2h, wpm 1..200, 0 < score <= duration * KTD_MAX_PPS
    (points per second; the cap is deliberately NOT derived from wpm —
    turrets, nukes and quakes pay points for zero keystrokes, so a slow
    tower build legitimately outscores a fast typist)
  - level must match duration (a level is 20s of play time)
  - per-IP rate limit: 30s between submissions, 30/day (in memory)

Moderation: when KTD_ADMIN_TOKEN is set, requests carrying it in
X-Admin-Token may GET /api/scores (all rows, with ids) and
DELETE /api/score/<id>. Without the env var both answer 404.

Stdlib only; SQLite lives on the /data volume so scores survive redeploys.
"""

import hmac
import json
import os
import re
import sqlite3
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

DB_PATH = os.environ.get("KTD_DB", "/data/scores.db")
NICK_RE = re.compile(r"^[a-z0-9]{3,10}$")

# Best observed human run is ~150 pts/s; 400 leaves room for better players
# and tower-heavy builds while rejecting the absurd (the July forgery ran
# at ~560).
MAX_PPS = int(os.environ.get("KTD_MAX_PPS", "400"))
MAX_DURATION = 7200  # 2h, the SSH gateway's session cap

ADMIN_TOKEN = os.environ.get("KTD_ADMIN_TOKEN", "")

_lock = threading.Lock()
_last_by_ip = {}   # ip -> unix time of last accepted submission
_day_counts = {}   # (ip, day) -> submissions today


def db():
    conn = sqlite3.connect(DB_PATH)
    conn.execute(
        "CREATE TABLE IF NOT EXISTS scores ("
        " id INTEGER PRIMARY KEY,"
        " nick TEXT NOT NULL,"
        " score INTEGER NOT NULL,"
        " wpm INTEGER NOT NULL,"
        " level INTEGER NOT NULL,"
        " duration INTEGER NOT NULL,"
        " at INTEGER NOT NULL)"
    )
    return conn


def top10():
    with db() as conn:
        rows = conn.execute(
            "SELECT nick, score, wpm, level, at FROM scores"
            " ORDER BY score DESC, at ASC LIMIT 10"
        ).fetchall()
    return [
        {"nick": n, "score": s, "wpm": w, "level": l, "at": a}
        for n, s, w, l, a in rows
    ]


def validate(d):
    """Returns (row tuple, None) or (None, reason)."""
    try:
        nick = str(d["nick"])
        score = int(d["score"])
        wpm = int(d["wpm"])
        level = int(d["level"])
        duration = int(d["duration"])
    except (KeyError, TypeError, ValueError):
        return None, "bad fields"
    if not NICK_RE.match(nick):
        return None, "bad nick"
    if not 60 <= duration <= MAX_DURATION:
        return None, "bad duration"
    if not 1 <= wpm <= 200:
        return None, "bad wpm"
    if not 0 < score <= duration * MAX_PPS:
        return None, "implausible score"
    if abs(level - (1 + duration // 20)) > 2:
        return None, "level does not match duration"
    return (nick, score, wpm, level, duration), None


def allowed(ip):
    now = time.time()
    day = int(now // 86400)
    with _lock:
        if now - _last_by_ip.get(ip, 0) < 30:
            return False
        if _day_counts.get((ip, day), 0) >= 30:
            return False
        _last_by_ip[ip] = now
        _day_counts[(ip, day)] = _day_counts.get((ip, day), 0) + 1
        # Don't let the day-count map grow forever.
        for key in [k for k in _day_counts if k[1] != day]:
            del _day_counts[key]
    return True


class Handler(BaseHTTPRequestHandler):
    def client_ip(self):
        # Connections arrive via the container's nginx, so the socket peer is
        # always 127.0.0.1; the real client is in the forwarded headers set
        # by the proxy chain.
        return (
            self.headers.get("X-Real-IP")
            or (self.headers.get("X-Forwarded-For") or "").split(",")[0].strip()
            or self.client_address[0]
        )

    def reply(self, code, payload):
        body = json.dumps(payload).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def is_admin(self):
        token = self.headers.get("X-Admin-Token") or ""
        return bool(ADMIN_TOKEN) and hmac.compare_digest(token, ADMIN_TOKEN)

    def do_GET(self):
        path = self.path.rstrip("/")
        if path == "/api/top10":
            self.reply(200, top10())
        elif path == "/api/scores" and self.is_admin():
            with db() as conn:
                rows = conn.execute(
                    "SELECT id, nick, score, wpm, level, duration, at"
                    " FROM scores ORDER BY score DESC, at ASC"
                ).fetchall()
            self.reply(200, [
                {"id": i, "nick": n, "score": s, "wpm": w, "level": l,
                 "duration": d, "at": a}
                for i, n, s, w, l, d, a in rows
            ])
        else:
            self.reply(404, {"error": "not found"})

    def do_DELETE(self):
        m = re.fullmatch(r"/api/score/(\d+)", self.path.rstrip("/"))
        if not m or not self.is_admin():
            self.reply(404, {"error": "not found"})
            return
        with db() as conn:
            cur = conn.execute(
                "DELETE FROM scores WHERE id = ?", (int(m.group(1)),)
            )
        self.reply(200, {"ok": True, "deleted": cur.rowcount})

    def do_POST(self):
        if self.path.rstrip("/") != "/api/score":
            self.reply(404, {"error": "not found"})
            return
        length = int(self.headers.get("Content-Length") or 0)
        if not 0 < length <= 1024:
            self.reply(400, {"error": "bad request"})
            return
        try:
            data = json.loads(self.rfile.read(length))
        except (json.JSONDecodeError, UnicodeDecodeError):
            self.reply(400, {"error": "bad json"})
            return
        if not allowed(self.client_ip()):
            self.reply(429, {"error": "slow down"})
            return
        row, reason = validate(data)
        if row is None:
            self.reply(400, {"error": reason})
            return
        with db() as conn:
            conn.execute(
                "INSERT INTO scores (nick, score, wpm, level, duration, at)"
                " VALUES (?, ?, ?, ?, ?, ?)",
                row + (int(time.time()),),
            )
        self.reply(200, {"ok": True, "top10": top10()})

    def log_message(self, fmt, *args):  # keep container logs quiet
        pass


if __name__ == "__main__":
    os.makedirs(os.path.dirname(DB_PATH), exist_ok=True)
    db().close()
    ThreadingHTTPServer(("127.0.0.1", 8081), Handler).serve_forever()
