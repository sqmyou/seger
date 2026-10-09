#include "tt.h"

#include <algorithm>

namespace seger {

void TranspositionTable::resize(size_t mb) {
    if (mb < 1) mb = 1;
    size_t bytes = mb * 1024 * 1024;
    size_t count = bytes / sizeof(TTEntry);
    size_t power = 1;
    while (power * 2 <= count) power *= 2;
    entries_.assign(power, TTEntry{});
    mask_ = power - 1;
}

void TranspositionTable::clear() {
    std::fill(entries_.begin(), entries_.end(), TTEntry{});
}

void TranspositionTable::store(uint64_t key, int score, int depth,
                               BoundType bound, const Move& move) {
    TTEntry& e = slot(key);
    // Keep the deeper entry from the same position; otherwise overwrite.
    bool samePosition = (e.key == key) && (e.bound != BOUND_NONE);
    if (samePosition && depth < e.depth) return;

    // Prefer to retain a move even if we overwrite the score.
    Move keep = move.isNone() && samePosition ? e.move : move;
    e.key = key;
    e.score = score;
    e.depth = (int16_t)depth;
    e.bound = (uint8_t)bound;
    e.hasMove = keep.isNone() ? 0 : 1;
    e.move = keep;
}

const TTEntry* TranspositionTable::probe(uint64_t key) const {
    const TTEntry& e = slot(key);
    if (e.key != key || e.bound == BOUND_NONE) return nullptr;
    return &e;
}

}  // namespace seger
