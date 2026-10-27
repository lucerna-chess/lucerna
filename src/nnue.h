#pragma once
#include <string>
#include "types.h"

namespace Nnue {

constexpr int INPUTS = 768;
#ifndef LUCERNA_HIDDEN
#define LUCERNA_HIDDEN 512
#endif
constexpr int HIDDEN = LUCERNA_HIDDEN; // hidden width per side; set with make HIDDEN=1024
static_assert(HIDDEN % 16 == 0, "the AVX2 loops work on 16 values at a time");
constexpr int OUTPUT_BUCKETS = 8;
constexpr int QA = 255;
constexpr int QB = 1024; // output weights are small; a fine step keeps quantisation error low
constexpr int SCALE = 400;

struct alignas(64) Accumulator {
    int16_t v[2][HIDDEN];
};

struct alignas(64) Network {
    int16_t ftWeights[INPUTS * HIDDEN];
    int16_t ftBiases[HIDDEN];
    int16_t outWeights[OUTPUT_BUCKETS][2 * HIDDEN];
    int32_t outBias[OUTPUT_BUCKETS];
};

// Output layer chosen by how many pieces are on the board.
inline int output_bucket(int pieceCount) {
    int b = (pieceCount - 2) / 4;
    return b < OUTPUT_BUCKETS ? b : OUTPUT_BUCKETS - 1;
}

// True once a network is loaded; the hand-written eval is used otherwise.
extern bool active;

bool load_embedded();
bool load_file(const std::string& path);

inline int feature(int perspective, int pc, int sq) {
    int c = piece_color(pc), t = piece_type(pc);
    return perspective == WHITE ? c * 384 + t * 64 + sq : (c ^ 1) * 384 + t * 64 + (sq ^ 56);
}

void add_piece(Accumulator& acc, int pc, int sq);
void remove_piece(Accumulator& acc, int pc, int sq);
void move_piece(Accumulator& acc, int pc, int from, int to);
void refresh(Accumulator& acc, const int* board);

// Pieces added and removed by one move (castling and promotions need up to 3 each).
struct AccDelta {
    int addPc[4], addSq[4], subPc[4], subSq[4];
    int nAdd = 0, nSub = 0;
    bool computed = false;
};
// out = in + added features - removed features, in one pass per perspective.
void apply_delta(const Accumulator& in, Accumulator& out, const AccDelta& d);
int evaluate(const Accumulator& acc, int stm, int bucket);

} // namespace Nnue
