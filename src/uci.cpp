#include "uci.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>
#include "eval.h"
#include "movegen.h"
#include "search.h"
#include "tt.h"

void sync_print(const std::string& line) {
    static std::mutex m;
    std::lock_guard<std::mutex> lock(m);
    std::cout << line << std::endl;
}

namespace {

const char* StartFen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

const char* BenchFens[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    "r1bq1rk1/pp2bppp/2n2n2/3p4/3P4/2NBBN2/PP3PPP/R2Q1RK1 w - - 0 10",
    "2r2rk1/1bqnbppp/p2ppn2/1p6/3NP3/1BN1BP2/PPPQ2PP/2KR3R w - - 0 14",
    "r1bqkb1r/pp3ppp/2nppn2/8/3NP3/2N5/PPP2PPP/R1BQKB1R w KQkq - 0 6",
    "6k1/5pp1/4p2p/3pP3/1r1P4/5PP1/2R3KP/8 w - - 0 35",
    "8/5pk1/6p1/4P2p/5P1P/6P1/4K3/8 w - - 0 50",
    "4r1k1/pp3pp1/2p4p/3n4/3P4/1B3N1P/PP3PP1/4R1K1 b - - 0 25",
    "r2q1rk1/ppp2ppp/2np1n2/2b1p1B1/2B1P1b1/2NP1N2/PPP2PPP/R2Q1RK1 w - - 4 8",
    "3r2k1/p4ppp/1p6/2p5/2P1R3/1P4P1/P4P1P/6K1 w - - 0 30",
    "8/8/4k3/8/2p5/8/B2K4/8 w - - 0 60",
    "r1b1k2r/ppppnppp/2n2q2/2b5/3NP3/2P1B3/PP3PPP/RN1QKB1R w KQkq - 0 7",
};

constexpr int MAX_THREADS = 64;

Searcher searcher;
std::vector<std::unique_ptr<Searcher>> helperPool; // Threads - 1 helper searchers
std::thread searchThread;

void set_threads(int n) {
    helperPool.clear();
    searcher.helpers.clear();
    for (int i = 1; i < n; i++) {
        auto h = std::make_unique<Searcher>();
        h->isMain = false;
        h->silent = true;
        searcher.helpers.push_back(h.get());
        helperPool.push_back(std::move(h));
    }
}

void clear_all_history() {
    searcher.clear_history();
    for (auto& h : helperPool) h->clear_history();
}

void wait_search() {
    if (searchThread.joinable()) searchThread.join();
}

void stop_search() {
    searcher.stopFlag = true;
    wait_search();
}

Move parse_move(Position& pos, const std::string& s) {
    MoveList list;
    generate_moves(pos, list, GEN_ALL);
    for (int i = 0; i < list.size; i++) {
        if (move_to_uci(list.moves[i]) != s) continue;
        if (!pos.make_move(list.moves[i])) return NO_MOVE;
        pos.unmake_move(list.moves[i]);
        return list.moves[i];
    }
    return NO_MOVE;
}

void cmd_position(std::istringstream& is) {
    std::string token, fen;
    is >> token;
    if (token == "startpos") {
        fen = StartFen;
        is >> token;
    } else if (token == "fen") {
        while (is >> token && token != "moves") fen += token + " ";
    } else {
        return;
    }
    Position& pos = searcher.pos;
    if (!pos.set_fen(fen)) pos.set_fen(StartFen);
    if (token == "moves") {
        while (is >> token) {
            Move m = parse_move(pos, token);
            if (m == NO_MOVE) break;
            pos.make_move(m);
        }
    }
}

void cmd_go(std::istringstream& is) {
    SearchLimits limits;
    std::string token;
    int perftDepth = 0;
    while (is >> token) {
        if (token == "wtime") { is >> limits.time[WHITE]; limits.hasTime[WHITE] = true; }
        else if (token == "btime") { is >> limits.time[BLACK]; limits.hasTime[BLACK] = true; }
        else if (token == "winc") is >> limits.inc[WHITE];
        else if (token == "binc") is >> limits.inc[BLACK];
        else if (token == "movestogo") is >> limits.movestogo;
        else if (token == "depth") is >> limits.depth;
        else if (token == "nodes") is >> limits.nodes;
        else if (token == "movetime") is >> limits.movetime;
        else if (token == "infinite") limits.infinite = true;
        else if (token == "perft") is >> perftDepth;
    }

    stop_search();
    if (perftDepth > 0) {
        auto t0 = std::chrono::steady_clock::now();
        uint64_t n = perft(searcher.pos, perftDepth);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        std::cout << "nodes " << n << " time " << ms << std::endl;
        return;
    }

    searcher.stopFlag = false;
    searchThread = std::thread([limits]() {
        SearchResult r = searcher.go(limits);
        sync_print("bestmove " + move_to_uci(r.bestMove));
    });
}

void cmd_setoption(std::istringstream& is) {
    std::string token, name, value;
    is >> token; // "name"
    while (is >> token && token != "value") name += (name.empty() ? "" : " ") + token;
    std::getline(is >> std::ws, value);
    if (!value.empty() && value.back() == '\r') value.pop_back();
    if (name == "EvalFile") {
        if (value != "<embedded>" && !Nnue::load_file(value))
            sync_print("info string failed to load " + value);
        searcher.pos.set_fen(searcher.pos.fen());
        return;
    }
    int v;
    try { v = std::stoi(value); } catch (...) { return; }
    if (name == "Hash") TT.resize(std::clamp(v, 1, 65536));
    else if (name == "Move Overhead") searcher.moveOverhead = std::clamp(v, 0, 5000);
    else if (name == "Threads") set_threads(std::clamp(v, 1, MAX_THREADS));
}

// Prints `count` balanced openings: random plies from the start position,
// kept only if a shallow search says the position is roughly equal.
void cmd_genfens(std::istringstream& is) {
    int count = 1000, plies = 8, maxScore = 80;
    uint64_t seed = 1;
    std::string token;
    while (is >> token) {
        if (token == "count") is >> count;
        else if (token == "seed") is >> seed;
        else if (token == "plies") is >> plies;
        else if (token == "maxscore") is >> maxScore;
    }
    uint64_t s = seed * 0x9E3779B97F4A7C15ULL + 1;
    auto rnd = [&]() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; };

