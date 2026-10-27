"""Unattended improvement loop: datagen -> train -> build -> test, repeated.

Usage (from the project folder):  python tools/overnight.py [--gens 6]
Progress goes to logs/overnight.log; a readable summary to logs/summary.md.
State is kept in logs/state.json, so the script can be stopped and rerun.
"""
import argparse
import ctypes
import glob
import json
import math
import os
import re
import shutil
import subprocess
import sys
import time

# SRC holds the source, Makefile, book and baseline engine; ROOT receives
# data, nets, engines and logs. They are the same folder once the project lives in D:\Lucerna.
SRC = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = r"D:\Lucerna"
W64 = r"C:\Users\User\Downloads\w64devkit\bin"
CLI = r"d:\Program Files (x86)\Cute Chess\cutechess-cli.exe"
ARGON = r"C:\Users\User\Downloads\w64devkit\Argon5.exe"
BOOK = os.path.join(SRC, "books", "nj8.epd")
LOGS = os.path.join(ROOT, "logs")
STATE = os.path.join(LOGS, "state.json")

DATAGEN_THREADS = 12
POSITIONS_PER_GEN = 25_000_000
TRAIN_EPOCHS = 40
# Train only on the most recent generations: data from early, weaker engines
# raised held-out loss (gen1-6: 0.00761, gen3-6: 0.00719, gen4-6: 0.00719 on gen7).
TRAIN_WINDOW = 5
# Every generation since gen3 beat Argon5 by 400+ Elo; the check no longer tells us anything.
ARGON_CHECK = False
# External anchor against self-play inbreeding: every second generation the best engine
# plays Stash v36 (CCRL Blitz 3375). Recorded only; it does not decide promotion.
ANCHOR = os.path.join(ROOT, "reference", "stash-v36.0.exe")
ANCHOR_ELO = 3375
EPOCH_SIZE = 25_000_000


def log(msg):
    line = time.strftime("%Y-%m-%d %H:%M:%S ") + msg
    print(line, flush=True)
    with open(os.path.join(LOGS, "overnight.log"), "a", encoding="utf-8") as f:
        f.write(line + "\n")


def summary(msg):
    with open(os.path.join(LOGS, "summary.md"), "a", encoding="utf-8") as f:
        f.write(time.strftime("- %m-%d %H:%M ") + msg + "\n")
    log(msg)


def keep_awake():
    if sys.platform == "win32":
        # ES_CONTINUOUS | ES_SYSTEM_REQUIRED: no idle sleep while this process runs.
        ctypes.windll.kernel32.SetThreadExecutionState(0x80000000 | 0x00000001)


def run(cmd, logfile, env=None, cwd=ROOT, low_priority=False):
    log("run: " + " ".join(cmd))
    # Below-normal priority lets the Lichess bot take the CPU when it is thinking.
    flags = 0x00004000 if (low_priority and sys.platform == "win32") else 0
    with open(logfile, "a", encoding="utf-8") as f:
        p = subprocess.run(cmd, cwd=cwd, stdout=f, stderr=subprocess.STDOUT, env=env, creationflags=flags)
    if p.returncode != 0:
        raise RuntimeError(f"command failed ({p.returncode}): {cmd[0]}, see {logfile}")


def datagen_running(out):
    """True while some engine process is writing `out` (e.g. data/gen5)."""
    r = subprocess.run(["powershell", "-NoProfile", "-Command",
                        "@(Get-CimInstance Win32_Process | Where-Object { $_.ProcessId -ne $PID "
                        "-and $_.Name -ne 'powershell.exe' -and $_.CommandLine -match ' datagen ' "
                        f"-and $_.CommandLine -match '{out}' }}).Count"],
                       capture_output=True, text=True)
    return r.stdout.strip() not in ("", "0")


def load_state():
    if os.path.exists(STATE):
        with open(STATE) as f:
            return json.load(f)
    return {"gen": 0, "best": "engines/lucerna-0.1.exe", "history": []}


def save_state(st):
    with open(STATE, "w") as f:
        json.dump(st, f, indent=2)


def elo(score):
    score = min(max(score, 0.001), 0.999)
    return -400 * math.log10(1 / score - 1)


