#!/usr/bin/env python3
"""Play two Seger builds against each other and report the result.

Intended for measuring a change: point --a at the old binary and --b at the new
one (or the same binary twice), and read off the score and a rough Elo delta.

    make
    cp build/seger build/seger_old          # keep a reference first
    # ... change the engine, make ...
    python3 tools/selfplay.py --games 20 --depth 6

Requires python-chess. Use --depth for reproducible, hardware-independent play,
or --movetime for a time control. Use --parallel N to run N games at once across
cores, which roughly divides wall-clock time by N.

This is a small harness, not an SPRT replacement: a dozen games only resolves
large differences. Treat a +20 game scoreline as noise and run more games.
"""

import argparse
import itertools
import math
import multiprocessing
import sys
import time

import chess
import chess.engine

# A handful of short openings so the two builds do not replay one game forever.
OPENINGS = [
    [],
    ["e2e4", "e7e5"],
    ["e2e4", "c7c5"],
    ["e2e4", "e7e6"],
    ["d2d4", "d7d5"],
    ["d2d4", "g8f6"],
    ["g1f3", "d7d5"],
    ["c2c4", "e7e5"],
]


def elo_diff(score: float, games: int) -> float:
    """Rough Elo difference from a score fraction, clipped away from 0/1."""
    if games == 0:
        return 0.0
    p = min(max(score, 0.5 / games), 1.0 - 0.5 / games)
    return -400.0 * math.log10(1.0 / p - 1.0)


def error_margin(score: float, games: int) -> float:
    """95% confidence half-width on the score fraction."""
    if games == 0:
        return 0.0
    se = math.sqrt(score * (1.0 - score) / games)
    return 1.96 * se


def play_game(engine_a, engine_b, opening, limit, a_is_white, max_plies):
    board = chess.Board()
    for uci in opening:
        board.push_uci(uci)

    while not board.is_game_over(claim_draw=True) and board.ply() < max_plies:
        engine = engine_a if (board.turn == chess.WHITE) == a_is_white else engine_b
        result = engine.play(board, limit)
        if result.move is None:
            break
        board.push(result.move)

    if board.is_checkmate():
        winner_white = not board.turn  # side to move is mated
        a_won = winner_white == a_is_white
        return 1.0 if a_won else 0.0
    return 0.5  # draw, stalemate, or adjudicated by the ply cap


def _play_one(job):
    """Worker for a parallel match: play a single game in its own process.

    Each worker starts its own pair of engine processes, so the games are fully
    independent and can run across cores.
    """
    index, a_path, b_path, opening, depth, movetime, hash_mb, a_is_white, max_plies = job
    limit = (chess.engine.Limit(time=movetime) if movetime > 0
             else chess.engine.Limit(depth=depth))
    engine_a = chess.engine.SimpleEngine.popen_uci([a_path])
    engine_b = chess.engine.SimpleEngine.popen_uci([b_path])
    try:
        for e in (engine_a, engine_b):
            try:
                e.configure({"Hash": hash_mb})
            except chess.engine.EngineError:
                pass
        result = play_game(engine_a, engine_b, opening, limit, a_is_white, max_plies)
    finally:
        engine_a.quit()
        engine_b.quit()
    return index, result


