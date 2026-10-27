"""Pauses overnight.py once generation N has finished, so maintenance can run.

Usage: python tools/pause_after.py N [--wait-minutes 60]
After pausing it waits for a file logs/RESUME to appear (Claude creates it when
maintenance is done and overnight.py has been restarted). If nothing shows up
within --wait-minutes, it restarts overnight.py itself so the CPU never idles.
"""
import argparse
import json
import os
import subprocess
import sys
import time

MAIN = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("gen", type=int)
    ap.add_argument("--root", default=r"D:\Lucerna")
    ap.add_argument("--wait-minutes", type=int, default=60)
    args = ap.parse_args()
    logs = os.path.join(args.root, "logs")
    state = os.path.join(logs, "state.json")
    resume = os.path.join(logs, "RESUME")

    def log(msg):
        line = time.strftime("- %m-%d %H:%M ") + msg
        print(line, flush=True)
        with open(os.path.join(logs, "summary.md"), "a", encoding="utf-8") as f:
            f.write(line + "\n")

    while True:
        try:
            if json.load(open(state))["gen"] >= args.gen:
                break
        except (OSError, ValueError, KeyError):
            pass
        time.sleep(30)

    ps = lambda c: subprocess.run(["powershell", "-NoProfile", "-Command", c])
    ps("Get-CimInstance Win32_Process | Where-Object { $_.Name -eq 'python.exe' -and $_.CommandLine -match 'overnight.py' }"
       " | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }")
    time.sleep(2)
    ps("Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match ' datagen ' }"
       " | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }")
    time.sleep(3)
    nxt = args.gen + 1
    for f in [os.path.join(logs, f"datagen{nxt}.log")] + [
            os.path.join(args.root, "data", n) for n in os.listdir(os.path.join(args.root, "data"))
            if n.startswith(f"gen{nxt}_")]:
        if os.path.exists(f):
            os.remove(f)
    log(f"paused after generation {args.gen} for maintenance (rename, long time control test)")

    deadline = time.time() + args.wait_minutes * 60
    while time.time() < deadline:
        if os.path.exists(resume):
            os.remove(resume)
            log("maintenance finished, pause script exiting")
            return
        time.sleep(30)

    script = os.path.join(MAIN, "tools", "overnight.py")
    subprocess.Popen(["powershell", "-NoExit", "-Command",
                      f"$host.UI.RawUI.WindowTitle = 'Lucerna 整晚训练 - 请勿关闭'; python -u '{script}' --gens 8"],
                     creationflags=subprocess.CREATE_NEW_CONSOLE)
    log(f"no maintenance within {args.wait_minutes} min, restarted overnight.py automatically")


if __name__ == "__main__":
    main()
