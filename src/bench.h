#pragma once

#include <cstdint>
#include <ostream>

namespace seger {

// Runs a fixed suite of positions at a fixed depth and returns the total node
// count. Single-threaded with a cleared, deterministic TT between positions, so
// the total is a stable signature: any change to move generation, evaluation or
// search moves it, and an unchanged move set keeps it identical.
//
// `depth` is the per-position search depth. Prints a per-position breakdown to
// `out` when it is non-null.
uint64_t runBench(int depth, std::ostream* out);

}  // namespace seger
