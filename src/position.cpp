#include "position.h"
#include <algorithm>
#include <cstring>
#include <sstream>

namespace Zobrist {
Key psq[12][64];
Key castling[16];
Key ep[8];
Key side;

void init() {
    uint64_t s = 0x9E3779B97F4A7C15ULL;
    auto rnd = [&]() {
        s += 0x9E3779B97F4A7C15ULL;
        uint64_t z = s;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    };
    for (auto& p : psq)
        for (auto& k : p) k = rnd();
    for (auto& k : castling) k = rnd();
    for (auto& k : ep) k = rnd();
    side = rnd();
}
}

namespace {
// Castling rights that survive a move touching this square.
int CastleMask[64];
const char* PieceChars = "PNBRQKpnbrqk";

struct CastleMaskInit {
    CastleMaskInit() {
        for (int& m : CastleMask) m = 15;
        CastleMask[4] = 15 & ~(WK | WQ);
        CastleMask[0] = 15 & ~WQ;
        CastleMask[7] = 15 & ~WK;
        CastleMask[60] = 15 & ~(BK | BQ);
        CastleMask[56] = 15 & ~BQ;
        CastleMask[63] = 15 & ~BK;
    }
} castleMaskInit;
}

std::string square_name(int sq) {
    return std::string{char('a' + file_of(sq)), char('1' + rank_of(sq))};
}

std::string move_to_uci(Move m) {
    if (m == NO_MOVE) return "0000";
    std::string s = square_name(from_sq(m)) + square_name(to_sq(m));
    if (is_promo(m)) s += "nbrq"[promo_type(m) - KNIGHT];
    return s;
}

void Position::put_piece(int pc, int sq) {
    board[sq] = pc;
    byType[piece_type(pc)] |= sq_bb(sq);
    byColor[piece_color(pc)] |= sq_bb(sq);
    key ^= Zobrist::psq[pc][sq];
    if (Nnue::active && updateAcc) {
        Nnue::AccDelta& d = accDelta[accTop];
        d.addPc[d.nAdd] = pc; d.addSq[d.nAdd++] = sq;
    }
}

void Position::remove_piece(int sq) {
    int pc = board[sq];
    byType[piece_type(pc)] ^= sq_bb(sq);
    byColor[piece_color(pc)] ^= sq_bb(sq);
    board[sq] = NO_PIECE;
    key ^= Zobrist::psq[pc][sq];
    if (Nnue::active && updateAcc) {
        Nnue::AccDelta& d = accDelta[accTop];
        d.subPc[d.nSub] = pc; d.subSq[d.nSub++] = sq;
    }
}

void Position::move_piece(int from, int to) {
    int pc = board[from];
    Bitboard ft = sq_bb(from) | sq_bb(to);
    byType[piece_type(pc)] ^= ft;
    byColor[piece_color(pc)] ^= ft;
    board[from] = NO_PIECE;
    board[to] = pc;
    key ^= Zobrist::psq[pc][from] ^ Zobrist::psq[pc][to];
    if (Nnue::active && updateAcc) {
        Nnue::AccDelta& d = accDelta[accTop];
        d.addPc[d.nAdd] = pc; d.addSq[d.nAdd++] = to;
        d.subPc[d.nSub] = pc; d.subSq[d.nSub++] = from;
    }
}

