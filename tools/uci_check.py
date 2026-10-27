"""Sends a sequence of UCI commands and waits for bestmove after each go.
Usage: python tools/uci_check.py lucerna.exe "position startpos" "go depth 10" ..."""
import subprocess
import sys
import time


def main():
    exe, cmds = sys.argv[1], sys.argv[2:]
    p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
    for cmd in ["uci", "isready"] + cmds:
        p.stdin.write(cmd + "\n")
        p.stdin.flush()
        if cmd.startswith("go"):
            t0 = time.time()
            last_info = ""
            for line in p.stdout:
                if line.startswith("info"):
                    last_info = line.strip()
                if line.startswith("bestmove"):
                    print(last_info)
                    print(line.strip(), f"(wall {time.time() - t0:.2f}s)")
                    break
    p.stdin.write("quit\n")
    p.stdin.flush()
    p.wait()


if __name__ == "__main__":
    main()
