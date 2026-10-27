#include "tt.h"
#include <atomic>
#include <cstdlib>
#include <cstring>

TranspositionTable TT;

namespace {

// data layout: move 0-15 | score 16-31 | eval 32-47 | depth 48-55 | genBound 56-63
uint64_t pack(Move move, int score, int eval, int depth, int genBound) {
    return uint64_t(move)
         | uint64_t(uint16_t(int16_t(score))) << 16
         | uint64_t(uint16_t(int16_t(eval))) << 32
         | uint64_t(uint8_t(depth)) << 48
         | uint64_t(uint8_t(genBound)) << 56;
}

uint64_t load(const uint64_t& v) {
    return std::atomic_ref<const uint64_t>(v).load(std::memory_order_relaxed);
}

void save(uint64_t& v, uint64_t x) {
    std::atomic_ref<uint64_t>(v).store(x, std::memory_order_relaxed);
}

} // namespace

TranspositionTable::~TranspositionTable() { std::free(table); }

void TranspositionTable::resize(size_t mb) {
    std::free(table);
    count = mb * 1024 * 1024 / sizeof(Slot);
    table = static_cast<Slot*>(std::malloc(count * sizeof(Slot)));
    clear();
}

void TranspositionTable::clear() {
    std::memset(table, 0, count * sizeof(Slot));
    generation = 0;
}

bool TranspositionTable::probe(Key key, TTEntry& out) const {
    const Slot& s = table[index(key)];
    uint64_t data = load(s.data);
    if ((load(s.keyXor) ^ data) != key || data == 0) return false;
    out.key = key;
    out.move = Move(data & 0xFFFF);
    out.score = int16_t(data >> 16);
    out.eval = int16_t(data >> 32);
    out.depth = uint8_t(data >> 48);
    out.genBound = uint8_t(data >> 56);
    return true;
}

void TranspositionTable::store(Key key, Move move, int score, int eval, int depth, int bound, int ply) {
    Slot& s = table[index(key)];
    uint64_t old = load(s.data);
    bool sameKey = (load(s.keyXor) ^ old) == key && old != 0;
    int oldDepth = int(uint8_t(old >> 48));
    bool oldGen = (uint8_t(old >> 56) & 0xFC) != generation;
    if (!sameKey || oldGen || bound == BOUND_EXACT || depth + 3 >= oldDepth) {
        if (move == NO_MOVE && sameKey) move = Move(old & 0xFFFF);
        uint64_t data = pack(move, score_to_tt(score, ply), eval, depth < 0 ? 0 : depth, generation | bound);
        save(s.data, data);
        save(s.keyXor, key ^ data);
    }
}

int TranspositionTable::hashfull() const {
    int used = 0;
    for (size_t i = 0; i < 1000 && i < count; i++) {
        uint64_t data = load(table[i].data);
        if (data && (uint8_t(data >> 56) & 0xFC) == generation) used++;
    }
    return used;
}
