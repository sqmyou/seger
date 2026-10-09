# Seger

Seger is a UCI-compatible chess engine written in C++17.

It plays legal chess, understands the full UCI protocol (including
`setoption` for the hash size and a `perft` command), and searches to a
configurable depth, node count, or time limit.

## Features

- Complete legal move generation: castling, en passant, and promotions.
- Static evaluation with material values and piece-square tables, plus a
  king table that switches to an endgame version when the board opens up.
- Negamax search with alpha-beta pruning, principal-variation search, an
  aspiration window, killer/history/MVV-LVA move ordering, a transposition
  table, null-move pruning, late-move reductions, and check extensions.
- Quiescence search for captures and checking evasions, with delta pruning.
- Time management based on the remaining clock and increment.
- Repetition and fifty-move draw detection from an incremental Zobrist history.
- Perft tests that check move generation against published reference counts,
  and search tests that cross-check forced mates against a brute-force solver.

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

This builds and runs three test binaries:

- `position_test` checks FEN handling, make/unmake consistency, and edge cases
  such as en passant, promotion, and checkmate.
- `perft_test` compares node counts for six standard positions against the
  published perft results.
- `search_test` verifies forced mates against an independent brute-force
  solver, and checks that the transposition table changes neither the best move
  nor (for the worse) the node count.

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
setoption name Hash value 64
position startpos moves e2e4 e7e5
go movetime 1000
```

Supported `go` limits: `depth`, `movetime`, `wtime`/`btime`, `winc`/`binc`,
`movestogo`, `nodes`, `infinite` (with `stop`), and `perft <depth>`.

## Playing a game from the terminal

The easiest way to play is the bundled helper (needs `python-chess`):

```sh
make
python3 tools/play.py            # play White against the engine
python3 tools/play.py --black --time 2
```

Any other UCI client works too. For a quick manual session you can pipe commands:

```sh
printf 'uci\nisready\nposition startpos\ngo depth 8\nquit\n' | build/seger
```

## Playing in a browser

If you cannot run the binary on your own machine (for example, the engine is
built inside a sandbox), `tools/server.py` serves a small self-contained board
UI that talks to the engine over HTTP:

```sh
make
python3 tools/server.py                   # http://0.0.0.0:12000
python3 tools/server.py --port 12001
```

Then open the port's address (in a hosted workspace this is the provided work
URL for that port). The UI needs no internet access and no external assets; it
is a single HTML file in `tools/web/`. The engine runs as a child process of the
server, and only one move is searched at a time.

## Project layout

```
src/
  types.h/.cpp       Colors, pieces, squares, moves
  position.h/.cpp    Board representation, FEN, make/unmake, move generation
  eval.h/.cpp        Static evaluation
  tt.h/.cpp          Transposition table
  search.h/.cpp      Alpha-beta search, move ordering, time management
  uci.h/.cpp         UCI protocol loop
  perft.h/.cpp       Perft node counting
  main.cpp           Entry point
tests/
  position_test.cpp  Unit tests for the board
  perft_test.cpp     Perft reference suite
  search_test.cpp    Search and transposition-table tests
tools/
  play.py            Terminal play helper (python-chess)
  server.py          Local web-server play helper
  web/index.html     Self-contained browser board UI
```

## Board representation

Positions use a 0x88 mailbox board. Each square index is `rank * 16 + file`,
and `index & 0x88` detects off-board squares cheaply. This keeps move
generation simple and correct. Legality is a pseudo-legal generation pass
followed by a make/undo check; piece-type bitboards are maintained in parallel
for the evaluation's material test. The next version can migrate to bitboard
sliding attacks (magic bitboards) for a further speedup.

## Search

Iterative deepening drives a principal-variation search with a transposition
table (see `tt.h`), aspiration windows, null-move pruning, late-move
reductions, and killer/history/MVV-LVA move ordering. The transposition table
stores mate scores relative to the current ply so that they remain exact when
probed from a different path.

## License

MIT. See `LICENSE`.
