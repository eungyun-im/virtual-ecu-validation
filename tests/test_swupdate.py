"""Software update over UDS (REQ-20): good image, bad hash, old version, interrupted transfer."""
import time

import pytest
from udsoncan import DataFormatIdentifier, MemoryLocation

from vecu.image import build_image

pytestmark = pytest.mark.regression


def programming(c):
    assert c.change_session(3).positive
    assert c.unlock_security_access(1).positive
    assert c.change_session(2).positive


def download(c, image, version, stop_after=None):
    loc = MemoryLocation(address=version, memorysize=len(image), address_format=32, memorysize_format=32)
    r = c.request_download(loc, dfi=DataFormatIdentifier(compression=0, encryption=0))
    if not r.positive:
        return r
    block = r.service_data.max_length - 2
    seq = 1
    for off in range(0, len(image), block):
        if stop_after is not None and off >= stop_after:
            return None
        r = c.transfer_data(seq & 0xFF, image[off:off + block])
        assert r.positive, r
        seq += 1
    return c.request_transfer_exit()


def version(c):
    return c.read_data_by_identifier(0xF195).service_data.values[0xF195]


@pytest.mark.smoke
@pytest.mark.req("REQ-20")
def test_req20_good_image(env):
    img = build_image(0x00010200, 6000, seed=1)
    with env.uds() as c:
        programming(c)
        assert download(c, img, 0x00010200).positive
        assert version(c) == 0x00010000
        assert c.ecu_reset(1).positive
        time.sleep(0.1)
        assert version(c) == 0x00010200


@pytest.mark.req("REQ-20")
def test_req20_bad_hash_rejected(env):
    img = bytearray(build_image(0x00020000, 4000, seed=2))
    img[100] ^= 0xFF
    with env.uds() as c:
        programming(c)
        r = download(c, bytes(img), 0x00020000)
        assert not r.positive and r.code == 0x72
        c.ecu_reset(1)
        time.sleep(0.1)
        assert version(c) == 0x00010000


@pytest.mark.req("REQ-20")
def test_req20_old_version_rejected(env):
    img = build_image(0x00010000, 1000, seed=3)
    with env.uds() as c:
        programming(c)
        r = download(c, img, 0x00010000)
        assert not r.positive and r.code == 0x70


@pytest.mark.slow
@pytest.mark.req("REQ-20")
def test_req20_interrupted_transfer_keeps_version(env):
    img = build_image(0x00030000, 9000, seed=4)
    with env.uds() as c:
        programming(c)
        assert download(c, img, 0x00030000, stop_after=4000) is None
    time.sleep(5.3)  # tester gone: S3 ends the programming session
    with env.uds() as c:
        c.ecu_reset(1)
        time.sleep(0.1)
        assert version(c) == 0x00010000
