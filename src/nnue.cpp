#include "nnue.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#if defined(__AVX2__)
#include <immintrin.h>
#endif

#ifdef EVALFILE
#ifdef _WIN32
#define NJ_RODATA ".section .rdata,\"dr\"\n"
#else
#define NJ_RODATA ".section .rodata\n"
#endif
asm(NJ_RODATA
    ".global nj_net_data\n"
    ".p2align 6\n"
    "nj_net_data:\n"
    ".incbin \"" EVALFILE "\"\n"
    ".global nj_net_end\n"
    "nj_net_end:\n"
    ".byte 0\n"
    ".text\n");
extern "C" const unsigned char nj_net_data[];
extern "C" const unsigned char nj_net_end[];
#endif

namespace Nnue {

bool active = false;

namespace {

constexpr size_t NET_BYTES = sizeof(int16_t) * (INPUTS * HIDDEN + HIDDEN + OUTPUT_BUCKETS * 2 * HIDDEN)
                           + sizeof(int32_t) * OUTPUT_BUCKETS;
std::unique_ptr<Network> net(new Network());

bool load_bytes(const unsigned char* p, size_t size) {
    if (size != NET_BYTES) return false;
    std::memcpy(net->ftWeights, p, sizeof(net->ftWeights));
    p += sizeof(net->ftWeights);
    std::memcpy(net->ftBiases, p, sizeof(net->ftBiases));
    p += sizeof(net->ftBiases);
    std::memcpy(net->outWeights, p, sizeof(net->outWeights));
    p += sizeof(net->outWeights);
    std::memcpy(net->outBias, p, sizeof(net->outBias));
    active = true;
    return true;
}

} // namespace

bool load_embedded() {
#ifdef EVALFILE
    return load_bytes(nj_net_data, size_t(nj_net_end - nj_net_data));
#else
    return false;
#endif
}

bool load_file(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::unique_ptr<unsigned char[]> buf(new unsigned char[NET_BYTES + 1]);
    size_t n = std::fread(buf.get(), 1, NET_BYTES + 1, f);
    std::fclose(f);
    return load_bytes(buf.get(), n);
}

void add_piece(Accumulator& acc, int pc, int sq) {
    for (int p = 0; p < 2; p++) {
        const int16_t* w = net->ftWeights + feature(p, pc, sq) * HIDDEN;
        int16_t* a = acc.v[p];
        for (int i = 0; i < HIDDEN; i++) a[i] += w[i];
    }
}

void remove_piece(Accumulator& acc, int pc, int sq) {
    for (int p = 0; p < 2; p++) {
        const int16_t* w = net->ftWeights + feature(p, pc, sq) * HIDDEN;
        int16_t* a = acc.v[p];
        for (int i = 0; i < HIDDEN; i++) a[i] -= w[i];
    }
}

void move_piece(Accumulator& acc, int pc, int from, int to) {
    for (int p = 0; p < 2; p++) {
        const int16_t* wf = net->ftWeights + feature(p, pc, from) * HIDDEN;
        const int16_t* wt = net->ftWeights + feature(p, pc, to) * HIDDEN;
        int16_t* a = acc.v[p];
        for (int i = 0; i < HIDDEN; i++) a[i] += wt[i] - wf[i];
    }
}

void apply_delta(const Accumulator& in, Accumulator& out, const AccDelta& d) {
    for (int p = 0; p < 2; p++) {
        int16_t* o = out.v[p];
        // The common cases get a single fused loop; anything else falls back to repeated passes.
        if (d.nAdd == 1 && d.nSub == 1) {
            const int16_t* a = net->ftWeights + feature(p, d.addPc[0], d.addSq[0]) * HIDDEN;
            const int16_t* s = net->ftWeights + feature(p, d.subPc[0], d.subSq[0]) * HIDDEN;
            for (int i = 0; i < HIDDEN; i++) o[i] = int16_t(in.v[p][i] + a[i] - s[i]);
        } else if (d.nAdd == 1 && d.nSub == 2) {
            const int16_t* a = net->ftWeights + feature(p, d.addPc[0], d.addSq[0]) * HIDDEN;
            const int16_t* s1 = net->ftWeights + feature(p, d.subPc[0], d.subSq[0]) * HIDDEN;
            const int16_t* s2 = net->ftWeights + feature(p, d.subPc[1], d.subSq[1]) * HIDDEN;
            for (int i = 0; i < HIDDEN; i++) o[i] = int16_t(in.v[p][i] + a[i] - s1[i] - s2[i]);
        } else {
            std::memcpy(o, in.v[p], sizeof(in.v[p]));
            for (int k = 0; k < d.nAdd; k++) {
                const int16_t* w = net->ftWeights + feature(p, d.addPc[k], d.addSq[k]) * HIDDEN;
                for (int i = 0; i < HIDDEN; i++) o[i] += w[i];
            }
            for (int k = 0; k < d.nSub; k++) {
                const int16_t* w = net->ftWeights + feature(p, d.subPc[k], d.subSq[k]) * HIDDEN;
                for (int i = 0; i < HIDDEN; i++) o[i] -= w[i];
            }
        }
    }
}

void refresh(Accumulator& acc, const int* board) {
    for (int p = 0; p < 2; p++) std::memcpy(acc.v[p], net->ftBiases, sizeof(net->ftBiases));
    for (int sq = 0; sq < 64; sq++)
        if (board[sq] != 12) add_piece(acc, board[sq], sq);
}

namespace {
// Sum over HIDDEN entries of clamp(a[i], 0, QA)^2 * w[i]. Each product fits in
// int32 (255^2 * 2027 < 2^28), so int32 lanes take 8 products before widening.
int64_t screlu_dot(const int16_t* a, const int16_t* w) {
#if defined(__AVX2__)
    const __m256i zero = _mm256_setzero_si256(), qa = _mm256_set1_epi16(QA);
    __m256i total = _mm256_setzero_si256(); // 4 x int64
    for (int c = 0; c < HIDDEN; c += 64) {
        __m256i acc = _mm256_setzero_si256(); // 8 x int32
        for (int i = c; i < c + 64; i += 16) {
            __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a + i));
            v = _mm256_min_epi16(_mm256_max_epi16(v, zero), qa);
            __m256i ww = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(w + i));
            __m256i vlo = _mm256_cvtepi16_epi32(_mm256_castsi256_si128(v));
            __m256i vhi = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(v, 1));
            __m256i wlo = _mm256_cvtepi16_epi32(_mm256_castsi256_si128(ww));
            __m256i whi = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(ww, 1));
            acc = _mm256_add_epi32(acc, _mm256_mullo_epi32(_mm256_mullo_epi32(vlo, vlo), wlo));
            acc = _mm256_add_epi32(acc, _mm256_mullo_epi32(_mm256_mullo_epi32(vhi, vhi), whi));
        }
        total = _mm256_add_epi64(total, _mm256_cvtepi32_epi64(_mm256_castsi256_si128(acc)));
        total = _mm256_add_epi64(total, _mm256_cvtepi32_epi64(_mm256_extracti128_si256(acc, 1)));
    }
    alignas(32) int64_t t[4];
    _mm256_store_si256(reinterpret_cast<__m256i*>(t), total);
    return t[0] + t[1] + t[2] + t[3];
#else
    int64_t sum = 0;
    for (int i = 0; i < HIDDEN; i++) {
        int v = std::clamp<int>(a[i], 0, QA);
        sum += int64_t(v * v) * w[i];
    }
    return sum;
#endif
}
} // namespace

int evaluate(const Accumulator& acc, int stm, int bucket) {
    const int16_t* w = net->outWeights[bucket];
    int64_t sum = screlu_dot(acc.v[stm], w) + screlu_dot(acc.v[stm ^ 1], w + HIDDEN);
    return int((sum / QA + net->outBias[bucket]) * SCALE / (int64_t(QA) * QB));
}

} // namespace Nnue
