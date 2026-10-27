"""Held-out loss of one or more small-net checkpoints on the same sample of positions.

Usage: python tools/val_small.py <prefix> <a.pt> [<b.pt> ...] [--n 2000000] [--wdl 0.25]

Every checkpoint is scored against the same target (fixed --wdl), so the numbers
compare even when the nets were trained with different wdl weights.
"""
import argparse
import glob
import os
import sys

import numpy as np
import torch

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from train import RECORD, SCALE, Net, decode  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("prefix")
    ap.add_argument("ckpts", nargs="+")
    ap.add_argument("--n", type=int, default=2_000_000)
    ap.add_argument("--wdl", type=float, default=0.25)
    args = ap.parse_args()

    device = "cuda"
    files = sorted(glob.glob(args.prefix + "_*.bin"))
    maps = [np.memmap(f, dtype=np.uint8, mode="r").reshape(-1, RECORD) for f in files]
    ends = np.cumsum([len(m) for m in maps])
    idx = np.sort(np.random.default_rng(123).integers(0, ends[-1], args.n))
    which = np.searchsorted(ends, idx, side="right")
    rec_all = np.concatenate([maps[f][idx[which == f] - (ends[f - 1] if f else 0)] for f in np.unique(which)])
    print(f"held-out positions: {args.n:,} from {args.prefix} ({int(ends[-1]):,} available)")

    for path in args.ckpts:
        ck = torch.load(path, map_location=device, weights_only=False)
        net = Net(ck["model"]["ft.weight"].shape[0]).to(device)
        net.load_state_dict(ck["model"])
        total, batches = 0.0, 0
        with torch.no_grad():
            for i in range(0, args.n, 16384):
                rec = torch.from_numpy(rec_all[i:i + 16384]).to(device)
                us, them, bucket, score, result = decode(rec, device)
                target = (1 - args.wdl) * torch.sigmoid(score / SCALE) + args.wdl * result
                out = net(us, them, bucket).squeeze(1)
                total += torch.mean((torch.sigmoid(out) - target) ** 2).item()
                batches += 1
        print(f"{total / batches:.6f}  {path}")


if __name__ == "__main__":
    main()
