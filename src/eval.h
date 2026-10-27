#pragma once
#include "position.h"

constexpr int SeeValue[6] = {100, 300, 300, 500, 900, 0};

void init_eval();
// Score from the side to move's point of view, in centipawns.
int evaluate(const Position& pos);
