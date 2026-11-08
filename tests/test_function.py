"""Functional requirements on the running ECU: range, rate limit, SAFE_STOP behaviour, status message."""
import statistics
import time

import pytest

pytestmark = pytest.mark.regression


def settle(env, seconds=0.15):
    time.sleep(seconds)
    return env.monitor.last


# ---------- REQ-01 / REQ-02: boundary values ----------
STEER_CASES = [  # (command deg, accepted)
    (540.0, True), (-540.0, True), (539.9, True), (540.1, False), (-540.1, False), (1000.0, False), (0.0, True)]


@pytest.mark.req("REQ-01")
@pytest.mark.parametrize("cmd,accepted", STEER_CASES, ids=[f"{c:+.1f}deg" for c, _ in STEER_CASES])
def test_req01_steer_range_boundary(env, cmd, accepted):
    env.start_normal(steer=0.0, speed=10.0)
    env.restbus.set(steer=cmd)
    s = settle(env, 1.4)  # 540 deg at 5 deg per 10 ms takes 1.08 s
    if accepted:
        assert s.steer == pytest.approx(cmd, abs=0.05)
        assert not s.faults & 0x01
    else:
        assert s.steer == pytest.approx(0.0, abs=0.05), "out-of-range command must not move the output"
        assert s.faults & 0x01
        assert s.dtc_count >= 1
    assert s.state == "NORMAL"


SPEED_CASES = [(200.0, True), (0.0, True), (200.01, False), (300.0, False)]


@pytest.mark.req("REQ-02")
@pytest.mark.parametrize("cmd,accepted", SPEED_CASES, ids=[f"{c:.2f}kmh" for c, _ in SPEED_CASES])
def test_req02_speed_range_boundary(env, cmd, accepted):
    env.start_normal(steer=0.0, speed=50.0)
    env.restbus.set(speed=cmd)
    s = settle(env, 0.2)
    if accepted:
        assert s.speed == pytest.approx(cmd, abs=0.005)
    else:
        assert s.speed == pytest.approx(50.0, abs=0.005)
        assert s.faults & 0x02


@pytest.mark.smoke
@pytest.mark.req("REQ-01")
def test_req01_dtc_recorded_and_readable(env):
    env.start_normal()
    env.restbus.set(steer=600.0)
    time.sleep(0.1)
    with env.uds() as c:
        r = c.get_dtc_by_status_mask(0x08)
    assert any(d.id == 0x4A0011 for d in r.service_data.dtcs)


# ---------- REQ-03: rate limit ----------
@pytest.mark.smoke
@pytest.mark.req("REQ-03")
def test_req03_rate_limit(env):
    env.start_normal(steer=0.0)
    t0 = time.time()
    env.restbus.set(steer=100.0)
    end = env.monitor.wait_for(lambda s: s.steer >= 100.0, timeout=2.0)
    assert end is not None
    hist = [s for s in env.monitor.history if s.t >= t0 - 0.03]
    steps = [(b.steer - a.steer) / ((b.t - a.t) / 0.010) for a, b in zip(hist, hist[1:]) if b.t > a.t]
    assert max(steps) <= 5.0 + 0.6, f"max {max(steps):.2f} deg per 10 ms"
    assert end.t - t0 >= 0.19  # 100 deg / 5 deg per cycle = 20 cycles
    assert any(s.faults & 0x40 for s in hist), "RateLimited flag while limiting"


# ---------- REQ-09: SAFE_STOP behaviour ----------
@pytest.mark.req("REQ-09")
def test_req09_safe_stop_holds_steer_and_ramps_speed(env):
    env.start_normal(steer=20.0, speed=20.0)
    time.sleep(0.2)
    env.restbus.inject("steer", "stop")
    s = env.monitor.wait_state("SAFE_STOP", 1.0)
    assert s is not None
    held = s.steer
    env.restbus.set(speed=20.0)
    time.sleep(1.0)
    hist = [h for h in env.monitor.history if h.t >= s.t]
    assert all(h.steer == pytest.approx(held) for h in hist)
    # 10 km/h/s ramp: about 10 km/h after one second, allowing for status period and scheduling
    assert hist[-1].speed == pytest.approx(20.0 - 10.0 * (hist[-1].t - s.t), abs=0.6)
    time.sleep(1.3)
    assert env.monitor.last.speed == 0.0


# ---------- REQ-12: status message ----------
@pytest.mark.smoke
@pytest.mark.req("REQ-12")
def test_req12_status_period(env):
    env.start_normal()
    t0 = time.time()
    time.sleep(2.0)
    ts = [s.t for s in env.monitor.history if s.t >= t0]
    dt = [(b - a) * 1000 for a, b in zip(ts, ts[1:])]
    assert statistics.mean(dt) == pytest.approx(20.0, abs=0.5)
    assert sorted(dt)[int(0.95 * len(dt))] <= 25.0, "95 % of periods within 25 ms on the host scheduler"
    assert all(s.crc_ok for s in env.monitor.history)
    counters = [s.counter for s in env.monitor.history if s.t >= t0]
    assert all((b - a) % 16 == 1 for a, b in zip(counters, counters[1:]))


@pytest.mark.req("REQ-12")
def test_req12_event_frame_on_state_change(env):
    env.start_normal()
    time.sleep(0.2)
    env.restbus.inject("steer", "stop")
    s = env.monitor.wait_state("SAFE_STOP", 1.0)
    prev = [h for h in env.monitor.history if h.t < s.t][-1]
    # the event frame comes earlier than the next 20 ms periodic slot would
    assert (s.t - prev.t) * 1000 <= 20.0 + 3.0