def match(a_name, a_exe, b_name, b_exe, games, tc, logfile, b_opts=()):
    cmd = [CLI,
           "-engine", f"name={a_name}", f"cmd={os.path.join(ROOT, a_exe)}",
           "-engine", f"name={b_name}", f"cmd={b_exe if os.path.isabs(b_exe) else os.path.join(ROOT, b_exe)}", *b_opts,
           "-each", "proto=uci", f"tc={tc}", "option.Hash=16",
           "-openings", f"file={BOOK}", "format=epd", "order=random", "-repeat",
           "-games", "2", "-rounds", str(games // 2), "-concurrency", "6", "-recover",
           "-draw", "movenumber=40", "movecount=8", "score=10",
           "-resign", "movecount=4", "score=800", "twosided=true",
           "-pgnout", logfile.replace(".log", ".pgn")]
    run(cmd, logfile)
    last = None
    with open(logfile, encoding="utf-8") as f:
        for line in f:
            m = re.search(r"Score of .*?: (\d+) - (\d+) - (\d+)", line)
            if m:
                last = tuple(int(x) for x in m.groups())
    w, l, d = last
    score = (w + d / 2) / max(1, w + l + d)
    return w, l, d, score


def generation(st):
    g = st["gen"] + 1
    env = dict(os.environ, PATH=W64 + os.pathsep + os.environ["PATH"])
    best = st["best"]
    log(f"===== generation {g}, data from {best} =====")

    # 1. Self-play data with the current best engine. If a datagen started
    # outside this script is still writing, wait for it instead.
    out = f"data/gen{g}"
    dlog = os.path.join(LOGS, f"datagen{g}.log")
    # Windows does not refresh the mtime of a file that is still open, so ask
    # for the process itself instead of looking at the log's timestamp.
    while datagen_running(out):
        log(f"waiting for running datagen: {open(dlog, encoding='utf-8', errors='ignore').read().strip().splitlines()[-1:]}")
        time.sleep(120)
    if not glob.glob(os.path.join(ROOT, out + "_*.bin")):
        t0 = time.time()
        run([os.path.join(ROOT, best), "datagen", "threads", str(DATAGEN_THREADS),
             "positions", str(POSITIONS_PER_GEN), "nodes", "5000", "seed", str(1000 + g), "out", out],
            os.path.join(LOGS, f"datagen{g}.log"), low_priority=True)
        summary(f"gen{g}: generated {POSITIONS_PER_GEN:,} positions in {(time.time() - t0) / 3600:.1f} h")

    # 2. Train on the last TRAIN_WINDOW generations.
    prefixes = [f"data/gen{k}" for k in range(max(1, g - TRAIN_WINDOW + 1), g + 1)
                if glob.glob(os.path.join(ROOT, "data", f"gen{k}_*.bin"))]
    netbase = f"nets/gen{g}"
    net = f"{netbase}-e{TRAIN_EPOCHS}.nnue"
    if not os.path.exists(os.path.join(ROOT, net)):
        t0 = time.time()
        run([sys.executable, os.path.join(SRC, "tools", "train.py"), "--data", *prefixes, "--out", netbase,
             "--epochs", str(TRAIN_EPOCHS), "--epoch-size", str(EPOCH_SIZE)],
            os.path.join(LOGS, f"train{g}.log"))
        summary(f"gen{g}: trained {net} in {(time.time() - t0) / 60:.0f} min on {', '.join(prefixes)}")

    # 3. Build an engine with the network embedded.
    exe = f"engines/lucerna-g{g}.exe"
    root_fwd = ROOT.replace("\\", "/")
    run([os.path.join(W64, "make.exe"), f"EVALFILE={root_fwd}/{net}", f"EXE={root_fwd}/engines/lucerna-g{g}"],
        os.path.join(LOGS, f"build{g}.log"), env=env, cwd=SRC)

    # 4. New engine against the current best.
    w, l, d, s = match(f"Lucerna-g{g}", exe, "Best", best, 400, "8+0.08", os.path.join(LOGS, f"test{g}.log"))
    summary(f"gen{g} vs {os.path.basename(best)}: +{w} -{l} ={d}, score {s:.3f}, Elo {elo(s):+.0f}")
    if s > 0.5:
        st["best"] = exe
        summary(f"gen{g} is the new best engine")

    # 5. Current best against Argon5 (single thread, no book).
    if ARGON_CHECK:
        w, l, d, s = match(f"Lucerna-best-g{g}", st["best"], "Argon5", ARGON, 200, "10+0.1",
                           os.path.join(LOGS, f"argon{g}.log"),
                           b_opts=["dir=" + os.path.dirname(ARGON), "option.Threads=1",
                                   "option.OwnBook=false", "option.Ponder=false"])
        summary(f"best ({os.path.basename(st['best'])}) vs Argon5: +{w} -{l} ={d}, score {s:.3f}, Elo {elo(s):+.0f}")

    # 6. Every second generation: best engine against the fixed external anchor.
    if g % 2 == 0 and os.path.exists(ANCHOR):
        w, l, d, s = match(f"Lucerna-best-g{g}", st["best"], "Stash-v36", ANCHOR, 100, "10+0.1",
                           os.path.join(LOGS, f"anchor{g}.log"), b_opts=["option.Threads=1"])
        summary(f"best ({os.path.basename(st['best'])}) vs Stash v36: +{w} -{l} ={d}, score {s:.3f}, "
                f"about CCRL Blitz {ANCHOR_ELO + elo(s):.0f}")

    st["gen"] = g
    st["history"].append({"gen": g, "best": st["best"]})
    save_state(st)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gens", type=int, default=6)
    args = ap.parse_args()
    for d in ("logs", "data", "nets", "engines"):
        os.makedirs(os.path.join(ROOT, d), exist_ok=True)
    base = os.path.join(ROOT, "engines", "lucerna-0.1.exe")
    if not os.path.exists(base):
        shutil.copy(os.path.join(SRC, "engines", "lucerna-0.1.exe"), base)
    keep_awake()
    log(f"overnight started, source {SRC}, output {ROOT}")
    st = load_state()
    while st["gen"] < args.gens:
        try:
            generation(st)
        except Exception as e:
            summary(f"ERROR in generation {st['gen'] + 1}: {e}")
            raise
    summary("overnight run finished")


if __name__ == "__main__":
    main()
