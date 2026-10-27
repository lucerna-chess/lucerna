"""Converts Lc0 training chunks (V6 records, input format 1) into Lucerna's PackedPos files.

Lc0 training data is published by the Leela Chess Zero project under the Open Database
License (ODbL 1.0); individual contents under the Database Contents License (DbCL 1.0).

Each kept position takes Lc0's search value (best_q) as the score and the game result
as the result. Like Lucerna's own datagen, it keeps only quiet positions: not in check,
and the move played was Lc0's best move and was neither a capture nor a promotion.

  python tools/lc0_convert.py CHUNK_DIR OUT.bin [--check 200]

--check N first verifies on N games that every position reaches the next one with one
legal move, which proves the board decoding is right.
"""
import argparse
import glob
import gzip
import math
import os
import struct

import chess
import numpy as np

REC = 8356
# Maps Lc0 win probability onto Lucerna's own score scale. Fitting results against scores
# gives K = 275 for Lucerna self-play data and K = 350 for Lc0 data converted with 400,
# so 400 * 275 / 350 = 314 puts both on the same scale.
SCORE_SCALE = 314
PIECES = "PNBRQK"


def boards(data):
    """Raw chunk bytes -> list of (squares[64] of piece code or -1, white_to_move, best_q, result_q, played, best)."""
    n = len(data) // REC
    arr = np.frombuffer(data, dtype=np.uint8, count=n * REC).reshape(n, REC)
    planes = arr[:, 7440:7440 + 12 * 8].reshape(n, 12, 8)
    bits = np.unpackbits(planes, axis=2, bitorder="big").reshape(n, 12, 64)
    out = []
    for i in range(n):
        rec = arr[i]
        black = rec[8276] != 0
        best_q, = struct.unpack_from("<f", rec, 8284)
        result_q, = struct.unpack_from("<f", rec, 8308)
        played, best = struct.unpack_from("<HH", rec, 8344)
        sq = np.full(64, -1, dtype=np.int16)
        for p in range(12):
            on = np.nonzero(bits[i, p])[0]
            if black:
                on = on ^ 56
            # planes 0-5 are the side to move, 6-11 the opponent
            color = (0 if p < 6 else 1) ^ (1 if black else 0)
            sq[on] = color * 6 + p % 6
        out.append((sq, not black, best_q, result_q, played, best))
    return out


def to_board(sq, white):
    b = chess.Board(None)
    for s in range(64):
        if sq[s] >= 0:
            b.set_piece_at(s, chess.Piece(chess.PIECE_TYPES[int(sq[s]) % 6], bool(sq[s] < 6)))
    b.turn = bool(white)
    return b


def pack(sq, white, score, result):
    occ = 0
    nibs = []
    for s in range(64):
        if sq[s] >= 0:
            occ |= 1 << s
            nibs.append(int(sq[s]))
    nibs += [0] * (32 - len(nibs))
    pcs = bytes(nibs[2 * k] | (nibs[2 * k + 1] << 4) for k in range(16))
    return struct.pack("<Q16shBB4x", occ, pcs, score, result, 0 if white else 1)


def check(files, n):
    bad = 0
    for f in files[:n]:
        g = boards(gzip.open(f).read())
        if not g or to_board(g[0][0], g[0][1]).board_fen() != chess.STARTING_BOARD_FEN:
            n -= 1
            continue
        for a, b in zip(g, g[1:]):
            ba, bb = to_board(a[0], a[1]), to_board(b[0], b[1])
            ba.castling_rights = ba.clean_castling_rights() | chess.BB_A1 | chess.BB_H1 | chess.BB_A8 | chess.BB_H8
            ba.castling_rights = ba.clean_castling_rights()
            def reaches(board):
                return any((board.push(m) or board.board_fen() == bb.board_fen(), board.pop())[0] for m in list(board.pseudo_legal_moves))
            ok = reaches(ba)
            for ep in range(16, 48) if not ok else ():   # en passant squares are not stored in the record
                if chess.square_rank(ep) in (2, 5):
                    ba.ep_square = ep
                    if reaches(ba):
                        ok = True
                        break
            if not ok:
                bad += 1
                break
    print(f"check: {n - bad}/{n} games decode consistently")
    return bad == 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("chunks")
    ap.add_argument("out")
    ap.add_argument("--check", type=int, default=0)
    args = ap.parse_args()
    files = sorted(glob.glob(os.path.join(args.chunks, "*.gz")))
    if args.check and not check(files, args.check):
        raise SystemExit("decoding check failed")
    kept = seen = games = 0
    with open(args.out, "wb") as out:
        for gi, f in enumerate(files):
            g = boards(gzip.open(f).read())
            # Lc0 data mixes in Chess960 games; keep only games from the normal start position
            if not g or to_board(g[0][0], g[0][1]).board_fen() != chess.STARTING_BOARD_FEN:
                continue
            games += 1
            for a, b in zip(g, g[1:]):
                seen += 1
                sq, white, q, rq, played, best = a
                if played != best:
                    continue
                # a capture or a promotion changes the piece or pawn count of the next position
                if (a[0] >= 0).sum() != (b[0] >= 0).sum() or ((a[0] % 6 == 0) & (a[0] >= 0)).sum() != ((b[0] % 6 == 0) & (b[0] >= 0)).sum():
                    continue
                board = to_board(sq, white)
                if board.is_check():
                    continue
                p = min(max((q + 1) / 2, 0.001), 0.999)
                score_stm = SCORE_SCALE * math.log(p / (1 - p))
                score = int(round(score_stm if white else -score_stm))
                r = int(round(rq))
                result = (r if white else -r) + 1
                out.write(pack(sq, white, score, result))
                kept += 1
            if gi % 5000 == 0:
                print(f"{gi} games, {kept} kept of {seen}", flush=True)
    print(f"done: {games} standard games of {len(files)}, {kept} positions kept of {seen}")


if __name__ == "__main__":
    main()
