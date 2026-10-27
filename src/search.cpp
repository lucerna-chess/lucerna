#include "search.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>
#include <thread>
#include "eval.h"
#include "movegen.h"
#include "tt.h"
#include "uci.h"

namespace {

int LMRTable[64][MAX_MOVES];
constexpr int HISTORY_MAX = 16384;

constexpr int SCORE_TT = 30'000'000;
constexpr int SCORE_GOOD_NOISY = 20'000'000;
constexpr int SCORE_KILLER1 = 15'000'000;
constexpr int SCORE_KILLER2 = 14'000'000;
constexpr int SCORE_COUNTER = 13'000'000;
constexpr int SCORE_BAD_NOISY = -20'000'000;

void pick_move(MoveList& list, int start) {
    int best = start;
    for (int i = start + 1; i < list.size; i++)
        if (list.scores[i] > list.scores[best]) best = i;
    std::swap(list.moves[start], list.moves[best]);
    std::swap(list.scores[start], list.scores[best]);
}

} // namespace

void init_search() {
    for (int d = 0; d < 64; d++)
        for (int m = 0; m < MAX_MOVES; m++)
            LMRTable[d][m] = (d && m) ? int(0.77 + std::log(d) * std::log(m) / 2.36) : 0;
}

// Static exchange evaluation: does this move win at least `threshold`?
bool see_ge(const Position& pos, Move m, int threshold) {
    if (is_castle(m) || is_promo(m) || move_flag(m) == EP_CAPTURE) return threshold <= 0;

    int from = from_sq(m), to = to_sq(m);
    int swap = (is_capture(m) ? SeeValue[piece_type(pos.board[to])] : 0) - threshold;
    if (swap < 0) return false;
    swap = SeeValue[piece_type(pos.board[from])] - swap;
    if (swap <= 0) return true;

    Bitboard occ = pos.occupied() ^ sq_bb(from) ^ sq_bb(to);
    Bitboard attackers = pos.attackers_to(to, occ);
    Bitboard diag = pos.pieces(BISHOP) | pos.pieces(QUEEN);
    Bitboard orth = pos.pieces(ROOK) | pos.pieces(QUEEN);
    int stm = pos.side;
    int res = 1;

    while (true) {
        stm ^= 1;
        attackers &= occ;
        Bitboard stmAttackers = attackers & pos.byColor[stm];
        if (!stmAttackers) break;
        res ^= 1;

        Bitboard bb;
        if ((bb = stmAttackers & pos.pieces(PAWN))) {
            if ((swap = SeeValue[PAWN] - swap) < res) break;
            occ ^= sq_bb(lsb(bb));
            attackers |= bishop_attacks(to, occ) & diag;
        } else if ((bb = stmAttackers & pos.pieces(KNIGHT))) {
            if ((swap = SeeValue[KNIGHT] - swap) < res) break;
            occ ^= sq_bb(lsb(bb));
        } else if ((bb = stmAttackers & pos.pieces(BISHOP))) {
            if ((swap = SeeValue[BISHOP] - swap) < res) break;
            occ ^= sq_bb(lsb(bb));
            attackers |= bishop_attacks(to, occ) & diag;
        } else if ((bb = stmAttackers & pos.pieces(ROOK))) {
            if ((swap = SeeValue[ROOK] - swap) < res) break;
            occ ^= sq_bb(lsb(bb));
            attackers |= rook_attacks(to, occ) & orth;
        } else if ((bb = stmAttackers & pos.pieces(QUEEN))) {
            if ((swap = SeeValue[QUEEN] - swap) < res) break;
            occ ^= sq_bb(lsb(bb));
            attackers |= (bishop_attacks(to, occ) & diag) | (rook_attacks(to, occ) & orth);
        } else {
            // King capture is only legal if the other side has no attackers left.
            return (attackers & ~pos.byColor[stm]) ? res ^ 1 : res;
        }
    }
    return res;
}

void Searcher::clear_history() {
    std::memset(killers, 0, sizeof(killers));
    std::memset(history, 0, sizeof(history));
    std::memset(counterMoves, 0, sizeof(counterMoves));
    std::memset(captureHist, 0, sizeof(captureHist));
    std::memset(contHist, 0, sizeof(contHist));
}

