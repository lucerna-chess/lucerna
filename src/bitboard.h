#pragma once
#include <bit>
#include "types.h"

constexpr Bitboard FILE_A = 0x0101010101010101ULL;
constexpr Bitboard FILE_H = FILE_A << 7;
constexpr Bitboard RANK_1 = 0xFFULL;
constexpr Bitboard RANK_8 = RANK_1 << 56;

constexpr Bitboard sq_bb(int s) { return 1ULL << s; }
inline int popcount(Bitboard b) { return std::popcount(b); }
inline int lsb(Bitboard b) { return std::countr_zero(b); }
inline int pop_lsb(Bitboard& b) { int s = lsb(b); b &= b - 1; return s; }

constexpr Bitboard file_bb(int f) { return FILE_A << f; }
constexpr Bitboard rank_bb(int r) { return RANK_1 << (8 * r); }

constexpr Bitboard shift_up(Bitboard b, int c) { return c == WHITE ? b << 8 : b >> 8; }
constexpr Bitboard pawn_attacks_bb(Bitboard b, int c) {
    return c == WHITE ? ((b << 7) & ~FILE_H) | ((b << 9) & ~FILE_A)
                      : ((b >> 9) & ~FILE_H) | ((b >> 7) & ~FILE_A);
}

extern Bitboard PawnAttacks[2][64];
extern Bitboard KnightAttacks[64];
extern Bitboard KingAttacks[64];

struct Magic {
    Bitboard mask;
    Bitboard magic;
    Bitboard* attacks;
    int shift;
    unsigned index(Bitboard occ) const { return unsigned(((occ & mask) * magic) >> shift); }
};

extern Magic BishopMagics[64];
extern Magic RookMagics[64];

inline Bitboard bishop_attacks(int s, Bitboard occ) { return BishopMagics[s].attacks[BishopMagics[s].index(occ)]; }
inline Bitboard rook_attacks(int s, Bitboard occ) { return RookMagics[s].attacks[RookMagics[s].index(occ)]; }
inline Bitboard queen_attacks(int s, Bitboard occ) { return bishop_attacks(s, occ) | rook_attacks(s, occ); }

void init_bitboards();
