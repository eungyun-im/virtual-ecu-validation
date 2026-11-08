"""Listeners for tests and tools: ECU_STATUS decoding with a state timeline, and a frame recorder."""
import threading
import time
from dataclasses import dataclass

import can
import cantools

from . import e2e

STATES = {0: "NORMAL", 1: "DEGRADED", 2: "SAFE_STOP", 3: "INIT"}


@dataclass
class Status:
    t: float
    state: str
    steer: float
    speed: float
    faults: int
    dtc_count: int
    counter: int
    crc_ok: bool


class StatusMonitor(can.Listener):
    def __init__(self, dbc_path):
        self.db = cantools.database.load_file(dbc_path)
        self.msg = self.db.get_message_by_name("ECU_STATUS")
        self.history = []
        self.transitions = []  # (t, from, to)
        self._cv = threading.Condition()

    def on_message_received(self, m):
        if m.arbitration_id != self.msg.frame_id or m.dlc != 8:
            return
        d = self.msg.decode(bytes(m.data), decode_choices=False)
        faults = sum(int(d[n]) << i for i, n in enumerate(
            ["FaultSteerRange", "FaultSpeedRange", "FaultSteerTimeout", "FaultSpeedTimeout",
             "FaultSteerE2E", "FaultSpeedE2E", "RateLimited"]))
        s = Status(m.timestamp, STATES[int(d["EcuState"])], float(d["SteerOut"]), float(d["SpeedOut"]), faults,
                   int(d["ConfirmedDtcCount"]), int(d["StatusAlive"]), e2e.check(0x1A03, bytes(m.data)))
        with self._cv:
            if self.history and self.history[-1].state != s.state:
                self.transitions.append((s.t, self.history[-1].state, s.state))
            self.history.append(s)
            self._cv.notify_all()

    @property
    def last(self):
        return self.history[-1] if self.history else None

    def wait_for(self, pred, timeout=2.0):
        """Wait until pred(last status) is true; returns that status or None."""
        end = time.time() + timeout
        with self._cv:
            while True:
                if self.history and pred(self.history[-1]):
                    return self.history[-1]
                left = end - time.time()
                if left <= 0:
                    return None
                self._cv.wait(left)

    def wait_state(self, state, timeout=2.0):
        return self.wait_for(lambda s: s.state == state, timeout)

    def first_after(self, t0, pred):
        with self._cv:
            for s in self.history:
                if s.t >= t0 and pred(s):
                    return s
        return None


class Recorder(can.Listener):
    """Keeps every received frame (time, id, data)."""

    def __init__(self):
        self.frames = []
        self._lock = threading.Lock()

    def on_message_received(self, m):
        with self._lock:
            self.frames.append((m.timestamp, m.arbitration_id, bytes(m.data)))

    def since(self, t0, can_id=None):
        with self._lock:
            return [f for f in self.frames if f[0] >= t0 and (can_id is None or f[1] == can_id)]
