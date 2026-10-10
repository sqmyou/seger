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

There is no cmake; the project builds with plain `make` and `g++`. Objects carry
generated `-MMD` dependencies (`build/*.d`), so changing a header rebuilds every
translation unit that includes it. If you ever build without them — or
`CXXFLAGS` fails to pass `-MMD` — a header/struct change will silently leave
stale `.o` files with mismatched layouts and crash at `-O2` with no other hint.
When in doubt, `make clean && make`.

`tools/selfplay.py` plays two engine binaries against each other
(`--a build/seger --b /path/to/other --games 8 --depth 5`) and reports the
score and a rough Elo delta. Use it to check whether an evaluation or search
change is actually an improvement before keeping it. `--parallel N` runs N
games at once across cores (roughly N× faster wall-clock), and `--movetime`
selects a time control instead of a fixed depth.

CI (`.github/workflows/ci.yml`) runs `make`, `make test`, a UCI smoke test and
`make bench` on every push to `main` and on pull requests. Keep it green.

Bench/signature baseline: depth 9 from startpos settles at **140,502,299
nodes** (~1.31 Mnps) with the tapered evaluation. The node count moves whenever
the evaluation changes, so treat it as a regression tripwire rather than a
constant to preserve.

Measured strength of the tapered evaluation against the earlier one, at depth 6
with `tools/selfplay.py` (200 games, mixed openings, colours alternated):
**A 162 wins, 17 draws, 21 losses — score 0.853, +305 Elo (+/- 68 at 95%)**. The
interval is wide because the gap is large; the lower bound is still above +200.
Re-run the same command against a `build/seger` from commit `ee816e7` to
reproduce it.

Absolute strength via `tools/vs_stockfish.py` (Stockfish strength-limited with
`UCI_LimitStrength`/`UCI_Elo`, 100 ms/move, one SF thread, colours alternated).
After the move-ordering fix below, a 40-games-per-anchor run scored 0.925 vs
SF-Elo 2100, 0.713 vs 2200 and 0.600 vs 2300, putting Seger at roughly
**2350-2400 Elo** (the three anchors bracket it at 2358/2370/2536). The earlier
baseline was 0.662 / 0.400 / 0.475, i.e. about **2200-2250 Elo** — so this
session added on the order of +150 Elo absolute. (An even earlier 40-game run on
a loaded machine gave 2075; that was contention, not a real difference — run
these measurements on an idle CPU.) This is Seger against Stockfish's own
UCI_Elo calibration at a short time control, not an official rating-list number;
treat it as a ballpark. The `vs_stockfish.py` tool needs a Stockfish binary
(`--stockfish`), which is not vendored.

## Tried and rejected

These were measured with `tools/selfplay.py` and did **not** help; do not
re-introduce them without a fresh match that beats the baseline:

- A second, larger evaluation rewrite (king-safety term, mobility areas, threat
  detection, passed-pawn table, 7th-rank rook bonus) scored **0.417 over 60
  games at depth 5**, i.e. about -58 Elo. The existing tapered evaluation is
  already well tuned.
- Reverse-futility pruning, futility pruning and late-move pruning, added on top
  of the current search, cost roughly 300 Elo: the margins were too loose and the
  engine started cutting good lines. If you add pruning, tune the margins with a
  match, do not paste in textbook constants.
- Enabling late-move reductions more aggressively (the old `isCapture` bug had
  left LMR effectively dead) also lost: 0.500 at depth 5 and 0.125 at depth 7.
  A conservative 1-ply LMR with TT/killer exclusions was still below baseline
  (0.375 at depth 7). The move ordering is not yet good enough to support LMR.

## Wins (measured)

- **Move-ordering fix (killer/history were silently dead).** `isCapture` was
  computed *after* `doMove`, so it was always true and neither killer moves nor
  the history table was ever updated for quiet moves. Classifying the move
  before it is applied restored those mechanisms: benign total nodes at depth 9
  fell **140.5M -> 10.9M** (13x) and wall time 107s -> 17s. Strength: **0.562 over
  120 games at depth 6 (+44 Elo)**, and **0.658 over 60 games at 100 ms/move
  (+114 Elo)** — the gain is larger under a time control because the node cut
  lets it search deeper. LMR was removed in the same change: it is a separate
  idea and should only return behind a fresh match.
- **Passed-pawn king proximity (endgame).** Added to `eval.cpp`: the passer's
  own king scores for escorting it, the enemy king for catching it, weighted by
  rank and only when `phase <= 12`. Measured **0.550 over 60 games at depth 6
  (+35 Elo)** and **0.562 over 40 games at depth 7 (+44 Elo)** against the
  pre-change build. The effect grows with depth because deeper search reaches
  more king-and-pawn endings.
- **Insufficient-material draw detection.** `Position::hasInsufficientMaterial()`
  now reports K vs K, a lone minor, and all-bishops-on-one-colour as draws, and
  the search returns DRAW for them (in both `search` and `quiescence`). This is a
  correctness fix rather than a strength term; a same-binary control match
  confirms the harness's side-A bias is about -47 Elo, and the change is neutral
  against it. Covered by cases in `tests/position_test.cpp`.
- A full-ply check extension (search checking moves one ply deeper) blew up the
  search: `search_test` did not finish in minutes on a forced-check position
  because every check extends again. If you want extensions, add them with a
  cap (e.g. extend only above a depth threshold, or limit total extensions).

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
- `captureScore` returns 0 for quiet moves, and callers (notably
  `scoreMoves`) depend on that. It is called twice per element by the
  move-ordering comparator, so `scoreMoves` now caches the value in a local
  instead of calling it twice; the function itself short-circuits the quiet
  case first. This is strictly behaviour-preserving (same rank values, same
  stable-sort order): the depth-9 bench still reports 140,502,299 nodes and
  the depth-8 bench 10,523,028, matching the pre-change baseline.

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
