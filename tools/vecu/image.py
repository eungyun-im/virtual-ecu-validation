"""Software image builder for REQ-20 (format in ecu/include/vecu/swupdate.h).

    python -m vecu.image --version 1.2.0 --size 6000 --out app.bin
"""
import argparse
import hashlib
import random
import struct


def build_image(version: int, payload_len: int, seed: int = 0) -> bytes:
    rng = random.Random(seed)
    payload = bytes(rng.randrange(256) for _ in range(payload_len))
    header = b"VECU" + struct.pack(">II", version, payload_len) + hashlib.sha256(payload).digest()
    return header.ljust(64, b"\0") + payload


def parse_version(s: str) -> int:
    major, minor, patch = (int(x) for x in s.split("."))
    return (major << 16) | (minor << 8) | patch


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", required=True)
    ap.add_argument("--size", type=int, default=4096)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    open(a.out, "wb").write(build_image(parse_version(a.version), a.size))
