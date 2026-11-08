"""Fault injection on the bus: timeouts, E2E errors, escalation, recovery, bus-off, delay and jitter.
Detection times are measured on the bus: last good frame sent -> first ECU_STATUS with the new state."""
import time

import pytest

pytestmark = pytest.mark.regression


def detection_ms(env, channel, state, timeout=1.0):
    s = env.monitor.wait_state(state, timeout)
    assert s is not None, f"no {state}"
    last = env.restbus.last_sent(channel)
    return (s.t - last[0]) * 1000.0, s


# ---------- REQ-04 steering timeout ----------
@pytest.mark.smoke
@pytest.mark.req("REQ-04")
def test_req04_steer_timeout_safe_stop_within_80ms(env):
    env.start_normal(speed=30.0)
    time.sleep(0.2)
    env.restbus.inject("steer", "stop")
    ms, s = detection_ms(env, "steer", "SAFE_STOP")
    assert 29.0 <= ms <= 80.0, f"SAFE_STOP {ms:.1f} ms after the last steering frame"
    assert s.faults & 0x04


@pytest.mark.req("REQ-04")
def test_req04_short_gap_tolerated(env):
    env.start_normal()
    time.sleep(0.2)
    env.restbus.inject("steer", "drop", frames=1)  # 20 ms gap, below the 30 ms timeout
    time.sleep(0.3)
    assert all(s.state == "NORMAL" for s in env.monitor.history[-15:])


@pytest.mark.req("REQ-04")
def test_req04_busoff_all_silent(env):
    env.start_normal(speed=40.0)
    time.sleep(0.2)
    env.restbus.inject("all", "busoff", seconds=0.3)
    ms, s = detection_ms(env, "steer", "SAFE_STOP")
    assert ms <= 80.0
    s2 = env.monitor.wait_state("NORMAL", 2.5)
    assert s2 is not None and s2.t - s.t >= 1.0 + 0.2, "recovery only after 1 s of valid frames (REQ-10)"


# ---------- REQ-05 speed timeout ----------
@pytest.mark.req("REQ-05")
def test_req05_speed_timeout_degraded(env):
    env.start_normal(speed=50.0)
    time.sleep(0.2)
    env.restbus.inject("speed", "stop")
    ms, s = detection_ms(env, "speed", "DEGRADED")
    assert 29.0 <= ms <= 80.0
    time.sleep(1.0)
    assert env.monitor.last.speed == pytest.approx(50.0 - 10.0 * (env.monitor.last.t - s.t), abs=0.6)
    assert env.monitor.last.state == "DEGRADED"


# ---------- REQ-06 / REQ-07 E2E ----------
@pytest.mark.req("REQ-06")
@pytest.mark.parametrize("kind", ["counter_repeat", "replay", "counter_jump"])
def test_req06_bad_counter_discarded(env, kind):
    env.start_normal(steer=0.0)
    time.sleep(0.3)
    env.restbus.set(steer=40.0)
    env.restbus.inject("steer", kind, frames=1)
    time.sleep(0.011)
    env.restbus.set(steer=0.0)
    time.sleep(0.3)
    assert max(abs(s.steer) for s in env.monitor.history[-12:]) == 0.0
    assert all(s.state == "NORMAL" for s in env.monitor.history[-15:])


@pytest.mark.smoke
@pytest.mark.req("REQ-06")
def test_req06_lost_frames_tolerated_def001(env):
    """DEF-001 regression: corrupted frames still advance the sender's counter."""
    env.start_normal(steer=0.0)
    time.sleep(0.2)
    env.restbus.inject("steer", "crc", frames=2)
    time.sleep(0.4)
    assert all(s.state == "NORMAL" for s in env.monitor.history[-20:])
    env.restbus.inject("steer", "counter_skip", frames=1)
    time.sleep(0.3)
    assert all(s.state == "NORMAL" for s in env.monitor.history[-15:])


@pytest.mark.smoke
@pytest.mark.req("REQ-07")
@pytest.mark.parametrize("kind", ["crc", "forge", "wrong_data_id"])
def test_req07_corrupted_frame_discarded(env, kind):
    env.start_normal(steer=0.0)
    time.sleep(0.2)
    # two corrupted frames carrying another value: neither may reach the output
    env.restbus.set(steer=50.0)
    env.restbus.inject("steer", kind, frames=2)
    time.sleep(0.015)
    env.restbus.set(steer=0.0)
    time.sleep(0.3)
    assert max(abs(s.steer) for s in env.monitor.history[-25:]) == 0.0
    assert env.monitor.last.state == "NORMAL"


