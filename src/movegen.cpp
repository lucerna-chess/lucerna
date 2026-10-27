#include "movegen.h"

namespace {

void add_targets(MoveList& list, int from, Bitboard targets, int flag) {
    while (targets) list.add(make_move(from, pop_lsb(targets), flag));
}

void gen_pawns(const Position& pos, MoveList& list, GenType type) {
    int us = pos.side, them = us ^ 1;
    int up = us == WHITE ? 8 : -8;
    Bitboard empty = ~pos.occupied();
    Bitboard enemies = pos.byColor[them];
    Bitboard rank7 = rank_bb(us == WHITE ? 6 : 1);
    Bitboard rank3 = rank_bb(us == WHITE ? 2 : 5);
    Bitboard pawns = pos.pieces(us, PAWN);
    Bitboard promoPawns = pawns & rank7;
    Bitboard others = pawns & ~rank7;

    if (type & GEN_QUIET) {
        Bitboard single = shift_up(others, us) & empty;
        Bitboard dbl = shift_up(single & rank3, us) & empty;
        while (single) { int to = pop_lsb(single); list.add(make_move(to - up, to, QUIET)); }
        while (dbl) { int to = pop_lsb(dbl); list.add(make_move(to - 2 * up, to, DOUBLE_PUSH)); }
    }

    if (type & GEN_NOISY) {
        Bitboard b = others;
        while (b) {
            int from = pop_lsb(b);
            add_targets(list, from, PawnAttacks[us][from] & enemies, CAPTURE);
        }
        if (pos.ep != NO_SQ) {
            Bitboard att = PawnAttacks[them][pos.ep] & others;
            while (att) list.add(make_move(pop_lsb(att), pos.ep, EP_CAPTURE));
        }
    }

    Bitboard b = promoPawns;
    while (b) {
        int from = pop_lsb(b);
        int to = from + up;
        if (empty & sq_bb(to)) {
            if (type & GEN_NOISY) list.add(make_move(from, to, PROMO_Q));
            if (type & GEN_QUIET) {
                list.add(make_move(from, to, PROMO_N));
                list.add(make_move(from, to, PROMO_B));
                list.add(make_move(from, to, PROMO_R));
            }
        }
        Bitboard caps = PawnAttacks[us][from] & enemies;
        while (caps) {
            int t = pop_lsb(caps);
            if (type & GEN_NOISY) list.add(make_move(from, t, PROMO_CAP_Q));
            if (type & GEN_QUIET) {
                list.add(make_move(from, t, PROMO_CAP_N));
                list.add(make_move(from, t, PROMO_CAP_B));
                list.add(make_move(from, t, PROMO_CAP_R));
            }
        }
    }
}

void gen_castling(const Position& pos, MoveList& list) {
    int us = pos.side, them = us ^ 1;
    Bitboard occ = pos.occupied();
    int base = us == WHITE ? 0 : 56;
    int ks = us == WHITE ? WK : BK;
    int qs = us == WHITE ? WQ : BQ;
    if (!(pos.castling & (ks | qs))) return;
    if (pos.is_attacked(base + 4, them)) return;

    if ((pos.castling & ks) && !(occ & (sq_bb(base + 5) | sq_bb(base + 6)))
        && !pos.is_attacked(base + 5, them))
        list.add(make_move(base + 4, base + 6, KING_CASTLE));

    if ((pos.castling & qs) && !(occ & (sq_bb(base + 1) | sq_bb(base + 2) | sq_bb(base + 3)))
        && !pos.is_attacked(base + 3, them))
        list.add(make_move(base + 4, base + 2, QUEEN_CASTLE));
}

} // namespace

void generate_moves(const Position& pos, MoveList& list, GenType type) {
    int us = pos.side;
    Bitboard occ = pos.occupied();
    Bitboard enemies = pos.byColor[us ^ 1];
    Bitboard empty = ~occ;

    gen_pawns(pos, list, type);

    for (int pt = KNIGHT; pt <= KING; pt++) {
        Bitboard b = pos.pieces(us, pt);
        while (b) {
            int from = pop_lsb(b);
            Bitboard att;
            switch (pt) {
                case KNIGHT: att = KnightAttacks[from]; break;
                case BISHOP: att = bishop_attacks(from, occ); break;
                case ROOK: att = rook_attacks(from, occ); break;
                case QUEEN: att = queen_attacks(from, occ); break;
                default: att = KingAttacks[from]; break;
            }
            if (type & GEN_NOISY) add_targets(list, from, att & enemies, CAPTURE);
            if (type & GEN_QUIET) add_targets(list, from, att & empty, QUIET);
        }
    }

    if (type & GEN_QUIET) gen_castling(pos, list);
}

uint64_t perft(Position& pos, int depth) {
    MoveList list;
    generate_moves(pos, list, GEN_ALL);
    uint64_t nodes = 0;
    for (int i = 0; i < list.size; i++) {
        if (!pos.make_move(list.moves[i])) continue;
        nodes += depth > 1 ? perft(pos, depth - 1) : 1;
        pos.unmake_move(list.moves[i]);
    }
    return nodes;
}
