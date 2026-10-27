#pragma once

#define ENGINE_NAME "Lucerna"
#define ENGINE_VERSION "1.0"
#define ENGINE_AUTHOR "Tessera & Claude (Anthropic)"

#include <string>

// Writes one complete line to stdout; safe to call from any thread.
void sync_print(const std::string& line);

void uci_loop();
void run_bench(int depth);
