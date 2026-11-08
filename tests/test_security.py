"""Security checks: SecurityAccess rules, replay and forgery on the bus, UDS request fuzzing."""
import random
import time

import isotp
import pytest

from vecu.uds_client import PAD, REQ_ID, RESP_ID, raw_request, security_key

pytestmark = pytest.mark.regression


@pytest.mark.smoke
@pytest.mark.req("REQ-16")
def test_req16_security_access_default_session(env):
    resp, _ = raw_request(env.bus, env.recorder, bytes([0x27, 0x01]))
    assert resp == bytes([0x7F, 0x27, 0x7F])


@pytest.mark.req("REQ-17")
def test_req17_unlock_with_correct_key(env):
    with env.uds() as c:
        c.change_session(3)
        assert c.unlock_security_access(1).positive


@pytest.mark.slow
@pytest.mark.req("REQ-17")
def test_req17_attempt_limit_and_lockout(env):
    with env.uds() as c:
        c.change_session(3)
        codes = []
        for _ in range(3):
            seed = c.request_seed(1).service_data.seed
            bad = (security_key(int.from_bytes(seed, "big")) ^ 1).to_bytes(4, "big")
            codes.append(c.send_key(2, bad).code)
        assert codes == [0x35, 0x35, 0x36]
        t_lock = time.time()
        while time.time() - t_lock < 9.5:
            c.tester_present()
            r = c.request_seed(1)
            assert not r.positive and r.code == 0x37
            time.sleep(1.0)
        time.sleep(max(0.0, 10.05 - (time.time() - t_lock)))
        assert c.request_seed(1).positive


@pytest.mark.req("REQ-06")
def test_replayed_command_rejected_security(env):
    """An attacker records a valid frame and sends it again later: the counter check rejects it."""
    env.start_normal(steer=0.0)
    env.restbus.set(steer=30.0)
    time.sleep(0.3)
    env.restbus.set(steer=0.0)
    time.sleep(0.4)
    old = env.restbus.log[-40][3]  # a frame from when 30 deg was commanded
    import can
    for _ in range(5):
        env.bus.send(can.Message(arbitration_id=0x100, is_extended_id=False, data=old, dlc=8))
        time.sleep(0.002)
    time.sleep(0.2)
    assert max(abs(s.steer) for s in env.monitor.history[-12:]) == 0.0


@pytest.mark.req("REQ-07")
def test_forged_command_rejected_security(env):
    """A third node sends steering frames with a guessed CRC: rejected unless the CRC is right."""
    import can
    env.start_normal(steer=0.0)
    rng = random.Random(7)
    accepted = 0
    for _ in range(200):
        data = bytes([rng.randrange(256), rng.randrange(16), 0x58, 0x02, 0, 0, 0, 0])  # 60.0 deg
        env.bus.send(can.Message(arbitration_id=0x100, is_extended_id=False, data=data, dlc=8))
        time.sleep(0.001)
    time.sleep(0.2)
    accepted = sum(1 for s in env.monitor.history[-20:] if s.steer != 0.0)
    assert accepted == 0


@pytest.mark.req("REQ-19")
def test_req19_uds_request_fuzzing(env):
    """Random requests, single and multi-frame, up to 4095 bytes. The env fixture fails the test
    on any sanitizer report; here the ECU must still answer a valid request afterwards."""
    rng = random.Random(20261108)
    sids = [0x10, 0x11, 0x14, 0x19, 0x22, 0x27, 0x2E, 0x31, 0x34, 0x36, 0x37, 0x3E, 0x85, 0x00, 0xFF]
    addr = isotp.Address(isotp.AddressingMode.Normal_11bits, rxid=RESP_ID, txid=REQ_ID)
    stack = isotp.NotifierBasedCanStack(env.bus, env.notifier, address=addr,
                                        params={"tx_padding": PAD, "tx_data_min_length": 8, "blocksize": 0, "stmin": 0})
    stack.start()
    try:
        sent = 0
        for i in range(600):
            n = rng.choice([1, 2, 3, 4, 7, 11, 20, 64, 300, 1100, 4095]) if i % 10 else rng.randrange(1, 8)
            payload = bytes([rng.choice(sids)]) + bytes(rng.randrange(256) for _ in range(n - 1))
            if 0x11 == payload[0]:
                payload = bytes([0x11, 0x02])  # do not reset the ECU on purpose here
            stack.send(payload)
            t_end = time.time() + 2.0
            while stack.transmitting() and time.time() < t_end:
                time.sleep(0.001)
            while stack.available():
                stack.recv()
            sent += 1
        assert sent == 600
    finally:
        stack.stop()
    time.sleep(0.2)
    resp, _ = raw_request(env.bus, env.recorder, bytes([0x22, 0x01, 0x00]))
    assert resp is not None and resp[0] == 0x62
    assert env.proc.poll() is None, "ECU process died"
