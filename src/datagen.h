#pragma once
#include <cstdint>
#include <string>

// One training position, 32 bytes. Pieces are listed in occupancy order
// (lowest square first), one 4-bit piece code each. Score and result are
// from White's point of view; result is 0 = black win, 1 = draw, 2 = white win.
struct PackedPos {
    uint64_t occ;
    uint8_t pieces[16];
    int16_t score;
    uint8_t result;
    uint8_t stm;
    uint8_t pad[4];
};
static_assert(sizeof(PackedPos) == 32);

struct DatagenConfig {
    int threads = 1;
    uint64_t positions = 1'000'000;
    uint64_t nodes = 5000;
    uint64_t seed = 1;
    std::string out = "data/run";
};

void run_datagen(const DatagenConfig& cfg);

// Scores positions taken from other games with Lucerna's own search.
// Input: IN_<i>.txt per thread, one "FEN;result;game" line per position
// (result from White's view: 0, 1 or 2; game = any id, positions of one game
// are consecutive). Output: OUT_<i>.bin in the PackedPos format.
struct RescoreConfig {
    int threads = 1;
    uint64_t nodes = 5000;
    std::string in = "data/ext";
    std::string out = "data/ext";
};

void run_rescore(const RescoreConfig& cfg);