# ---------- REQ-08 escalation ----------
@pytest.mark.req("REQ-08")
def test_req08_three_invalid_degraded(env):
    env.start_normal()
    time.sleep(0.2)
    env.restbus.inject("steer", "crc", frames=3)
    s = env.monitor.wait_state("DEGRADED", 1.0)
    assert s is not None and s.faults & 0x10
    assert env.monitor.wait_state("NORMAL", 1.0) is not None  # 100 ms of valid frames


@pytest.mark.req("REQ-08")
def test_req08_two_invalid_no_change(env):
    env.start_normal()
    time.sleep(0.2)
    env.restbus.inject("steer", "crc", frames=2)
    time.sleep(0.3)
    assert all(s.state == "NORMAL" for s in env.monitor.history[-15:])


@pytest.mark.req("REQ-08")
def test_req08_ten_invalid_safe_stop(env):
    env.start_normal()
    time.sleep(0.2)
    env.restbus.inject("steer", "crc", frames=12)
    assert env.monitor.wait_state("SAFE_STOP", 1.0) is not None
    states = [t[2] for t in env.monitor.transitions if t[1] != "INIT"]
    assert states[:2] == ["DEGRADED", "SAFE_STOP"]


# ---------- REQ-10 / REQ-11 recovery ----------
@pytest.mark.smoke
@pytest.mark.req("REQ-10")
def test_req10_recovery_after_one_second(env):
    env.start_normal()
    time.sleep(0.2)
    env.restbus.inject("steer", "stop")
    s = env.monitor.wait_state("SAFE_STOP", 1.0)
    t_resume = time.time()
    env.restbus.clear()
    n = env.monitor.wait_state("NORMAL", 2.5)
    assert n is not None
    # measured from the first steering frame sent after the fault; REQ-10 allows +-10 ms, plus one
    # 20 ms status period and scheduling on the host for the upper bound
    first = min(e[0] for e in env.restbus.log if e[1] == "steer" and e[3] is not None and e[0] >= t_resume)
    assert 1.0 - 0.010 <= n.t - first <= 1.0 + 0.010 + 0.040, f"recovered after {n.t - first:.4f} s"
    with env.uds() as c:
        r = c.get_dtc_by_status_mask(0x08)
    assert any(d.id == 0xC10087 for d in r.service_data.dtcs), "DTC kept after recovery"


@pytest.mark.req("REQ-11")
def test_req11_no_safe_stop_to_degraded(env):
    env.start_normal()
    time.sleep(0.2)
    env.restbus.inject("steer", "stop")
    env.monitor.wait_state("SAFE_STOP", 1.0)
    env.restbus.inject("speed", "crc", frames=5)  # a DEGRADED condition while in SAFE_STOP
    time.sleep(0.4)
    assert env.monitor.last.state == "SAFE_STOP"
    # the transition check in the env fixture also fails on SAFE_STOP -> DEGRADED


@pytest.mark.req("REQ-11")
def test_req11_degraded_recovery_100ms(env):
    env.start_normal()
    time.sleep(0.2)
    env.restbus.inject("speed", "crc", frames=3)
    d = env.monitor.wait_state("DEGRADED", 1.0)
    n = env.monitor.wait_state("NORMAL", 1.0)
    assert d and n and 0.09 <= n.t - d.t <= 0.2


# ---------- timing faults within tolerance ----------
@pytest.mark.req("REQ-04")
def test_jitter_within_budget_tolerated(env):
    env.start_normal()
    env.restbus.inject("all", "jitter", ms=8, seconds=1.5)
    time.sleep(1.6)
    assert all(s.state == "NORMAL" for s in env.monitor.history[-60:])


@pytest.mark.req("REQ-04")
def test_constant_delay_tolerated(env):
    env.start_normal()
    env.restbus.inject("all", "delay", ms=15, seconds=1.0)
    time.sleep(1.2)
    assert all(s.state == "NORMAL" for s in env.monitor.history[-50:])
