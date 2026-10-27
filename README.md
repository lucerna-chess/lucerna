<p align="center"><img src="docs/logo-horizontal.svg" width="420" alt="Lucerna"></p>

# Lucerna

Lucerna (Latin for "lamp") is a UCI chess engine written from scratch in C++20.
All code and every network are original. The 1.0 network was trained on Lucerna's own
self-play games, on positions from human games that Lucerna scored itself, and on the
open Leela Chess Zero training data (see [Training data](#training-data)).

## Features

**Board and move generation**
- Bitboards with magic sliding-piece attacks
- Pseudo-legal generation with legality checked on make, verified by perft

**Search**
- Principal variation search inside iterative deepening, with aspiration windows
- Shared transposition table (lockless, xor-verified) and Lazy SMP for multiple threads
- Pruning: null move, reverse futility, razoring, late move pruning, futility, SEE pruning
- Late move reductions, internal iterative reductions, check extension
- Singular extensions with double and negative extensions and multi-cut
- Move ordering: TT move, MVV/LVA with capture history, killers, counter moves,
  butterfly history and 1-/2-ply continuation history
- Quiescence search with SEE filtering
- Time management that adapts to best-move stability and node distribution

**Evaluation**
- NNUE: (768 → 512) × 2 perspectives, SCReLU activation, 8 output buckets chosen by piece count
- Incrementally updated, lazily computed accumulators; AVX2 output layer
- The network is embedded in the binary

## Building

Requires a C++20 compiler (GCC or Clang) and `make`.

```
make                      # native build for this CPU
make ARCH=x86-64-v3       # AVX2 build for distribution
make ARCH=x86-64-v2       # SSE4.2/POPCNT build
make ARCH=x86-64          # baseline build
```

The network in `nets/` is embedded at compile time. If the build fails while
linking statically on Linux, use `make LDFLAGS=`.

## UCI options

| Option | Default | Range | Description |
|---|---|---|---|
| Hash | 16 | 1–65536 | Transposition table size in MB |
| Threads | 1 | 1–64 | Search threads |
| Move Overhead | 30 | 0–5000 | Milliseconds reserved per move for GUI and network delays |
| EvalFile | `<embedded>` | path | Load a different network file |

Extra commands: `bench [depth]`, `d` (show board), `eval`, `go perft N`.

## How the network was trained

1. Self-play games at 5000 nodes per move from randomised, balanced openings. The first
   generation started from a simple hand-written evaluation; every later generation
   played with the previous network.
2. Each position stores a score and the game result. The training target is 75% of the
   sigmoid of the score and 25% of the result. Positions in check and positions whose
   best move is a capture or promotion are skipped.
3. A network is trained (PyTorch, cosine learning rate) and kept only if it beats the
   previous best in a match. Tools for data generation, conversion, training and testing
   are in `tools/`.

## Training data

The 1.0 network (`nets/default.nnue`, 80 epochs) was trained on about 270 million positions:

| Source | Positions | Scores from |
|---|---|---|
| Lucerna self-play, generations 7–11 | 125 M | Lucerna |
| [Leela Chess Zero training data](https://storage.lczero.org/files/training_data/) (test80, standard-chess games only) | 109 M | Lc0 search value |
| [Lichess Elite Database](https://database.nikonoel.fr/) (November 2025, from the CC0 [Lichess database](https://database.lichess.org/)) | 13.6 M | Lucerna, rescored with `lucerna rescore` |

The Leela Chess Zero training data is made available under the
[Open Database License (ODbL 1.0)](http://opendatacommons.org/licenses/odbl/1.0/), with its
contents under the [Database Contents License (DbCL 1.0)](http://opendatacommons.org/licenses/dbcl/1.0/).
Thank you to the Leela Chess Zero project, Lichess, and the maintainer of the Lichess Elite
Database for publishing this data.

## Testing

Search changes are accepted only after an SPRT test (cutechess-cli, 8+0.08,
elo0=0, elo1=5) against the current version.

## License

MIT, see [LICENSE](LICENSE).

## Authors

- Tessera: project owner, direction and testing
- Claude (Anthropic): co-author, wrote the engine, training pipeline and tests
