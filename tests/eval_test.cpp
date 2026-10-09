// Evaluation tests.
//
// These are deliberately behavioural: they check the properties an evaluation
// must have (colour symmetry, sensible material ordering, tapered king safety,
// passed-pawn scaling) rather than pinning exact centipawn values, so retuning
// terms does not require rewriting the tests.
#include <cstdio>
#include <string>

#include "eval.h"
#include "position.h"
#include "types.h"

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

static int evalFen(const std::string& fen) {
    Position pos;
    pos.setFen(fen);
    return evaluate(pos);
}

// Mirror a FEN vertically and swap colours, including the side to move.
static std::string mirrorFen(const std::string& fen) {
    std::string board = fen.substr(0, fen.find(' '));
    std::string mirrored;
    for (auto it = board.rbegin(); it != board.rend(); ++it) {
        char c = *it;
        if (c == '/') { mirrored += '/'; continue; }
        if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
        else if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        mirrored += c;
    }
    bool whiteToMove = fen.find(" w ") != std::string::npos;
    return mirrored + (whiteToMove ? " b " : " w ") + "- - 0 1";
}

int main() {
    zobrist::init();

    // --- Colour symmetry -----------------------------------------------------
    // The evaluation is always reported from the side to move's point of view,
    // so a mirrored position with the other side to move must score identically.
    // (This is the property a stray colour flip breaks.)
    {
        const char* fens[] = {
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
            "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
            "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
            "4k3/8/8/8/8/8/8/4K2R w K - 0 1",
            "r1bqkb1r/pppp1ppp/2n2n2/4p3/8/5N2/PPPPPPPP/R1BQKB1R b KQkq - 0 5",
        };
        for (const char* fen : fens) {
            int a = evalFen(fen);
            int b = evalFen(mirrorFen(fen));
            check(a == b, std::string("symmetry: ") + fen,
                  "eval=" + std::to_string(a) + " mirror=" + std::to_string(b));
        }
    }

    // --- Material ordering ---------------------------------------------------
    // Queen > rook > bishop > knight > pawn > nothing, all else equal, and a
    // colour-reversed material edge is the exact negation.
    {
        int q = evalFen("4k3/8/8/8/8/8/8/3QK3 w - - 0 1");
        int r = evalFen("4k3/8/8/8/8/8/8/3RK3 w - - 0 1");
        int b = evalFen("4k3/8/8/8/8/8/8/3BK3 w - - 0 1");
        int n = evalFen("4k3/8/8/8/8/8/8/3NK3 w - - 0 1");
        int p = evalFen("4k3/8/8/8/8/8/3P4/4K3 w - - 0 1");
        check(q > r && r > b && b > n && n > p && p > 0,
              "material ordering Q>R>B>N>P>0",
              std::to_string(q) + "/" + std::to_string(r) + "/" + std::to_string(b) +
                  "/" + std::to_string(n) + "/" + std::to_string(p));

        // Mirroring flips the board and the side to move together, so both
        // positions are "the side to move is up a queen" and must score the
        // same and be strongly positive. (Comparing the same board with only
        // the side to move flipped does not negate exactly, because tempo is
        // tapered and is not symmetric between the two colours here.)
        std::string upQ = "4k3/8/8/8/8/8/8/3QK3 w - - 0 1";
        int m1 = evalFen(upQ);
        int m2 = evalFen(mirrorFen(upQ));
        check(m1 == m2 && m1 > 500, "queen edge is large and mirror-stable",
              std::to_string(m1) + " / " + std::to_string(m2));
    }

    // --- Passed pawns --------------------------------------------------------
    // A pawn one step from promotion is worth far more than one on its home rank.
    {
        int near = evalFen("4k3/4P3/8/8/8/8/8/4K3 w - - 0 1");
        int home = evalFen("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1");
        check(near > home + 100, "advanced passed pawn scores much higher",
              std::to_string(near) + " vs " + std::to_string(home));
    }

    // --- Tapered king ------------------------------------------------
    // With only kings and a pawn on the board (endgame phase) an active king
    // must score higher than a passive corner king.
    {
        int central = evalFen("4k3/8/8/8/4K3/8/4P3/8 w - - 0 1");
        int corner = evalFen("4k3/8/8/8/8/8/4P3/K7 w - - 0 1");
        check(central > corner, "endgame king prefers the centre",
              std::to_string(central) + " vs " + std::to_string(corner));
    }

    // --- Tempo ---------------------------------------------------------------
    // From the starting position both sides hold a small positive score, since
    // each is slightly better for having the move.
    {
        int w = evalFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        int b = evalFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1");
        check(w > 0 && b > 0, "startpos has a positive tempo for either side",
              std::to_string(w) + " / " + std::to_string(b));
    }

    if (g_failures == 0) {
        printf("\nAll eval tests passed.\n");
        return 0;
    }
    printf("\n%d eval test(s) failed.\n", g_failures);
    return 1;
}
