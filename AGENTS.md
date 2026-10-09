# AGENTS.md

Repository-specific guidance for agents working on Seger.

## What this is

Seger is a UCI chess engine in C++17. It uses a 0x88 mailbox board, incremental
Zobrist hashing, a transposition table, and an alpha-beta / principal-variation
search with null-move pruning and late-move reductions.

## Build and test

```sh
make          # builds build/seger
make test     # builds the engine + runs position, perft, search, uci and eval tests
make divide   # builds build/perft_divide for move-generation debugging
make bench    # quick fixed-depth benchmark (startpos, depth 9)
```

There is no cmake; the project builds with plain `make` and `g++`.

`tools/selfplay.py` plays two engine binaries against each other
(`--a build/seger --b /path/to/other --games 8 --depth 5`) and reports the
score and a rough Elo delta. Use it to check whether an evaluation or search
change is actually an improvement before keeping it.

Bench/signature baseline: depth 9 from startpos settles at **140,502,299
nodes** (~1.31 Mnps) with the tapered evaluation. The node count moves whenever
the evaluation changes, so treat it as a regression tripwire rather than a
constant to preserve.

## Verifying move generation

`make test` must pass in full. `perft_test` checks six standard positions
against published node counts; any change to `position.cpp` move generation or
make/unmake must keep all of them passing. When a perft count is wrong, use
`./build/perft_divide "<fen>" <depth>` to find the offending root move, and
cross-check with python-chess (`chess.Board`), which is installed.

## Invariants and gotchas

- **0x88 vs bitboards.** Squares are 0x88 indices (rank*16+file). Bitboards use
  a dense 0..63 index. Never shift `1ULL` by a raw 0x88 square index: the shift
  exceeds 63 and is undefined behaviour on x86. Use `bitOf`, `bitIndexOf`,
  `testBit`, and `squareOfBitIndex` from `types.h`.
- **Zobrist keys are incremental.** `putPiece`/`removePiece` XOR the piece key,
  and `doMove`/`undoMove`/null move swap the side/castling/en-passant terms.
  Never rebuild the key by rescanning the board on every move. `undoMove`
  restores the key from `StateInfo::key`, so anything that leaves the board in a
  changed state without pushing a `StateInfo` will corrupt the key.
- **`movePiece` clears the destination.** It removes any captured piece (and its
  bitboard bits) before moving. Keep it that way; skipping the destination
  removal leaves phantom enemy pieces in the occupancy bitboards.
- **En passant squares are only set when a capture is available.** The standard
  perft references assume this; always setting the ep square breaks perft.
- **Piece-square tables mirror by rank for Black**, via the table index helpers.
  A symmetric position (startpos) must evaluate to 0 for the side to move.
- **`evaluate()` returns from the side to move's point of view already.** Every
  term is signed by `(colour == sideToMove)`, so an extra "flip if Black" at the
  end negates the score for the wrong side and makes the engine play garbage.
  `eval_test` mirrors positions vertically to catch exactly this; keep that test
  passing when adding terms.
- **Transposition-table mate scores are ply-relative.** Store/search through
  `scoreToTT`/`scoreFromTT` so a mate found on one path is not reported at the
  wrong distance on another. `search_test` guards this.
- **Null-move pruning needs non-pawn material.** Do not null-move the side to
  move when it only has a king and pawns: those positions are full of zugzwang.
- Move generation is pseudo-legal plus a make/undo legality filter
  (`isLegalMove`). Do not "optimise" the filter away without perft
  revalidation.

## Conventions

- C++17, no external dependencies, no cmake.
- Keep `-Wall -Wextra -Wpedantic` clean.
- Identifiers use `camelCase` (see `types.h`); search `namespace seger`.

## Playing / local UI

`tools/play.py` plays in a terminal and `tools/server.py` serves a
self-contained browser board (`tools/web/index.html`). Both drive the engine
through `python-chess`'s UCI bridge, so they are the quickest way to exercise a
change end to end. The web UI takes no external assets and needs no network.

Both HTTP endpoints return the same shape (`fen`, `turn`, `legal[]`, `status`),
including `legal` in the `/api/engine` reply. Keep it that way: the browser
client assumes it can move again immediately after the engine replies, and a
response that drops `legal` strands the player after their first move.

The board pieces are the Cburnett set vendored in `tools/web/pieces/`
(CC BY-SA 3.0; attribution in `PIECES-LICENSE`). Do not re-download them at
runtime.