bool Position::set_fen(const std::string& fen) {
    std::istringstream ss(fen);
    std::string placement, stm, castle, eps;
    ss >> placement >> stm >> castle >> eps;
    if (placement.empty()) return false;

    for (int& b : board) b = NO_PIECE;
    for (auto& b : byType) b = 0;
    for (auto& b : byColor) b = 0;
    key = 0;
    history.clear();
    keyHistory.clear();
    accStack.resize(std::max<size_t>(accStack.size(), 1));
    accDelta.assign(accStack.size(), Nnue::AccDelta());
    accTop = 0;
    updateAcc = false;

    int sq = 56;
    for (char c : placement) {
        if (c == '/') sq -= 16;
        else if (c >= '1' && c <= '8') sq += c - '0';
        else {
            const char* p = strchr(PieceChars, c);
            if (!p || sq < 0 || sq > 63) return false;
            put_piece(int(p - PieceChars), sq++);
        }
    }
    if (popcount(pieces(WHITE, KING)) != 1 || popcount(pieces(BLACK, KING)) != 1) return false;

    side = (stm == "b") ? BLACK : WHITE;
    castling = 0;
    for (char c : castle) {
        if (c == 'K') castling |= WK;
        if (c == 'Q') castling |= WQ;
        if (c == 'k') castling |= BK;
        if (c == 'q') castling |= BQ;
    }
    // Drop rights that do not match the board so movegen can trust them.
    if (board[4] != make_piece(WHITE, KING)) castling &= ~(WK | WQ);
    if (board[7] != make_piece(WHITE, ROOK)) castling &= ~WK;
    if (board[0] != make_piece(WHITE, ROOK)) castling &= ~WQ;
    if (board[60] != make_piece(BLACK, KING)) castling &= ~(BK | BQ);
    if (board[63] != make_piece(BLACK, ROOK)) castling &= ~BK;
    if (board[56] != make_piece(BLACK, ROOK)) castling &= ~BQ;

    ep = NO_SQ;
    if (eps.size() == 2 && eps[0] >= 'a' && eps[0] <= 'h') {
        int e = (eps[1] - '1') * 8 + (eps[0] - 'a');
        // Only keep an en-passant square that can actually be captured.
        if (e >= 0 && e < 64 && (PawnAttacks[side ^ 1][e] & pieces(side, PAWN))) ep = e;
    }

    halfmove = 0;
    fullmove = 1;
    ss >> halfmove >> fullmove;
    pliesFromNull = 0;

    key ^= Zobrist::castling[castling];
    if (ep != NO_SQ) key ^= Zobrist::ep[file_of(ep)];
    if (side == BLACK) key ^= Zobrist::side;
    updateAcc = true;
    if (Nnue::active) Nnue::refresh(accStack[0], board);
    accDelta[0].computed = true;
    return true;
}

std::string Position::fen() const {
    std::string s;
    for (int r = 7; r >= 0; r--) {
        int empty = 0;
        for (int f = 0; f < 8; f++) {
            int pc = board[r * 8 + f];
            if (pc == NO_PIECE) { empty++; continue; }
            if (empty) { s += char('0' + empty); empty = 0; }
            s += PieceChars[pc];
        }
        if (empty) s += char('0' + empty);
        if (r) s += '/';
    }
    s += side == WHITE ? " w " : " b ";
    if (!castling) s += '-';
    if (castling & WK) s += 'K';
    if (castling & WQ) s += 'Q';
    if (castling & BK) s += 'k';
    if (castling & BQ) s += 'q';
    s += ' ';
    s += ep == NO_SQ ? "-" : square_name(ep);
    s += ' ' + std::to_string(halfmove) + ' ' + std::to_string(fullmove);
    return s;
}

Bitboard Position::attackers_to(int sq, Bitboard occ) const {
    return (PawnAttacks[BLACK][sq] & pieces(WHITE, PAWN))
         | (PawnAttacks[WHITE][sq] & pieces(BLACK, PAWN))
         | (KnightAttacks[sq] & byType[KNIGHT])
         | (KingAttacks[sq] & byType[KING])
         | (bishop_attacks(sq, occ) & (byType[BISHOP] | byType[QUEEN]))
         | (rook_attacks(sq, occ) & (byType[ROOK] | byType[QUEEN]));
}

bool Position::is_attacked(int sq, int by) const {
    Bitboard them = byColor[by];
    Bitboard occ = occupied();
    return (PawnAttacks[by ^ 1][sq] & byType[PAWN] & them)
        || (KnightAttacks[sq] & byType[KNIGHT] & them)
        || (KingAttacks[sq] & byType[KING] & them)
        || (bishop_attacks(sq, occ) & (byType[BISHOP] | byType[QUEEN]) & them)
        || (rook_attacks(sq, occ) & (byType[ROOK] | byType[QUEEN]) & them);
}

void Position::push_acc() {
    if (++accTop == int(accStack.size())) {
        accStack.emplace_back();
        accDelta.emplace_back();
    }
    accDelta[accTop] = Nnue::AccDelta();
}

const Nnue::Accumulator& Position::accumulator() const {
    int j = accTop;
    while (!accDelta[j].computed) j--; // entry 0 is always computed by set_fen
    for (int k = j + 1; k <= accTop; k++) {
        Nnue::apply_delta(accStack[k - 1], accStack[k], accDelta[k]);
        accDelta[k].computed = true;
    }
    return accStack[accTop];
}

