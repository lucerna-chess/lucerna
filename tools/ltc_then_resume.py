"""Long time control check against Argon5, then restart the overnight loop.

Usage: python tools/ltc_then_resume.py ENGINE [--tc 60+0.6] [--games 200]
"""
import argparse
import math
import os
import re
import subprocess
import time

MAIN = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = r"D:\Lucerna"
LOGS = os.path.join(ROOT, "logs")
CLI = r"d:\Program Files (x86)\Cute Chess\cutechess-cli.exe"
ARGON = r"C:\Users\User\Downloads\w64devkit\Argon5.exe"
BOOK = os.path.join(MAIN, "books", "nj8.epd")


def elo(s):
    s = min(max(s, 0.001), 0.999)
    return -400 * math.log10(1 / s - 1)


def summary(msg):
    with open(os.path.join(LOGS, "summary.md"), "a", encoding="utf-8") as f:
        f.write(time.strftime("- %m-%d %H:%M ") + msg + "\n")
    print(msg, flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("engine")
    ap.add_argument("--tc", default="60+0.6")
    ap.add_argument("--games", type=int, default=200)
    args = ap.parse_args()

    name = os.path.splitext(os.path.basename(args.engine))[0]
    logfile = os.path.join(LOGS, f"ltc_{name}_vs_argon5.log")
    summary(f"long time control test started: {name} vs Argon5, {args.games} games at {args.tc}")
    cmd = [CLI,
           "-engine", f"name={name}", f"cmd={args.engine}",
           "-engine", "name=Argon5", f"cmd={ARGON}", f"dir={os.path.dirname(ARGON)}",
           "option.Threads=1", "option.OwnBook=false", "option.Ponder=false",
           "-each", "proto=uci", f"tc={args.tc}", "option.Hash=64",
           "-openings", f"file={BOOK}", "format=epd", "order=random", "-repeat",
           "-games", "2", "-rounds", str(args.games // 2), "-concurrency", "6", "-recover",
           "-draw", "movenumber=40", "movecount=8", "score=10",
           "-resign", "movecount=4", "score=800", "twosided=true",
           "-pgnout", logfile.replace(".log", ".pgn")]
    with open(logfile, "w", encoding="utf-8") as f:
        subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT)

    text = open(logfile, encoding="utf-8").read()
    m = re.findall(r"Score of .*?: (\d+) - (\d+) - (\d+)", text)
    if m:
        w, l, d = (int(x) for x in m[-1])
        s = (w + d / 2) / max(1, w + l + d)
        flags = len(re.findall(r"loses on time", text))
        summary(f"LONG TC {args.tc}: {name} vs Argon5: +{w} -{l} ={d}, score {s:.3f}, Elo {elo(s):+.0f}, "
                f"time losses {flags}")
    else:
        summary("long time control test produced no result, see " + logfile)

    script = os.path.join(MAIN, "tools", "overnight.py")
    subprocess.Popen(["powershell", "-NoExit", "-Command",
                      f"$host.UI.RawUI.WindowTitle = 'Lucerna 整晚训练 - 请勿关闭'; python -u '{script}' --gens 8"],
                     creationflags=subprocess.CREATE_NEW_CONSOLE)
    summary("restarted overnight.py for generation 4 onwards")


if __name__ == "__main__":
    main()
