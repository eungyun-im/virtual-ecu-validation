/* Shared types and the numbers fixed by the requirements (requirements/requirements.yaml).
 * The core has no I/O and no clock of its own: the caller passes the time in ms, so every timing
 * requirement can be tested exactly on a simulated clock and again in real time on a CAN bus. */
#ifndef VECU_TYPES_H
#define VECU_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t id;
  uint8_t dlc;
  uint8_t data[8];
} vecu_frame_t;

/* CAN identifiers (dbc/vecu.dbc) */
#define VECU_ID_STEER_CMD 0x100u
#define VECU_ID_SPEED_CMD 0x101u
#define VECU_ID_STATUS 0x200u
#define VECU_ID_UDS_PHYS 0x7E0u
#define VECU_ID_UDS_FUNC 0x7DFu
#define VECU_ID_UDS_RESP 0x7E8u

/* E2E Data IDs */
#define VECU_DATAID_STEER 0x1A01u
#define VECU_DATAID_SPEED 0x1A02u
#define VECU_DATAID_STATUS 0x1A03u

/* Signal scaling (dbc/vecu.dbc): SteerAngleCmd and SteerOut 0.1 deg per bit, SpeedCmd and SpeedOut
 * 0.01 km/h per bit. The core works in raw units only. */

/* Limits in raw units (REQ-01, REQ-02, REQ-03) */
#define VECU_STEER_MIN_RAW (-5400)      /* -540.0 deg */
#define VECU_STEER_MAX_RAW 5400         /* +540.0 deg */
#define VECU_SPEED_MAX_RAW 20000u       /* 200.00 km/h */
#define VECU_STEER_RATE_RAW 50          /* 5.0 deg per control cycle */
#define VECU_SPEED_RAMP_RAW 10u         /* 0.10 km/h per control cycle = 10 km/h/s */

/* Timing in ms */
#define VECU_CYCLE_MS 10u               /* control cycle */
#define VECU_STATUS_PERIOD_MS 20u       /* REQ-12 */
#define VECU_CMD_TIMEOUT_MS 30u         /* REQ-04, REQ-05 */
#define VECU_RECOVER_SAFE_MS 1000u      /* REQ-10 */
#define VECU_RECOVER_DEGRADED_MS 100u   /* REQ-11 */
#define VECU_INVALID_TO_DEGRADED 3u     /* REQ-08 */
#define VECU_INVALID_TO_SAFE 10u        /* REQ-08 */

typedef enum { VECU_NORMAL = 0, VECU_DEGRADED = 1, VECU_SAFE_STOP = 2, VECU_INIT = 3 } vecu_state_t;

/* Fault bits (ECU_STATUS byte 6) */
#define VECU_F_STEER_RANGE 0x01u
#define VECU_F_SPEED_RANGE 0x02u
#define VECU_F_STEER_TIMEOUT 0x04u
#define VECU_F_SPEED_TIMEOUT 0x08u
#define VECU_F_STEER_E2E 0x10u
#define VECU_F_SPEED_E2E 0x20u
#define VECU_F_RATE_LIMITED 0x40u

/* True when 'now' has reached 'deadline' on the wrapping 32-bit millisecond clock: the forward
 * distance from deadline to now is below half the range. */
static inline bool vecu_time_reached(uint32_t now, uint32_t deadline)
{
  return (uint32_t)(now - deadline) < 0x80000000u;
}

typedef void (*vecu_tx_fn)(void *ctx, const vecu_frame_t *f);

#ifdef __cplusplus
}
#endif

#endif
