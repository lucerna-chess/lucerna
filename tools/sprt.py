"""SPRT test of a new engine build against a base build, using cutechess-cli.

Usage: python tools/sprt.py NAME new.exe base.exe [--elo0 0 --elo1 5 --tc 8+0.08 --concurrency 6]
Stops by itself once the result is statistically clear (H1 = new is stronger,
H0 = it is not). Log goes to D:\\Lucerna\\logs\\sprt_NAME.log and a one-line
verdict is appended to D:\\Lucerna\\logs\\sprt_results.md.
"""
import argparse
import os
import re
import subprocess
import time

CLI = r"d:\Program Files (x86)\Cute Chess\cutechess-cli.exe"
LOGS = r"D:\Lucerna\logs"
BOOK = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "books", "nj8.epd")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("name")
    ap.add_argument("new")
    ap.add_argument("base")
    ap.add_argument("--elo0", type=float, default=0)
    ap.add_argument("--elo1", type=float, default=5)
    ap.add_argument("--tc", default="8+0.08")
    ap.add_argument("--concurrency", type=int, default=6)
    ap.add_argument("--max-games", type=int, default=30000)
    args = ap.parse_args()

    logfile = os.path.join(LOGS, f"sprt_{args.name}.log")
    cmd = [CLI,
           "-engine", f"name=new", f"cmd={os.path.abspath(args.new)}",
           "-engine", f"name=base", f"cmd={os.path.abspath(args.base)}",
           "-each", "proto=uci", f"tc={args.tc}", "option.Hash=16",
           "-openings", f"file={BOOK}", "format=epd", "order=random", "-repeat",
           "-games", "2", "-rounds", str(args.max_games // 2),
           "-concurrency", str(args.concurrency), "-recover",
           "-sprt", f"elo0={args.elo0}", f"elo1={args.elo1}", "alpha=0.05", "beta=0.05",
           "-draw", "movenumber=40", "movecount=8", "score=10",
           "-resign", "movecount=4", "score=800", "twosided=true",
           "-ratinginterval", "200"]
    t0 = time.time()
    with open(logfile, "w", encoding="utf-8") as f:
        subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT)

    text = open(logfile, encoding="utf-8").read()
    score = re.findall(r"Score of new vs base: (\d+) - (\d+) - (\d+)", text)
    elo = re.findall(r"Elo difference: ([-\d.]+) \+/- ([\d.]+)", text)
    if "H1 was accepted" in text:
        verdict = "PASSED (H1)"
    elif "H0 was accepted" in text:
        verdict = "FAILED (H0)"
    else:
        verdict = "INCONCLUSIVE"
    w, l, d = score[-1] if score else ("?", "?", "?")
    e = f"{elo[-1][0]} +/- {elo[-1][1]}" if elo else "?"
    line = (f"- {time.strftime('%m-%d %H:%M')} {args.name}: {verdict}, +{w} -{l} ={d}, "
            f"Elo {e}, bounds [{args.elo0}, {args.elo1}], tc {args.tc}, {(time.time() - t0) / 60:.0f} min")
    print(line)
    with open(os.path.join(LOGS, "sprt_results.md"), "a", encoding="utf-8") as f:
        f.write(line + "\n")


if __name__ == "__main__":
    main()
