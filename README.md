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

This builds and runs five test binaries:

- `position_test` checks FEN handling, make/unmake consistency, and edge cases
  such as en passant, promotion, and checkmate.
- `perft_test` compares node counts for six standard positions against the
  published perft results.
- `search_test` verifies forced mates against an independent brute-force
  solver, and checks that the transposition table changes neither the best move
  nor (for the worse) the node count.
- `uci_test` drives the UCI loop in-process and checks the handshake, `go`
  limits, `perft`, and mate reporting.
- `eval_test` checks evaluation symmetry by mirroring positions and re-loads
  the piece values and endgame terms.

For a build/test/bench pass that carries its own wall-clock timeouts — use it in
CI jobs, automation runs, or any session that has a time limit — run:

```sh
python3 tools/preflight.py            # build + test + bench
python3 tools/preflight.py --selfplay # also a capped A/B match
```

Each step is killed if it overruns, and the script exits nonzero on failure or
timeout, so a slow build or a runaway search cannot hold a run past its budget.

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

The board uses the Cburnett SVG chess set, vendored from
`sqmyou/games/assets/chess/` (Wikimedia Commons, CC BY-SA 3.0). See
`tools/web/pieces/PIECES-LICENSE` for the attribution and terms.

The server's JSON contract is small and both endpoints return the same shape, so
the client never has to guess what is on the board:

```
POST /api/state   {"fen"?, "moves"?}          -> {fen, turn, legal[], status}
POST /api/engine  {"fen"?, "moves"?, "time"}  -> {move, fen, turn, legal[], status}
```

`status` is `{over, result, reason}`. `legal` is always the mover's full legal
move list, including in the `/api/engine` reply.

## Playing on Lichess as a bot

Seger can run as a [Lichess bot](https://lichess.org/player/bots): it listens
for incoming challenges, accepts the ones that match the configured limits, and
plays every game with the engine. Lichess hosts the games, the clocks, and the
rating; you only need a small always-on process.

One-time setup:

1. Create a Lichess account for the bot. It must not have played a game yet, or
   the upgrade below will be refused — create a fresh one if unsure.
2. Create a personal API token with the `bot:play` scope (Account → API tokens).
3. Upgrade the account to a bot account. This is irreversible and moves the
   account off the web UI for good:
   ```sh
   curl -X POST https://lichess.org/api/bot/account/upgrade \
     -H "Authorization: Bearer $LICHESS_TOKEN"
   ```
4. Run the bot:
   ```sh
   make
   LICHESS_TOKEN=xxxxxxxx python3 tools/lichess_bot.py --accept-rated
   ```

The bot accepts standard chess by default, casual games only, with a clock of at
least three minutes. Useful flags: `--accept-rated` to also play rated games,
`--variants standard,chess3000` to widen the accepted variants, `--max-rating N`
to turn down stronger challengers, and `--min-time S` to guard against
ultra-fast games. It needs only outbound HTTPS to `lichess.org`; no ports have
to be opened. Run it under `systemd` (or `screen`/`tmux`) on a machine that
stays up, or in a small container; the process reconnects to the event stream on
its own after a drop.

Lichess also slots the bot into the bot arena and the global bot list once it is
online, so anyone can challenge it from its profile page.

### Free hosting

A Lichess bot must keep an outbound connection open to receive challenges, so a
free tier that "sleeps" a web service when it is idle (Render, Koyeb, Railway)
is not suitable — the bot would go offline and stop accepting games. What is
needed is an always-on machine, and as of late 2026 only two providers give one
for free with no expiry:

| Provider | Specs | Catch |
|---|---|---|
| **Oracle Cloud Always Free** | 2 Arm Ampere cores, 12 GB RAM, 200 GB disk, 10 TB/mo egress | Needs a card for identity (a $1 hold); ARM capacity is often full, so retry or upgrade to Pay-As-You-Go (still $0 within limits) |
| **Google Cloud free tier** | 1 `e2-micro` (2 shared vCPU burst, 1 GB RAM), 30 GB disk | Needs a card; must live in `us-west1`, `us-central1` or `us-east1`; 1 GB egress/mo |

Oracle is the better fit: the ARM box is far more than a bot needs and it can
run several engines at once. Everything else marketed as free is a time-limited
trial (AWS, Azure, Fly.io) or a card-gated paid plan.

Files are included for both routes:

```sh
# Plain Linux box (Oracle VM, Raspberry Pi, home server)
sudo cp tools/seger-bot.service /etc/systemd/system/
sudo systemctl enable --now seger-bot      # token in /etc/seger-bot.env

# Container host
docker build -t seger-bot .
docker run -d --restart unless-stopped -e LICHESS_TOKEN=xxxx seger-bot --accept-rated
```

A home machine works too, but Lichess drops the event stream on its own
maintenance restarts; the bot reconnects itself, so a short outage is fine.

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
  uci_test.cpp       UCI protocol loop tests
  eval_test.cpp      Evaluation symmetry and term tests
tools/
  play.py            Terminal play helper (python-chess)
  server.py          Local web-server play helper
  selfplay.py        A/B match harness for measuring a change
  vs_stockfish.py    Absolute-strength estimate against Stockfish
  preflight.py       Bounded build/test/bench (and optional capped match)
  lichess_bot.py     Lichess bot bridge driving the engine over UCI
  seger-bot.service  systemd unit for running the Lichess bot
  web/index.html     Self-contained browser board UI
  web/pieces/*.svg   Cburnett chess set (CC BY-SA 3.0)
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
