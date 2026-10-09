#pragma once

#include <string>
#include <vector>

#include "position.h"
#include "search.h"

namespace seger {

// Implements the Universal Chess Interface protocol. Reads commands from
// standard input and writes responses to standard output until "quit".
void uciLoop();

}  // namespace seger
