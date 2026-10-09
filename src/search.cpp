#include "search.h"

#include <algorithm>
#include <iostream>

#include "eval.h"

namespace seger {

namespace {
// Scores an ordinary capture for move ordering: most-valuable victim first.
int captureScore(const Position& pos, const Move& m) {
    Piece victim = pos.pieceOn(m.to);
    int score = 0;
    if (m.hasFlag(MF_ENPASSANT)) {
        score = 100 * 10;
    } else if (victim != NO_PIECE) {
        score = pieceValue(typeOf(victim)) * 10 - pieceValue(typeOf(pos.pieceOn(m.from)));
    }
    if (m.promotion != NO_PIECE_TYPE) score += pieceValue(m.promotion);
    return score;
}

void orderMoves(const Position& pos, std::vector<Move>& moves) {
    std::stable_sort(moves.begin(), moves.end(), [&](const Move& a, const Move& b) {
        return captureScore(pos, a) > captureScore(pos, b);
    });
}
}  // namespace

bool Search::timeUp() {
    if (stopped_) return true;
    if (stopRequested_.load()) {
        stopped_ = true;
        return true;
    }
    if (hardDeadlineMs_ <= 0) return false;
    auto now = std::chrono::steady_clock::now();
    int64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_).count();
    if (elapsed >= hardDeadlineMs_) {
        stopped_ = true;
        return true;
    }
    return false;
}

int Search::quiescence(int alpha, int beta, int ply) {
    ++nodes_;
    if ((nodes_ & 2047) == 0 && timeUp()) return alpha;

    int standPat = evaluate(pos_);
    if (standPat >= beta) return beta;
    if (standPat > alpha) alpha = standPat;

    std::vector<Move> moves;
    moves.reserve(32);
    pos_.generateMoves(moves, /*onlyCaptures=*/true);
    orderMoves(pos_, moves);

    for (const Move& m : moves) {
        if (!pos_.isLegal(m)) continue;
        pos_.doMove(m);
        int score = -quiescence(-beta, -alpha, ply + 1);
        pos_.undoMove(m);
        if (stopped_) return alpha;
        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }
    return alpha;
}

int Search::search(bool /*pvNode*/, int depth, int alpha, int beta, int ply) {
    // Repetition / fifty-move draw detection.
    if (ply > 0) {
        if (pos_.fiftyMove() >= 100) return DRAW;
        if (pos_.isInCheck(pos_.sideToMove()) == false) {
            int count = 0;
            uint64_t key = pos_.key();
            for (auto it = history_.rbegin(); it != history_.rend(); ++it)
                if (*it == key) ++count;
            if (count >= 2) return DRAW;  // twofold within search tree
        }
    }

    bool inCheck = pos_.isInCheck(pos_.sideToMove());
    if (inCheck && ply < MAX_PLY) ++depth;  // check extension

    if (depth <= 0) return quiescence(alpha, beta, ply);

    if (ply >= MAX_PLY - 1) return evaluate(pos_);

    ++nodes_;
    if ((nodes_ & 2047) == 0 && timeUp()) return alpha;

    std::vector<Move> moves;
    moves.reserve(64);
    pos_.generateMoves(moves, false);
    orderMoves(pos_, moves);

    bool anyLegal = false;
    for (const Move& m : moves) {
        if (!pos_.isLegal(m)) continue;
        anyLegal = true;
        pos_.doMove(m);
        history_.push_back(pos_.key());
        int score = -search(false, depth - 1, -beta, -alpha, ply + 1);
        history_.pop_back();
        pos_.undoMove(m);

        if (stopped_) return alpha;
        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }

    if (!anyLegal) {
        if (inCheck) return -MATE + ply;  // checkmate
        return DRAW;                      // stalemate
    }
    return alpha;
}

Move Search::think(const SearchLimits& limits) {
    start_ = std::chrono::steady_clock::now();
    stopped_ = false;
    stopRequested_.store(false);
    nodes_ = 0;
    history_.clear();

    // Time management: budget a slice of the remaining clock.
    hardDeadlineMs_ = 0;
    if (limits.movetime > 0) {
        hardDeadlineMs_ = limits.movetime;
    } else if (limits.timeLeft > 0) {
        int budget;
        if (limits.movestogo > 0) {
            budget = limits.timeLeft / limits.movestogo;
        } else {
            budget = limits.timeLeft / 30 + limits.increment / 2;
        }
        budget = std::min(budget, limits.timeLeft / 2);
        hardDeadlineMs_ = std::max(budget, 1);
    } else if (limits.infinite) {
        hardDeadlineMs_ = 0;  // rely on "stop"
    }

    std::vector<Move> rootMoves;
    rootMoves.reserve(64);
    pos_.generateLegalMoves(rootMoves);
    if (rootMoves.empty()) return MOVE_NONE;

    Move best = rootMoves.front();
    int maxDepth = limits.useDepth ? limits.depth : MAX_PLY;

    for (int depth = 1; depth <= maxDepth; ++depth) {
        int alpha = -INF, beta = INF;
        Move iterBest = MOVE_NONE;
        int bestScore = -INF;
        orderMoves(pos_, rootMoves);  // best move first from the previous iteration

        for (const Move& m : rootMoves) {
            pos_.doMove(m);
            history_.push_back(pos_.key());
            int score = -search(false, depth - 1, -beta, -alpha, 1);
            history_.pop_back();
            pos_.undoMove(m);
            if (stopped_) break;
            if (score > bestScore) {
                bestScore = score;
                iterBest = m;
            }
            if (score > alpha) alpha = score;
        }

        if (iterBest.isNone()) break;  // aborted before completing this depth
        best = iterBest;

        if (out_) {
            int64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - start_).count();
            int64_t nps = elapsed > 0 ? (nodes_ * 1000 / elapsed) : 0;
            int shown = bestScore;
            if (isMateScore(shown))
                shown = (shown > 0 ? (MATE - shown + 1) / 2 : -(MATE + shown + 1) / 2);
            *out_ << "info depth " << depth << " score "
                  << (isMateScore(bestScore) ? "mate " : "cp ") << shown
                  << " nodes " << nodes_ << " nps " << nps
                  << " time " << elapsed << " pv " << moveToString(best) << "\n";
            out_->flush();
        }

        // Move the best move to the front so the next iteration tries it first.
        auto it = std::find(rootMoves.begin(), rootMoves.end(), best);
        if (it != rootMoves.end()) std::rotate(rootMoves.begin(), it, it + 1);

        if (isMateScore(bestScore)) break;
        if (limits.useDepth && depth >= limits.depth) break;
        if (!limits.infinite && limits.movetime == 0 && limits.timeLeft == 0) continue;
        if (hardDeadlineMs_ > 0) {
            int64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - start_).count();
            if (elapsed > hardDeadlineMs_ / 2) break;  // no time for another full depth
        }
    }

    return best;
}

}  // namespace seger
