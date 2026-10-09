// UCI protocol tests.
//
// Drives uciLoop() in-process by swapping std::cin/std::cout for string
// streams, then checks the responses. This exercises the real command parser
// rather than a re-implementation of it.
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>

#include "../src/uci.h"

using namespace seger;

static int g_failures = 0;

static void check(bool ok, const std::string& name, const std::string& detail = "") {
    if (ok) {
        printf("[ ok ] %s\n", name.c_str());
    } else {
        printf("[FAIL] %s %s\n", name.c_str(), detail.c_str());
        ++g_failures;
    }
}

static bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

// Runs the given command script through uciLoop and returns everything it
// printed. The script must end with "quit\n" so the loop terminates.
static std::string runUci(const std::string& script) {
    std::istringstream in(script);
    std::ostringstream out;
    std::streambuf* oldIn = std::cin.rdbuf(in.rdbuf());
    std::streambuf* oldOut = std::cout.rdbuf(out.rdbuf());
    uciLoop();
    std::cin.rdbuf(oldIn);
    std::cout.rdbuf(oldOut);
    return out.str();
}

int main() {
    {
        std::string o = runUci("uci\nquit\n");
        check(contains(o, "id name Seger"), "uci: id name", o);
        check(contains(o, "id author "), "uci: id author");
        check(contains(o, "option name Hash"), "uci: advertises Hash option");
        check(contains(o, "uciok"), "uci: uciok");
    }

    {
        std::string o = runUci("isready\nquit\n");
        check(contains(o, "readyok"), "isready: readyok");
    }

    {
        // Handshake then readiness must both be acknowledged in order.
        std::string o = runUci("uci\nisready\nucinewgame\nisready\nquit\n");
        size_t uci = o.find("uciok");
        size_t r1 = o.find("readyok");
        size_t r2 = o.find("readyok", r1 + 1);
        check(uci != std::string::npos && r1 != std::string::npos && r2 != std::string::npos,
              "handshake: uciok then two readyok");
        check(uci < r1 && r1 < r2, "handshake: responses in order");
    }

    {
        std::string o = runUci("setoption name Hash value 4\nisready\nquit\n");
        check(contains(o, "readyok"), "setoption Hash accepted");
    }

    {
        std::string o = runUci("position startpos\ngo perft 3\nquit\n");
        check(contains(o, "nodes 8902"), "go perft 3 from startpos = 8902", o);
    }

    {
        std::string o = runUci(
            "position startpos moves e2e4 e7e5 g1f3\ngo depth 4\nquit\n");
        check(contains(o, "bestmove "), "go depth: prints bestmove", o);
        // Any legal reply to 1.e4 e5 2.Nf3 is one of a known set; just ensure a
        // concrete 4-character move was printed.
        size_t p = o.find("bestmove ");
        std::string mv = o.substr(p + 9, 5);
        check(mv.size() >= 4 && mv != "0000", "go depth: a real move", mv);
    }

    {
        std::string o = runUci("position startpos\ngo nodes 2000\nquit\n");
        check(contains(o, "bestmove "), "go nodes: prints bestmove");
    }

    {
        // A position with a forced mate must be reported as "mate".
        std::string o = runUci(
            "position fen 6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1\ngo depth 6\nquit\n");
        check(contains(o, "mate "), "go depth: reports mate in 1", o);
    }

    {
        std::string o = runUci("position startpos\nd\neval\nquit\n");
        check(contains(o, "FEN: "), "d: prints FEN");
        check(contains(o, "static eval:"), "eval: prints static eval");
    }

    {
        // Unknown commands must be ignored, not crash.
        std::string o = runUci("bogus command\nnonsense\nisready\nquit\n");
        check(contains(o, "readyok"), "unknown commands ignored");
    }

    if (g_failures == 0) {
        printf("\nAll UCI tests passed.\n");
        return 0;
    }
    printf("\n%d UCI test(s) failed.\n", g_failures);
    return 1;
}
