#include "datagen.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#include "movegen.h"
#include "search.h"

namespace {

const char* StartFen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

std::atomic<uint64_t> totalPositions{0};
std::atomic<uint64_t> totalGames{0};

int legal_moves(Position& pos, MoveList& legal) {
    MoveList list;
    generate_moves(pos, list, GEN_ALL);
    legal.size = 0;
    for (int i = 0; i < list.size; i++)
        if (pos.make_move(list.moves[i])) {
            pos.unmake_move(list.moves[i]);
            legal.add(list.moves[i]);
        }
    return legal.size;
}

PackedPos pack(const Position& pos, int whiteScore) {
    PackedPos p{};
    p.occ = pos.occupied();
    Bitboard b = p.occ;
    int i = 0;
    while (b) {
        int sq = pop_lsb(b);
        p.pieces[i / 2] |= uint8_t(pos.board[sq] << (4 * (i & 1)));
        i++;
    }
    p.score = int16_t(whiteScore);
    p.stm = uint8_t(pos.side);
    return p;
}

void worker(int id, const DatagenConfig& cfg, uint64_t target) {
    auto searcher = std::make_unique<Searcher>();
    TranspositionTable tt;
    tt.resize(16);
    searcher->tt = &tt;
    searcher->silent = true;
    Position& pos = searcher->pos;

    uint64_t s = (cfg.seed * 1000003 + id) * 0x9E3779B97F4A7C15ULL | 1;
    auto rnd = [&]() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; };

    std::string path = cfg.out + "_" + std::to_string(id) + ".bin";
    FILE* f = std::fopen(path.c_str(), "ab");
    if (!f) {
        std::cerr << "cannot open " << path << std::endl;
        return;
    }

    uint64_t written = 0;
    std::vector<PackedPos> buf;
    MoveList legal;

    while (written < target) {
        // Random opening: 8 or 9 plies so both colours start to move.
        pos.set_fen(StartFen);
        int randomPlies = 8 + int(rnd() % 2);
        bool ok = true;
        for (int p = 0; p < randomPlies && ok; p++) {
            if (legal_moves(pos, legal) == 0) ok = false;
            else pos.make_move(legal.moves[rnd() % legal.size]);
        }
        if (!ok || legal_moves(pos, legal) == 0) continue;
        pos.set_fen(pos.fen());

        tt.clear();
        searcher->clear_history();
        SearchLimits limits;
        limits.nodes = cfg.nodes;

        searcher->stopFlag = false;
        if (std::abs(searcher->go(limits).score) > 1000) continue;

        buf.clear();
        int result = -1;
        int winPlies = 0, drawPlies = 0, ply = 0;

        while (result < 0) {
            if (legal_moves(pos, legal) == 0) {
                result = pos.in_check() ? (pos.side == WHITE ? 0 : 2) : 1;
                break;
            }
            if (pos.halfmove >= 100 || pos.insufficient_material() || pos.is_repetition(0)) {
                result = 1;
                break;
            }

            searcher->stopFlag = false;
            SearchResult r = searcher->go(limits);
            int score = r.score;
            int whiteScore = pos.side == WHITE ? score : -score;

            if (std::abs(score) >= MATE_BOUND) {
                result = whiteScore > 0 ? 2 : 0;
                break;
            }
            winPlies = std::abs(score) >= 2000 ? winPlies + 1 : 0;
            if (winPlies >= 4) {
                result = whiteScore > 0 ? 2 : 0;
                break;
            }
            drawPlies = (ply >= 80 && std::abs(score) <= 10) ? drawPlies + 1 : 0;
            if (drawPlies >= 10) {
                result = 1;
                break;
            }

            if (!pos.in_check() && is_quiet(r.bestMove))
                buf.push_back(pack(pos, whiteScore));

            pos.make_move(r.bestMove);
            ply++;
        }

        for (auto& p : buf) p.result = uint8_t(result);
        std::fwrite(buf.data(), sizeof(PackedPos), buf.size(), f);
        std::fflush(f);
        written += buf.size();
        totalPositions += buf.size();
        totalGames++;
    }
    std::fclose(f);
}

std::atomic<uint64_t> totalRead{0};

