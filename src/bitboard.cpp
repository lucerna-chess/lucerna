#include "bitboard.h"
#include <cstdlib>
#include <cstring>

Bitboard PawnAttacks[2][64];
Bitboard KnightAttacks[64];
Bitboard KingAttacks[64];
Magic BishopMagics[64];
Magic RookMagics[64];

namespace {

Bitboard RookTable[102400];
Bitboard BishopTable[5248];

const int RookDirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
const int BishopDirs[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

Bitboard sliding_attacks(int sq, Bitboard occ, const int dirs[4][2]) {
    Bitboard att = 0;
    int r = rank_of(sq), f = file_of(sq);
    for (int d = 0; d < 4; d++) {
        int rr = r + dirs[d][0], ff = f + dirs[d][1];
        while (rr >= 0 && rr < 8 && ff >= 0 && ff < 8) {
            int s = rr * 8 + ff;
            att |= sq_bb(s);
            if (occ & sq_bb(s)) break;
            rr += dirs[d][0];
            ff += dirs[d][1];
        }
    }
    return att;
}

Bitboard step_attacks(int sq, const int steps[][2], int n) {
    Bitboard att = 0;
    int r = rank_of(sq), f = file_of(sq);
    for (int i = 0; i < n; i++) {
        int rr = r + steps[i][0], ff = f + steps[i][1];
        if (rr >= 0 && rr < 8 && ff >= 0 && ff < 8) att |= sq_bb(rr * 8 + ff);
    }
    return att;
}

struct PRNG {
    uint64_t s;
    uint64_t next() {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return s * 2685821657736338717ULL;
    }
    uint64_t sparse() { return next() & next() & next(); }
};

void init_magics(Magic* magics, Bitboard* table, const int dirs[4][2]) {
    PRNG rng{728};
    Bitboard occupancy[4096], reference[4096];
    int epoch[4096] = {}, cnt = 0;
    Bitboard* next = table;

    for (int sq = 0; sq < 64; sq++) {
        Bitboard edges = ((RANK_1 | RANK_8) & ~rank_bb(rank_of(sq))) | ((FILE_A | FILE_H) & ~file_bb(file_of(sq)));
        Magic& m = magics[sq];
        m.mask = sliding_attacks(sq, 0, dirs) & ~edges;
        m.shift = 64 - popcount(m.mask);
        m.attacks = next;

        int size = 0;
        Bitboard b = 0;
        do {
            occupancy[size] = b;
            reference[size] = sliding_attacks(sq, b, dirs);
            size++;
            b = (b - m.mask) & m.mask;
        } while (b);

        for (int i = 0; i < size;) {
            do m.magic = rng.sparse();
            while (popcount((m.mask * m.magic) >> 56) < 6);
            cnt++;
            for (i = 0; i < size; i++) {
                unsigned idx = m.index(occupancy[i]);
                if (epoch[idx] < cnt) {
                    epoch[idx] = cnt;
                    m.attacks[idx] = reference[i];
                } else if (m.attacks[idx] != reference[i]) {
                    break;
                }
            }
        }
        next += size;
    }
}

} // namespace

void init_bitboards() {
    const int knight[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
    const int king[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
    const int wpawn[2][2] = {{1, -1}, {1, 1}};
    const int bpawn[2][2] = {{-1, -1}, {-1, 1}};
    for (int sq = 0; sq < 64; sq++) {
        KnightAttacks[sq] = step_attacks(sq, knight, 8);
        KingAttacks[sq] = step_attacks(sq, king, 8);
        PawnAttacks[WHITE][sq] = step_attacks(sq, wpawn, 2);
        PawnAttacks[BLACK][sq] = step_attacks(sq, bpawn, 2);
    }
    init_magics(RookMagics, RookTable, RookDirs);
    init_magics(BishopMagics, BishopTable, BishopDirs);
}
