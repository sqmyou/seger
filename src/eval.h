#pragma once

#include "position.h"

namespace seger {

// Static evaluation in centipawns, from the perspective of the side to move.
// Positive values favour the side to move.
int evaluate(const Position& pos);

}  // namespace seger
