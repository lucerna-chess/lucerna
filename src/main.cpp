#include <string>
#include "bitboard.h"
#include "datagen.h"
#include "eval.h"
#include "position.h"
#include "search.h"
#include "tt.h"
#include "uci.h"

int main(int argc, char** argv) {
    init_bitboards();
    Zobrist::init();
    init_eval();
    init_search();
    Nnue::load_embedded();
    TT.resize(16);

    if (argc > 1 && std::string(argv[1]) == "bench") {
        run_bench(argc > 2 ? std::stoi(argv[2]) : 12);
        return 0;
    }
    // lucerna datagen threads N positions N nodes N seed N out PATH
    if (argc > 1 && std::string(argv[1]) == "datagen") {
        DatagenConfig cfg;
        for (int i = 2; i + 1 < argc; i += 2) {
            std::string k = argv[i], v = argv[i + 1];
            if (k == "threads") cfg.threads = std::stoi(v);
            else if (k == "positions") cfg.positions = std::stoull(v);
            else if (k == "nodes") cfg.nodes = std::stoull(v);
            else if (k == "seed") cfg.seed = std::stoull(v);
            else if (k == "out") cfg.out = v;
        }
        run_datagen(cfg);
        return 0;
    }
    // lucerna rescore threads N nodes N in PREFIX out PREFIX
    if (argc > 1 && std::string(argv[1]) == "rescore") {
        RescoreConfig cfg;
        for (int i = 2; i + 1 < argc; i += 2) {
            std::string k = argv[i], v = argv[i + 1];
            if (k == "threads") cfg.threads = std::stoi(v);
            else if (k == "nodes") cfg.nodes = std::stoull(v);
            else if (k == "in") cfg.in = v;
            else if (k == "out") cfg.out = v;
        }
        run_rescore(cfg);
        return 0;
    }
    uci_loop();
    return 0;
}