// Butterfly history plus continuation history for the moves one and two plies back.
int Searcher::quiet_score(Move m, int pc, int ply) const {
    int to = to_sq(m);
    int s = history[pos.side][from_sq(m)][to];
    if (ply >= 1 && moveStack[ply - 1] != NO_MOVE)
        s += contHist[pieceStack[ply - 1]][to_sq(moveStack[ply - 1])][pc][to];
    if (ply >= 2 && moveStack[ply - 2] != NO_MOVE)
        s += contHist[pieceStack[ply - 2]][to_sq(moveStack[ply - 2])][pc][to];
    return s;
}

int64_t Searcher::elapsed() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - startTime).count();
}

bool Searcher::should_stop() {
    if ((nodes & 1023) == 0) publishedNodes.store(nodes, std::memory_order_relaxed);
    if (stopFlag.load(std::memory_order_relaxed)) return true;
    if (rootDepth <= 1) return false; // always finish depth 1 so there is a move to play
    if (nodeLimit && nodes >= nodeLimit) { stopFlag = true; return true; }
    if (hardLimit && (nodes & 1023) == 0 && elapsed() >= hardLimit) { stopFlag = true; return true; }
    return false;
}

void Searcher::setup_time(const SearchLimits& limits) {
    softLimit = hardLimit = 0;
    if (limits.infinite) return;
    if (limits.movetime > 0) {
        softLimit = hardLimit = std::max<int64_t>(1, limits.movetime - moveOverhead);
        return;
    }
    int us = pos.side;
    if (!limits.hasTime[us]) return;
    int64_t avail = std::max<int64_t>(1, limits.time[us] - moveOverhead);
    int mtg = limits.movestogo > 0 ? std::min(limits.movestogo, 40) : 20;
    softLimit = avail / mtg + limits.inc[us] * 3 / 4;
    hardLimit = std::min<int64_t>(avail * 3 / 4, softLimit * 3);
    hardLimit = std::max<int64_t>(1, hardLimit);
    softLimit = std::max<int64_t>(1, std::min(softLimit, hardLimit));
}

void Searcher::score_moves(MoveList& list, Move ttMove, int ply) {
    Move counter = NO_MOVE;
    if (ply > 0 && moveStack[ply - 1] != NO_MOVE)
        counter = counterMoves[pieceStack[ply - 1]][to_sq(moveStack[ply - 1])];

    for (int i = 0; i < list.size; i++) {
        Move m = list.moves[i];
        int& s = list.scores[i];
        if (m == ttMove) {
            s = SCORE_TT;
        } else if (!is_quiet(m)) {
            int victimType = is_capture(m) ? piece_type(pos.captured_piece(m)) : PAWN;
            int victim = is_capture(m) ? SeeValue[victimType] : 0;
            int pc = pos.board[from_sq(m)];
            if (is_promo(m) && promo_type(m) != QUEEN)
                s = SCORE_BAD_NOISY - 1'000'000 + victim;
            else {
                int promo = is_promo(m) ? SeeValue[QUEEN] : 0;
                s = (see_ge(pos, m, 0) ? SCORE_GOOD_NOISY : SCORE_BAD_NOISY) + (victim + promo) * 16
                  + captureHist[pc][to_sq(m)][victimType] / 8;
            }
        } else if (m == killers[ply][0]) {
            s = SCORE_KILLER1;
        } else if (m == killers[ply][1]) {
            s = SCORE_KILLER2;
        } else if (m == counter) {
            s = SCORE_COUNTER;
        } else {
            s = quiet_score(m, pos.board[from_sq(m)], ply);
        }
    }
}

namespace {
// Only called when the 50-move counter has run out, so its cost does not matter.
bool has_legal_move(Position& pos) {
    MoveList list;
    generate_moves(pos, list, GEN_ALL);
    for (int i = 0; i < list.size; i++)
        if (pos.make_move(list.moves[i])) {
            pos.unmake_move(list.moves[i]);
            return true;
        }
    return false;
}

template <typename T>
void gravity(T& h, int bonus) {
    h += T(bonus - int(h) * std::abs(bonus) / HISTORY_MAX);
}
}

