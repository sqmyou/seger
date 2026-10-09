#include <iostream>
#include <string>
#include <vector>
#include "../src/perft.h"
#include "../src/position.h"
using namespace seger;

int main(int argc, char** argv) {
    zobrist::init();
    std::string fen = (argc > 1) ? argv[1] : "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1";
    int depth = (argc > 2) ? std::atoi(argv[2]) : 2;
    Position pos;
    pos.setFen(fen);
    std::vector<Move> moves;
    pos.generateMoves(moves, false);
    uint64_t total = 0;
    for (const Move& m : moves) {
        Position c = pos;
        c.doMove(m);
        if (c.isInCheck(pos.sideToMove())) continue;
        uint64_t n = perft(c, depth - 1);
        total += n;
        std::cout << moveToString(m) << ": " << n << "\n";
    }
    std::cout << "total " << total << "\n";
}
