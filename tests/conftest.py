"""System test fixtures: the ECU runs as its own process on a CAN bus (SocketCAN vcan or the UDP
virtual bus); the tests drive it through the rest-bus, the fault injector and a UDS tester.

Every test gets a fresh ECU process (initial state = power-up). At teardown the ECU log is checked
for sanitizer reports and every observed state change is checked against the allowed transitions
(REQ-11), so a forbidden transition fails whichever test caused it.
"""
import os
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path

import can
import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from vecu.bus import make_bus  # noqa: E402
from vecu.monitor import Recorder, StatusMonitor  # noqa: E402
from vecu.restbus import RestBus  # noqa: E402
from vecu.uds_client import make_client  # noqa: E402

DBC = str(ROOT / "dbc" / "vecu.dbc")
ALLOWED = {("INIT", "NORMAL"), ("INIT", "SAFE_STOP"), ("NORMAL", "DEGRADED"), ("NORMAL", "SAFE_STOP"),
           ("DEGRADED", "NORMAL"), ("DEGRADED", "SAFE_STOP"), ("SAFE_STOP", "NORMAL")}
SANITIZER_MARKERS = ("AddressSanitizer", "UndefinedBehaviorSanitizer", "runtime error:", "LeakSanitizer")


def pytest_addoption(parser):
    parser.addoption("--vecu", default=str(ROOT / "build" / "release" / "vecu"), help="ECU binary")
    parser.addoption("--bus", default="auto", help="socketcan:vcan0, vbus, or auto (vcan0 if present)")
    parser.addoption("--ecu-logs", default=str(ROOT / "build" / "ecu-logs"), help="ECU stderr per test")
    parser.addoption("--results-json", default=str(ROOT / "reports" / "out" / "systest.json"),
                     help="per-test results with requirement IDs")


def pytest_configure(config):
    for m in ("smoke: fast checks run on every push", "regression: full requirement coverage",
              "slow: tests that wait for long timers (S3, security lockout)",
              "req(id): requirement verified by this test"):
        config.addinivalue_line("markers", m)


def _free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


@pytest.fixture(scope="session")
def bus_spec(request):
    spec = request.config.getoption("--bus")
    if spec == "auto":
        spec = "socketcan:vcan0" if Path("/sys/class/net/vcan0").exists() else "vbus"
    request.config._vecu_bus_kind = spec.split(":")[0]
    if spec == "vbus":
        port = _free_port()
        broker = subprocess.Popen([sys.executable, "-m", "vecu.vbus", "--port", str(port)],
                                  env={**os.environ, "PYTHONPATH": str(ROOT / "tools")},
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(0.3)
        yield f"vbus:127.0.0.1:{port}"
        broker.terminate()
        broker.wait(timeout=5)
    else:
        yield spec


class Env:
    """Everything a test needs: bus, rest-bus, status monitor, recorder, UDS client factory."""

    def __init__(self, spec, ecu_bin, log_path):
        self.spec = spec
        self.ecu_bin = ecu_bin
        self.log_path = log_path
        self.bus = make_bus(spec)
        self.monitor = StatusMonitor(DBC)
        self.recorder = Recorder()
        self.notifier = can.Notifier(self.bus, [self.monitor, self.recorder], timeout=0.01)
        self.restbus = RestBus(self.bus, DBC)
        self.proc = None
        self._log = None

    def start_ecu(self):
        self._log = open(self.log_path, "w")
        args = [self.ecu_bin, "--bus", self.spec.replace("vbus:", "vbus:", 1), "--trace"]
        self.proc = subprocess.Popen(args, stdout=self._log, stderr=subprocess.STDOUT)
        time.sleep(0.15)
        assert self.proc.poll() is None, "ECU process exited at start-up"

    def stop_ecu(self):
        if self.proc and self.proc.poll() is None:
            self.proc.send_signal(signal.SIGTERM)
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        if self._log:
            self._log.close()
        return self.proc.returncode if self.proc else None

    def uds(self, p2_timeout=1.0):
        return make_client(self.bus, self.notifier, p2_timeout=p2_timeout)

    def start_normal(self, steer=0.0, speed=0.0, timeout=2.0):
        self.restbus.set(steer=steer, speed=speed)
        self.restbus.start()
        s = self.monitor.wait_state("NORMAL", timeout)
        assert s is not None, "ECU did not reach NORMAL"
        return s

    def close(self):
        self.restbus.stop()
        self.notifier.stop()
        self.bus.shutdown()


@pytest.fixture
def env(request, bus_spec):
    logs = Path(request.config.getoption("--ecu-logs"))
    logs.mkdir(parents=True, exist_ok=True)
    e = Env(bus_spec, request.config.getoption("--vecu"), logs / f"{request.node.name}.log")
    e.start_ecu()
    yield e
    e.close()
    rc = e.stop_ecu()
    text = Path(e.log_path).read_text(errors="replace")
    bad = [m for m in SANITIZER_MARKERS if m in text]
    assert not bad, f"sanitizer report in ECU log {e.log_path}: {bad}"
    assert rc == 0, f"ECU exit code {rc}, log {e.log_path}"
    forbidden = [t for t in e.monitor.transitions if (t[1], t[2]) not in ALLOWED]
    assert not forbidden, f"forbidden state transitions (REQ-11): {forbidden}"


# ---------- results per requirement for tools/rtm.py and tools/test_report.py ----------
_RESULTS = []


@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_makereport(item, call):
    outcome = yield
    rep = outcome.get_result()
    if rep.when == "call" or (rep.when == "setup" and rep.outcome != "passed") or (rep.when == "teardown" and rep.failed):
        reqs = [m.args[0] for m in item.iter_markers("req")]
        marks = sorted({m.name for m in item.iter_markers()} & {"smoke", "regression", "slow"})
        status = "skipped" if rep.skipped else ("passed" if rep.passed else "failed")
        prev = next((r for r in _RESULTS if r["nodeid"] == item.nodeid), None)
        if prev is None:
            _RESULTS.append({"nodeid": item.nodeid, "reqs": reqs, "markers": marks, "outcome": status,
                             "duration_s": round(rep.duration, 3), "message": str(rep.longrepr)[-400:] if rep.failed else ""})
        elif rep.failed:  # a teardown failure (sanitizer, forbidden transition) fails the test
            prev.update(outcome="failed", message=str(rep.longrepr)[-400:])
        p2 = getattr(item.funcargs.get("env"), "p2_ms", None) if hasattr(item, "funcargs") else None
        if p2 and rep.when == "call":
            _RESULTS[-1]["p2_ms"] = {"max": round(max(p2), 3), "mean": round(sum(p2) / len(p2), 3), "n": len(p2)}


def pytest_sessionfinish(session, exitstatus):
    out = Path(session.config.getoption("--results-json"))
    out.parent.mkdir(parents=True, exist_ok=True)
    import json
    out.write_text(json.dumps({"bus": getattr(session.config, "_vecu_bus_kind", session.config.getoption("--bus")), "vecu": session.config.getoption("--vecu"),
                               "tests": _RESULTS}, indent=1))
