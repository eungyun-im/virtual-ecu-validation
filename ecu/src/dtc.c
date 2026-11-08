#include "vecu/dtc.h"

/* 3-byte DTC numbers. C-codes (chassis, top bits 01) for command range, U-codes (network, 11) for
 * communication. Failure type byte: 0x11 = signal out of range? used as "above maximum", 0x87 = missing
 * message, 0x81 = invalid serial data received. */
uint32_t vecu_dtc_code(vecu_dtc_idx_t i)
{
  static const uint32_t k_codes[DTC_COUNT] = {
      0x4A0011u, /* STEER_RANGE */
      0x4A0111u, /* SPEED_RANGE */
      0xC10087u, /* STEER_TIMEOUT */
      0xC10187u, /* SPEED_TIMEOUT */
      0xC10281u, /* STEER_E2E */
      0xC10381u, /* SPEED_E2E */
  };
  return ((unsigned)i < (unsigned)DTC_COUNT) ? k_codes[i] : 0u;
}

void vecu_dtc_clear(vecu_dtc_mem_t *m)
{
  int i;
  for (i = 0; i < (int)DTC_COUNT; ++i) {
    m->status[i] = (uint8_t)(DTC_TNCSLC | DTC_TNCTOC);
  }
}

void vecu_dtc_new_cycle(vecu_dtc_mem_t *m)
{
  int i;
  for (i = 0; i < (int)DTC_COUNT; ++i) {
    m->status[i] = (uint8_t)((m->status[i] & (uint8_t)~DTC_TFTOC) | DTC_TNCTOC);
  }
}

void vecu_dtc_report(vecu_dtc_mem_t *m, vecu_dtc_idx_t i, bool failed)
{
  if ((unsigned)i < (unsigned)DTC_COUNT) {
    uint8_t s = (uint8_t)(m->status[i] & (uint8_t)~(DTC_TNCSLC | DTC_TNCTOC));
    if (failed) {
      s = (uint8_t)(s | DTC_TF | DTC_TFTOC | DTC_PDTC | DTC_CDTC | DTC_TFSLC);
    } else {
      s = (uint8_t)(s & (uint8_t)~DTC_TF);
    }
    m->status[i] = s;
  }
}

uint8_t vecu_dtc_confirmed_count(const vecu_dtc_mem_t *m)
{
  uint8_t n = 0u;
  int i;
  for (i = 0; i < (int)DTC_COUNT; ++i) {
    if ((m->status[i] & DTC_CDTC) != 0u) {
      ++n;
    }
  }
  return n;
}
