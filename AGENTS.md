# AGENTS.md

Repository-specific guidance for agents working on Seger.

## What this is

Seger is a UCI chess engine in C++17. First version: 0x88 mailbox board,
alpha-beta search, piece-square evaluation, full UCI protocol.

## Build and test

```sh
make          # builds build/seger
make test     # builds and runs position_test and perft_test
make divide   # builds build/perft_divide for move-generation debugging
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
- **`movePiece` clears the destination.** It removes any captured piece (and its
  bitboard bits) before moving. Keep it that way; skipping the destination
  removal leaves phantom enemy pieces in the occupancy bitboards.
- **En passant squares are only set when a capture is available.** The standard
  perft references assume this; always setting the ep square breaks perft.
- **Piece-square tables mirror by rank for Black**, via `tableIndexFor`. A
  symmetric position (startpos) must evaluate to 0 for the side to move.
- Move generation is pseudo-legal plus a legality filter; a double-check is a
  normal check. Do not "optimise" the filter away without perft revalidation.

## Conventions

- C++17, no external dependencies, no cmake.
- Keep `-Wall -Wextra -Wpedantic` clean.
- Identifiers use `camelCase` (see `types.h`); search `namespace seger`.
