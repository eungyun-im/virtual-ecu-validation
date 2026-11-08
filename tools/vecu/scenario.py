"""Scripted demo scenario: normal driving -> faults -> detection -> SAFE_STOP -> recovery, with a
CAN log of everything on the bus. Used for the log analyzer example and the demo video.

    python -m vecu.scenario --vecu build/release/vecu --log logs/demo.log
Starts its own vbus broker and ECU unless --bus is given.
"""
import argparse
import json
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

import can

from .bus import make_bus
from .monitor import Recorder, StatusMonitor
from .restbus import RestBus

ROOT = Path(__file__).resolve().parents[2]
DBC = str(ROOT / "dbc" / "vecu.dbc")

STEPS = [  # (seconds from start, caption, action)
    (0.0, "normal driving", lambda rb: rb.set(steer=0.0, speed=40.0)),
    (1.0, "steering command 90 deg (rate limited)", lambda rb: rb.set(steer=90.0)),
    (2.5, "out-of-range command 600 deg: discarded", lambda rb: rb.set(steer=600.0)),
    (3.5, "back to 20 deg", lambda rb: rb.set(steer=20.0)),
    (5.0, "fault: 3 corrupted steering frames", lambda rb: rb.inject("steer", "crc", frames=3)),
    (6.5, "fault: steering messages stop", lambda rb: rb.inject("steer", "stop")),
    (8.0, "steering messages resume", lambda rb: rb.clear()),
    (10.5, "fault: bus-off on all channels for 0.2 s", lambda rb: rb.inject("all", "busoff", seconds=0.2)),
    (13.0, "end", None),
]


class LockedLogger(can.Listener):
    """The receive thread and the rest-bus thread both write to the log file."""

    def __init__(self, inner):
        self.inner = inner
        self.lock = threading.Lock()

    def on_message_received(self, m):
        with self.lock:
            self.inner.on_message_received(m)

    def stop(self):
        with self.lock:
            self.inner.stop()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--vecu", default=str(ROOT / "build" / "release" / "vecu"))
    ap.add_argument("--bus", default=None)
    ap.add_argument("--log", default=str(ROOT / "logs" / "demo.log"))
    a = ap.parse_args()
    procs = []
    spec = a.bus
    if spec is None:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(("127.0.0.1", 0)); port = s.getsockname()[1]; s.close()
        procs.append(subprocess.Popen([sys.executable, "-m", "vecu.vbus", "--port", str(port)], stdout=subprocess.DEVNULL))
        time.sleep(0.3)
        spec = f"vbus:127.0.0.1:{port}"
    Path(a.log).parent.mkdir(parents=True, exist_ok=True)
    ecu_log = open(Path(a.log).with_suffix(".ecu.txt"), "w")
    procs.append(subprocess.Popen([a.vecu, "--bus", spec, "--trace"], stdout=ecu_log, stderr=subprocess.STDOUT))
    time.sleep(0.2)
    bus = make_bus(spec)
    mon, rec = StatusMonitor(DBC), Recorder()
    log_path = Path(a.log)
    if log_path.exists():
        log_path.unlink()
    logger = LockedLogger(can.Logger(a.log))
    notifier = can.Notifier(bus, [mon, rec, logger], timeout=0.01)
    rb = RestBus(bus, DBC)
    # the rest-bus frames are sent by us, so they are not received back: log them through the logger too
    orig_send = rb._send

    def send_and_log(c, data, kind):
        orig_send(c, data, kind)
        logger.on_message_received(can.Message(timestamp=time.time(), arbitration_id=c.msg.frame_id,
                                               is_extended_id=False, data=data, dlc=8, channel="vbus0"))
    rb._send = send_and_log
    rb.start()
    t0 = time.time()
    captions = []
    for at, caption, action in STEPS:
        while time.time() - t0 < at:
            time.sleep(0.005)
        captions.append({"t_s": round(time.time() - t0, 3), "caption": caption})
        print(f"{time.time() - t0:6.2f} s  {caption}  [{mon.last.state if mon.last else '-'}]", flush=True)
        if action:
            action(rb)
    rb.stop()
    time.sleep(0.1)
    notifier.stop()
    logger.stop()
    bus.shutdown()
    for p in reversed(procs):
        p.terminate()
        p.wait(timeout=5)
    Path(a.log).with_suffix(".captions.json").write_text(json.dumps({"t0": t0, "steps": captions}, indent=1))
    print("transitions:", [(round(t - t0, 3), f, to) for t, f, to in mon.transitions])


if __name__ == "__main__":
    main()
