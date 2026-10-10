#!/usr/bin/env python3
"""Run Seger as a Lichess bot.

The bot connects to Lichess with a personal API token, listens for incoming
challenges on the global event stream, accepts (or declines) them, and plays
every game by driving the UCI engine binary.

    LICHESS_TOKEN=xxxxxxxx python3 tools/lichess_bot.py
    python3 tools/lichess_bot.py --token xxxxxxxx --engine build/seger
    python3 tools/lichess_bot.py --accept-rated --max-rating 2600
    python3 tools/lichess_bot.py --variants standard,chess960 --min-time 3

Setup (one time):

  1. Create a Lichess account for the bot (or reuse an empty one; an account
     that has already played a game cannot be upgraded).
  2. Create a personal API token with the `bot:play` scope.
  3. Upgrade the account to a bot account, which is irreversible:
         curl -X POST https://lichess.org/api/bot/account/upgrade \\
           -H "Authorization: Bearer $LICHESS_TOKEN"
     After this the account can only be used through the Bot API, never the
     web UI.
  4. Run this script on an always-on machine (see README). It needs outbound
     HTTPS to lichess.org and nothing else.

The engine is driven over UCI, so it may be any UCI binary, not just Seger.
"""

import argparse
import json
import os
import signal
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request

LICHESS = os.environ.get("LICHESS_HOST", "https://lichess.org")

# Lichess clock accounting, in milliseconds.
MOVE_OVERHEAD_MS = 150
MIN_MOVETIME_MS = 20
MAX_MOVETIME_MS = 5000
FIRST_MOVE_MOVETIME_MS = 8000
FLOOR_MOVETIME_MS = 30


def _request(method, path, token, body=None, timeout=60):
    data = None
    headers = {"Authorization": f"Bearer {token}"}
    if body is not None:
        data = body.encode("utf-8")
        headers["Content-Type"] = "text/plain"
    req = urllib.request.Request(
        LICHESS + path, data=data, headers=headers, method=method)
    return urllib.request.urlopen(req, timeout=timeout)


def post(path, token, body=None):
    try:
        with _request("POST", path, token, body) as r:
            return r.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", "replace")
        print(f"  ! POST {path} -> {exc.code} {detail[:200]}", file=sys.stderr)
        return None


def stream_ndjson(path, token, stop_event):
    """Yield decoded JSON objects from an ndjson stream until it closes."""
    try:
        with _request("GET", path, token, timeout=120) as r:
            for raw in r:
                if stop_event.is_set():
                    break
                line = raw.decode("utf-8", "replace").strip()
                if not line:
                    continue
                try:
                    yield json.loads(line)
                except json.JSONDecodeError:
                    continue
    except (urllib.error.HTTPError, urllib.error.URLError, OSError) as exc:
        print(f"  ! GET {path} closed: {exc}", file=sys.stderr)


