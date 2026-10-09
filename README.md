# Seger

Seger is a UCI-compatible chess engine written in C++17.

This is the first version. It plays legal chess, understands the full UCI
protocol, and searches to a configurable depth or time limit.

## Features

- Complete legal move generation: castling, en passant, and promotions.
- Static evaluation with material values and piece-square tables, plus a
  king table that switches to an endgame version when the board opens up.
- Negamax search with alpha-beta pruning, quiescence search for captures,
  check extensions, and iterative deepening.
- Time management based on the remaining clock and increment.
- Repetition and fifty-move draw detection.
- Zobrist hashing of positions.
- Perft tests that check move generation against published reference counts.

## Building

You only need a C++17 compiler and `make`.

```sh
make
```

The engine binary is written to `build/seger`.

## Testing

```sh
make test
```

This builds and runs two test binaries:

- `position_test` checks FEN handling, make/unmake consistency, and edge cases
  such as en passant, promotion, and checkmate.
- `perft_test` compares node counts for six standard positions against the
  published perft results.

## Running

Seger speaks UCI on standard input/output, so it works with any UCI GUI
(Arena, Cute Chess, BanksiaGUI, and so on).

```sh
build/seger
```

Example session:

```
uci
isready
position startpos moves e2e4 e7e5
go movetime 1000
```

## Playing a game from the terminal

Any UCI client can drive it. For a quick manual game you can pipe commands:

```sh
printf 'uci\nisready\nposition startpos\ngo depth 8\nquit\n' | build/seger
```

## Project layout

```
src/
  types.h/.cpp       Colors, pieces, squares, moves
  position.h/.cpp    Board representation, FEN, make/unmake, move generation
  eval.h/.cpp        Static evaluation
  search.h/.cpp      Alpha-beta search and time management
  uci.h/.cpp         UCI protocol loop
  perft.h/.cpp       Perft node counting
  main.cpp           Entry point
tests/
  position_test.cpp  Unit tests for the board
  perft_test.cpp     Perft reference suite
```

## Board representation

Positions use a 0x88 mailbox board. Each square index is `rank * 16 + file`,
and `index & 0x88` detects off-board squares cheaply. This keeps move
generation simple and correct, which matters more than raw speed for a first
version. The next version can migrate to bitboards with magic bitboard sliding
attacks for a large speedup.

## License

MIT. See `LICENSE`.