bool Position::make_move(Move m) {
    history.push_back({key, castling, ep, halfmove, pliesFromNull, NO_PIECE});
    keyHistory.push_back(key);
    if (Nnue::active) push_acc();
    StateInfo& st = history.back();

    int from = from_sq(m), to = to_sq(m), flag = move_flag(m);
    int us = side, them = us ^ 1;
    int pc = board[from];

    halfmove++;
    pliesFromNull++;
    if (ep != NO_SQ) {
        key ^= Zobrist::ep[file_of(ep)];
        ep = NO_SQ;
    }

    if (flag == EP_CAPTURE) {
        st.captured = board[to ^ 8];
        remove_piece(to ^ 8);
    } else if (is_capture(m)) {
        st.captured = board[to];
        remove_piece(to);
    }
    if (st.captured != NO_PIECE) halfmove = 0;

    move_piece(from, to);

    if (piece_type(pc) == PAWN) {
        halfmove = 0;
        if (flag == DOUBLE_PUSH) {
            int e = to ^ 8;
            if (PawnAttacks[us][e] & pieces(them, PAWN)) {
                ep = e;
                key ^= Zobrist::ep[file_of(ep)];
            }
        } else if (is_promo(m)) {
            remove_piece(to);
            put_piece(make_piece(us, promo_type(m)), to);
        }
    } else if (flag == KING_CASTLE) {
        move_piece(to + 1, to - 1);
    } else if (flag == QUEEN_CASTLE) {
        move_piece(to - 2, to + 1);
    }

    key ^= Zobrist::castling[castling];
    castling &= CastleMask[from] & CastleMask[to];
    key ^= Zobrist::castling[castling];

    side = them;
    key ^= Zobrist::side;
    if (us == BLACK) fullmove++;

    if (is_attacked(king_sq(us), them)) {
        unmake_move(m);
        return false;
    }
    return true;
}

void Position::unmake_move(Move m) {
    StateInfo st = history.back();
    history.pop_back();
    keyHistory.pop_back();

    side ^= 1;
    int us = side;
    if (us == BLACK) fullmove--;
    int from = from_sq(m), to = to_sq(m), flag = move_flag(m);
    updateAcc = false; // the previous accumulator is restored by popping the stack

    if (is_promo(m)) {
        remove_piece(to);
        put_piece(make_piece(us, PAWN), to);
    }
    if (flag == KING_CASTLE) move_piece(to - 1, to + 1);
    else if (flag == QUEEN_CASTLE) move_piece(to + 1, to - 2);

    move_piece(to, from);

    if (flag == EP_CAPTURE) put_piece(st.captured, to ^ 8);
    else if (st.captured != NO_PIECE) put_piece(st.captured, to);

    key = st.key;
    castling = st.castling;
    ep = st.ep;
    halfmove = st.halfmove;
    pliesFromNull = st.pliesFromNull;
    updateAcc = true;
    if (Nnue::active) accTop--;
}

void Position::make_null() {
    history.push_back({key, castling, ep, halfmove, pliesFromNull, NO_PIECE});
    keyHistory.push_back(key);
    if (Nnue::active) push_acc();
    if (ep != NO_SQ) {
        key ^= Zobrist::ep[file_of(ep)];
        ep = NO_SQ;
    }
    halfmove++;
    pliesFromNull = 0;
    side ^= 1;
    key ^= Zobrist::side;
}

void Position::unmake_null() {
    StateInfo st = history.back();
    history.pop_back();
    keyHistory.pop_back();
    side ^= 1;
    key = st.key;
    ep = st.ep;
    halfmove = st.halfmove;
    pliesFromNull = st.pliesFromNull;
    if (Nnue::active) accTop--;
}

// Repeats inside the search tree count after one occurrence; repeats of
// positions from before the root need two (true threefold).
bool Position::is_repetition(int ply) const {
    int n = int(keyHistory.size());
    int limit = std::min(std::min(halfmove, pliesFromNull), n);
    int count = 0;
    for (int i = 4; i <= limit; i += 2) {
        if (keyHistory[n - i] == key) {
            if (i <= ply) return true;
            if (++count == 2) return true;
        }
    }
    return false;
}

bool Position::insufficient_material() const {
    if (byType[PAWN] | byType[ROOK] | byType[QUEEN]) return false;
    return popcount(byType[KNIGHT] | byType[BISHOP]) <= 1;
}
