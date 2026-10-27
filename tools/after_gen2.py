"""One-off chain for the night of 2026-10-22, runs unattended:

1. wait until generation 2 of overnight.py has finished (state.json gen == 2),
2. stop overnight.py before generation 3 gets going,
3. build the search-dev variants with the gen2 network and SPRT them,
4. merge the variants that passed into the main branch,
5. restart overnight.py in a new window with the resulting best engine.

Progress is appended to D:\\Lucerna\\logs\\after_gen2.log and summary.md.
"""
import ctypes
import glob
import json
import os
import re
import subprocess
import sys
import time

MAIN = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEV = r"D:\Lucerna\devsrc"
ROOT = r"D:\Lucerna"
LOGS = os.path.join(ROOT, "logs")
STATE = os.path.join(LOGS, "state.json")
W64 = r"C:\Users\User\Downloads\w64devkit\bin"
GIT = r"C:\Program Files\Git\cmd\git.exe"
CLI = r"d:\Program Files (x86)\Cute Chess\cutechess-cli.exe"
NET = "D:/Lucerna/nets/gen2-e20.nnue"
ATTRIBUTION = "\n\nCo-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"


def log(msg, important=False):
    line = time.strftime("%Y-%m-%d %H:%M:%S ") + msg
    print(line, flush=True)
    with open(os.path.join(LOGS, "after_gen2.log"), "a", encoding="utf-8") as f:
        f.write(line + "\n")
    if important:
        with open(os.path.join(LOGS, "summary.md"), "a", encoding="utf-8") as f:
            f.write(time.strftime("- %m-%d %H:%M ") + msg + "\n")


def ps(command):
    subprocess.run(["powershell", "-NoProfile", "-Command", command])


def git(*args, cwd=MAIN):
    r = subprocess.run([GIT, *args], cwd=cwd, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)} failed: {r.stderr.strip()}")
    return r.stdout.strip()


def build(rev, exe_base):
    env = dict(os.environ, PATH=W64 + os.pathsep + os.environ["PATH"])
    git("checkout", "-q", rev, cwd=DEV)
    r = subprocess.run([os.path.join(W64, "make.exe"), f"EVALFILE={NET}", f"EXE={exe_base}"],
                       cwd=DEV, env=env, capture_output=True, text=True)
    exe = exe_base + ".exe"
    if r.returncode != 0 or not os.path.exists(exe):
        raise RuntimeError(f"build of {rev} failed: {r.stdout[-500:]} {r.stderr[-500:]}")
    log(f"built {exe} from {rev}")
    return exe


def sprt(name, new, base):
    subprocess.run([sys.executable, os.path.join(MAIN, "tools", "sprt.py"), name, new, base,
                    "--max-games", "8000"])
    lines = [l for l in open(os.path.join(LOGS, "sprt_results.md"), encoding="utf-8") if f" {name}:" in l]
    line = lines[-1].strip() if lines else f"{name}: no result"
    log("SPRT " + line.lstrip("- "), important=True)
    return "PASSED" in line


