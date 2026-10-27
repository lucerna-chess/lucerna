"""Stress test for multi-threaded search: random positions, short searches,
interrupted infinite searches and thread-count changes. Every bestmove must be
legal and every go must answer. Needs python-chess.

Usage: python tools/smp_stress.py engine.exe [--iterations 300]
"""
import argparse
import queue
import random
import subprocess
import sys
import threading
import time

import chess


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("engine")
    ap.add_argument("--iterations", type=int, default=300)
    args = ap.parse_args()

    p = subprocess.Popen([args.engine], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
    lines = queue.Queue()
    threading.Thread(target=lambda: [lines.put(l.strip()) for l in p.stdout], daemon=True).start()

    def send(cmd):
        p.stdin.write(cmd + "\n")
        p.stdin.flush()

    def wait_for(prefix, timeout):
        end = time.time() + timeout
        while time.time() < end:
            try:
                line = lines.get(timeout=0.1)
            except queue.Empty:
                continue
            if line.startswith(prefix):
                return line
        return None

    rng = random.Random(7)
    send("uci")
    assert wait_for("uciok", 5), "no uciok"
    bad = 0
    t0 = time.time()
    for it in range(args.iterations):
        if it % 50 == 0:
            send(f"setoption name Threads value {rng.choice([2, 4, 6, 8])}")
            send("ucinewgame")
        board = chess.Board()
        for _ in range(rng.randint(0, 60)):
            moves = list(board.legal_moves)
            if not moves:
                break
            board.push(rng.choice(moves))
        if board.is_game_over():
            continue
        send("position startpos moves " + " ".join(m.uci() for m in board.move_stack))
        if rng.random() < 0.3:
            send("go infinite")
            time.sleep(rng.uniform(0.01, 0.3))
            send("stop")
        else:
            send(f"go movetime {rng.randint(5, 80)}")
        line = wait_for("bestmove", 10)
        if line is None:
            print(f"iteration {it}: NO BESTMOVE (hang?)")
            bad += 1
            break
        mv = line.split()[1]
        if chess.Move.from_uci(mv) not in board.legal_moves:
            print(f"iteration {it}: ILLEGAL {mv} in {board.fen()}")
            bad += 1
    send("quit")
    try:
        p.wait(timeout=10)
    except subprocess.TimeoutExpired:
        print("engine did not quit")
        bad += 1
        p.kill()
    print(f"{args.iterations} iterations in {time.time() - t0:.0f}s, problems: {bad}, exit code {p.returncode}")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