// Called at a beta cutoff, before the cutoff move is added to the tried lists.
void Searcher::update_history(Move best, const Move* quiets, int quietCount, const Move* noisies, int noisyCount,
                              int depth, int ply) {
    int us = pos.side;
    int bonus = std::min(150 * depth, 1500);

    auto update_quiet = [&](Move m, int b) {
        int pc = pos.board[from_sq(m)], to = to_sq(m);
        gravity(history[us][from_sq(m)][to], b);
        if (ply >= 1 && moveStack[ply - 1] != NO_MOVE)
            gravity(contHist[pieceStack[ply - 1]][to_sq(moveStack[ply - 1])][pc][to], b);
        if (ply >= 2 && moveStack[ply - 2] != NO_MOVE)
            gravity(contHist[pieceStack[ply - 2]][to_sq(moveStack[ply - 2])][pc][to], b);
    };
    auto update_noisy = [&](Move m, int b) {
        int victimType = is_capture(m) ? piece_type(pos.captured_piece(m)) : PAWN;
        gravity(captureHist[pos.board[from_sq(m)]][to_sq(m)][victimType], b);
    };

    if (is_quiet(best)) {
        if (killers[ply][0] != best) {
            killers[ply][1] = killers[ply][0];
            killers[ply][0] = best;
        }
        if (ply > 0 && moveStack[ply - 1] != NO_MOVE)
            counterMoves[pieceStack[ply - 1]][to_sq(moveStack[ply - 1])] = best;
        update_quiet(best, bonus);
        for (int i = 0; i < quietCount; i++) update_quiet(quiets[i], -bonus);
    } else {
        update_noisy(best, bonus);
    }
    // Noisy moves that failed to cut get a malus whichever kind of move did.
    for (int i = 0; i < noisyCount; i++) update_noisy(noisies[i], -bonus);
}

int Searcher::qsearch(int alpha, int beta, int ply) {
    pvLength[ply] = ply;
    if (should_stop()) return 0;
    nodes++;
    if (ply > seldepth) seldepth = ply;

    bool inCheck = pos.in_check();
    if (ply >= MAX_PLY) return inCheck ? 0 : evaluate(pos);
    if ((pos.halfmove >= 100 && !(inCheck && !has_legal_move(pos))) || pos.insufficient_material()) return 0;

    TTEntry tte;
    bool ttHit = tt->probe(pos.key, tte);
    Move ttMove = ttHit ? tte.move : NO_MOVE;
    int ttScore = ttHit ? score_from_tt(tte.score, ply) : SCORE_NONE;
    if (ttHit && beta - alpha == 1) {
        int b = tte.bound();
        if (b == BOUND_EXACT || (b == BOUND_LOWER && ttScore >= beta) || (b == BOUND_UPPER && ttScore <= alpha))
            return ttScore;
    }

    int bestScore, rawEval = SCORE_NONE;
    if (inCheck) {
        bestScore = -MATE + ply;
    } else {
        rawEval = ttHit && tte.eval != SCORE_NONE ? tte.eval : evaluate(pos);
        bestScore = rawEval;
        if (ttHit) {
            int b = tte.bound();
            if ((b == BOUND_LOWER && ttScore > bestScore) || (b == BOUND_UPPER && ttScore < bestScore) || b == BOUND_EXACT)
                bestScore = ttScore;
        }
        if (bestScore >= beta) return bestScore;
        alpha = std::max(alpha, bestScore);
    }

    MoveList list;
    generate_moves(pos, list, inCheck ? GEN_ALL : GEN_NOISY);
    score_moves(list, ttMove, ply);

    Move bestMove = NO_MOVE;
    int legal = 0;
    for (int i = 0; i < list.size; i++) {
        pick_move(list, i);
        Move m = list.moves[i];
        if (!inCheck && !see_ge(pos, m, 0)) continue;

        int pc = pos.board[from_sq(m)];
        if (!pos.make_move(m)) continue;
        legal++;
        moveStack[ply] = m;
        pieceStack[ply] = pc;
        int score = -qsearch(-beta, -alpha, ply + 1);
        pos.unmake_move(m);
        if (stopFlag) return 0;

        if (score > bestScore) {
            bestScore = score;
            if (score > alpha) {
                alpha = score;
                bestMove = m;
                if (alpha >= beta) break;
            }
        }
    }

    if (inCheck && legal == 0) return -MATE + ply;

    tt->store(pos.key, bestMove, bestScore, rawEval, 0, bestScore >= beta ? BOUND_LOWER : BOUND_UPPER, ply);
    return bestScore;
}

