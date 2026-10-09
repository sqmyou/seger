#include <iostream>
#include <string>

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
    }

    seger::uciLoop();
    return 0;
}