void rescore_worker(int id, const RescoreConfig& cfg) {
    auto searcher = std::make_unique<Searcher>();
    TranspositionTable tt;
    tt.resize(16);
    searcher->tt = &tt;
    searcher->silent = true;
    Position& pos = searcher->pos;

    std::string inPath = cfg.in + "_" + std::to_string(id) + ".txt";
    std::string outPath = cfg.out + "_" + std::to_string(id) + ".bin";
    FILE* in = std::fopen(inPath.c_str(), "r");
    FILE* out = std::fopen(outPath.c_str(), "wb");
    if (!in || !out) {
        std::cerr << "cannot open " << (in ? outPath : inPath) << std::endl;
        if (in) std::fclose(in);
        if (out) std::fclose(out);
        return;
    }

    SearchLimits limits;
    limits.nodes = cfg.nodes;
    std::vector<PackedPos> buf;
    std::string lastGame;
    char line[256];
    while (std::fgets(line, sizeof line, in)) {
        std::string s(line);
        size_t a = s.find(';'), b = s.find(';', a + 1);
        if (a == std::string::npos || b == std::string::npos) continue;
        std::string fen = s.substr(0, a), game = s.substr(b + 1);
        int result = std::atoi(s.c_str() + a + 1);
        if (result < 0 || result > 2) continue;
        totalRead++;

        if (game != lastGame) {
            // A new game: forget the previous one, as self-play datagen does.
            tt.clear();
            searcher->clear_history();
            lastGame = game;
        }
        pos.set_fen(fen);
        if (pos.in_check()) continue;

        searcher->stopFlag = false;
        SearchResult r = searcher->go(limits);
        if (r.bestMove == NO_MOVE || !is_quiet(r.bestMove) || std::abs(r.score) >= MATE_BOUND) continue;

        PackedPos p = pack(pos, pos.side == WHITE ? r.score : -r.score);
        p.result = uint8_t(result);
        buf.push_back(p);
        if (buf.size() >= 4096) {
            std::fwrite(buf.data(), sizeof(PackedPos), buf.size(), out);
            std::fflush(out);
            totalPositions += buf.size();
            buf.clear();
        }
    }
    std::fwrite(buf.data(), sizeof(PackedPos), buf.size(), out);
    totalPositions += buf.size();
    std::fclose(in);
    std::fclose(out);
}

} // namespace

void run_rescore(const RescoreConfig& cfg) {
    std::filesystem::path outDir = std::filesystem::path(cfg.out).parent_path();
    if (!outDir.empty()) std::filesystem::create_directories(outDir);

    std::vector<std::thread> threads;
    for (int i = 0; i < cfg.threads; i++) threads.emplace_back(rescore_worker, i, std::cref(cfg));

    auto t0 = std::chrono::steady_clock::now();
    std::atomic<bool> done{false};
    std::thread reporter([&]() {
        while (!done) {
            for (int i = 0; i < 300 && !done; i++) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            std::cout << "rescore: " << totalRead << " read, " << totalPositions << " kept, "
                      << uint64_t(totalRead / std::max(sec, 1.0)) << " pos/s, " << int(sec) << " s" << std::endl;
        }
    });
    for (auto& t : threads) t.join();
    done = true;
    reporter.join();
    std::cout << "rescore finished: " << totalPositions << " positions kept of " << totalRead << std::endl;
}

void run_datagen(const DatagenConfig& cfg) {
    std::filesystem::path outDir = std::filesystem::path(cfg.out).parent_path();
    if (!outDir.empty()) std::filesystem::create_directories(outDir);

    uint64_t perThread = (cfg.positions + cfg.threads - 1) / cfg.threads;
    std::vector<std::thread> threads;
    for (int i = 0; i < cfg.threads; i++) threads.emplace_back(worker, i, std::cref(cfg), perThread);

    auto t0 = std::chrono::steady_clock::now();
    std::atomic<bool> done{false};
    std::thread reporter([&]() {
        while (!done) {
            for (int i = 0; i < 300 && !done; i++) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            uint64_t n = totalPositions;
            std::cout << "datagen: " << n << " positions, " << totalGames << " games, "
                      << uint64_t(n / std::max(sec, 1.0)) << " pos/s, " << int(sec) << " s" << std::endl;
        }
    });
    for (auto& t : threads) t.join();
    done = true;
    reporter.join();
    std::cout << "datagen finished" << std::endl;
}