class Engine:
    """A UCI engine in a child process, with a line-reading thread."""

    def __init__(self, path, hash_mb=64, threads=1):
        self.proc = subprocess.Popen(
            [path], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL, text=True, bufsize=1)
        self._lines = []
        self._lock = threading.Lock()
        self._new = threading.Event()
        threading.Thread(target=self._reader, daemon=True).start()
        self._send("uci")
        self._wait(lambda l: l == "uciok", 10)
        self.setoption("Hash", hash_mb)
        self.setoption("Threads", threads)
        self._send("isready")
        self._wait(lambda l: l == "readyok", 10)

    def _reader(self):
        for line in self.proc.stdout:
            with self._lock:
                self._lines.append(line.strip())
            self._new.set()

    def _send(self, cmd):
        self.proc.stdin.write(cmd + "\n")
        self.proc.stdin.flush()

    def setoption(self, name, value):
        self._send(f"setoption name {name} value {value}")

    def _wait(self, predicate, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            with self._lock:
                for i, line in enumerate(self._lines):
                    if predicate(line):
                        del self._lines[:i + 1]
                        return line
            self._new.wait(0.05)
            self._new.clear()
        return None

    def bestmove(self, moves, movetime_ms, timeout):
        """Return the engine's best UCI move for the position after `moves`."""
        pos = "position startpos" + (f" moves {' '.join(moves)}" if moves else "")
        self._send(pos)
        self._send(f"go movetime {int(movetime_ms)}")
        line = self._wait(lambda l: l.startswith("bestmove"), timeout)
        if not line:
            return None
        parts = line.split()
        return parts[1] if len(parts) > 1 else None

    def quit(self):
        try:
            self._send("quit")
        except (BrokenPipeError, OSError):
            pass
        try:
            self.proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.proc.kill()


def decide_movetime(clock_initial, clock_increment, moves_played):
    """Milliseconds to spend on the move, from the Lichess clock in ms."""
    initial = clock_initial or 0
    inc = clock_increment or 0
    if moves_played == 0:
        budget = min(FIRST_MOVE_MOVETIME_MS, initial // 4)
    elif inc:
        budget = (inc - MOVE_OVERHEAD_MS) + initial // 40
    else:
        budget = initial // 40
    budget -= MOVE_OVERHEAD_MS
    budget = max(FLOOR_MOVETIME_MS, min(budget, MAX_MOVETIME_MS))
    return max(MIN_MOVETIME_MS, budget)


def game_is_finished(status):
    return status not in ("created", "started")


def should_accept(challenge, accept_rated, max_rating, variants, min_time):
    """Return (True, '') to accept a challenge, or (False, reason)."""
    key = (challenge.get("variant") or {}).get("key", "standard")
    if key not in variants:
        return False, "variant"
    if challenge.get("speed") == "ultrabullet":
        return False, "tooFast"
    tc = challenge.get("timeControl") or {}
    if tc.get("type") == "unlimited":
        return False, "timeControl"
    if tc.get("limit") is not None and tc["limit"] < min_time:
        return False, "tooFast"
    if challenge.get("rated") and not accept_rated:
        return False, "rated"
    rating = (challenge.get("challenger") or {}).get("rating")
    if max_rating and rating and rating > max_rating:
        return False, "tooStrong"
    return True, ""


class Bot:
    def __init__(self, args):
        self.args = args
        self.token = args.token
        self.bot_id = None
        self.stop_event = threading.Event()
        self.engine = Engine(args.engine, hash_mb=args.hash, threads=args.threads)
        self.games = set()

    def log(self, msg):
        print(f"{time.strftime('%H:%M:%S')} {msg}", flush=True)

    def upgrade_if_needed(self):
        try:
            with _request("GET", "/api/account", self.token) as r:
                me = json.load(r)
        except (urllib.error.HTTPError, urllib.error.URLError) as exc:
            print(f"cannot read /api/account: {exc}", file=sys.stderr)
            return False
        self.bot_id = me.get("id")
        self.log(f"logged in as {me.get('username')} ({self.bot_id})")
        if me.get("title") == "BOT":
            return True
        if not self.args.upgrade:
            self.log("account is not a bot; rerun with --upgrade to convert it "
                     "(irreversible)")
            return False
        self.log("upgrading account to a bot account (irreversible)")
        post("/api/bot/account/upgrade", self.token)
        return True

    def handle_challenge(self, challenge):
        cid = challenge.get("id")
        if not cid:
            return
        accept, reason = should_accept(
            challenge, self.args.accept_rated, self.args.max_rating,
            set(self.args.variants.split(",")), self.args.min_time)
        who = (challenge.get("challenger") or {}).get("name", "?")
        if accept:
            self.log(f"accepting challenge {cid} from {who}")
            post(f"/api/challenge/{cid}/accept", self.token)
        else:
            self.log(f"declining challenge {cid} from {who} ({reason})")
            post(f"/api/challenge/{cid}/decline", self.token, reason)

    def play_game(self, game_id):
        if not game_id or game_id in self.games:
            return
        self.games.add(game_id)
        self.log(f"game {game_id} started")
        threading.Thread(target=self._play_game, args=(game_id,),
                         daemon=True).start()

    def _play_game(self, game_id):
        """Play one game to completion; exits when the stream reports a result."""
        try:
            self._play_game_inner(game_id)
        finally:
            self.games.discard(game_id)

    def _play_game_inner(self, game_id):
        my_color = None
        for event in stream_ndjson(f"/api/bot/game/stream/{game_id}",
                                   self.token, self.stop_event):
            etype = event.get("type")
            if etype == "gameFull":
                my_color = self._color_in(event)
            state = event.get("state") if etype == "gameFull" else event
            if not state:
                continue
            moves = (state.get("moves") or "").split()
            status = state.get("status", "started")
            if game_is_finished(status):
                self.log(f"game {game_id} finished: {status}")
                return
            if my_color is None:
                continue
            if ("white" if len(moves) % 2 == 0 else "black") != my_color:
                continue
            if my_color == "white":
                clock, inc = state.get("wtime"), state.get("winc")
            else:
                clock, inc = state.get("btime"), state.get("binc")
            movetime = decide_movetime(clock, inc, len(moves))
            best = self.engine.bestmove(moves, movetime, 30)
            if not best or best == "0000":
                continue
            self.log(f"game {game_id}: {best} ({movetime}ms, {len(moves)} plies)")
            post(f"/api/bot/game/{game_id}/move/{best}", self.token)

    def _color_in(self, game_full):
        for color in ("white", "black"):
            if self.bot_id and (game_full.get(color) or {}).get("id") == self.bot_id:
                return color
        return None

    def run(self):
        if not self.upgrade_if_needed():
            return 1
        while not self.stop_event.is_set():
            self.log("listening on /api/stream/event")
            try:
                for event in stream_ndjson("/api/stream/event", self.token,
                                           self.stop_event):
                    etype = event.get("type")
                    if etype == "challenge":
                        self.handle_challenge(event.get("challenge") or {})
                    elif etype == "gameStart":
                        self.play_game((event.get("game") or {}).get("id"))
                    elif etype in ("challengeCanceled", "challengeDeclined"):
                        self.log(f"{etype}: "
                                 f"{(event.get('challenge') or {}).get('id')}")
                    elif etype == "gameFinish":
                        self.log(f"gameFinish: "
                                 f"{(event.get('game') or {}).get('id')}")
            except KeyboardInterrupt:
                break
            # The stream closed (server restart, network blip): back off briefly
            # and reconnect. Games already running have their own streams.
            if not self.stop_event.is_set():
                self.log("event stream closed; reconnecting in 5s")
                self.stop_event.wait(5)
        return 0

    def close(self):
        self.stop_event.set()
        self.engine.quit()


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--token", default=os.environ.get("LICHESS_TOKEN", ""),
                    help="Lichess bot API token (or set LICHESS_TOKEN)")
    ap.add_argument("--engine", default="build/seger",
                    help="UCI engine binary to play with")
    ap.add_argument("--hash", type=int, default=64, help="engine hash in MB")
    ap.add_argument("--threads", type=int, default=1, help="engine threads")
    ap.add_argument("--variants", default="standard",
                    help="accepted variant keys, comma separated")
    ap.add_argument("--min-time", type=int, default=180,
                    help="decline games with an initial clock below this (s)")
    ap.add_argument("--accept-rated", action="store_true",
                    help="also accept rated challenges (default: casual only)")
    ap.add_argument("--max-rating", type=int, default=0,
                    help="decline challengers rated above this (0 = any)")
    ap.add_argument("--upgrade", action="store_true",
                    help="upgrade the account to a bot account (irreversible)")
    args = ap.parse_args()

    if not args.token:
        print("no token: pass --token or set LICHESS_TOKEN", file=sys.stderr)
        return 2
    if not os.path.exists(args.engine):
        print(f"engine not built: {args.engine} (run `make`)", file=sys.stderr)
        return 2

    bot = Bot(args)
    signal.signal(signal.SIGTERM,
                  lambda *_: (bot.stop_event.set(), sys.exit(0)))
    try:
        return bot.run()
    finally:
        bot.close()


if __name__ == "__main__":
    raise SystemExit(main())
