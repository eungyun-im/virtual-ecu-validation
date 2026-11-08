"""UDS tester side: udsoncan over can-isotp on a shared python-can bus (via a Notifier),
plus a raw single-frame request helper used to measure P2 without library overhead."""
import struct
import time

import can
import isotp
import udsoncan
from udsoncan.client import Client
from udsoncan.connections import PythonIsoTpConnection

REQ_ID, FUNC_ID, RESP_ID = 0x7E0, 0x7DF, 0x7E8
PAD = 0xCC


class IntCodec(udsoncan.DidCodec):
    """One integer per DID (udsoncan's struct strings decode to a tuple)."""

    def __init__(self, fmt):
        self.fmt = struct.Struct(fmt)

    def encode(self, value):
        return self.fmt.pack(value)

    def decode(self, payload):
        return self.fmt.unpack(payload)[0]

    def __len__(self):
        return self.fmt.size


def security_key(seed: int) -> int:
    """Same demo algorithm as ecu/src/uds.c: rotl32(seed ^ 0x5A3C96E1, 7) + 0x1234."""
    x = (seed ^ 0x5A3C96E1) & 0xFFFFFFFF
    return (((x << 7) | (x >> 25)) + 0x1234) & 0xFFFFFFFF


def _key_algo(level, seed, params=None):
    return security_key(int.from_bytes(seed, "big")).to_bytes(4, "big")


def make_client(bus, notifier, p2_timeout=1.0):
    addr = isotp.Address(isotp.AddressingMode.Normal_11bits, rxid=RESP_ID, txid=REQ_ID)
    params = {"tx_padding": PAD, "tx_data_min_length": 8, "stmin": 0, "blocksize": 0,
              "rx_flowcontrol_timeout": 1000, "rx_consecutive_frame_timeout": 1000}
    stack = isotp.NotifierBasedCanStack(bus, notifier, address=addr, params=params)
    conn = PythonIsoTpConnection(stack)
    cfg = dict(udsoncan.configs.default_client_config)
    cfg.update({
        "p2_timeout": p2_timeout, "p2_star_timeout": 5.0, "request_timeout": 6.0,
        "security_algo": _key_algo, "exception_on_negative_response": False,
        "exception_on_invalid_response": True, "exception_on_unexpected_response": True,
        "data_identifiers": {0xF190: udsoncan.AsciiCodec(17), 0xF195: IntCodec(">L"), 0xF18C: udsoncan.AsciiCodec(7),
                             0x0100: IntCodec(">B"), 0x0101: IntCodec(">B"), 0x0102: IntCodec(">h"),
                             0x0103: IntCodec(">H")},
    })
    return Client(conn, config=cfg)


def raw_request(bus, recorder, payload: bytes, functional=False, timeout=0.5):
    """Send a single-frame request; return (response payload or None, latency seconds or None).
    Latency = first response frame time - request send time (P2 as seen on the bus)."""
    assert 1 <= len(payload) <= 7
    data = bytes([len(payload)]) + payload
    t0 = time.time()
    bus.send(can.Message(arbitration_id=FUNC_ID if functional else REQ_ID, is_extended_id=False,
                         data=data.ljust(8, bytes([PAD])), dlc=8))
    end = t0 + timeout
    while time.time() < end:
        frames = recorder.since(t0, RESP_ID)
        if frames:
            t, _, d = frames[0]
            pci = d[0] >> 4
            if pci == 0:
                return d[1:1 + (d[0] & 0x0F)], t - t0
            if pci == 1:  # first frame: latency is defined by it; let the ECU finish the transfer
                bus.send(can.Message(arbitration_id=REQ_ID, is_extended_id=False,
                                     data=bytes([0x30, 0, 0]).ljust(8, bytes([PAD])), dlc=8))
                time.sleep(0.02)
                return d[2:8], t - t0
        time.sleep(0.001)
    return None, None
