"""UDP virtual CAN bus for machines without vcan (the C side is ecu/app/bus.c).

A broker relays every frame to all other clients, like a CAN bus where a node does not receive
its own frames. Datagram (16 bytes, little-endian): u32 id | u8 dlc | u8 kind | u16 0 | data[8];
kind 0 = frame, 1 = hello/keep-alive, 2 = bye.

    python -m vecu.vbus --port 29536          # run the broker
    bus = VBus(channel="127.0.0.1:29536")     # python-can bus for a client
"""
import argparse
import socket
import struct
import threading
import time

import can

FMT = struct.Struct("<IBBH8s")
CLIENT_TTL_S = 3.0


class VBus(can.BusABC):
    """python-can bus speaking the vbus protocol."""

    def __init__(self, channel="127.0.0.1:29536", **kwargs):
        host, port = channel.rsplit(":", 1)
        self._peer = (host, int(port))
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.bind(("127.0.0.1", 0))
        self._stop = threading.Event()
        self._ctl(1)
        self._ka = threading.Thread(target=self._keepalive, daemon=True)
        self._ka.start()
        self.channel_info = f"vbus {channel}"
        super().__init__(channel=channel, **kwargs)

    def _ctl(self, kind):
        try:
            self._sock.sendto(FMT.pack(0, 0, kind, 0, bytes(8)), self._peer)
        except OSError:
            pass

    def _keepalive(self):
        while not self._stop.wait(0.5):
            self._ctl(1)

    def send(self, msg, timeout=None):
        data = bytes(msg.data).ljust(8, b"\0")
        self._sock.sendto(FMT.pack(msg.arbitration_id, msg.dlc, 0, 0, data), self._peer)

    def _recv_internal(self, timeout):
        self._sock.settimeout(timeout)
        while True:
            try:
                raw = self._sock.recv(64)
            except socket.timeout:
                return None, False
            except OSError:
                return None, False
            if len(raw) != FMT.size:
                continue
            fid, dlc, kind, _, data = FMT.unpack(raw)
            if kind != 0:
                continue
            dlc = min(dlc, 8)
            return can.Message(timestamp=time.time(), arbitration_id=fid, is_extended_id=False, dlc=dlc,
                               data=data[:dlc], channel="vbus0"), False

    def shutdown(self):
        self._stop.set()
        self._ctl(2)
        self._sock.close()
        super().shutdown()


def run_broker(port, host="127.0.0.1"):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((host, port))
    clients = {}
    print(f"vbus broker on {host}:{port}", flush=True)
    while True:
        raw, addr = sock.recvfrom(64)
        now = time.monotonic()
        if len(raw) != FMT.size:
            continue
        kind = raw[5]
        if kind == 2:
            clients.pop(addr, None)
            continue
        clients[addr] = now
        if kind != 0:
            continue
        for c, seen in list(clients.items()):
            if now - seen > CLIENT_TTL_S:
                del clients[c]
            elif c != addr:
                try:
                    sock.sendto(raw, c)
                except OSError:
                    pass


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="vbus broker")
    ap.add_argument("--port", type=int, default=29536)
    run_broker(ap.parse_args().port)
