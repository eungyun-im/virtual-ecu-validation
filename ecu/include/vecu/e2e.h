/* End-to-end protection: CRC8 SAE J1850 over Data ID + payload, 4-bit alive counter (REQ-06, REQ-07).
 * Byte 0 = CRC, byte 1 bits 0..3 = counter, bytes 2..7 = data. Similar in spirit to AUTOSAR E2E
 * profile 1 but simplified: the Data ID is always both bytes, and the counter must step by exactly 1. */
#ifndef VECU_E2E_H
#define VECU_E2E_H

#include "vecu/vecu_types.h"

#ifdef __cplusplus
extern "C" {
#endif

uint8_t vecu_crc8_j1850(const uint8_t *data, size_t len, uint8_t crc);
uint8_t vecu_e2e_crc(uint16_t data_id, const uint8_t payload[8]);
/* Fill byte 0 and the counter nibble of byte 1 */
void vecu_e2e_protect(uint16_t data_id, uint8_t counter, uint8_t payload[8]);

typedef enum { VECU_E2E_OK = 0, VECU_E2E_BAD_CRC = 1, VECU_E2E_BAD_COUNTER = 2, VECU_E2E_BAD_DLC = 3 } vecu_e2e_result_t;

typedef struct {
  uint8_t last_counter;
  uint8_t synced; /* 0 until the first accepted frame, and again after a timeout */
} vecu_e2e_rx_t;

void vecu_e2e_rx_reset(vecu_e2e_rx_t *s);
/* Checks CRC first, then the counter. Updates the counter state only for an accepted frame. */
vecu_e2e_result_t vecu_e2e_check(vecu_e2e_rx_t *s, uint16_t data_id, const vecu_frame_t *f);

#ifdef __cplusplus
}
#endif

#endif
