"""E2E protection used on STEER_CMD, SPEED_CMD and ECU_STATUS (same algorithm as ecu/src/e2e.c).

CRC8 SAE J1850 (poly 0x1D, init 0xFF, xor-out 0xFF) over Data ID low byte, Data ID high byte and
payload bytes 1..7, stored in byte 0. Alive counter in byte 1 bits 0..3.
"""

DATA_ID = {0x100: 0x1A01, 0x101: 0x1A02, 0x200: 0x1A03}


def crc8_j1850(data, crc=0xFF):
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1D) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def e2e_crc(data_id, payload):
    crc = crc8_j1850(bytes([data_id & 0xFF, data_id >> 8]))
    return crc8_j1850(bytes(payload[1:8]), crc) ^ 0xFF


def protect(data_id, counter, payload):
    p = bytearray(payload)
    p[1] = (p[1] & 0xF0) | (counter & 0x0F)
    p[0] = e2e_crc(data_id, p)
    return bytes(p)


def check(data_id, payload):
    return len(payload) == 8 and e2e_crc(data_id, payload) == payload[0]
