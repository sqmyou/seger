#!/usr/bin/env python3
"""Bounded preflight for the engine: build, tests, bench, and a capped match.

Each step runs under a hard wall-clock timeout and its own process group, so a
slow build, a hung search, or a runaway self-play match cannot hold an
automation run past its budget. Every step prints PASS / FAIL / TIMEOUT and the
script exits nonzero if anything failed, timed out, or if the remaining budget
ran out.

    python3 tools/preflight.py                    # build + test + bench
    python3 tools/preflight.py --selfplay         # also a short A/B match
    python3 tools/preflight.py --total-budget 600 --selfplay --games 60

Use this instead of running `make`, `make test`, `make bench` and
`tools/selfplay.py` as separate commands: the per-step timeouts are what keep a
run from being killed by the harness.
"""

import argparse
import os
import signal
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


class Result:
    def __init__(self, name, status, seconds, detail=""):
        self.name = name
        self.status = status  # PASS | FAIL | TIMEOUT | SKIP
        self.seconds = seconds
        self.detail = detail


def run_step(name, cmd, timeout, cwd=ROOT):
    """Run one command with a hard timeout, killing its whole process group."""
    print(f"\n== {name} ==\n$ {' '.join(cmd)}", flush=True)
    start = time.monotonic()
    try:
        proc = subprocess.Popen(
            cmd, cwd=cwd, start_new_session=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    except FileNotFoundError as exc:
        return Result(name, "FAIL", 0.0, str(exc))

    try:
        out, _ = proc.communicate(timeout=timeout)
        elapsed = time.monotonic() - start
        sys.stdout.write(out)
        sys.stdout.flush()
        if proc.returncode == 0:
            return Result(name, "PASS", elapsed)
        return Result(name, "FAIL", elapsed, f"exit code {proc.returncode}")
    except subprocess.TimeoutExpired:
        elapsed = time.monotonic() - start
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        out, _ = proc.communicate()
        sys.stdout.write(out or "")
        return Result(name, "TIMEOUT", elapsed, f"exceeded {timeout:.0f}s")


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--total-budget", type=float, default=900.0,
                    help="whole-run wall-clock budget in seconds (default 900)")
    ap.add_argument("--selfplay", action="store_true",
                    help="also run a capped A/B self-play match")
    ap.add_argument("--games", type=int, default=60, help="self-play games")
    ap.add_argument("--depth", type=int, default=5, help="self-play depth")
    ap.add_argument("--parallel", type=int, default=4,
                    help="self-play games to run at once")
    ap.add_argument("--engine", default="build/seger",
                    help="engine under test (A side)")
    ap.add_argument("--reference", default="build/seger",
                    help="reference engine (B side)")
    ap.add_argument("--match-timeout", type=float, default=240.0,
                    help="hard cap on the self-play match in seconds")
    ap.add_argument("--bench-depth", type=int, default=9, help="bench depth")
    args = ap.parse_args()

    deadline = time.monotonic() + args.total_budget
    results = []

    def remaining():
        return deadline - time.monotonic()

    def budgeted(name, cmd, timeout):
        """Run a step, but never past the overall deadline."""
        left = remaining()
        if left <= 1.0:
            results.append(Result(name, "SKIP", 0.0, "total budget exhausted"))
            print(f"\n== {name} ==\nSKIP: total budget exhausted", flush=True)
            return False
        r = run_step(name, cmd, min(timeout, left))
        results.append(r)
        return r.status == "PASS"

    steps = [
        ("build", ["make"], 300.0),
        ("make test", ["make", "test"], 300.0),
        ("bench", [args.engine, "bench", str(args.bench_depth)], 180.0),
    ]
    for name, cmd, timeout in steps:
        budgeted(name, cmd, timeout)

    if args.selfplay:
        cmd = [sys.executable, os.path.join(ROOT, "tools", "selfplay.py"),
               "--a", args.engine, "--b", args.reference,
               "--games", str(args.games), "--depth", str(args.depth),
               "--parallel", str(args.parallel),
               # Let the match stop itself at 90% of the step cap so it reports a
               # partial score instead of being killed with no result.
               "--max-seconds", str(int(args.match_timeout * 0.9))]
        budgeted("selfplay", cmd, args.match_timeout)

    print("\n==== preflight summary ====")
    failed = False
    for r in results:
        line = f"{r.status:>7}  {r.name}  ({r.seconds:.1f}s)"
        if r.detail:
            line += f"  - {r.detail}"
        print(line)
        if r.status in ("FAIL", "TIMEOUT"):
            failed = True
        if r.status == "SKIP":
            failed = True
    print(f"budget: {args.total_budget:.0f}s  elapsed: "
          f"{args.total_budget - remaining():.1f}s")
    print("RESULT:", "FAIL" if failed else "PASS")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