def fixed_match(new, base, games=400):
    logfile = os.path.join(LOGS, "after_gen2_vs_best.log")
    book = os.path.join(MAIN, "books", "nj8.epd")
    subprocess.run([CLI, "-engine", "name=new", f"cmd={new}", "-engine", "name=base", f"cmd={base}",
                    "-each", "proto=uci", "tc=8+0.08", "option.Hash=16",
                    "-openings", f"file={book}", "format=epd", "order=random", "-repeat",
                    "-games", "2", "-rounds", str(games // 2), "-concurrency", "6", "-recover",
                    "-draw", "movenumber=40", "movecount=8", "score=10",
                    "-resign", "movecount=4", "score=800", "twosided=true"],
                   stdout=open(logfile, "w", encoding="utf-8"), stderr=subprocess.STDOUT)
    m = re.findall(r"Score of new vs base: (\d+) - (\d+) - (\d+)", open(logfile, encoding="utf-8").read())
    w, l, d = (int(x) for x in m[-1])
    return w, l, d, (w + d / 2) / max(1, w + l + d)


def main():
    if sys.platform == "win32":
        ctypes.windll.kernel32.SetThreadExecutionState(0x80000000 | 0x00000001)

    # 1. Wait for generation 2 to finish.
    log("waiting for generation 2 to finish")
    while True:
        try:
            if json.load(open(STATE))["gen"] >= 2:
                break
        except (OSError, ValueError, KeyError):
            pass
        time.sleep(30)

    # 2. Stop overnight.py and the generation 3 datagen it just started.
    ps("Get-CimInstance Win32_Process | Where-Object { $_.Name -eq 'python.exe' -and $_.CommandLine -match 'overnight.py' }"
       " | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }")
    time.sleep(2)
    ps("Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match ' datagen ' }"
       " | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }")
    time.sleep(3)
    for f in glob.glob(os.path.join(ROOT, "data", "gen3_*.bin")) + [os.path.join(LOGS, "datagen3.log")]:
        if os.path.exists(f):
            os.remove(f)
    st = json.load(open(STATE))
    log(f"paused overnight run after generation 2 (best: {st['best']}) to test search changes", important=True)
    try:
        test_and_merge(st)
    except Exception as e:
        log(f"ERROR while testing search changes, keeping the previous best: {e}", important=True)
    finally:
        restart_overnight()


def test_and_merge(st):
    # 3. Build and SPRT the search variants, all with the gen2 network.
    base = os.path.join(ROOT, "engines", "lucerna-g2.exe")
    hist_rev = git("rev-parse", "search-dev~1", cwd=DEV)
    se_rev = git("rev-parse", "search-dev", cwd=DEV)
    hist = build(hist_rev, "D:/Lucerna/engines/lucerna-g2-hist")
    hist_se = build(se_rev, "D:/Lucerna/engines/lucerna-g2-hist-se")
    git("checkout", "-q", "search-dev", cwd=DEV)

    best_exe, merge_rev = base, None
    if sprt("g2-hist", hist, base):
        best_exe, merge_rev = hist, hist_rev
        if sprt("g2-se", hist_se, hist):
            best_exe, merge_rev = hist_se, se_rev
    elif sprt("g2-hist-se-vs-base", hist_se, base):
        best_exe, merge_rev = hist_se, se_rev

    # 4. Merge what passed into main.
    if merge_rev:
        git("merge", "--no-edit", "-m", f"Merge search improvements that passed SPRT ({merge_rev[:7]}){ATTRIBUTION}",
            merge_rev)
        log(f"merged {merge_rev[:7]} into main", important=True)

    # The search tests used the gen2 net. If gen2 itself lost to gen1, check the
    # new build against the current best before replacing it.
    candidate = os.path.join(ROOT, "engines", "lucerna-g2s.exe")
    subprocess.run(["powershell", "-NoProfile", "-Command", f"Copy-Item '{best_exe}' '{candidate}' -Force"])
    current_best = os.path.join(ROOT, st["best"])
    if os.path.normcase(current_best) != os.path.normcase(base):
        w, l, d, s = fixed_match(candidate, current_best)
        log(f"lucerna-g2s vs {st['best']}: +{w} -{l} ={d}, score {s:.3f}", important=True)
        if s <= 0.5:
            candidate = None
    if candidate:
        st["best"] = "engines/lucerna-g2s.exe"
        json.dump(st, open(STATE, "w"), indent=2)
        log(f"new best engine: {st['best']} ({os.path.basename(best_exe)})", important=True)


def restart_overnight():
    # 5. Restart the overnight loop in its own window.
    script = os.path.join(MAIN, "tools", "overnight.py")
    title = "Lucerna 整晚训练 - 请勿关闭"
    subprocess.Popen(["powershell", "-NoExit", "-Command",
                      f"$host.UI.RawUI.WindowTitle = '{title}'; python -u '{script}' --gens 6"],
                     creationflags=subprocess.CREATE_NEW_CONSOLE)
    log("restarted overnight.py for generation 3 onwards", important=True)


if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        log(f"ERROR in after_gen2: {e}", important=True)
        raise
