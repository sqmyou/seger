#!/usr/bin/env python3
"""Match Seger against a strength-limited Stockfish and estimate its Elo.

Stockfish at full strength is far too strong (roughly 3600 Elo), so a straight
match just returns 100% losses. Instead we ask Stockfish to play at a target
level via `UCI_LimitStrength` + `UCI_Elo` and find the level where Seger scores
about 50%; that level is Seger's estimated Elo.

    python3 tools/vs_stockfish.py --elo 1500 --games 40 --movetime 0.1
    python3 tools/vs_stockfish.py --elo 1500 --elo 1800 --games 40   # compare

Both engines get the same per-move time (or the same depth), colours alternate
over a small opening set, and unfinished games are adjudicated as draws at the
ply cap. Stockfish is pinned to one thread and its own hash so it is not rated
against a multi-core search.

`UCI_Elo` is only calibrated down to about 1320; below that Stockfish will not
play any weaker through this option, so treat any result at the floor as an
upper bound rather than a measurement.
"""

import argparse
import itertools
import math
import sys

import chess
import chess.engine

OPENINGS = [
    [],
    ["e2e4", "e7e5"],
    ["e2e4", "c7c5"],
    ["e2e4", "e7e6"],
    ["d2d4", "d7d5"],
    ["d2d4", "g8f6"],
    ["g1f3", "d7d5"],
    ["c2c4", "e7e5"],
    ["e2e4", "c7c6"],
    ["d2d4", "e7e6"],
]

# Stockfish's calibrated UCI_Elo floor and a practical ceiling.
SF_ELO_MIN = 1320


def elo_diff(score: float, games: int) -> float:
    if games == 0:
        return 0.0
    p = min(max(score, 0.5 / games), 1.0 - 0.5 / games)
    return -400.0 * math.log10(1.0 / p - 1.0)


def error_margin(score: float, games: int) -> float:
    if games == 0:
        return 0.0
    se = math.sqrt(score * (1.0 - score) / games)
    return 1.96 * se


def ci_elo(frac: float, games: int) -> float:
    if not (0.0 < frac < 1.0):
        return float("nan")
    margin = error_margin(frac, games)
    return 400.0 * margin / (math.log(10.0) * frac * (1.0 - frac))


def play_game(seger, sf, opening, limit, seger_is_white, max_plies):
    board = chess.Board()
    for uci in opening:
        board.push_uci(uci)
    while not board.is_game_over(claim_draw=True) and board.ply() < max_plies:
        engine = seger if (board.turn == chess.WHITE) == seger_is_white else sf
        result = engine.play(board, limit)
        if result.move is None:
            break
        board.push(result.move)
    if board.is_checkmate():
        seger_won = (not board.turn) == seger_is_white
        return 1.0 if seger_won else 0.0
    return 0.5  # draw, stalemate, repetition, or the ply-cap adjudication


def match_at_elo(seger_path, sf_path, sf_elo, games, movetime, depth, hash_mb,
                 opening_offset, max_plies):
    seger = chess.engine.SimpleEngine.popen_uci([seger_path])
    sf = chess.engine.SimpleEngine.popen_uci([sf_path])
    for e in (seger, sf):
        try:
            e.configure({"Hash": hash_mb})
        except chess.engine.EngineError:
            pass
    try:
        sf.configure({"UCI_LimitStrength": True, "UCI_Elo": sf_elo, "Threads": 1})
    except chess.engine.EngineError as exc:
        print(f"warning: could not limit Stockfish strength: {exc}", file=sys.stderr)

    limit = (chess.engine.Limit(time=movetime) if movetime > 0
             else chess.engine.Limit(depth=depth))
    score = 0.0
    wins = losses = draws = 0
    try:
        openings = itertools.islice(itertools.cycle(OPENINGS),
                                    opening_offset, opening_offset + games)
        for i, opening in enumerate(openings):
            seger_is_white = (i % 2 == 0)
            r = play_game(seger, sf, opening, limit, seger_is_white, max_plies)
            score += r
            if r == 1.0:
                wins += 1
            elif r == 0.0:
                losses += 1
            else:
                draws += 1
            print(f"  game {i + 1}/{games}: Seger "
                  f"({'white' if seger_is_white else 'black'}) "
                  f"{'win' if r == 1 else 'loss' if r == 0 else 'draw'} "
                  f"| {score:.1f}", flush=True)
    finally:
        seger.quit()
        sf.quit()

    frac = score / games if games else 0.0
    return dict(elo=sf_elo, games=games, score=score, frac=frac,
                wins=wins, draws=draws, losses=losses)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--seger", default="build/seger", help="Seger binary")
    ap.add_argument("--stockfish", default="stockfish", help="Stockfish binary")
    ap.add_argument("--elo", type=int, action="append", default=None,
                    help="Stockfish target Elo (repeat to test several)")
    ap.add_argument("--games", type=int, default=40, help="games per Elo")
    ap.add_argument("--movetime", type=float, default=0.1, help="seconds per move")
    ap.add_argument("--depth", type=int, default=6, help="fixed depth (if no movetime)")
    ap.add_argument("--hash", type=int, default=32, help="TT MB per engine")
    ap.add_argument("--max-plies", type=int, default=300)
    ap.add_argument("--opening-offset", type=int, default=0)
    args = ap.parse_args()

    elos = args.elo or [1500]
    results = []
    for i, elo in enumerate(elos):
        floor = elo <= SF_ELO_MIN
        print(f"=== Stockfish limited to UCI_Elo {elo}"
              f"{' (floor: upper bound only)' if floor else ''} ===")
        r = match_at_elo(args.seger, args.stockfish, elo, args.games,
                         args.movetime, args.depth, args.hash,
                         args.opening_offset + i * args.games, args.max_plies)
        results.append(r)
        print(f"  => Seger {r['score']:.1f}/{r['games']} = {r['frac']:.3f} "
              f"({r['wins']}W {r['draws']}D {r['losses']}L) "
              f"vs SF-Elo {elo}; Seger - SF ~ {elo_diff(r['frac'], r['games']):+.0f} Elo")
        print()

    print("summary (Seger estimated Elo = SF target + observed edge):")
    for r in results:
        est = r["elo"] + elo_diff(r["frac"], r["games"])
        ci = ci_elo(r["frac"], r["games"])
        print(f"  SF {r['elo']:>4}: Seger scored {r['frac']:.3f}, "
              f"estimated {est:+.0f} Elo (+/- {ci:.0f} Elo at 95%)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
