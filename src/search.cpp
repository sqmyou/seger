#include "search.h"

#include <algorithm>
#include <iostream>

#include "eval.h"

namespace seger {

namespace {

// MVV-LVA: most valuable victim first, least valuable attacker as tie-break.
int captureScore(const Position& pos, const Move& m) {
    Piece victim = pos.pieceOn(m.to);
    int score = 0;
    if (m.hasFlag(MF_ENPASSANT)) {
        score = 100 * 16 - pieceValue(PAWN);
    } else if (victim != NO_PIECE) {
        score = pieceValue(typeOf(victim)) * 16 - pieceValue(typeOf(pos.pieceOn(m.from)));
    }
    if (m.promotion != NO_PIECE_TYPE) score += pieceValue(m.promotion);
    return score;
}

// Transposition-table scores store mate distances relative to the current
// node, so they must be shifted by the ply when written and read back.
int scoreToTT(int s, int ply) {
    if (s >= MATE_IN_MAX_PLY) return s + ply;
    if (s <= -MATE_IN_MAX_PLY) return s - ply;
    return s;
}
int scoreFromTT(int s, int ply) {
    if (s >= MATE_IN_MAX_PLY) return s - ply;
    if (s <= -MATE_IN_MAX_PLY) return s + ply;
    return s;
}

}  // namespace

bool Search::timeUp() {
    if (stopped_) return true;
    if (stopRequested_.load()) {
        stopped_ = true;
        return true;
    }
    if (nodeLimit_ > 0 && nodes_ >= nodeLimit_) {
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

void Search::scoreMoves(std::vector<Move>& moves, const Move& ttMove, int ply) {
    const int k1 = pieceValue(QUEEN) * 2;
    std::stable_sort(moves.begin(), moves.end(), [&](const Move& a, const Move& b) {
        auto rank = [&](const Move& m) -> int {
            if (m == ttMove) return 1'000'000;
            if (captureScore(pos_, m) != 0) return 500'000 + captureScore(pos_, m);
            if (m == killers_[ply][0]) return 400'000;
            if (m == killers_[ply][1]) return k1;
            return history_[pos_.pieceOn(m.from)][m.to];
        };
        return rank(a) > rank(b);
    });
}

int Search::quiescence(int alpha, int beta, int ply) {
    ++nodes_;
    if ((nodes_ & 2047) == 0 && timeUp()) return alpha;

    const bool inCheck = pos_.isInCheck(pos_.sideToMove());
    int standPat = -INF;
    if (!inCheck) {
        standPat = evaluate(pos_);
        if (standPat >= beta) return beta;
        if (standPat > alpha) alpha = standPat;
    } else if (ply >= MAX_PLY - 1) {
        return evaluate(pos_);
    }

    std::vector<Move> moves;
    moves.reserve(32);
    // In check, search every evasion; otherwise only captures and promotions.
    pos_.generateMoves(moves, /*onlyCaptures=*/!inCheck);
    scoreMoves(moves, MOVE_NONE, ply);

    for (const Move& m : moves) {
        // Delta pruning: skip captures that cannot raise alpha even if they win
        // the victim outright.
        if (!inCheck && !isMateScore(alpha)) {
            int gain = m.hasFlag(MF_ENPASSANT) ? pieceValue(PAWN) : 0;
            if (!gain) {
                Piece victim = pos_.pieceOn(m.to);
                if (victim != NO_PIECE) gain = pieceValue(typeOf(victim));
            }
            if (m.promotion != NO_PIECE_TYPE) gain += pieceValue(m.promotion);
            if (standPat + gain + 200 <= alpha) continue;
        }
        if (!pos_.isLegalMove(m)) continue;
        pos_.doMove(m);
        int score = -quiescence(-beta, -alpha, ply + 1);
        pos_.undoMove(m);
        if (stopped_) return alpha;
        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }
    return alpha;
}

int Search::search(int depth, int alpha, int beta, int ply, bool canNull) {
    // Draw by repetition or the fifty-move rule.
    if (ply > 0) {
        if (pos_.fiftyMove() >= 100) return DRAW;
        if (pos_.isRepetition()) return DRAW;
    }

    const bool inCheck = pos_.isInCheck(pos_.sideToMove());
    if (inCheck && ply < MAX_PLY) ++depth;  // check extension

    if (depth <= 0) return quiescence(alpha, beta, ply);
    if (ply >= MAX_PLY - 1) return evaluate(pos_);

    ++nodes_;
    if ((nodes_ & 2047) == 0 && timeUp()) return alpha;

    const int alphaOrig = alpha;

    // --- Transposition table probe ------------------------------------------
    Move ttMove = MOVE_NONE;
    if (ttEnabled_) {
        if (const TTEntry* e = tt_.probe(pos_.key())) {
            if (e->hasMove) ttMove = e->move;
            if (e->depth >= depth) {
                int s = scoreFromTT(e->score, ply);
                if (e->bound == BOUND_EXACT ||
                    (e->bound == BOUND_LOWER && s >= beta) ||
                    (e->bound == BOUND_UPPER && s <= alpha)) {
                    return s;
                }
            }
        }
    }

    // --- Null-move pruning ---------------------------------------------------
    // Skip when in check, in a pawn-only ending (zugzwang), or when disallowed
    // by a preceding null move.
    if (canNull && !inCheck && depth >= 3 && pos_.hasNonPawnMaterial(pos_.sideToMove())) {
        int staticEval = evaluate(pos_);
        if (staticEval >= beta) {
            int R = 2 + depth / 4;
            pos_.makeNullMove();
            int score = -search(depth - 1 - R, -beta, -beta + 1, ply + 1, false);
            pos_.undoNullMove();
            if (stopped_) return alpha;
            if (score >= beta) return beta;
        }
    }

    std::vector<Move> moves;
    moves.reserve(64);
    pos_.generateMoves(moves, false);
    scoreMoves(moves, ttMove, ply);

    Move bestMove = MOVE_NONE;
    int bestScore = -INF;
    int moveCount = 0;
    bool anyLegal = false;

    for (const Move& m : moves) {
        if (!pos_.isLegalMove(m)) continue;
        anyLegal = true;

        pos_.doMove(m);
        int score;
        const bool isCapture = pos_.pieceOn(m.to) != NO_PIECE || m.hasFlag(MF_ENPASSANT) ||
                               m.promotion != NO_PIECE_TYPE;

        if (moveCount == 0) {
            score = -search(depth - 1, -beta, -alpha, ply + 1, true);
        } else {
            // Late move reduction for quiet, late moves at deeper nodes.
            int reduction = 0;
            if (depth >= 3 && moveCount >= 3 && !isCapture && !inCheck) {
                reduction = 1 + (moveCount >= 6 ? 1 : 0);
            }
            score = -search(depth - 1 - reduction, -alpha - 1, -alpha, ply + 1, true);
            if (score > alpha && (reduction > 0 || score < beta))
                score = -search(depth - 1, -beta, -alpha, ply + 1, true);
        }
        pos_.undoMove(m);
        ++moveCount;

        if (stopped_) return alpha;
        if (score > bestScore) {
            bestScore = score;
            bestMove = m;
        }
        if (score >= beta) {
            if (!isCapture) {
                if (killers_[ply][0] != m) {
                    killers_[ply][1] = killers_[ply][0];
                    killers_[ply][0] = m;
                }
                history_[pos_.pieceOn(m.from)][m.to] += depth * depth;
            }
            break;
        }
        if (score > alpha) alpha = score;
    }

    if (!anyLegal) {
        if (inCheck) return -MATE + ply;  // checkmate
        return DRAW;                      // stalemate
    }

    // --- Transposition table store ------------------------------------------
    if (ttEnabled_ && !stopped_) {
        BoundType bound = BOUND_EXACT;
        if (bestScore <= alphaOrig) bound = BOUND_UPPER;
        else if (bestScore >= beta) bound = BOUND_LOWER;
        tt_.store(pos_.key(), scoreToTT(bestScore, ply), depth, bound, bestMove);
    }

    return bestScore;
}

Move Search::think(const SearchLimits& limits) {
    start_ = std::chrono::steady_clock::now();
    stopped_ = false;
    stopRequested_.store(false);
    nodes_ = 0;
    nodeLimit_ = limits.nodes;

    std::fill(&killers_[0][0], &killers_[0][0] + MAX_PLY * 2, MOVE_NONE);
    for (auto& row : history_) for (int& v : row) v = 0;

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
    int prevScore = DRAW;

    for (int depth = 1; depth <= maxDepth; ++depth) {
        // Aspiration window around the previous score (widened on a fail).
        int delta = 25;
        int alpha = -INF, beta = INF;
        if (depth >= 5 && !isMateScore(prevScore)) {
            alpha = std::max(-INF, prevScore - delta);
            beta = std::min(INF, prevScore + delta);
        }

        Move iterBest = MOVE_NONE;
        int bestScore = -INF;
        const Move ttRootMove = [&]() -> Move {
            const TTEntry* e = ttEnabled_ ? tt_.probe(pos_.key()) : nullptr;
            return (e && e->hasMove) ? e->move : MOVE_NONE;
        }();
        scoreMoves(rootMoves, ttRootMove, 0);

        auto searchRoot = [&]() {
            alpha = (depth >= 5 && !isMateScore(prevScore)) ? std::max(-INF, prevScore - delta) : -INF;
            beta = (depth >= 5 && !isMateScore(prevScore)) ? std::min(INF, prevScore + delta) : INF;
            iterBest = MOVE_NONE;
            bestScore = -INF;
            int mc = 0;
            for (const Move& m : rootMoves) {
                pos_.doMove(m);
                int score;
                if (mc == 0) {
                    score = -search(depth - 1, -beta, -alpha, 1, true);
                } else {
                    score = -search(depth - 1, -alpha - 1, -alpha, 1, true);
                    if (score > alpha && score < beta)
                        score = -search(depth - 1, -beta, -alpha, 1, true);
                }
                pos_.undoMove(m);
                ++mc;
                if (stopped_) return;
                if (score > bestScore) {
                    bestScore = score;
                    iterBest = m;
                }
                if (score > alpha) alpha = score;
            }
        };

        // Re-search with progressively wider aspiration windows on failure.
        for (int guard = 0; guard < 4; ++guard) {
            searchRoot();
            if (stopped_) break;
            if (iterBest.isNone()) break;
            if (bestScore <= alpha && alpha > -INF) { delta *= 4; continue; }
            if (bestScore >= beta && beta < INF) { delta *= 4; continue; }
            break;
        }

        if (stopped_ || iterBest.isNone()) break;
        best = iterBest;
        prevScore = bestScore;

        if (ttEnabled_) tt_.store(pos_.key(), scoreToTT(bestScore, 0), depth, BOUND_EXACT, best);

        if (out_) {
            int64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - start_).count();
            int64_t nps = elapsed > 0 ? (nodes_ * 1000 / elapsed) : 0;

            // Build the principal variation by walking the transposition table.
            std::string pv = moveToString(best);
            std::vector<Move> undoStack;
            pos_.doMove(best);
            undoStack.push_back(best);
            uint64_t seen[64];
            int seenN = 0;
            for (int i = 1; i < depth && i < MAX_PLY; ++i) {
                const TTEntry* e = ttEnabled_ ? tt_.probe(pos_.key()) : nullptr;
                if (!e || !e->hasMove) break;
                Move pm = e->move;
                bool dup = false;
                for (int j = 0; j < seenN; ++j) if (seen[j] == pos_.key()) dup = true;
                if (dup) break;
                if (seenN < 64) seen[seenN++] = pos_.key();
                if (!pos_.isLegalMove(pm)) break;
                pos_.doMove(pm);
                undoStack.push_back(pm);
                pv += ' ';
                pv += moveToString(pm);
            }
            for (auto it = undoStack.rbegin(); it != undoStack.rend(); ++it) pos_.undoMove(*it);

            int shown = bestScore;
            if (isMateScore(shown))
                shown = (shown > 0 ? (MATE - shown + 1) / 2 : -(MATE + shown + 1) / 2);
            *out_ << "info depth " << depth << " score "
                  << (isMateScore(bestScore) ? "mate " : "cp ") << shown
                  << " nodes " << nodes_ << " nps " << nps
                  << " time " << elapsed << " pv " << pv << "\n";
            out_->flush();
        }

        if (isMateScore(bestScore)) break;
        if (limits.useDepth && depth >= limits.depth) break;
        if (nodeLimit_ > 0 && nodes_ >= nodeLimit_) break;
        if (hardDeadlineMs_ > 0) {
            int64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - start_).count();
            if (elapsed > hardDeadlineMs_ / 2) break;  // no time for another full depth
        }
        if (stopped_) break;
    }

    return best;
}

}  // namespace seger