    searcher.silent = true;
    int made = 0;
    while (made < count) {
        Position& pos = searcher.pos;
        pos.set_fen(StartFen);
        bool ok = true;
        for (int p = 0; p < plies && ok; p++) {
            MoveList list, legal;
            generate_moves(pos, list, GEN_ALL);
            for (int i = 0; i < list.size; i++)
                if (pos.make_move(list.moves[i])) { pos.unmake_move(list.moves[i]); legal.add(list.moves[i]); }
            if (legal.size == 0) ok = false;
            else pos.make_move(legal.moves[rnd() % legal.size]);
        }
        if (!ok) continue;
        std::string fen = pos.fen();
        pos.set_fen(fen); // drop move history so the search sees a fresh root
        MoveList list;
        generate_moves(pos, list, GEN_ALL);
        int legalCount = 0;
        for (int i = 0; i < list.size; i++)
            if (pos.make_move(list.moves[i])) { pos.unmake_move(list.moves[i]); legalCount++; }
        if (legalCount == 0) continue;

        TT.clear();
        searcher.stopFlag = false;
        SearchLimits limits;
        limits.depth = 8;
        if (std::abs(searcher.go(limits).score) > maxScore) continue;
        std::cout << fen << "\n";
        made++;
    }
    std::cout.flush();
    searcher.silent = false;
}

} // namespace

void run_bench(int depth) {
    uint64_t totalNodes = 0;
    auto t0 = std::chrono::steady_clock::now();
    // Bench is single-threaded so its node count stays reproducible.
    std::vector<Searcher*> savedHelpers;
    savedHelpers.swap(searcher.helpers);
    searcher.silent = true;
    for (const char* fen : BenchFens) {
        searcher.pos.set_fen(fen);
        TT.clear();
        searcher.clear_history();
        searcher.stopFlag = false;
        SearchLimits limits;
        limits.depth = depth;
        totalNodes += searcher.go(limits).nodes;
    }
    searcher.silent = false;
    searcher.helpers.swap(savedHelpers);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "Bench: " << totalNodes << " nodes " << totalNodes * 1000 / std::max<int64_t>(1, ms) << " nps" << std::endl;
}

void uci_loop() {
    searcher.pos.set_fen(StartFen);
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream is(line);
        std::string cmd;
        is >> cmd;
        if (cmd == "uci") {
            std::cout << "id name " << ENGINE_NAME << ' ' << ENGINE_VERSION << "\n"
                      << "id author " << ENGINE_AUTHOR << "\n"
                      << "option name Hash type spin default 16 min 1 max 65536\n"
                      << "option name Threads type spin default 1 min 1 max " << MAX_THREADS << "\n"
                      << "option name Move Overhead type spin default 30 min 0 max 5000\n"
                      << "option name EvalFile type string default <embedded>\n"
                      << "uciok" << std::endl;
        } else if (cmd == "isready") {
            sync_print("readyok");
        } else if (cmd == "ucinewgame") {
            stop_search();
            TT.clear();
            clear_all_history();
        } else if (cmd == "position") {
            stop_search();
            cmd_position(is);
        } else if (cmd == "go") {
            cmd_go(is);
        } else if (cmd == "stop") {
            stop_search();
        } else if (cmd == "quit") {
            break;
        } else if (cmd == "setoption") {
            stop_search();
            cmd_setoption(is);
        } else if (cmd == "d") {
            std::cout << searcher.pos.fen() << std::endl;
        } else if (cmd == "eval") {
            std::cout << "eval " << evaluate(searcher.pos) << std::endl;
        } else if (cmd == "bench") {
            int depth = 12;
            is >> depth;
            stop_search();
            run_bench(depth);
        } else if (cmd == "nnuecheck") {
            // Plays random games and compares the incremental accumulator with a full refresh.
            stop_search();
            uint64_t s = 12345;
            int bad = 0, checked = 0;
            for (int game = 0; game < 200 && Nnue::active; game++) {
                Position& pos = searcher.pos;
                pos.set_fen(StartFen);
                for (int ply = 0; ply < 200; ply++) {
                    MoveList list;
                    generate_moves(pos, list, GEN_ALL);
                    Move legal[MAX_MOVES];
                    int n = 0;
                    for (int i = 0; i < list.size; i++)
                        if (pos.make_move(list.moves[i])) { pos.unmake_move(list.moves[i]); legal[n++] = list.moves[i]; }
                    if (!n) break;
                    s ^= s << 13; s ^= s >> 7; s ^= s << 17;
                    pos.make_move(legal[s % n]);
                    Nnue::Accumulator fresh;
                    Nnue::refresh(fresh, pos.board);
                    checked++;
                    if (std::memcmp(&fresh, &pos.accumulator(), sizeof(fresh))) bad++;
                }
            }
            std::cout << "nnuecheck: " << checked << " positions, " << bad << " mismatches"
                      << (Nnue::active ? "" : " (no network loaded)") << std::endl;
            searcher.pos.set_fen(StartFen);
        } else if (cmd == "genfens") {
            stop_search();
            cmd_genfens(is);
        }
    }
    stop_search();
}
