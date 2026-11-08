#!/usr/bin/env python3
"""Seed corpus for ecu/fuzz/fuzz_ecu.c: valid command traffic, faults and UDS sessions, so the
fuzzer starts from inputs that pass the E2E check. Record: [ticks][selector][dlc][8 data bytes]."""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from vecu import e2e  # noqa: E402
from vecu.uds_client import security_key  # noqa: E402

STEER, SPEED, PHYS, FUNC = 0, 1, 2, 3


def rec(ticks, sel, data, dlc=8):
    return bytes([ticks, sel, dlc]) + bytes(data).ljust(8, b"\xCC")[:8]


def cmd(sel, raw, counter, signed=True):
    p = bytearray(8)
    p[2:4] = struct.pack("<h" if signed else "<H", raw)
    return rec(10, sel, e2e.protect(0x1A01 if sel == STEER else 0x1A02, counter, bytes(p)))


def traffic(n, steer=0, speed=1000, start=0):
    out = b""
    for i in range(n):
        out += cmd(STEER, steer, (start + i) & 15) + rec(0, SPEED, cmd(SPEED, speed, (start + i) & 15, False)[3:])
    return out


def sf(payload, sel=PHYS, ticks=1):
    return rec(ticks, sel, bytes([len(payload)]) + payload)


seeds = {
    "normal": traffic(40),
    "range": traffic(20) + cmd(STEER, 6000, 4) + traffic(5, start=5),
    "timeout": traffic(20) + rec(100, SPEED, b"") + traffic(120),
    "e2e": traffic(20) + rec(10, STEER, bytes(8)) * 12 + traffic(30),
    "uds": sf(b"\x10\x03") + sf(b"\x27\x01") + sf(b"\x27\x02" + security_key(0).to_bytes(4, "big"))
           + sf(b"\x22\xF1\x90") + rec(1, PHYS, b"\x30\x00\x00") + sf(b"\x19\x02\xFF") + sf(b"\x14\xFF\xFF\xFF")
           + sf(b"\x3E\x80", FUNC) + sf(b"\x10\x02") + sf(b"\x34\x00\x44\x00\x02\x00\x00\x00\x00\x01\x00"),
    "multiframe": rec(1, PHYS, b"\x10\x11\x22\xF1\x90\xF1\x95\xF1") + rec(1, PHYS, b"\x21\x8C\x01\x00\x01\x01\x01\x02")
                  + rec(1, PHYS, b"\x22\x01\x03\xF1\x95"),
}
out = Path(__file__).resolve().parents[1] / "ecu" / "fuzz" / "corpus"
out.mkdir(parents=True, exist_ok=True)
for name, data in seeds.items():
    (out / f"seed_{name}.bin").write_bytes(data)
print(f"{len(seeds)} seeds in {out}")
