#pragma once
#include <string>
#include <vector>
#include "bitboard.h"
#include "nnue.h"

namespace Zobrist {
extern Key psq[12][64];
extern Key castling[16];
extern Key ep[8];
extern Key side;
void init();
}

struct StateInfo {
    Key key;
    int castling;
    int ep;
    int halfmove;
    int pliesFromNull;
    int captured;
};

class Position {
public:
    int board[64];
    Bitboard byType[6];
    Bitboard byColor[2];
    int side = WHITE;
    int castling = 0;
    int ep = NO_SQ;
    int halfmove = 0;
    int fullmove = 1;
    int pliesFromNull = 0;
    Key key = 0;

    std::vector<StateInfo> history;
    std::vector<Key> keyHistory;
    // One accumulator per ply while a network is active. Moves only record their
    // piece changes in accDelta; accumulator() computes the entries lazily.
    // The vectors only grow; accTop is the current ply's entry.
    mutable std::vector<Nnue::Accumulator> accStack;
    mutable std::vector<Nnue::AccDelta> accDelta;
    int accTop = 0;
    const Nnue::Accumulator& accumulator() const;

    bool set_fen(const std::string& fen);
    std::string fen() const;

    Bitboard pieces(int c, int pt) const { return byType[pt] & byColor[c]; }
    Bitboard pieces(int pt) const { return byType[pt]; }
    Bitboard occupied() const { return byColor[WHITE] | byColor[BLACK]; }
    int king_sq(int c) const { return lsb(pieces(c, KING)); }

    Bitboard attackers_to(int sq, Bitboard occ) const;
    bool is_attacked(int sq, int by) const;
    bool in_check() const { return is_attacked(king_sq(side), side ^ 1); }
    bool has_non_pawn(int c) const {
        return byColor[c] & ~(byType[PAWN] | byType[KING]);
    }

    // Returns false (and leaves the position unchanged) if the move is illegal.
    bool make_move(Move m);
    void unmake_move(Move m);
    void make_null();
    void unmake_null();

    bool is_repetition(int ply) const;
    bool insufficient_material() const;
    int moved_piece(Move m) const { return board[from_sq(m)]; }
    int captured_piece(Move m) const {
        return move_flag(m) == EP_CAPTURE ? make_piece(side ^ 1, PAWN) : board[to_sq(m)];
    }

private:
    bool updateAcc = true;
    void push_acc();
    void put_piece(int pc, int sq);
    void remove_piece(int sq);
    void move_piece(int from, int to);
};

std::string move_to_uci(Move m);
std::string square_name(int sq);
