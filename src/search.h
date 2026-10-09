#pragma once

#include <chrono>
#include <atomic>
#include <cstdint>
#include <ostream>
#include <vector>

#include "position.h"

namespace seger {

constexpr int MAX_PLY = 64;
constexpr int MATE = 30000;
constexpr int MATE_IN_MAX_PLY = MATE - MAX_PLY;
constexpr int INF = 32000;
constexpr int DRAW = 0;

// Scores at or beyond MATE_IN_MAX_PLY encode a forced mate.
inline bool isMateScore(int s) {
    int a = s < 0 ? -s : s;
    return a >= MATE_IN_MAX_PLY;
}

struct SearchLimits {
    int depth = MAX_PLY;       // fixed depth, if `useDepth`
    bool useDepth = false;
    int movetime = 0;          // milliseconds for this move
    int timeLeft = 0;          // remaining time on the clock
    int increment = 0;         // per-move increment
    int movestogo = 0;         // moves until the next time control (0 = sudden death)
    bool infinite = false;

    bool hasTimeControl() const {
        return movetime > 0 || timeLeft > 0 || infinite;
    }
};

class Search {
public:
    explicit Search(Position& pos) : pos_(pos) {}

    // Runs an iterative-deepening search and returns the best move found.
    Move think(const SearchLimits& limits);

    // Reports search progress as UCI "info" lines to `out`.
    void setInfoCallback(std::ostream& out) { out_ = &out; }

    // Requests that an in-progress search stop as soon as possible.
    void stop() { stopRequested_.store(true); }

private:
    Position& pos_;
    std::ostream* out_ = nullptr;

    std::chrono::steady_clock::time_point start_;
    int64_t hardDeadlineMs_ = 0;
    bool stopped_ = false;
    mutable std::atomic<bool> stopRequested_{false};

    uint64_t nodes_ = 0;
    // Move history keyed by position for simple repetition detection.
    std::vector<uint64_t> history_;

    int search(bool pvNode, int depth, int alpha, int beta, int ply);
    int quiescence(int alpha, int beta, int ply);

    bool timeUp();
};

}  // namespace seger
