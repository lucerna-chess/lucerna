#include "eval.h"
#include <algorithm>
#include <cstdlib>

// Hand-written placeholder evaluation. It only has to be good enough to
// bootstrap self-play data for the neural network that will replace it.

namespace {

const int MaterialMg[6] = {82, 337, 365, 477, 1025, 0};
const int MaterialEg[6] = {94, 281, 297, 512, 936, 0};
const int PhaseInc[6] = {0, 1, 1, 2, 4, 0};

const int PawnRankMg[8] = {0, 0, 2, 6, 14, 30, 55, 0};
const int PawnRankEg[8] = {0, 0, 4, 8, 16, 30, 50, 0};
const int PassedMg[8] = {0, 5, 8, 14, 28, 50, 80, 0};
const int PassedEg[8] = {0, 10, 15, 25, 45, 80, 130, 0};
const int KingFileMg[8] = {20, 30, 10, 0, 0, 10, 30, 20};

int PstMg[6][64], PstEg[6][64];
Bitboard PassedMask[2][64];
Bitboard AdjacentFiles[8];
Bitboard ShieldMask[2][64];

} // namespace

void init_eval() {
    for (int sq = 0; sq < 64; sq++) {
        int f = file_of(sq), r = rank_of(sq);
        int dist = (std::abs(2 * f - 7) + std::abs(2 * r - 7)) / 2; // 1 (centre) .. 7 (corner)
        bool central = f == 3 || f == 4;

        PstMg[PAWN][sq] = PawnRankMg[r] + (central && (r == 3 || r == 4) ? 15 : 0);
        PstEg[PAWN][sq] = PawnRankEg[r];
        PstMg[KNIGHT][sq] = 25 - 8 * dist;
        PstEg[KNIGHT][sq] = 20 - 7 * dist;
        PstMg[BISHOP][sq] = 10 - 3 * dist;
        PstEg[BISHOP][sq] = 10 - 4 * dist;
        PstMg[ROOK][sq] = (r == 6 ? 20 : 0) + (central ? 5 : 0);
        PstEg[ROOK][sq] = r == 6 ? 10 : 0;
        PstMg[QUEEN][sq] = 5 - 2 * dist;
        PstEg[QUEEN][sq] = 15 - 5 * dist;
        PstMg[KING][sq] = r == 0 ? KingFileMg[f] : r == 1 ? KingFileMg[f] / 2 - 10 : std::max(-80, -20 - 15 * (r - 2));
        PstEg[KING][sq] = 30 - 10 * dist;
    }

    for (int f = 0; f < 8; f++)
        AdjacentFiles[f] = (f > 0 ? file_bb(f - 1) : 0) | (f < 7 ? file_bb(f + 1) : 0);

    for (int sq = 0; sq < 64; sq++) {
        int f = file_of(sq), r = rank_of(sq);
        Bitboard files = file_bb(f) | AdjacentFiles[f];
        Bitboard aboveW = 0, belowB = 0;
        for (int rr = r + 1; rr < 8; rr++) aboveW |= rank_bb(rr);
        for (int rr = r - 1; rr >= 0; rr--) belowB |= rank_bb(rr);
        PassedMask[WHITE][sq] = files & aboveW;
        PassedMask[BLACK][sq] = files & belowB;

        Bitboard shieldW = 0, shieldB = 0;
        if (r + 1 < 8) shieldW |= rank_bb(r + 1);
        if (r + 2 < 8) shieldW |= rank_bb(r + 2);
        if (r - 1 >= 0) shieldB |= rank_bb(r - 1);
        if (r - 2 >= 0) shieldB |= rank_bb(r - 2);
        ShieldMask[WHITE][sq] = files & shieldW;
        ShieldMask[BLACK][sq] = files & shieldB;
    }
}

