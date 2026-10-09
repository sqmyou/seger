#pragma once

#include <cstdint>

#include "position.h"

namespace seger {

// Counts the number of leaf nodes reachable in exactly `depth` plies. Used to
// validate move generation against published perft reference values.
uint64_t perft(const Position& pos, int depth);

}  // namespace seger
