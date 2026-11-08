#include "vecu/e2e.h"

uint8_t vecu_crc8_j1850(const uint8_t *data, size_t len, uint8_t crc)
{
  size_t i;
  int b;
  for (i = 0u; i < len; ++i) {
    crc = (uint8_t)(crc ^ data[i]);
    for (b = 0; b < 8; ++b) {
      if ((crc & 0x80u) != 0u) {
        crc = (uint8_t)(((unsigned)crc << 1) ^ 0x1Du);
      } else {
        crc = (uint8_t)((unsigned)crc << 1);
      }
    }
  }
  return crc;
}

uint8_t vecu_e2e_crc(uint16_t data_id, const uint8_t payload[8])
{
  const uint8_t id[2] = {(uint8_t)(data_id & 0xFFu), (uint8_t)(data_id >> 8)};
  uint8_t crc = vecu_crc8_j1850(id, 2u, 0xFFu);
  crc = vecu_crc8_j1850(&payload[1], 7u, crc);
  return (uint8_t)(crc ^ 0xFFu);
}

void vecu_e2e_protect(uint16_t data_id, uint8_t counter, uint8_t payload[8])
{
  payload[1] = (uint8_t)((payload[1] & 0xF0u) | (counter & 0x0Fu));
  payload[0] = vecu_e2e_crc(data_id, payload);
}

void vecu_e2e_rx_reset(vecu_e2e_rx_t *s)
{
  s->last_counter = 0u;
  s->synced = 0u;
}

vecu_e2e_result_t vecu_e2e_check(vecu_e2e_rx_t *s, uint16_t data_id, const vecu_frame_t *f)
{
  vecu_e2e_result_t r = VECU_E2E_OK;
  const uint8_t counter = (uint8_t)(f->data[1] & 0x0Fu);
  if (f->dlc != 8u) {
    r = VECU_E2E_BAD_DLC;
  } else if (vecu_e2e_crc(data_id, f->data) != f->data[0]) {
    r = VECU_E2E_BAD_CRC;
  } else if (s->synced == 0u) {
    s->last_counter = counter; /* first frame after start-up or a timeout */
    s->synced = 1u;
  } else {
    const uint8_t delta = (uint8_t)((uint8_t)(counter - s->last_counter) & 0x0Fu);
    if (delta == 0u) {
      r = VECU_E2E_BAD_COUNTER; /* repeated: never accepted, no resync */
    } else if (delta > VECU_E2E_MAX_DELTA) {
      r = VECU_E2E_BAD_COUNTER; /* wrong sequence: discard, then follow the sender (DEF-001) */
      s->last_counter = counter;
    } else {
      s->last_counter = counter; /* 1..MAX_DELTA: up to MAX_DELTA - 1 frames were lost */
    }
  }
  return r;
}