int evaluate(const Position& pos) {
    if (Nnue::active)
        return std::clamp(Nnue::evaluate(pos.accumulator(), pos.side, Nnue::output_bucket(popcount(pos.occupied()))),
                          -MATE_BOUND + 1, MATE_BOUND - 1);

    int mg[2] = {0, 0}, eg[2] = {0, 0};
    int phase = 0;
    Bitboard occ = pos.occupied();

    Bitboard pawnAtt[2] = {pawn_attacks_bb(pos.pieces(WHITE, PAWN), WHITE),
                           pawn_attacks_bb(pos.pieces(BLACK, PAWN), BLACK)};

    for (int c = WHITE; c <= BLACK; c++) {
        int them = c ^ 1;
        Bitboard ownPawns = pos.pieces(c, PAWN);
        Bitboard enemyPawns = pos.pieces(them, PAWN);
        Bitboard mobilityArea = ~pos.byColor[c] & ~pawnAtt[them];
        int enemyKing = pos.king_sq(them);
        Bitboard kingZone = KingAttacks[enemyKing] | sq_bb(enemyKing);
        int attackUnits = 0, attackers = 0;

        for (int pt = PAWN; pt <= KING; pt++) {
            Bitboard b = pos.pieces(c, pt);
            while (b) {
                int sq = pop_lsb(b);
                int rsq = relative_sq(c, sq);
                mg[c] += MaterialMg[pt] + PstMg[pt][rsq];
                eg[c] += MaterialEg[pt] + PstEg[pt][rsq];
                phase += PhaseInc[pt];

                Bitboard att = 0;
                switch (pt) {
                    case PAWN: {
                        int f = file_of(sq);
                        if (!(PassedMask[c][sq] & enemyPawns)) {
                            int rr = relative_rank(c, sq);
                            mg[c] += PassedMg[rr];
                            eg[c] += PassedEg[rr];
                        }
                        if (!(AdjacentFiles[f] & ownPawns)) { mg[c] -= 10; eg[c] -= 12; }
                        if (PassedMask[c][sq] & file_bb(f) & ownPawns) { mg[c] -= 10; eg[c] -= 20; }
                        break;
                    }
                    case KNIGHT: {
                        att = KnightAttacks[sq];
                        int mob = popcount(att & mobilityArea);
                        mg[c] += (mob - 4) * 4;
                        eg[c] += (mob - 4) * 4;
                        if (att & kingZone) { attackers++; attackUnits += 2 * popcount(att & kingZone); }
                        break;
                    }
                    case BISHOP: {
                        att = bishop_attacks(sq, occ);
                        int mob = popcount(att & mobilityArea);
                        mg[c] += (mob - 6) * 5;
                        eg[c] += (mob - 6) * 5;
                        if (att & kingZone) { attackers++; attackUnits += 2 * popcount(att & kingZone); }
                        break;
                    }
                    case ROOK: {
                        att = rook_attacks(sq, occ);
                        int mob = popcount(att & mobilityArea);
                        mg[c] += (mob - 6) * 2;
                        eg[c] += (mob - 6) * 4;
                        Bitboard file = file_bb(file_of(sq));
                        if (!(file & ownPawns)) {
                            if (!(file & enemyPawns)) { mg[c] += 25; eg[c] += 10; }
                            else { mg[c] += 12; eg[c] += 6; }
                        }
                        if (att & kingZone) { attackers++; attackUnits += 3 * popcount(att & kingZone); }
                        break;
                    }
                    case QUEEN: {
                        att = queen_attacks(sq, occ);
                        int mob = popcount(att & mobilityArea);
                        mg[c] += (mob - 13) * 1;
                        eg[c] += (mob - 13) * 2;
                        if (att & kingZone) { attackers++; attackUnits += 5 * popcount(att & kingZone); }
                        break;
                    }
                    case KING: {
                        if (relative_rank(c, sq) <= 1)
                            mg[c] += 12 * popcount(ShieldMask[c][sq] & ownPawns);
                        break;
                    }
                }
            }
        }

        if (popcount(pos.pieces(c, BISHOP)) >= 2) { mg[c] += 30; eg[c] += 50; }

        if (attackers >= 2 && pos.pieces(c, QUEEN))
            mg[c] += std::min(attackUnits * attackUnits * 3 / 2, 600);
    }

    int mgScore = mg[WHITE] - mg[BLACK];
    int egScore = eg[WHITE] - eg[BLACK];
    phase = std::min(phase, 24);
    int score = (mgScore * phase + egScore * (24 - phase)) / 24;
    if (pos.side == BLACK) score = -score;
    return score + 15;
}
