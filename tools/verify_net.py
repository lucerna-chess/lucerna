"""Compares the float model in a training checkpoint with the engine's quantised eval.

Usage: python tools/verify_net.py <engine.exe> <net.nnue> <checkpoint.pt> <fens.epd>
"""
import subprocess
import sys

import torch

sys.path.insert(0, __import__("os").path.dirname(__file__))
from train import BUCKETS, SCALE, Net  # noqa: E402

PIECES = "PNBRQKpnbrqk"


def features(fen):
    board, stm = fen.split()[:2]
    w, b = torch.zeros(1, 768), torch.zeros(1, 768)
    sq, count = 56, 0
    for ch in board:
        if ch == "/":
            sq -= 16
        elif ch.isdigit():
            sq += int(ch)
        else:
            p = PIECES.index(ch)
            color, ptype = p // 6, p % 6
            w[0, color * 384 + ptype * 64 + sq] = 1
            b[0, (1 - color) * 384 + ptype * 64 + (sq ^ 56)] = 1
            sq += 1
            count += 1
    bucket = torch.tensor([min((count - 2) // 4, BUCKETS - 1)])
    return (w, b, bucket) if stm == "w" else (b, w, bucket)


def main():
    exe, net, ckpt, epd = sys.argv[1:5]
    fens = [line.strip() for line in open(epd) if line.strip()][:20]
    state = torch.load(ckpt, map_location="cpu")["model"]
    hidden = state["ft.weight"].shape[0]
    model = Net(hidden)
    model.load_state_dict(state)

    script = f"setoption name EvalFile value {net}\n"
    script += "".join(f"position fen {f}\neval\n" for f in fens) + "quit\n"
    out = subprocess.run([exe], input=script, capture_output=True, text=True).stdout
    engine = [int(l.split()[1]) for l in out.splitlines() if l.startswith("eval ")]

    worst = 0
    for fen, e in zip(fens, engine):
        us, them, bucket = features(fen)
        with torch.no_grad():
            f = model(us, them, bucket).item() * SCALE
        worst = max(worst, abs(f - e))
        print(f"float {f:8.1f}  engine {e:6d}  diff {f - e:6.1f}  {fen}")
    print(f"max diff {worst:.1f} cp over {len(engine)} positions")


if __name__ == "__main__":
    main()
