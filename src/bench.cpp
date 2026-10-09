#include "bench.h"

#include <chrono>
#include <sstream>
#include <string>
#include <vector>

#include "position.h"
#include "search.h"

namespace seger {

namespace {

// A spread of quiet and tactical positions, including two endgames, so the
// signature exercises most of the evaluation and search paths.
const char* kBenchFens[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    "8/8/8/4k3/8/4K3/4P3/8 w - - 0 1",
    "4k3/8/8/8/8/8/8/4K2R w K - 0 1",
};

}  // namespace

uint64_t runBench(int depth, std::ostream* out) {
    uint64_t total = 0;
    auto start = std::chrono::steady_clock::now();

    for (const char* fen : kBenchFens) {
        Position pos;
        if (!pos.setFen(fen)) continue;
        Search search(pos);
        search.clearTT();
        std::ostringstream discard;
        search.setInfoCallback(discard);
        SearchLimits lim;
        lim.useDepth = true;
        lim.depth = depth;
        search.think(lim);
        uint64_t n = search.lastNodes();
        total += n;
        if (out) *out << fen << " -> " << n << " nodes\n";
    }

    if (out) {
        double ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - start)
                        .count();
        uint64_t nps = ms > 0 ? (uint64_t)(total / (ms / 1000.0)) : 0;
        *out << "bench total " << total << " nodes  time " << (int64_t)ms
             << " ms  nps " << nps << "\n";
    }
    return total;
}

}  // namespace seger
