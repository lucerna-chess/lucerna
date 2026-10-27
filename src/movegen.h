#pragma once
#include "position.h"

// NOISY: captures and queen promotions. QUIET: everything else.
enum GenType { GEN_NOISY = 1, GEN_QUIET = 2, GEN_ALL = 3 };

void generate_moves(const Position& pos, MoveList& list, GenType type);
uint64_t perft(Position& pos, int depth);
