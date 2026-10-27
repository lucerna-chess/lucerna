"""Turns a PGN file into position lists for `lucerna rescore`.

Writes OUT_<i>.txt for i in 0..shards-1, one "FEN;result;game" line per position,
whole games kept together in one shard. Result is from White's view (0, 1, 2).
Skips the first plies of each game (openings repeat a lot), positions in check,
and positions already seen in this run.

  python tools/pgn_positions.py games.pgn data/ext/lichess --shards 12 [--skip 10] [--every 1]
"""
import argparse
import hashlib

import chess
import chess.pgn

RESULTS = {"1-0": 2, "1/2-1/2": 1, "0-1": 0}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pgn")
    ap.add_argument("out")
    ap.add_argument("--shards", type=int, default=12)
    ap.add_argument("--skip", type=int, default=10, help="plies to skip at the start of each game")
    ap.add_argument("--every", type=int, default=1, help="keep one position in this many")
    args = ap.parse_args()

    outs = [open(f"{args.out}_{i}.txt", "w", encoding="ascii") for i in range(args.shards)]
    seen = set()
    games = kept = 0
    with open(args.pgn, encoding="utf-8", errors="replace") as f:
        while True:
            game = chess.pgn.read_game(f)
            if game is None:
                break
            result = RESULTS.get(game.headers.get("Result", "*"))
            if result is None:
                continue
            board = game.board()
            shard = outs[games % args.shards]
            for ply, move in enumerate(game.mainline_moves()):
                board.push(move)
                if ply + 1 < args.skip or (ply + 1) % args.every or board.is_check():
                    continue
                key = hashlib.blake2b(board.board_fen().encode() + bytes([board.turn]), digest_size=8).digest()
                if key in seen:
                    continue
                seen.add(key)
                shard.write(f"{board.fen()};{result};{games}\n")
                kept += 1
            games += 1
            if games % 20000 == 0:
                print(f"{games} games, {kept} positions", flush=True)
    for o in outs:
        o.close()
    print(f"done: {games} games, {kept} positions")


if __name__ == "__main__":
    main()
