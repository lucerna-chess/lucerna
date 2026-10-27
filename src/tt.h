#pragma once
#include <cstddef>
#include "types.h"

enum Bound : uint8_t { BOUND_NONE = 0, BOUND_UPPER = 1, BOUND_LOWER = 2, BOUND_EXACT = 3 };

// Decoded table entry handed to the search.
struct TTEntry {
    Key key;
    Move move;
    int16_t score;
    int16_t eval;
    uint8_t depth;
    uint8_t genBound; // generation in the upper 6 bits, bound in the lower 2
    int bound() const { return genBound & 3; }
};

// Shared by all search threads. Each slot stores (key ^ data, data) so a slot
// torn by a concurrent write fails the key check instead of returning garbage.
class TranspositionTable {
public:
    ~TranspositionTable();
    void resize(size_t mb);
    void clear();
    void new_search() { generation = uint8_t(generation + 4); }
    bool probe(Key key, TTEntry& out) const;
    void store(Key key, Move move, int score, int eval, int depth, int bound, int ply);
    int hashfull() const;

private:
    struct Slot {
        uint64_t keyXor;
        uint64_t data;
    };
    Slot* table = nullptr;
    size_t count = 0;
    uint8_t generation = 0;
    size_t index(Key key) const { return size_t((unsigned __int128)key * count >> 64); }
};

extern TranspositionTable TT;

// Mate scores are stored relative to the node, not the root.
inline int score_to_tt(int s, int ply) { return s >= MATE_BOUND ? s + ply : s <= -MATE_BOUND ? s - ply : s; }
inline int score_from_tt(int s, int ply) { return s >= MATE_BOUND ? s - ply : s <= -MATE_BOUND ? s + ply : s; }