int Searcher::negamax(int alpha, int beta, int depth, int ply, bool cutNode) {
    pvLength[ply] = ply;
    if (depth <= 0) return qsearch(alpha, beta, ply);
    if (should_stop()) return 0;

    bool pvNode = beta - alpha > 1;
    bool root = ply == 0;
    nodes++;
    if (ply > seldepth) seldepth = ply;
    bool inCheck = pos.in_check();

    if (!root) {
        if ((pos.halfmove >= 100 && !(inCheck && !has_legal_move(pos))) || pos.insufficient_material()
            || pos.is_repetition(ply))
            return 0;
        if (ply >= MAX_PLY) return inCheck ? 0 : evaluate(pos);
        alpha = std::max(alpha, -MATE + ply);
        beta = std::min(beta, MATE - ply - 1);
        if (alpha >= beta) return alpha;
    }
    if (ply + 1 < MAX_PLY) killers[ply + 1][0] = killers[ply + 1][1] = NO_MOVE;

    Move excludedMove = excluded[ply];
    TTEntry tte;
    bool ttHit = tt->probe(pos.key, tte);
    Move ttMove = ttHit ? tte.move : NO_MOVE;
    int ttScore = ttHit ? score_from_tt(tte.score, ply) : SCORE_NONE;
    if (!pvNode && !excludedMove && ttHit && tte.depth >= depth) {
        int b = tte.bound();
        if (b == BOUND_EXACT || (b == BOUND_LOWER && ttScore >= beta) || (b == BOUND_UPPER && ttScore <= alpha))
            return ttScore;
    }

    int rawEval = SCORE_NONE, eval = SCORE_NONE;
    if (!inCheck) {
        rawEval = ttHit && tte.eval != SCORE_NONE ? tte.eval : evaluate(pos);
        eval = rawEval;
        if (ttHit) {
            int b = tte.bound();
            if ((b == BOUND_LOWER && ttScore > eval) || (b == BOUND_UPPER && ttScore < eval) || b == BOUND_EXACT)
                eval = ttScore;
        }
    }
    staticEval[ply] = rawEval;
    bool improving = !inCheck && ply >= 2 && staticEval[ply - 2] != SCORE_NONE && rawEval > staticEval[ply - 2];

    if (!pvNode && !inCheck && !excludedMove) {
        // Reverse futility pruning
        if (depth <= 8 && eval < MATE_BOUND && eval - 80 * (depth - improving) >= beta)
            return eval;

        // Razoring
        if (depth <= 3 && eval + 200 + 250 * depth <= alpha) {
            int s = qsearch(alpha, alpha + 1, ply);
            if (s <= alpha) return s;
        }

        // Null move pruning
        if (!root && depth >= 3 && eval >= beta && moveStack[ply - 1] != NO_MOVE
            && pos.has_non_pawn(pos.side) && beta > -MATE_BOUND) {
            int R = 3 + depth / 3 + std::min((eval - beta) / 200, 3);
            moveStack[ply] = NO_MOVE;
            pieceStack[ply] = NO_PIECE;
            pos.make_null();
            int s = -negamax(-beta, -beta + 1, depth - R, ply + 1, !cutNode);
            pos.unmake_null();
            if (stopFlag) return 0;
            if (s >= beta) return s >= MATE_BOUND ? beta : s;
        }
    }

    // Internal iterative reduction
    if (depth >= 4 && ttMove == NO_MOVE && !excludedMove && (pvNode || cutNode)) depth--;

    MoveList list;
    generate_moves(pos, list, GEN_ALL);
    score_moves(list, ttMove, ply);

    Move quiets[64], noisies[32];
    int quietCount = 0, noisyCount = 0;
    int bestScore = -INF;
    Move bestMove = NO_MOVE;
    int legal = 0;
    bool skipQuiets = false;

    for (int i = 0; i < list.size; i++) {
        pick_move(list, i);
        Move m = list.moves[i];
        if (m == excludedMove) continue;
        bool quiet = is_quiet(m);
        if (quiet && skipQuiets) continue;

        if (!root && bestScore > -MATE_BOUND) {
            if (quiet) {
                // Late move pruning
                if (depth <= 8 && legal >= (3 + depth * depth) / (2 - improving)) { skipQuiets = true; continue; }
                // Futility pruning
                if (!inCheck && depth <= 8 && eval + 100 + 100 * depth <= alpha) { skipQuiets = true; continue; }
                if (depth <= 8 && !see_ge(pos, m, -60 * depth)) continue;
            } else {
                if (depth <= 6 && !see_ge(pos, m, -100 * depth)) continue;
            }
        }

        // Singular extension: if the TT move is much better than every alternative,
        // search it deeper; if alternatives also beat beta, cut the whole node.
        int extension = 0;
        if (!root && m == ttMove && !excludedMove && depth >= 8 && tte.depth >= depth - 3
            && (tte.bound() & BOUND_LOWER) && std::abs(ttScore) < MATE_BOUND) {
            int sBeta = ttScore - 2 * depth;
            excluded[ply] = m;
            int s = negamax(sBeta - 1, sBeta, (depth - 1) / 2, ply, cutNode);
            excluded[ply] = NO_MOVE;
            if (stopFlag) return 0;
            // Far below the bound: the move is forced, so look two plies deeper (a few times per line at most).
            if (s < sBeta - 20 && !pvNode && doubleExts[ply] < 6) extension = 2;
            else if (s < sBeta) extension = 1;
            else if (sBeta >= beta) return sBeta;
            else if (ttScore >= beta) extension = -1;
        }

        int pc = pos.board[from_sq(m)];
        int hist = quiet ? quiet_score(m, pc, ply) : 0;
        if (!pos.make_move(m)) continue;
        uint64_t nodesBefore = nodes;
        legal++;
        moveStack[ply] = m;
        pieceStack[ply] = pc;

        bool givesCheck = pos.in_check();
        doubleExts[ply + 1] = doubleExts[ply] + (extension == 2);
        int newDepth = depth - 1 + (extension != 0 ? extension : givesCheck ? 1 : 0);
        int score;

        if (legal == 1) {
            score = -negamax(-beta, -alpha, newDepth, ply + 1, pvNode ? false : !cutNode);
        } else {
            int r = 0;
            if (depth >= 3 && legal > 1 + (pvNode ? 1 : 0)) {
                if (quiet) {
                    r = LMRTable[std::min(depth, 63)][std::min(legal, MAX_MOVES - 1)];
                    r += !pvNode;
                    r += cutNode;
                    r += !improving;
                    r -= givesCheck;
                    r -= (m == killers[ply][0] || m == killers[ply][1]);
                    r -= hist / 10000;
                } else if (list.scores[i] < 0) {
                    r = 1;
                }
                r = std::clamp(r, 0, newDepth - 1);
            }

            score = -negamax(-alpha - 1, -alpha, newDepth - r, ply + 1, true);
            if (score > alpha && r > 0)
                score = -negamax(-alpha - 1, -alpha, newDepth, ply + 1, !cutNode);
            if (score > alpha && score < beta)
                score = -negamax(-beta, -alpha, newDepth, ply + 1, false);
        }

        pos.unmake_move(m);
        if (stopFlag) return 0;
        if (root) rootMoveNodes[from_sq(m)][to_sq(m)] += nodes - nodesBefore;

        if (score > bestScore) {
            bestScore = score;
            if (score > alpha) {
                alpha = score;
                bestMove = m;
                pvTable[ply][ply] = m;
                for (int j = ply + 1; j < pvLength[ply + 1]; j++) pvTable[ply][j] = pvTable[ply + 1][j];
                pvLength[ply] = std::max(pvLength[ply + 1], ply + 1);
                if (alpha >= beta) {
                    update_history(m, quiets, quietCount, noisies, noisyCount, depth, ply);
                    break;
                }
            }
        }
        if (quiet && quietCount < 64) quiets[quietCount++] = m;
        else if (!quiet && noisyCount < 32) noisies[noisyCount++] = m;
    }

    if (legal == 0) return excludedMove ? alpha : inCheck ? -MATE + ply : 0;

    if (!excludedMove) {
        int bound = bestScore >= beta ? BOUND_LOWER : (pvNode && bestMove != NO_MOVE) ? BOUND_EXACT : BOUND_UPPER;
        tt->store(pos.key, bestMove, bestScore, rawEval, depth, bound, ply);
    }
    return bestScore;
}

