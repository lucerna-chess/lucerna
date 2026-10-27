"""Checks move generation against known perft counts. Usage: python tools/perft_test.py lucerna.exe"""
import subprocess
import sys

CASES = [
    ("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 6, 119060324),
    ("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 5, 193690690),
    ("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 7, 178633661),
    ("r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 6, 706045033),
    ("rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 5, 89941194),
    ("r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 5, 164075551),
]


def main():
    exe = sys.argv[1]
    script = "".join(f"position fen {fen}\ngo perft {d}\n" for fen, d, _ in CASES) + "quit\n"
    out = subprocess.run([exe], input=script, capture_output=True, text=True).stdout
    results = [line.split() for line in out.splitlines() if line.startswith("nodes ")]
    ok = True
    for (fen, d, expected), r in zip(CASES, results):
        got = int(r[1])
        status = "OK  " if got == expected else "FAIL"
        ok &= got == expected
        print(f"{status} depth {d} expected {expected} got {got} ({r[3]} ms)  {fen}")
    if len(results) != len(CASES):
        print("FAIL: engine produced", len(results), "results")
        ok = False
    print("ALL PASSED" if ok else "SOME FAILED")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
