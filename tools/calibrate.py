"""Plays rating pairings and estimates CCRL Blitz ratings from Stash anchors.

Usage: python tools/calibrate.py [--games 60] [--concurrency 2]
Results go to D:\\Lucerna\\logs\\calibration.md.
"""
import argparse
import math
import os
import re
import subprocess
import time

CLI = r"d:\Program Files (x86)\Cute Chess\cutechess-cli.exe"
BOOK = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "books", "nj8.epd")
LOGS = r"D:\Lucerna\logs"
REF = r"D:\Lucerna\reference"
W64 = r"C:\Users\User\Downloads\w64devkit"
ARGON_OPTS = ["option.Threads=1", "option.OwnBook=false", "option.Ponder=false"]

# Stash versions with published CCRL Blitz ratings.
ANCHORS = {"Stash-v19": 2474, "Stash-v21": 2714, "Stash-v25": 2933}

ENGINES = {
    "Argon3": [f"cmd={W64}\\Argon3.exe", f"dir={W64}", *ARGON_OPTS],
    "Argon4": [f"cmd={W64}\\Argon4.exe", f"dir={W64}", *ARGON_OPTS],
    "Argon5": [f"cmd={W64}\\Argon5.exe", f"dir={W64}", *ARGON_OPTS],
    "Stash-v19": [f"cmd={REF}\\stash-v19.0.exe"],
    "Stash-v21": [f"cmd={REF}\\stash-v21.0.exe"],
    "Stash-v25": [f"cmd={REF}\\stash-v25.0.exe"],
}

PAIRINGS = [
    ("Argon5", "Stash-v19"),
    ("Argon5", "Stash-v21"),
    ("Argon5", "Stash-v25"),
    ("Argon4", "Argon5"),
    ("Argon3", "Argon5"),
]


def elo(score):
    score = min(max(score, 0.01), 0.99)
    return -400 * math.log10(1 / score - 1)


def play(a, b, games, concurrency):
    logfile = os.path.join(LOGS, f"calib_{a}_vs_{b}.log")
    cmd = [CLI, "-engine", f"name={a}", *ENGINES[a], "-engine", f"name={b}", *ENGINES[b],
           "-each", "proto=uci", "tc=10+0.1", "option.Hash=16",
           "-openings", f"file={BOOK}", "format=epd", "order=random", "-repeat",
           "-games", "2", "-rounds", str(games // 2), "-concurrency", str(concurrency), "-recover",
           "-draw", "movenumber=40", "movecount=8", "score=10",
           "-resign", "movecount=4", "score=800", "twosided=true"]
    with open(logfile, "w", encoding="utf-8") as f:
        subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT)
    last = (0, 0, 0)
    for line in open(logfile, encoding="utf-8"):
        m = re.search(r"Score of .*?: (\d+) - (\d+) - (\d+)", line)
        if m:
            last = tuple(int(x) for x in m.groups())
    return last


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--games", type=int, default=60)
    ap.add_argument("--concurrency", type=int, default=2)
    args = ap.parse_args()
    out = os.path.join(LOGS, "calibration.md")
    with open(out, "a", encoding="utf-8") as f:
        f.write(f"\n## Calibration {time.strftime('%Y-%m-%d %H:%M')} (10+0.1, 1 thread, {args.games} games each)\n\n")
    estimates = []
    for a, b in PAIRINGS:
        w, l, d = play(a, b, args.games, args.concurrency)
        n = max(1, w + l + d)
        s = (w + d / 2) / n
        line = f"- {a} vs {b}: +{w} -{l} ={d}, score {s:.2f}, {elo(s):+.0f} Elo"
        if b in ANCHORS:
            est = ANCHORS[b] + elo(s)
            estimates.append(est)
            line += f" -> {a} about {est:.0f} CCRL"
        print(line, flush=True)
        with open(out, "a", encoding="utf-8") as f:
            f.write(line + "\n")
    if estimates:
        line = f"\nArgon5 estimated CCRL Blitz: about {sum(estimates) / len(estimates):.0f} (rough, small sample)\n"
        print(line, flush=True)
        with open(out, "a", encoding="utf-8") as f:
            f.write(line)


if __name__ == "__main__":
    main()
