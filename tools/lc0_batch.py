"""Downloads Lc0 test80 tar files one by one, converts each with lc0_convert.py, then
moves the tar and its extracted chunks to the Windows Recycle Bin (never deletes them).

  python tools/lc0_batch.py LIST.txt [--workers 3]

LIST.txt holds one tar file name per line. Converted data goes to D:/Lucerna/data/lc0/.
"""
import argparse
import os
import subprocess
import sys
import tarfile
from concurrent.futures import ProcessPoolExecutor

BASE = "https://storage.lczero.org/files/training_data/test80/"
EXT = r"D:\Lucerna\external\lc0_test80"
OUT = r"D:\Lucerna\data\lc0"
HERE = os.path.dirname(os.path.abspath(__file__))


def recycle(path):
    """Send a file or folder to the Recycle Bin."""
    kind = "DeleteDirectory" if os.path.isdir(path) else "DeleteFile"
    ps = ("Add-Type -AssemblyName Microsoft.VisualBasic; "
          f"[Microsoft.VisualBasic.FileIO.FileSystem]::{kind}('{path}', 'OnlyErrorDialogs', 'SendToRecycleBin')")
    subprocess.run(["powershell", "-NoProfile", "-Command", ps], check=True)


def out_path(name):
    return os.path.join(OUT, "t80_" + name[:-4].split("test80-")[1].replace("-", "_") + ".bin")


def convert(name):
    stem = name[:-4]
    chunks = os.path.join(EXT, stem)
    out = out_path(name)
    log = open(os.path.join(r"D:\Lucerna\logs", f"lc0_convert_{stem[-13:]}.log"), "w")
    subprocess.run([sys.executable, "-I", os.path.join(HERE, "lc0_convert.py"), chunks, out], stdout=log, stderr=subprocess.STDOUT, check=True)
    open(out.replace(".bin", ".ok"), "w").close()   # marks a finished conversion
    recycle(chunks)
    return name, os.path.getsize(out) // 32


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("list")
    ap.add_argument("--workers", type=int, default=3)
    args = ap.parse_args()
    names = [l.split()[0] for l in open(args.list) if l.strip()]
    os.makedirs(OUT, exist_ok=True)
    with ProcessPoolExecutor(args.workers) as pool:
        jobs = []
        for name in names:
            tar = os.path.join(EXT, name)
            chunks = os.path.join(EXT, name[:-4])
            done = os.path.exists(out_path(name)) and os.path.exists(out_path(name).replace(".bin", ".ok"))
            if done:
                # converted in an earlier run; clear what is left of it
                for leftover in (tar, chunks):
                    if os.path.exists(leftover):
                        recycle(leftover)
                continue
            if os.path.isdir(chunks) and not os.path.exists(tar):
                jobs.append(pool.submit(convert, name))
                continue
            if not os.path.exists(tar):
                # the server refuses urllib's default client, so fetch with curl; resume and retry on dropped connections
                subprocess.run(["curl", "-sS", "-f", "--retry", "10", "--retry-all-errors", "--retry-delay", "15", "-C", "-", "-o", tar + ".part", BASE + name], check=True)
                os.replace(tar + ".part", tar)
            with tarfile.open(tar) as t:
                t.extractall(EXT, filter="data")
            recycle(tar)
            print(f"downloaded and extracted {name}", flush=True)
            jobs.append(pool.submit(convert, name))
            # keep at most a few extracted tars waiting on disk
            while sum(not j.done() for j in jobs) >= args.workers + 1:
                jobs[[j.done() for j in jobs].index(False)].result()
        for j in jobs:
            name, n = j.result()
            print(f"converted {name}: {n} positions", flush=True)
    print("batch finished", flush=True)


if __name__ == "__main__":
    main()
