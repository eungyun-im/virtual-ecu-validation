/* The safety monitor virtual ECU: command monitoring, state machine, outputs, status message,
 * DTC memory and the UDS server, behind three calls. The caller owns the bus and the clock:
 *   vecu_init(&e, tx, ctx, now)   once
 *   vecu_rx(&e, &frame, now)      for every received frame
 *   vecu_tick(&e, now)            every 1 ms
 * Times are uint32 milliseconds; differences are wrap-safe. */
#ifndef VECU_ECU_H
#define VECU_ECU_H

#include "vecu/dtc.h"
#include "vecu/e2e.h"
#include "vecu/isotp.h"
#include "vecu/swupdate.h"
#include "vecu/vecu_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  vecu_e2e_rx_t e2e;
  uint32_t last_rx_ms;    /* any frame with this ID (timeout supervision) */
  uint8_t seen;           /* a frame has arrived since start-up */
  uint8_t timed_out;
  uint8_t invalid_run;    /* consecutive frames discarded by E2E */
  uint8_t streak;         /* valid frames arriving continuously */
  uint32_t streak_start_ms;
  int32_t cmd_raw;        /* last accepted command */
  uint8_t have_cmd;
} vecu_chan_t;

typedef enum { UDS_SESSION_DEFAULT = 1, UDS_SESSION_PROGRAMMING = 2, UDS_SESSION_EXTENDED = 3 } vecu_session_t;

typedef struct {
  vecu_session_t session;
  uint32_t last_req_ms;
  uint8_t unlocked;
  uint8_t seed_pending;
  uint32_t seed;
  uint8_t failed_attempts;
  uint8_t locked_out;
  uint32_t lockout_until_ms;
  uint32_t seed_state; /* PRNG */
  uint8_t reset_pending;
} vecu_uds_t;

typedef void (*vecu_trace_fn)(void *ctx, vecu_state_t from, vecu_state_t to, uint32_t now);

typedef struct vecu {
  /* volatile part, re-initialised by power-up and ECUReset */
  vecu_state_t state;
  uint32_t state_since_ms;
  vecu_chan_t steer, speed;
  int32_t steer_out;
  int32_t speed_out;
  uint8_t faults;
  uint32_t next_cycle_ms, next_status_ms;
  uint8_t status_counter;
  vecu_uds_t uds;
  vecu_isotp_t isotp;
  /* persistent part: DTC memory and "flash" */
  vecu_dtc_mem_t dtc;
  vecu_flash_t flash;
  /* environment */
  vecu_tx_fn tx;
  void *tx_ctx;
  vecu_trace_fn trace;
  void *trace_ctx;
  uint32_t transitions;
} vecu_t;

void vecu_init(vecu_t *e, vecu_tx_fn tx, void *ctx, uint32_t now);
void vecu_rx(vecu_t *e, const vecu_frame_t *f, uint32_t now);
void vecu_tick(vecu_t *e, uint32_t now);
/* ECUReset: volatile part re-initialised, DTC memory and flash kept, staged image activated */
void vecu_reset(vecu_t *e, uint32_t now);

/* UDS server (uds.c). Returns the response length (0 = no response). */
uint16_t vecu_uds_handle(vecu_t *e, const uint8_t *req, uint16_t len, int functional, uint32_t now, uint8_t *resp);
void vecu_uds_init(vecu_uds_t *u, uint32_t now);
void vecu_uds_tick(vecu_t *e, uint32_t now);
uint32_t vecu_uds_key(uint32_t seed);

#ifdef __cplusplus
}
#endif

#endif
