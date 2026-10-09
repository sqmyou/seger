#!/usr/bin/env python3
"""Play Seger from the terminal.

Requires python-chess (`pip install chess`). Build the engine first with `make`.

    python3 tools/play.py            # you play White, engine moves in 1s
    python3 tools/play.py --black    # you play Black
    python3 tools/play.py --time 0.5 --hash 64

Enter moves in UCI/long algebraic form (e.g. e2e4, e7e8q). Type 'help' for the
legal moves, 'board' to redraw, or 'quit' to leave.
"""

import argparse
import sys

import chess
import chess.engine


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--engine", default="build/seger", help="path to the engine binary")
    ap.add_argument("--black", action="store_true", help="play as Black")
    ap.add_argument("--time", type=float, default=1.0, help="engine seconds per move")
    ap.add_argument("--hash", type=int, default=32, help="transposition table size in MB")
    ap.add_argument("--fen", default=chess.STARTING_FEN, help="starting position")
    args = ap.parse_args()

    human = chess.BLACK if args.black else chess.WHITE
    board = chess.Board(args.fen)
    engine = chess.engine.SimpleEngine.popen_uci([args.engine])
    engine.configure({"Hash": args.hash})

    print("You are", "Black" if args.black else "White", "- enter UCI moves like e2e4.")
    try:
        while not board.is_game_over(claim_draw=True):
            print()
            print(board, "\n")
            if board.turn == human:
                move = None
                while move is None:
                    text = input("your move: ").strip().lower()
                    if text in ("quit", "exit"):
                        print("bye")
                        return 0
                    if text == "help":
                        print("legal:", " ".join(m.uci() for m in board.legal_moves))
                        continue
                    if text == "board":
                        break
                    try:
                        move = board.parse_uci(text)
                        if move not in board.legal_moves:
                            raise ValueError
                    except ValueError:
                        print("illegal move; try again or type 'help'")
                        move = None
                if text == "board":
                    continue
            else:
                print("engine thinking...")
                move = engine.play(board, chess.engine.Limit(time=args.time)).move
                print("engine plays:", move.uci())
            board.push(move)

        print()
        print(board, "\n")
        print("Game over:", board.result(claim_draw=True), board.outcome(claim_draw=True))
    finally:
        engine.quit()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
