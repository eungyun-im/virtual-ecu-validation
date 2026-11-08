/* ISO 15765-2 transport (ISO-TP) in user space, so UDS works on any bus, including the virtual
 * bus used where SocketCAN is not available. Classic CAN, 8-byte frames padded with 0xCC,
 * normal addressing, messages up to 4095 bytes. The receiver answers a first frame with
 * flow control CTS, BS 0, STmin 0. The sender honours the tester's BS and STmin. */
#ifndef VECU_ISOTP_H
#define VECU_ISOTP_H

#include "vecu/vecu_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VECU_ISOTP_MAX 4095u
#define VECU_ISOTP_PAD 0xCCu
#define VECU_ISOTP_N_BS_MS 1000u /* wait for flow control */
#define VECU_ISOTP_N_CR_MS 1000u /* wait for the next consecutive frame */

typedef struct {
  uint32_t tx_id;
  vecu_tx_fn tx;
  void *tx_ctx;
  /* receive */
  uint8_t rx_buf[VECU_ISOTP_MAX];
  uint16_t rx_len;
  uint16_t rx_got;
  uint8_t rx_sn;
  uint8_t rx_active;
  uint32_t rx_deadline;
  /* transmit */
  uint8_t tx_buf[VECU_ISOTP_MAX];
  uint16_t tx_len;
  uint16_t tx_sent;
  uint8_t tx_sn;
  uint8_t tx_state; /* 0 idle, 1 wait FC, 2 sending CFs */
  uint8_t tx_bs;
  uint8_t tx_bs_left;
  uint8_t tx_stmin;
  uint32_t tx_next_ms;
  uint32_t tx_deadline;
  uint32_t errors; /* dropped frames and timeouts, for diagnostics */
} vecu_isotp_t;

void vecu_isotp_init(vecu_isotp_t *t, uint32_t tx_id, vecu_tx_fn tx, void *ctx);
/* Feed a frame addressed to us. Returns the length of a completed message (copied to out) or 0. */
uint16_t vecu_isotp_on_frame(vecu_isotp_t *t, const vecu_frame_t *f, uint32_t now, uint8_t *out);
/* Start sending; returns 0, or -1 if busy or too long */
int vecu_isotp_send(vecu_isotp_t *t, const uint8_t *data, uint16_t len, uint32_t now);
void vecu_isotp_tick(vecu_isotp_t *t, uint32_t now);
bool vecu_isotp_busy(const vecu_isotp_t *t);

#ifdef __cplusplus
}
#endif

#endif
