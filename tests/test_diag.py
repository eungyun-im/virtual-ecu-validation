"""Diagnostics on the running ECU: services, NRCs, DTC status byte, P2 timing, session timeout."""
import time

import pytest
from udsoncan import Dtc, services

from vecu.uds_client import raw_request

pytestmark = pytest.mark.regression


@pytest.mark.smoke
@pytest.mark.req("REQ-14")
def test_req14_read_identifiers(env):
    env.start_normal(steer=-12.5, speed=42.0)
    time.sleep(0.4)
    with env.uds() as c:
        r = c.read_data_by_identifier([0xF190, 0xF195, 0x0100, 0x0102, 0x0103])
    v = r.service_data.values
    assert v[0xF190] == "VECUDEMO000000001"
    assert v[0xF195] == 0x00010000
    assert v[0x0100] == 0
    assert v[0x0102] == -125
    assert v[0x0103] == 4200


@pytest.mark.req("REQ-14")
def test_req14_sessions_and_tester_present(env):
    with env.uds() as c:
        r = c.change_session(3)
        assert r.positive and r.service_data.p2_server_max == 0.05
        assert c.tester_present().positive
        assert c.change_session(1).positive


NRC_CASES = [  # request, expected NRC
    (bytes([0x2E, 0xF1, 0x90]), 0x11),
    (bytes([0x10, 0x09]), 0x12),
    (bytes([0x10]), 0x13),
    (bytes([0x22, 0xAB, 0xCD]), 0x31),
    (bytes([0x19, 0x55]), 0x12),
    (bytes([0x14, 0x00, 0x00, 0x00]), 0x31),
    (bytes([0x34, 0x00, 0x44]), 0x7F),
]


@pytest.mark.req("REQ-14")
@pytest.mark.parametrize("req,nrc", NRC_CASES, ids=[r.hex() for r, _ in NRC_CASES])
def test_req14_negative_responses(env, req, nrc):
    resp, _ = raw_request(env.bus, env.recorder, req)
    assert resp is not None
    assert resp[0] == 0x7F and resp[1] == req[0] and resp[2] == nrc


@pytest.mark.req("REQ-14")
def test_req14_functional_unknown_service_silent(env):
    resp, _ = raw_request(env.bus, env.recorder, bytes([0x2E, 0x00]), functional=True, timeout=0.2)
    assert resp is None
    resp, _ = raw_request(env.bus, env.recorder, bytes([0x3E, 0x00]), functional=True)
    assert resp == bytes([0x7E, 0x00])


@pytest.mark.req("REQ-13")
def test_req13_dtc_status_byte_lifecycle(env):
    env.start_normal()
    time.sleep(0.2)
    env.restbus.set(steer=700.0)
    time.sleep(0.1)
    with env.uds() as c:
        dtcs = {d.id: d.status.get_byte_as_int() for d in c.get_dtc_by_status_mask(0xFF).service_data.dtcs}
        assert dtcs[0x4A0011] == 0x2F
        env.restbus.set(steer=0.0)
        time.sleep(0.1)
        dtcs = {d.id: d.status.get_byte_as_int() for d in c.get_dtc_by_status_mask(0xFF).service_data.dtcs}
        assert dtcs[0x4A0011] == 0x2E
        assert c.clear_dtc(0xFFFFFF).positive
        r = c.get_number_of_dtc_by_status_mask(0x08)
        assert r.service_data.dtc_count == 0
        supported = {d.id: d.status.get_byte_as_int() for d in c.get_supported_dtc().service_data.dtcs}
        assert len(supported) == 6
        # After the clear: testNotCompletedSinceLastClear | testNotCompletedThisOperationCycle (0x50).
        # The range monitors run on every valid command frame (10 ms), so a range DTC may already
        # show "tested, passed" (0x00) by the time the response is read; the timeout and E2E
        # monitors have not run since the clear and must still read 0x50.
        for code, status in supported.items():
            if code in (0x4A0011, 0x4A0111):
                assert status in (0x50, 0x00), hex(status)
            else:
                assert status == 0x50, hex(status)


@pytest.mark.smoke
@pytest.mark.req("REQ-15")
def test_req15_p2_timing(env):
    env.start_normal()
    requests = [bytes([0x3E, 0x00]), bytes([0x22, 0x01, 0x00]), bytes([0x19, 0x01, 0x08]),
                bytes([0x22, 0xF1, 0x90]), bytes([0x10, 0x03]), bytes([0x10, 0x01]), bytes([0x2E, 0x00])]
    lat = []
    for _ in range(20):
        for q in requests:
            resp, t = raw_request(env.bus, env.recorder, q)
            assert resp is not None, q.hex()
            lat.append(t * 1000.0)
            time.sleep(0.005)
    env.p2_ms = lat
    assert max(lat) < 50.0, f"max P2 {max(lat):.2f} ms"


@pytest.mark.slow
@pytest.mark.req("REQ-18")
def test_req18_session_timeout(env):
    with env.uds() as c:
        c.change_session(3)
        c.unlock_security_access(1)
        time.sleep(4.8)
        r = c.read_data_by_identifier(0x0100)  # any request restarts S3
        time.sleep(4.8)
        assert c.request_seed(1).service_data.seed == b"\x00\x00\x00\x00", "still unlocked at 4.8 s"
        time.sleep(5.2)
        r = c.request_seed(1)
        assert not r.positive and r.code == 0x7F, "back in the default session after S3"


@pytest.mark.req("REQ-18")
def test_req18_tester_present_keeps_session(env):
    with env.uds() as c:
        c.change_session(3)
        for _ in range(4):
            time.sleep(2.0)
            c.tester_present()
        assert c.request_seed(1).positive
