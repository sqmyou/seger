#include "eval.h"

namespace seger {

namespace {

// Piece-square tables are written from White's point of view, with the first
// row being rank 8 (index 0 = a8). They reward sensible piece placement in the
// opening and middlegame: knights and bishops leave the back rank, pawns
// advance toward the centre, and the king prefers the corner before castling.

constexpr int PawnTable[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
    50, 50, 50, 50, 50, 50, 50, 50,
    10, 10, 20, 30, 30, 20, 10, 10,
     5,  5, 10, 25, 25, 10,  5,  5,
     0,  0,  0, 20, 20,  0,  0,  0,
     5, -5,-10,  0,  0,-10, -5,  5,
     5, 10, 10,-20,-20, 10, 10,  5,
     0,  0,  0,  0,  0,  0,  0,  0
};

constexpr int KnightTable[64] = {
   -50,-40,-30,-30,-30,-30,-40,-50,
   -40,-20,  0,  0,  0,  0,-20,-40,
   -30,  0, 10, 15, 15, 10,  0,-30,
   -30,  5, 15, 20, 20, 15,  5,-30,
   -30,  0, 15, 20, 20, 15,  0,-30,
   -30,  5, 10, 15, 15, 10,  5,-30,
   -40,-20,  0,  5,  5,  0,-20,-40,
   -50,-40,-30,-30,-30,-30,-40,-50
};

constexpr int BishopTable[64] = {
   -20,-10,-10,-10,-10,-10,-10,-20,
   -10,  0,  0,  0,  0,  0,  0,-10,
   -10,  0,  5, 10, 10,  5,  0,-10,
   -10,  5,  5, 10, 10,  5,  5,-10,
   -10,  0, 10, 10, 10, 10,  0,-10,
   -10, 10, 10, 10, 10, 10, 10,-10,
   -10,  5,  0,  0,  0,  0,  5,-10,
   -20,-10,-10,-10,-10,-10,-10,-20
};

constexpr int RookTable[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
     5, 10, 10, 10, 10, 10, 10,  5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
     0,  0,  0,  5,  5,  0,  0,  0
};

constexpr int QueenTable[64] = {
   -20,-10,-10, -5, -5,-10,-10,-20,
   -10,  0,  0,  0,  0,  0,  0,-10,
   -10,  0,  5,  5,  5,  5,  0,-10,
    -5,  0,  5,  5,  5,  5,  0, -5,
     0,  0,  5,  5,  5,  5,  0, -5,
   -10,  5,  5,  5,  5,  5,  0,-10,
   -10,  0,  5,  0,  0,  0,  0,-10,
   -20,-10,-10, -5, -5,-10,-10,-20
};

constexpr int KingMiddleTable[64] = {
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -20,-30,-30,-40,-40,-30,-30,-20,
   -10,-20,-20,-20,-20,-20,-20,-10,
    20, 20,  0,  0,  0,  0, 20, 20,
    20, 30, 10,  0,  0, 10, 30, 20
};

constexpr int KingEndTable[64] = {
   -50,-40,-30,-20,-20,-30,-40,-50,
   -30,-20,-10,  0,  0,-10,-20,-30,
   -30,-10, 20, 30, 30, 20,-10,-30,
   -30,-10, 30, 40, 40, 30,-10,-30,
   -30,-10, 30, 40, 40, 30,-10,-30,
   -30,-10, 20, 30, 30, 20,-10,-30,
   -30,-30,  0,  0,  0,  0,-30,-30,
   -50,-30,-30,-30,-30,-30,-30,-50
};

// Converts a 0x88 square index to a 0..63 table index (a8 = 0) for White.
inline int tableIndexWhite(int sq) { return (7 - rankOf(sq)) * 8 + fileOf(sq); }
// Black mirrors the table vertically (a1 = 0 from Black's perspective).
inline int tableIndexBlack(int sq) { return rankOf(sq) * 8 + fileOf(sq); }
inline int tableIndexFor(Color c, int sq) {
    return c == WHITE ? tableIndexWhite(sq) : tableIndexBlack(sq);
}

}  // namespace

int evaluate(const Position& pos) {
    int score = 0;
    int nonPawnMaterial = 0;

    // First pass: material plus non-king piece-square terms, and a rough count
    // of non-pawn material to decide whether we are in the endgame.
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            int sq = makeSquare(file, rank);
            Piece p = pos.pieceOn(sq);
            if (p == NO_PIECE) continue;

            Color c = colorOf(p);
            PieceType pt = typeOf(p);
            if (pt == KING) continue;

            int sign = (c == WHITE) ? 1 : -1;
            int idx = tableIndexFor(c, sq);

            int psq = 0;
            switch (pt) {
                case PAWN:   psq = PawnTable[idx]; break;
                case KNIGHT: psq = KnightTable[idx]; nonPawnMaterial += 320; break;
                case BISHOP: psq = BishopTable[idx]; nonPawnMaterial += 330; break;
                case ROOK:   psq = RookTable[idx];   nonPawnMaterial += 500; break;
                case QUEEN:  psq = QueenTable[idx];  nonPawnMaterial += 900; break;
                default: break;
            }
            score += sign * (pieceValue(pt) + psq);
        }
    }

    // Second pass: the king uses a middle-game table until the board opens up.
    bool endgame = nonPawnMaterial <= 1300;
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            int sq = makeSquare(file, rank);
            Piece p = pos.pieceOn(sq);
            if (p == NO_PIECE || typeOf(p) != KING) continue;
            Color c = colorOf(p);
            int idx = tableIndexFor(c, sq);
            int sign = (c == WHITE) ? 1 : -1;
            score += sign * (endgame ? KingEndTable[idx] : KingMiddleTable[idx]);
        }
    }

    return (pos.sideToMove() == WHITE) ? score : -score;
}

}  // namespace seger
