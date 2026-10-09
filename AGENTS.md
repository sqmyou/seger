# AGENTS.md

Repository-specific guidance for agents working on Seger.

## What this is

Seger is a UCI chess engine in C++17. It uses a 0x88 mailbox board, incremental
Zobrist hashing, a transposition table, and an alpha-beta / principal-variation
search with null-move pruning and late-move reductions.

## Build and test

```sh
make          # builds build/seger
make test     # builds and runs position_test, perft_test, and search_test
make divide   # builds build/perft_divide for move-generation debugging
make bench    # quick fixed-depth benchmark (startpos, depth 9)
```

There is no cmake; the project builds with plain `make` and `g++`.

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
