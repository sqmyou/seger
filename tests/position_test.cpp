// Unit tests for the Position class: FEN round-tripping, make/unmake
// consistency, and basic move generation sanity checks.
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "../src/position.h"

using namespace seger;

static int failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) {
        std::cout << "[ ok ] " << what << "\n";
    } else {
        std::cout << "[FAIL] " << what << "\n";
        ++failures;
    }
}

int main() {
    zobrist::init();

    // FEN round-trip on the start position.
    {
        Position pos;
        pos.setStartpos();
        std::string f = pos.fen();
        check(f == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
              "startpos FEN round-trip: " + f);
    }

    // FEN round-trip on a richer position.
    {
        Position pos;
        std::string fen = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
        pos.setFen(fen);
        check(pos.fen() == fen, "kiwipete FEN round-trip");
    }

    // Make/unmake restores the exact key and FEN.
    {
        Position pos;
        pos.setFen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
        std::string before = pos.fen();
        uint64_t keyBefore = pos.key();
        std::vector<Move> moves;
        pos.generateLegalMoves(moves);
        check(!moves.empty(), "kiwipete has legal moves");
        for (const Move& m : moves) {
            pos.doMove(m);
            pos.undoMove(m);
            if (pos.fen() != before || pos.key() != keyBefore) {
                check(false, "make/unmake restores position after " + moveToString(m));
                return 1;
            }
        }
        check(true, "make/unmake consistent across " + std::to_string(moves.size()) + " moves");
    }

    // En passant available and generated.
    {
        Position pos;
        pos.setFen("rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3");
        std::vector<Move> moves;
        pos.generateLegalMoves(moves);
        bool found = false;
        for (const Move& m : moves)
            if (m.hasFlag(MF_ENPASSANT)) found = true;
        check(found, "en passant capture is generated");
    }

    // Promotion generates four options.
    {
        Position pos;
        pos.setFen("8/P7/8/8/8/8/8/k6K w - - 0 1");
        std::vector<Move> moves;
        pos.generateLegalMoves(moves);
        int promos = 0;
        for (const Move& m : moves)
            if (m.hasFlag(MF_PROMOTION)) ++promos;
        check(promos == 4, "promotion generates 4 moves (got " + std::to_string(promos) + ")");
    }

    // Checkmate detection: fool's mate position, side to move is White.
    {
        Position pos;
        pos.setFen("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");
        std::vector<Move> moves;
        pos.generateLegalMoves(moves);
        check(moves.empty() && pos.isInCheck(WHITE), "fool's mate: no legal moves, in check");
    }

    // Insufficient-material detection.
    {
        struct Case { const char* fen; bool dead; const char* what; };
        const Case cases[] = {
            {"8/8/8/8/8/8/8/K6k w - - 0 1", true, "K vs K is a dead draw"},
            {"8/8/8/8/8/8/8/KN5k w - - 0 1", true, "K+N vs K is a dead draw"},
            {"8/8/8/8/8/8/8/KB5k w - - 0 1", true, "K+B vs K is a dead draw"},
            {"8/8/8/8/8/8/8/Kb5k w - - 0 1", true, "K vs K+B is a dead draw"},
            // Both bishops on dark squares, one per side: no mate is possible.
            {"8/8/8/8/8/8/8/KB1b3k w - - 0 1", true, "opposite-side bishops on one colour: draw"},
            {"8/8/8/8/8/8/8/KB3b1k w - - 0 1", true, "same-side bishops on one colour: draw"},
            // Same-colour bishops split across sides that cannot cooperate is
            // still a draw; but two bishops on *different* colours can mate.
            {"8/8/8/8/8/8/8/KB2b2k w - - 0 1", false, "bishops on both colours: not dead"},
            {"8/8/8/8/8/8/8/KB5k b - - 0 1", true, "same test holds with Black to move"},
            {"8/8/8/8/8/8/8/KN4nk w - - 0 1", false, "two knights with a knight each: not dead"},
            {"8/8/8/8/8/8/8/KP5k w - - 0 1", false, "a pawn can promote and mate"},
            {"8/8/8/8/8/8/8/KR5k w - - 0 1", false, "a rook mates"},
        };
        for (const Case& c : cases) {
            Position pos;
            pos.setFen(c.fen);
            check(pos.hasInsufficientMaterial() == c.dead, c.what);
        }
    }

    if (failures == 0) std::cout << "\nAll position tests passed.\n";
    else std::cout << "\n" << failures << " test(s) failed.\n";
    return failures == 0 ? 0 : 1;
}
