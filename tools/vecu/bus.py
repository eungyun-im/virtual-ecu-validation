"""Bus factory: the same spec strings as the ECU runtime.

    socketcan:vcan0           python-can SocketCAN
    vbus:127.0.0.1:29536      UDP virtual bus (vecu.vbus)
"""
import can

from .vbus import VBus


def make_bus(spec):
    kind, _, rest = spec.partition(":")
    if kind == "socketcan":
        return can.Bus(interface="socketcan", channel=rest, receive_own_messages=False)
    if kind == "vbus":
        return VBus(channel=rest)
    raise ValueError(f"unknown bus spec {spec!r} (socketcan:<if> or vbus:<host>:<port>)")
