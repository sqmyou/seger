#pragma once

#include <cstdint>
#include <string>

namespace seger {

// ---------------------------------------------------------------------------
// Colors
// ---------------------------------------------------------------------------
enum Color : int { WHITE, BLACK, COLOR_NB };

constexpr Color operator~(Color c) { return Color(c ^ 1); }

// ---------------------------------------------------------------------------
// Pieces
// ---------------------------------------------------------------------------
enum PieceType : int {
    NO_PIECE_TYPE,
    PAWN,
    KNIGHT,
    BISHOP,
    ROOK,
    QUEEN,
    KING,
    PIECE_TYPE_NB = 7
};

// White pieces occupy 1..6, black pieces 9..14. The color is bit 3.
enum Piece : int {
    NO_PIECE = 0,
    W_PAWN = 1, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
    B_PAWN = 9, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING,
    PIECE_NB = 16
};

constexpr Piece makePiece(Color c, PieceType pt) {
    return Piece((c << 3) | pt);
}
constexpr PieceType typeOf(Piece p) { return PieceType(p & 7); }
constexpr Color colorOf(Piece p) { return Color((p >> 3) & 1); }

// ---------------------------------------------------------------------------
// Squares (0x88 mailbox: index = rank * 16 + file, a1 = 0)
// ---------------------------------------------------------------------------
constexpr int SQ_NONE = -1;

constexpr bool isOnBoard(int sq) { return (sq & 0x88) == 0; }
constexpr int makeSquare(int file, int rank) { return rank * 16 + file; }
constexpr int fileOf(int sq) { return sq & 7; }
constexpr int rankOf(int sq) { return sq >> 4; }

// Bitboards use a dense 0..63 indexing (a1 = bit 0, h8 = bit 63), which is
// independent of the 0x88 square layout. Do not shift by a raw 0x88 square
// index: it exceeds 63 and the shift is undefined behaviour.
constexpr int bitIndexOf(int sq) { return (rankOf(sq) << 3) | fileOf(sq); }
constexpr int squareOfBitIndex(int i) { return makeSquare(i & 7, i >> 3); }
constexpr uint64_t bitOf(int sq) { return 1ULL << bitIndexOf(sq); }
constexpr bool testBit(uint64_t bb, int sq) { return (bb >> bitIndexOf(sq)) & 1ULL; }

// Parses/prints squares in algebraic notation (e.g. "e4").
int stringToSquare(const std::string& s);
std::string squareToString(int sq);

// ---------------------------------------------------------------------------
// Castling rights (bitmask)
// ---------------------------------------------------------------------------
enum CastlingRight : int {
    NO_CASTLING  = 0,
    WHITE_OO     = 1,
    WHITE_OOO    = 2,
    BLACK_OO     = 4,
    BLACK_OOO    = 8,
    CASTLING_NB  = 16
};

// ---------------------------------------------------------------------------
// Moves
// ---------------------------------------------------------------------------
enum MoveFlag : uint8_t {
    MF_NORMAL      = 0,
    MF_ENPASSANT   = 1,
    MF_CASTLE      = 2,
    MF_PROMOTION   = 4,
    MF_DOUBLE_PUSH = 8
};

struct Move {
    int from = SQ_NONE;
    int to = SQ_NONE;
    PieceType promotion = NO_PIECE_TYPE;
    uint8_t flags = MF_NORMAL;

    bool isNone() const { return from == SQ_NONE; }
    bool hasFlag(MoveFlag f) const { return (flags & f) != 0; }

    friend bool operator==(const Move& a, const Move& b) {
        return a.from == b.from && a.to == b.to &&
               a.promotion == b.promotion && a.flags == b.flags;
    }
    friend bool operator!=(const Move& a, const Move& b) { return !(a == b); }
};

constexpr Move MOVE_NONE{SQ_NONE, SQ_NONE, NO_PIECE_TYPE, MF_NORMAL};

// Renders a move as a UCI string (e.g. "e2e4", "e7e8q").
std::string moveToString(const Move& m);

// Material value of a piece type, in centipawns.
constexpr int pieceValue(PieceType pt) {
    switch (pt) {
        case PAWN:   return 100;
        case KNIGHT: return 320;
        case BISHOP: return 330;
        case ROOK:   return 500;
        case QUEEN:  return 900;
        default:     return 0;
    }
}

}  // namespace seger
