#include "perft.h"

#include <vector>

namespace seger {

uint64_t perft(const Position& pos, int depth) {
    if (depth == 0) return 1;

    std::vector<Move> moves;
    moves.reserve(64);
    pos.generateMoves(moves, false);

    uint64_t nodes = 0;
    for (const Move& m : moves) {
        Position copy = pos;
        copy.doMove(m);
        if (copy.isInCheck(pos.sideToMove())) continue;  // leaves king in check
        nodes += perft(copy, depth - 1);
    }
    return nodes;
}

}  // namespace seger
