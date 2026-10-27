#pragma once
#include <cstdint>
#include <string>

using Bitboard = uint64_t;
using Key = uint64_t;

constexpr int WHITE = 0, BLACK = 1;
enum PieceType : int { PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING };
constexpr int NO_PIECE = 12;
constexpr int NO_SQ = 64;

constexpr int make_piece(int c, int pt) { return c * 6 + pt; }
constexpr int piece_type(int p) { return p % 6; }
constexpr int piece_color(int p) { return p / 6; }
constexpr int file_of(int s) { return s & 7; }
constexpr int rank_of(int s) { return s >> 3; }
constexpr int relative_rank(int c, int s) { return c == WHITE ? rank_of(s) : 7 - rank_of(s); }
constexpr int relative_sq(int c, int s) { return c == WHITE ? s : s ^ 56; }

enum CastlingRight : int { WK = 1, WQ = 2, BK = 4, BQ = 8 };

// Move layout: bits 0-5 from, 6-11 to, 12-15 flag
using Move = uint16_t;
constexpr Move NO_MOVE = 0;
enum MoveFlag : int {
    QUIET = 0, DOUBLE_PUSH = 1, KING_CASTLE = 2, QUEEN_CASTLE = 3,
    CAPTURE = 4, EP_CAPTURE = 5,
    PROMO_N = 8, PROMO_B = 9, PROMO_R = 10, PROMO_Q = 11,
    PROMO_CAP_N = 12, PROMO_CAP_B = 13, PROMO_CAP_R = 14, PROMO_CAP_Q = 15
};

constexpr Move make_move(int from, int to, int flag = QUIET) { return Move(from | (to << 6) | (flag << 12)); }
constexpr int from_sq(Move m) { return m & 63; }
constexpr int to_sq(Move m) { return (m >> 6) & 63; }
constexpr int move_flag(Move m) { return m >> 12; }
constexpr bool is_capture(Move m) { return move_flag(m) & 4; }
constexpr bool is_promo(Move m) { return move_flag(m) & 8; }
constexpr int promo_type(Move m) { return (move_flag(m) & 3) + KNIGHT; }
constexpr bool is_castle(Move m) { return move_flag(m) == KING_CASTLE || move_flag(m) == QUEEN_CASTLE; }
constexpr bool is_quiet(Move m) { return !is_capture(m) && !is_promo(m); }

constexpr int MAX_PLY = 128;
constexpr int MAX_MOVES = 256;

constexpr int INF = 32000;
constexpr int MATE = 31000;
constexpr int MATE_BOUND = MATE - MAX_PLY;
constexpr int SCORE_NONE = 32001;

struct MoveList {
    Move moves[MAX_MOVES];
    int scores[MAX_MOVES];
    int size = 0;
    void add(Move m) { moves[size++] = m; }
};
