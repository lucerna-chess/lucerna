"""Trains a Lucerna NNUE (768 -> HIDDEN)x2 -> 8 output buckets with SCReLU on self-play data.
The output bucket is chosen by piece count: min((pieces - 2) // 4, 7).

Usage:
  python tools/train.py --data data/run1 --out nets/nj1 [--epochs 40] [--hidden 512]

--data takes one or more glob prefixes; every <prefix>_*.bin file is loaded.
Each epoch is `--epoch-size` random positions. A quantised net is written
after every epoch as <out>-e<N>.nnue, plus <out>.pt for resuming.
"""
import argparse
import glob
import math
import os
import time

import numpy as np
import torch

QA = 255
QB = 1024
SCALE = 400
RECORD = 32
BUCKETS = 8


class Net(torch.nn.Module):
    def __init__(self, hidden):
        super().__init__()
        self.ft = torch.nn.Linear(768, hidden)
        self.out = torch.nn.Linear(2 * hidden, BUCKETS)

    def forward(self, us, them, bucket):
        a = torch.clamp(self.ft(us), 0, 1) ** 2
        b = torch.clamp(self.ft(them), 0, 1) ** 2
        return self.out(torch.cat([a, b], dim=1)).gather(1, bucket.unsqueeze(1))

    def clip(self):
        # Keeps weights inside the range the int16/int8 quantisation can represent.
        with torch.no_grad():
            self.ft.weight.clamp_(-1.98, 1.98)
            self.out.weight.clamp_(-1.98, 1.98)


def load_data(prefixes):
    files = []
    for p in prefixes:
        files += sorted(glob.glob(p + "_*.bin"))
    if not files:
        raise SystemExit(f"no data files for {prefixes}")
    arrays = []
    for f in files:
        n = os.path.getsize(f) // RECORD
        if n:
            arrays.append(np.fromfile(f, dtype=np.uint8, count=n * RECORD).reshape(n, RECORD))
    data = np.concatenate(arrays)
    print(f"loaded {len(data):,} positions from {len(files)} files")
    return data


def decode(rec, device):
    """rec: uint8 tensor (B, 32) on device -> (us, them, bucket, score_stm, result_stm)."""
    B = rec.shape[0]
    shifts = torch.arange(8, device=device, dtype=torch.uint8)
    bits = ((rec[:, :8, None] >> shifts) & 1).reshape(B, 64).bool()
    pcs = rec[:, 8:24]
    nib = torch.stack([pcs & 15, pcs >> 4], dim=2).reshape(B, 32).long()
    order = (torch.cumsum(bits.long(), dim=1) - 1).clamp(0, 31)
    piece = torch.gather(nib, 1, order)

    b_idx, sq = bits.nonzero(as_tuple=True)
    p = piece[b_idx, sq]
    color, ptype = p // 6, p % 6
    w_feat = color * 384 + ptype * 64 + sq
    b_feat = (1 - color) * 384 + ptype * 64 + (sq ^ 56)

    xw = torch.zeros(B, 768, device=device)
    xb = torch.zeros(B, 768, device=device)
    xw[b_idx, w_feat] = 1.0
    xb[b_idx, b_feat] = 1.0

    score = (rec[:, 24].int() | (rec[:, 25].int() << 8))
    score = torch.where(score >= 32768, score - 65536, score).float()
    result = rec[:, 26].float() / 2.0
    stm_black = rec[:, 27].bool()

    us = torch.where(stm_black[:, None], xb, xw)
    them = torch.where(stm_black[:, None], xw, xb)
    score = torch.where(stm_black, -score, score)
    result = torch.where(stm_black, 1.0 - result, result)
    bucket = ((bits.sum(dim=1) - 2) // 4).clamp(0, BUCKETS - 1)
    return us, them, bucket, score, result


def save_quantised(model, path):
    ft_w = model.ft.weight.detach().cpu().numpy().T  # (768, hidden)
    ft_b = model.ft.bias.detach().cpu().numpy()
    out_w = model.out.weight.detach().cpu().numpy().reshape(-1)  # [bucket][2 * hidden]
    out_b = model.out.bias.detach().cpu().numpy()
    parts = [
        np.round(ft_w * QA).astype(np.int16).reshape(-1),
        np.round(ft_b * QA).astype(np.int16),
        np.round(out_w * QB).astype(np.int16),
        np.round(out_b * QA * QB).astype(np.int32),
    ]
    with open(path, "wb") as f:
        for p in parts:
            f.write(p.tobytes())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", nargs="+", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--hidden", type=int, default=512)
    ap.add_argument("--epochs", type=int, default=40)
    ap.add_argument("--epoch-size", type=int, default=100_000_000)
    ap.add_argument("--batch", type=int, default=16384)
    ap.add_argument("--lr", type=float, default=0.001)
    ap.add_argument("--wdl", type=float, default=0.25, help="weight of game result in the target")
    ap.add_argument("--hard", type=float, default=0.0,
                    help="extra weight for positions the net currently gets most wrong (0 = plain MSE)")
    ap.add_argument("--resume", default=None)
    args = ap.parse_args()

    device = "cuda"
    torch.manual_seed(0)
    data = torch.from_numpy(load_data(args.data))
    n = len(data)

    model = Net(args.hidden).to(device)
    opt = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=0.0)
    start_epoch = 1
    if args.resume:
        ck = torch.load(args.resume)
        model.load_state_dict(ck["model"])
        opt.load_state_dict(ck["opt"])
        start_epoch = ck["epoch"] + 1

    steps = max(1, args.epoch_size // args.batch)
    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)

    for epoch in range(start_epoch, args.epochs + 1):
        # Cosine decay from lr to lr/100 over the run.
        lr = args.lr * (0.01 + 0.99 * 0.5 * (1 + math.cos(math.pi * (epoch - 1) / args.epochs)))
        for g in opt.param_groups:
            g["lr"] = lr
        t0 = time.time()
        total = 0.0
        for step in range(steps):
            idx = torch.randint(0, n, (args.batch,))
            rec = data[idx].to(device, non_blocking=True)
            us, them, bucket, score, result = decode(rec, device)
            target = (1 - args.wdl) * torch.sigmoid(score / SCALE) + args.wdl * result
            pred = torch.sigmoid(model(us, them, bucket).squeeze(1))
            err = (pred - target) ** 2
            if args.hard > 0:
                # Hard-example weighting: weight grows with the current error, normalised to mean 1
                w = 1 + args.hard * err.detach().sqrt()
                loss = torch.mean(w * err) / w.mean()
            else:
                loss = torch.mean(err)
            opt.zero_grad(set_to_none=True)
            loss.backward()
            opt.step()
            model.clip()
            total += loss.item() if step % 64 == 0 else 0.0
        avg = total / ((steps + 63) // 64)
        dt = time.time() - t0
        print(f"epoch {epoch}/{args.epochs} loss {avg:.6f} lr {lr:.6f} "
              f"{steps * args.batch / dt:,.0f} pos/s {dt:.0f}s", flush=True)
        save_quantised(model, f"{args.out}-e{epoch}.nnue")
        torch.save({"model": model.state_dict(), "opt": opt.state_dict(), "epoch": epoch}, args.out + ".pt")


if __name__ == "__main__":
    main()
