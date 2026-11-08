"""Rest-bus simulation and fault injection.

The rest-bus plays the controller: STEER_CMD and SPEED_CMD every 10 ms, encoded from the DBC and
E2E-protected. Faults are injected per channel for a number of frames or a time window:

    rb.inject("steer", "drop", frames=5)            # message missing
    rb.inject("steer", "stop")                      # transmission stops until rb.clear()
    rb.inject("all", "busoff", seconds=0.2)         # every channel silent (bus-off emulation)
    rb.inject("steer", "delay", ms=15)              # every frame late by 15 ms
    rb.inject("steer", "jitter", ms=4)              # random delay 0..4 ms
    rb.inject("steer", "counter_skip", frames=1)    # counter +2: one frame lost (tolerated, REQ-06)
    rb.inject("steer", "counter_jump", frames=1)    # counter +5: sender lost sequence (discarded, then resync)
    rb.inject("steer", "counter_repeat", frames=1)  # counter not incremented
    rb.inject("steer", "crc", frames=3)             # CRC byte corrupted
    rb.inject("steer", "replay", frames=1)          # an earlier frame sent again unchanged
    rb.inject("steer", "forge", frames=1)           # payload changed by a third party, CRC not recomputed
    rb.inject("steer", "wrong_data_id", frames=1)   # protected with another message's Data ID
    rb.set(steer=540.1)                             # out-of-range value: just a value
Every transmitted frame is logged with its send time and the fault applied to it.
"""
import random
import threading
import time
from dataclasses import dataclass, field

import can
import cantools

from . import e2e

CHANNELS = {
    "steer": ("STEER_CMD", "SteerAngleCmd", "SteerCrc", "SteerAlive"),
    "speed": ("SPEED_CMD", "SpeedCmd", "SpeedCrc", "SpeedAlive"),
}


@dataclass
class Fault:
    kind: str
    frames: int = 0           # remaining frames, 0 = use the time window
    until: float = 0.0        # time.time() deadline, 0 = until cleared
    ms: float = 0.0


@dataclass
class Chan:
    name: str
    msg: object
    signal: str
    crc_sig: str
    alive_sig: str
    value: float = 0.0
    counter: int = 0
    enabled: bool = True
    faults: list = field(default_factory=list)
    history: list = field(default_factory=list)  # last frames sent, for replay
    next_free: float = 0.0  # delayed frames keep their order


class RestBus:
    def __init__(self, bus, dbc_path, period=0.010, seed=1):
        self.bus = bus
        self.db = cantools.database.load_file(dbc_path)
        self.period = period
        self.rng = random.Random(seed)
        self.chans = {n: Chan(n, self.db.get_message_by_name(m), s, c, a) for n, (m, s, c, a) in CHANNELS.items()}
        self.log = []  # (send time, channel, can_id, data, fault kind or "")
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._thread = None
        self.tx_errors = 0

    # ---------- control ----------
    def set(self, steer=None, speed=None):
        with self._lock:
            if steer is not None:
                self.chans["steer"].value = float(steer)
            if speed is not None:
                self.chans["speed"].value = float(speed)

    def enable(self, name, on=True):
        with self._lock:
            for c in self._select(name):
                c.enabled = on

    def inject(self, name, kind, frames=0, seconds=0.0, ms=0.0):
        until = time.time() + seconds if seconds else 0.0
        with self._lock:
            for c in self._select(name):
                c.faults.append(Fault(kind, frames, until, ms))

    def clear(self, name="all"):
        with self._lock:
            for c in self._select(name):
                c.faults.clear()

    def _select(self, name):
        return list(self.chans.values()) if name == "all" else [self.chans[name]]

    def last_sent(self, name):
        for entry in reversed(self.log):
            if entry[1] == name and entry[4] not in ("drop", "stop", "busoff"):
                return entry
        return None

    # ---------- frame building ----------
    def _encode(self, c, value, counter):
        raw = c.msg.encode({c.signal: value, c.crc_sig: 0, c.alive_sig: 0}, scaling=True, strict=False)
        return e2e.protect(e2e.DATA_ID[c.msg.frame_id], counter, raw)

    def _active_fault(self, c, now):
        for f in list(c.faults):
            if f.until and now > f.until:
                c.faults.remove(f)
                continue
            return f
        return None

    def _consume(self, c, f):
        if f.frames:
            f.frames -= 1
            if f.frames == 0:
                c.faults.remove(f)

    def _frame_for(self, c, now):
        """Returns (data or None, delay seconds, fault kind)."""
        f = self._active_fault(c, now)
        kind = f.kind if f else ""
        delay = 0.0
        if kind in ("drop", "stop", "busoff"):
            self._consume(c, f)
            return None, 0.0, kind
        if kind == "replay" and c.history:
            data = c.history[0]
            self._consume(c, f)
            return data, 0.0, kind
        counter = c.counter
        if kind == "counter_skip":
            counter = (counter + 1) & 0x0F
        if kind == "counter_jump":
            counter = (counter + 4) & 0x0F
        if kind == "counter_repeat":
            counter = (counter - 1) & 0x0F
        data = bytearray(self._encode(c, c.value, counter))
        if kind == "crc":
            data[0] ^= 0xA5
        elif kind == "forge":
            data[2] ^= 0x10  # value changed after protection
        elif kind == "wrong_data_id":
            other = 0x1A02 if c.name == "steer" else 0x1A01
            data = bytearray(e2e.protect(other, counter, bytes(data)))
        elif kind == "delay":
            delay = f.ms / 1000.0
        elif kind == "jitter":
            delay = self.rng.uniform(0.0, f.ms / 1000.0)
        if kind not in ("counter_repeat",):
            c.counter = (counter + 1) & 0x0F
        if kind not in ("crc", "forge", "wrong_data_id", "counter_skip", "counter_jump", "counter_repeat"):
            c.history.append(bytes(data))
            c.history = c.history[-8:]
        if f:
            self._consume(c, f)
        return bytes(data), delay, kind

    # ---------- loop ----------
    def _send(self, c, data, kind):
        msg = can.Message(arbitration_id=c.msg.frame_id, is_extended_id=False, data=data, dlc=8)
        t = time.time()
        try:
            self.bus.send(msg)
        except can.CanError:
            self.tx_errors += 1
        self.log.append((t, c.name, c.msg.frame_id, data, kind))

    def _schedule(self, c, when, data, kind):
        """Send at `when`, but never before an earlier frame of the same channel (no reordering)."""
        when = max(when, c.next_free + 0.0002)
        c.next_free = when
        threading.Timer(max(0.0, when - time.time()), self._send, args=(c, data, kind)).start()

    def _run(self):
        next_t = time.perf_counter()
        while not self._stop.is_set():
            now = time.time()
            delayed = []
            with self._lock:
                for c in self.chans.values():
                    if not c.enabled:
                        continue
                    data, delay, kind = self._frame_for(c, now)
                    if data is None:
                        self.log.append((now, c.name, c.msg.frame_id, None, kind))
                    elif delay > 0 or c.next_free > now:
                        delayed.append((delay, c, data, kind))
                    else:
                        self._send(c, data, kind)
            for delay, c, data, kind in delayed:
                self._schedule(c, now + delay, data, kind)
            next_t += self.period
            sleep = next_t - time.perf_counter()
            if sleep > 0:
                time.sleep(sleep)
            else:
                next_t = time.perf_counter()  # we fell behind: do not burst

    def start(self):
        self._stop.clear()
        self._thread = threading.Thread(target=self._run, name="restbus", daemon=True)
        self._thread.start()
        return self

    def stop(self):
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=1.0)