def _ordered_jobs(args, openings):
    """Yield (index, job) pairs, skipping games once the global budget is spent.

    Iterating lazily means a stopped match still produces a valid partial score
    over the games that did finish, instead of being killed mid-run.
    """
    for i in range(args.games):
        if args.max_seconds > 0 and time.monotonic() - args._start >= args.max_seconds:
            print(f"stopping after {i} games: --max-seconds "
                  f"{args.max_seconds:.0f} reached", flush=True)
            break
        yield (i, (i, args.a, args.b, openings[i // 2], args.depth, args.movetime,
                   args.hash, i % 2 == 0, args.max_plies))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--a", default="build/seger", help="engine A path")
    ap.add_argument("--b", default="build/seger", help="engine B path")
    ap.add_argument("--games", type=int, default=20, help="number of games")
    ap.add_argument("--depth", type=int, default=6, help="fixed search depth")
    ap.add_argument("--movetime", type=float, default=0.0,
                    help="seconds per move (overrides --depth)")
    ap.add_argument("--hash", type=int, default=32, help="TT size in MB per engine")
    ap.add_argument("--max-plies", type=int, default=400)
    ap.add_argument("--opening-offset", type=int, default=0,
                    help="rotate the opening list by this many entries")
    ap.add_argument("--parallel", type=int, default=1,
                    help="games to run at once (default 1; use your core count)")
    ap.add_argument("--max-seconds", type=float, default=0.0,
                    help="stop after this many wall-clock seconds (0 = no limit). "
                         "The partial score over finished games is still reported, "
                         "so an automation never hangs on a long match.")
    args = ap.parse_args()
    args._start = time.monotonic()

    if args.movetime > 0:
        limit = chess.engine.Limit(time=args.movetime)
    else:
        limit = chess.engine.Limit(depth=args.depth)

    openings = list(itertools.islice(
        itertools.cycle(OPENINGS), args.opening_offset, args.opening_offset + args.games))

    score = 0.0
    wins = losses = draws = 0

    if args.parallel > 1:
        # Pair each opening with both colours: game 2k has A on white, game 2k+1
        # has A on black for the same opening. Decoupling colour from the opening
        # removes the side-A bias a plain i%2 / i%8 scheme produces.
        jobs = [job for _, job in _ordered_jobs(args, openings)]
        results = {}
        with multiprocessing.Pool(processes=args.parallel) as pool:
            for index, result in pool.imap_unordered(_play_one, jobs):
                results[index] = result
                score += result
                if result == 1.0:
                    wins += 1
                elif result == 0.0:
                    losses += 1
                else:
                    draws += 1
                print(f"game {len(results)}/{args.games} done "
                      f"| score {score:.1f}", flush=True)
                # Leave the pool (terminating in-flight workers) once the budget
                # is spent, so a scheduled match can never overrun its slot.
                if args.max_seconds > 0 and \
                        time.monotonic() - args._start >= args.max_seconds:
                    print(f"stopping after {len(results)} games: --max-seconds "
                          f"{args.max_seconds:.0f} reached", flush=True)
                    break
        games_played = len(results)
    else:
        engine_a = chess.engine.SimpleEngine.popen_uci([args.a])
        engine_b = chess.engine.SimpleEngine.popen_uci([args.b])
        for e in (engine_a, engine_b):
            try:
                e.configure({"Hash": args.hash})
            except chess.engine.EngineError:
                pass
        games_played = 0
        try:
            for i, _job in _ordered_jobs(args, openings):
                opening = openings[i // 2]
                a_is_white = (i % 2 == 0)
                result = play_game(engine_a, engine_b, opening, limit, a_is_white,
                                   args.max_plies)
                score += result
                games_played += 1
                if result == 1.0:
                    wins += 1
                elif result == 0.0:
                    losses += 1
                else:
                    draws += 1
                color = "white" if a_is_white else "black"
                print(f"game {i + 1}/{args.games}: A ({color}) "
                      f"{'win' if result == 1 else 'loss' if result == 0 else 'draw'} "
                      f"| score {score:.1f}", flush=True)
        finally:
            engine_a.quit()
            engine_b.quit()

    frac = score / games_played if games_played else 0.0
    print()
    print(f"games {games_played}  A wins {wins}  draws {draws}  B wins {losses}")
    print(f"A score {score:.1f}/{games_played} = {frac:.3f}")
    margin = error_margin(frac, games_played)
    ci_elo = 0.0
    if 0.0 < frac < 1.0:
        # d(Elo)/d(score) = 400 / (ln10 * score * (1 - score)).
        ci_elo = 400.0 * margin / (math.log(10.0) * frac * (1.0 - frac))
    print(f"A - B Elo ~ {elo_diff(frac, games_played):+.1f} "
          f"(+/- {ci_elo:.0f} at 95%, rough)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
