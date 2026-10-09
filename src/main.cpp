#include <iostream>
#include <string>

#include "bench.h"
#include "position.h"
#include "search.h"
#include "types.h"
#include "uci.h"

int main(int argc, char** argv) {
    seger::zobrist::init();

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::cout << "Seger chess engine\n"
                      << "Usage:\n"
                      << "  seger                 run UCI protocol on stdin/stdout\n"
                      << "  seger bench [depth]   run a fixed-depth search benchmark\n";
            return 0;
        }
        if (arg == "bench") {
            int depth = 12;
            if (i + 1 < argc) {
                int d = std::atoi(argv[i + 1]);
                if (d > 0 && d <= seger::MAX_PLY) depth = d;
            }
            seger::runBench(depth, &std::cout);
            return 0;
        }
    }

    seger::uciLoop();
    return 0;
}
