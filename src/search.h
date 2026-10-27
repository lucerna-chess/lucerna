#pragma once
#include <atomic>
#include <chrono>
#include <vector>
#include "position.h"
#include "tt.h"

struct SearchLimits {
    int64_t time[2] = {0, 0};
    bool hasTime[2] = {false, false}; // a clock was given (it may be zero or negative)
    int64_t inc[2] = {0, 0};
    int movestogo = 0;
    int depth = 0;
    uint64_t nodes = 0;
    int64_t movetime = 0;
    bool infinite = false;
};

struct SearchResult {
    Move bestMove = NO_MOVE;
    int score = 0;
    int depth = 0;
    uint64_t nodes = 0;
};

class Searcher {
public:
    Position pos;
    TranspositionTable* tt = &TT;
    std::atomic<bool> stopFlag{false};
    bool silent = false;
    int64_t moveOverhead = 30;

    // Lazy SMP: the main searcher starts these helpers on its position when go()
    // begins and stops them when it finishes. Helpers share only the TT.
    bool isMain = true;
    std::vector<Searcher*> helpers;
    std::atomic<uint64_t> publishedNodes{0}; // node count readable by other threads

    void clear_history();
    SearchResult go(const SearchLimits& limits);

private:
    using Clock = std::chrono::steady_clock;
    Clock::time_point startTime;
    int64_t softLimit = 0, hardLimit = 0;
    uint64_t nodeLimit = 0;
    uint64_t nodes = 0;
    int seldepth = 0;
    int rootDepth = 0;

    Move killers[MAX_PLY][2];
    int history[2][64][64];
    Move counterMoves[12][64];
    int16_t captureHist[12][64][6];       // [moving piece][to][captured type]
    int16_t contHist[12][64][12][64];     // [previous piece][previous to][piece][to]
    int staticEval[MAX_PLY + 4];
    Move excluded[MAX_PLY + 4];           // move skipped by a singular-extension search at this ply
    int doubleExts[MAX_PLY + 4];          // double extensions already made on the path to this ply
    Move moveStack[MAX_PLY + 4];
    int pieceStack[MAX_PLY + 4];
    Move pvTable[MAX_PLY + 1][MAX_PLY + 1];
    int pvLength[MAX_PLY + 1];
    uint64_t rootMoveNodes[64][64]; // nodes spent below each root move, [from][to]

    int64_t elapsed() const;
    bool should_stop();
    void setup_time(const SearchLimits& limits);
    int negamax(int alpha, int beta, int depth, int ply, bool cutNode);
    int qsearch(int alpha, int beta, int ply);
    void score_moves(MoveList& list, Move ttMove, int ply);
    int quiet_score(Move m, int pc, int ply) const;
    void update_history(Move best, const Move* quiets, int quietCount, const Move* noisies, int noisyCount,
                        int depth, int ply);
    void print_info(int depth, int score);
};

void init_search();
bool see_ge(const Position& pos, Move m, int threshold);
