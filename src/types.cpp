#include "types.h"

namespace seger {

int stringToSquare(const std::string& s) {
    if (s.size() < 2) return SQ_NONE;
    int file = s[0] - 'a';
    int rank = s[1] - '1';
    if (file < 0 || file > 7 || rank < 0 || rank > 7) return SQ_NONE;
    return makeSquare(file, rank);
}

std::string squareToString(int sq) {
    if (sq == SQ_NONE) return "-";
    std::string s;
    s += char('a' + fileOf(sq));
    s += char('1' + rankOf(sq));
    return s;
}

std::string moveToString(const Move& m) {
    if (m.isNone()) return "0000";
    std::string s = squareToString(m.from) + squareToString(m.to);
    if (m.promotion != NO_PIECE_TYPE) {
        switch (m.promotion) {
            case KNIGHT: s += 'n'; break;
            case BISHOP: s += 'b'; break;
            case ROOK:   s += 'r'; break;
            case QUEEN:  s += 'q'; break;
            default: break;
        }
    }
    return s;
}

}  // namespace seger