void Searcher::print_info(int depth, int score) {
    if (silent) return;
    int64_t ms = elapsed();
    uint64_t total = nodes;
    for (Searcher* h : helpers) total += h->publishedNodes.load(std::memory_order_relaxed);
    std::ostringstream ss;
    ss << "info depth " << depth << " seldepth " << seldepth << " score ";
    if (std::abs(score) >= MATE_BOUND)
        ss << "mate " << (score > 0 ? (MATE - score + 1) / 2 : -(MATE + score) / 2);
    else
        ss << "cp " << score;
    ss << " nodes " << total << " nps " << total * 1000 / std::max<int64_t>(1, ms)
       << " hashfull " << tt->hashfull() << " time " << ms << " pv";
    for (int i = 0; i < pvLength[0]; i++) ss << ' ' << move_to_uci(pvTable[0][i]);
    sync_print(ss.str());
}

SearchResult Searcher::go(const SearchLimits& limits) {
    startTime = Clock::now();
    nodes = 0;
    publishedNodes = 0;
    rootDepth = 0;
    doubleExts[0] = 0;
    setup_time(limits);
    nodeLimit = limits.nodes;

    std::vector<std::thread> helperThreads;
    if (isMain) {
        tt->new_search(); // only the main thread ages the table
        for (Searcher* h : helpers) {
            h->pos = pos;
            h->tt = tt;
            h->stopFlag = false;
            helperThreads.emplace_back([h] {
                SearchLimits helperLimits;
                helperLimits.infinite = true;
                h->go(helperLimits);
            });
        }
    }
    for (int i = 0; i < MAX_PLY + 4; i++) {
        staticEval[i] = SCORE_NONE;
        excluded[i] = NO_MOVE;
        moveStack[i] = NO_MOVE;
        pieceStack[i] = NO_PIECE;
    }

    SearchResult result;
    MoveList list;
    generate_moves(pos, list, GEN_ALL);
    for (int i = 0; i < list.size; i++) {
        if (pos.make_move(list.moves[i])) {
            pos.unmake_move(list.moves[i]);
            result.bestMove = list.moves[i];
            break;
        }
    }

    int maxDepth = limits.depth > 0 ? std::min(limits.depth, MAX_PLY - 1) : MAX_PLY - 1;
    int score = 0;
    std::memset(rootMoveNodes, 0, sizeof(rootMoveNodes));
    Move prevBest = NO_MOVE;
    int stability = 0;
    for (rootDepth = 1; rootDepth <= maxDepth; rootDepth++) {
        seldepth = 0;
        int delta = 25;
        int alpha = -INF, beta = INF;
        if (rootDepth >= 5 && std::abs(score) < 1000) {
            alpha = std::max(score - delta, -INF);
            beta = std::min(score + delta, INF);
        }
        while (true) {
            int s = negamax(alpha, beta, rootDepth, 0, false);
            if (stopFlag) break;
            if (s <= alpha) {
                beta = (alpha + beta) / 2;
                alpha = std::max(s - delta, -INF);
            } else if (s >= beta) {
                beta = std::min(s + delta, INF);
            } else {
                score = s;
                break;
            }
            delta += delta / 2;
        }
        if (stopFlag) break;

        result.bestMove = pvTable[0][0];
        result.score = score;
        result.depth = rootDepth;
        print_info(rootDepth, score);

        // Think longer while the best move keeps changing, and less when one move
        // has stayed best and takes most of the search effort.
        stability = result.bestMove == prevBest ? std::min(stability + 1, 4) : 0;
        prevBest = result.bestMove;
        double scale = 1.0;
        if (rootDepth >= 7) {
            static constexpr double StabilityScale[5] = {2.2, 1.4, 1.1, 0.9, 0.8};
            double bestFrac = double(rootMoveNodes[from_sq(result.bestMove)][to_sq(result.bestMove)])
                            / double(std::max<uint64_t>(1, nodes));
            scale = StabilityScale[stability] * (1.5 - bestFrac) * 1.35;
        }
        if (softLimit && elapsed() >= int64_t(softLimit * scale)) break;
        if (nodeLimit && nodes >= nodeLimit) break;
    }

    for (Searcher* h : helpers) h->stopFlag = true;
    for (auto& t : helperThreads) t.join();

    publishedNodes = nodes;
    result.nodes = nodes;
    return result;
}
