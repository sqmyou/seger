#!/usr/bin/env python3
"""Serve a small web UI for playing Seger in a browser.

The engine runs as a child process; this server exposes a tiny JSON API and
serves a self-contained board UI (no CDN, no external assets).

    make
    python3 tools/server.py            # listens on 0.0.0.0:12000
    python3 tools/server.py --port 12001

Then open the workspace's work URL for that port.
"""

import argparse
import json
import os
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import chess
import chess.engine

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
INDEX = os.path.join(HERE, "web", "index.html")

ENGINE_PATH = os.path.join(ROOT, "build", "seger")

# The engine and its position are touched from request threads, so serialise.
_lock = threading.Lock()
_engine = None


def engine():
    global _engine
    if _engine is None:
        _engine = chess.engine.SimpleEngine.popen_uci([ENGINE_PATH])
        try:
            _engine.configure({"Hash": 64})
        except chess.engine.EngineError:
            pass
    return _engine


def board_from(fen, moves):
    board = chess.Board(fen or chess.STARTING_FEN)
    for uci in moves or []:
        board.push(chess.Move.from_uci(uci))
    return board


def status_of(board):
    if board.is_checkmate():
        return {"over": True, "result": board.result(), "reason": "checkmate"}
    if board.is_stalemate():
        return {"over": True, "result": "1/2-1/2", "reason": "stalemate"}
    if board.is_insufficient_material():
        return {"over": True, "result": "1/2-1/2", "reason": "insufficient material"}
    if board.is_seventyfive_moves():
        return {"over": True, "result": "1/2-1/2", "reason": "75-move rule"}
    if board.is_fivefold_repetition():
        return {"over": True, "result": "1/2-1/2", "reason": "fivefold repetition"}
    return {"over": False, "result": None,
            "reason": "check" if board.is_check() else None}


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass  # keep the console quiet

    def _send(self, code, body, content_type):
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _json(self, obj, code=200):
        self._send(code, json.dumps(obj), "application/json")

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path in ("/", "/index.html"):
            try:
                with open(INDEX, "rb") as f:
                    self._send(200, f.read(), "text/html; charset=utf-8")
            except OSError:
                self._send(500, "index.html missing", "text/plain")
        else:
            self._send(404, "not found", "text/plain")

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        try:
            payload = json.loads(self.rfile.read(length) or b"{}")
        except json.JSONDecodeError:
            self._json({"error": "bad json"}, 400)
            return

        path = self.path.split("?", 1)[0]
        if path == "/api/state":
            self._handle_state(payload)
        elif path == "/api/engine":
            self._handle_engine(payload)
        else:
            self._send(404, "not found", "text/plain")

    def _handle_state(self, payload):
        try:
            board = board_from(payload.get("fen"), payload.get("moves"))
        except ValueError as exc:
            self._json({"error": str(exc)}, 400)
            return
        self._json({
            "fen": board.fen(),
            "turn": "white" if board.turn else "black",
            "legal": sorted(m.uci() for m in board.legal_moves),
            "status": status_of(board),
        })

    def _handle_engine(self, payload):
        try:
            board = board_from(payload.get("fen"), payload.get("moves"))
        except ValueError as exc:
            self._json({"error": str(exc)}, 400)
            return

        st = status_of(board)
        if st["over"]:
            self._json({"move": None, "fen": board.fen(), "status": st})
            return

        seconds = float(payload.get("time", 1.0))
        seconds = max(0.05, min(seconds, 30.0))
        limit = chess.engine.Limit(time=seconds)

        with _lock:
            try:
                result = engine().play(board, limit)
            except chess.engine.EngineError as exc:
                self._json({"error": str(exc)}, 500)
                return

        move = result.move
        if move is None:
            self._json({"move": None, "fen": board.fen(), "status": status_of(board)})
            return
        board.push(move)
        self._json({
            "move": move.uci(),
            "fen": board.fen(),
            "status": status_of(board),
        })


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", type=int, default=int(os.environ.get("PORT", 12000)))
    ap.add_argument("--host", default="0.0.0.0")
    args = ap.parse_args()

    if not os.path.exists(ENGINE_PATH):
        raise SystemExit(f"engine not built: {ENGINE_PATH} (run `make`)")

    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"Seger play server on http://{args.host}:{args.port}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        with _lock:
            if _engine is not None:
                _engine.quit()


if __name__ == "__main__":
    main()
