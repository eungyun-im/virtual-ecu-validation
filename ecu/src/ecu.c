#include "vecu/ecu.h"

#include <string.h>

/* ---------- helpers ---------- */

static bool elapsed(uint32_t now, uint32_t since, uint32_t ms) { return (uint32_t)(now - since) >= ms; }

static int16_t rd_i16(const uint8_t *p) { return (int16_t)(uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }
static uint16_t rd_u16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }

static void chan_reset(vecu_chan_t *c)
{
  (void)memset(c, 0, sizeof *c);
  vecu_e2e_rx_reset(&c->e2e);
}

static void set_fault(vecu_t *e, uint8_t bit, bool on)
{
  if (on) {
    e->faults = (uint8_t)(e->faults | bit);
  } else {
    e->faults = (uint8_t)(e->faults & (uint8_t)~bit);
  }
}

/* ---------- status message (REQ-12) ---------- */

static void send_status(vecu_t *e)
{
  vecu_frame_t f;
  const int16_t so = (int16_t)e->steer_out;
  const uint16_t vo = (uint16_t)e->speed_out;
  f.id = VECU_ID_STATUS;
  f.dlc = 8u;
  f.data[1] = (uint8_t)(((uint8_t)e->state & 0x03u) << 4);
  f.data[2] = (uint8_t)((uint16_t)so & 0xFFu);
  f.data[3] = (uint8_t)((uint16_t)so >> 8);
  f.data[4] = (uint8_t)(vo & 0xFFu);
  f.data[5] = (uint8_t)(vo >> 8);
  f.data[6] = e->faults;
  f.data[7] = vecu_dtc_confirmed_count(&e->dtc);
  vecu_e2e_protect(VECU_DATAID_STATUS, e->status_counter, f.data);
  e->status_counter = (uint8_t)((e->status_counter + 1u) & 0x0Fu);
  e->tx(e->tx_ctx, &f);
}

/* ---------- state machine (REQ-04, 05, 08, 10, 11) ---------- */

static void go(vecu_t *e, vecu_state_t to, uint32_t now)
{
  const vecu_state_t from = e->state;
  if (from != to) {
    e->state = to;
    e->state_since_ms = now;
    ++e->transitions;
    if (e->trace != NULL) {
      e->trace(e->trace_ctx, from, to, now);
    }
    send_status(e); /* REQ-12: extra status frame in the same tick as the change */
  }
}

static bool streak_ok(const vecu_chan_t *c, uint32_t now, uint32_t ms)
{
  return (c->streak != 0u) && elapsed(now, c->streak_start_ms, ms);
}

static void evaluate(vecu_t *e, uint32_t now)
{
  const bool safe = (e->steer.timed_out != 0u) || (e->steer.invalid_run >= VECU_INVALID_TO_SAFE);
  const bool degraded = (e->speed.timed_out != 0u) || (e->steer.invalid_run >= VECU_INVALID_TO_DEGRADED) ||
                       (e->speed.invalid_run >= VECU_INVALID_TO_DEGRADED);
  switch (e->state) {
    case VECU_INIT:
      if (safe) {
        go(e, VECU_SAFE_STOP, now);
      } else if ((e->steer.streak != 0u) && (e->speed.streak != 0u)) {
        go(e, VECU_NORMAL, now);
      } else {
        /* wait for the first valid commands */
      }
      break;
    case VECU_NORMAL:
      if (safe) {
        go(e, VECU_SAFE_STOP, now);
      } else if (degraded) {
        go(e, VECU_DEGRADED, now);
      } else {
        /* stay */
      }
      break;
    case VECU_DEGRADED:
      if (safe) {
        go(e, VECU_SAFE_STOP, now);
      } else if (!degraded && streak_ok(&e->steer, now, VECU_RECOVER_DEGRADED_MS) &&
                 streak_ok(&e->speed, now, VECU_RECOVER_DEGRADED_MS)) {
        go(e, VECU_NORMAL, now);
      } else {
        /* stay */
      }
      break;
    default: /* SAFE_STOP: only back to NORMAL, never to DEGRADED (REQ-11) */
      if (!safe && !degraded && streak_ok(&e->steer, now, VECU_RECOVER_SAFE_MS) &&
          streak_ok(&e->speed, now, VECU_RECOVER_SAFE_MS)) {
        go(e, VECU_NORMAL, now);
      }
      break;
  }
}

/* ---------- command reception (REQ-01, 02, 06, 07, 08) ---------- */

static void on_command(vecu_t *e, vecu_chan_t *c, uint16_t data_id, const vecu_frame_t *f, uint32_t now)
{
  const bool is_steer = (c == &e->steer);
  vecu_e2e_result_t r;
  c->last_rx_ms = now;
  c->seen = 1u;
  r = vecu_e2e_check(&c->e2e, data_id, f);
  if (r != VECU_E2E_OK) {
    c->streak = 0u;
    if (c->invalid_run < 255u) {
      ++c->invalid_run;
    }
    if (c->invalid_run >= VECU_INVALID_TO_DEGRADED) {
      set_fault(e, is_steer ? VECU_F_STEER_E2E : VECU_F_SPEED_E2E, true);
      vecu_dtc_report(&e->dtc, is_steer ? DTC_STEER_E2E : DTC_SPEED_E2E, true);
    }
  } else {
    int32_t raw;
    bool in_range;
    if (c->timed_out != 0u) {
      c->timed_out = 0u;
      set_fault(e, is_steer ? VECU_F_STEER_TIMEOUT : VECU_F_SPEED_TIMEOUT, false);
      vecu_dtc_report(&e->dtc, is_steer ? DTC_STEER_TIMEOUT : DTC_SPEED_TIMEOUT, false);
    }
    if (c->invalid_run != 0u) {
      c->invalid_run = 0u;
      set_fault(e, is_steer ? VECU_F_STEER_E2E : VECU_F_SPEED_E2E, false);
      vecu_dtc_report(&e->dtc, is_steer ? DTC_STEER_E2E : DTC_SPEED_E2E, false);
    }
    if (is_steer) {
      raw = rd_i16(&f->data[2]);
      in_range = (raw >= VECU_STEER_MIN_RAW) && (raw <= VECU_STEER_MAX_RAW);
    } else {
      raw = (int32_t)rd_u16(&f->data[2]);
      in_range = raw <= (int32_t)VECU_SPEED_MAX_RAW;
    }
    set_fault(e, is_steer ? VECU_F_STEER_RANGE : VECU_F_SPEED_RANGE, !in_range);
    vecu_dtc_report(&e->dtc, is_steer ? DTC_STEER_RANGE : DTC_SPEED_RANGE, !in_range);
    if (in_range) {
      c->cmd_raw = raw;
      c->have_cmd = 1u;
      if (c->streak == 0u) {
        c->streak = 1u;
        c->streak_start_ms = now;
      }
    } else {
      c->streak = 0u; /* an out-of-range command is not a valid frame */
    }
  }
  evaluate(e, now);
}

/* ---------- control cycle (REQ-03, 05, 09) ---------- */

static void control_cycle(vecu_t *e)
{
  bool rate_limited = false;
  if ((e->state == VECU_NORMAL) || (e->state == VECU_DEGRADED)) {
    if (e->steer.have_cmd != 0u) {
      int32_t d = e->steer.cmd_raw - e->steer_out;
      if (d > VECU_STEER_RATE_RAW) {
        d = VECU_STEER_RATE_RAW;
        rate_limited = true;
      } else if (d < -VECU_STEER_RATE_RAW) {
        d = -VECU_STEER_RATE_RAW;
        rate_limited = true;
      } else {
        /* within the limit */
      }
      e->steer_out += d;
    }
    if (e->speed.timed_out != 0u) {
      e->speed_out = (e->speed_out > (int32_t)VECU_SPEED_RAMP_RAW) ? (e->speed_out - (int32_t)VECU_SPEED_RAMP_RAW) : 0;
    } else if (e->speed.have_cmd != 0u) {
      e->speed_out = e->speed.cmd_raw;
    } else {
      /* no speed command yet */
    }
  } else if (e->state == VECU_SAFE_STOP) {
    e->speed_out = (e->speed_out > (int32_t)VECU_SPEED_RAMP_RAW) ? (e->speed_out - (int32_t)VECU_SPEED_RAMP_RAW) : 0;
  } else {
    e->steer_out = 0;
    e->speed_out = 0;
  }
  set_fault(e, VECU_F_RATE_LIMITED, rate_limited);
}

/* ---------- diagnostics glue ---------- */

static void on_diag(vecu_t *e, const vecu_frame_t *f, int functional, uint32_t now)
{
  static uint8_t req[VECU_ISOTP_MAX];
  static uint8_t resp[VECU_ISOTP_MAX];
  uint16_t n = 0u;
  if (functional != 0) {
    /* functional requests: single frames only, anything else is ignored */
    const uint8_t pci = f->data[0];
    if (((pci >> 4) == 0u) && ((pci & 0x0Fu) != 0u) && ((pci & 0x0Fu) <= 7u)) {
      n = (uint16_t)(pci & 0x0Fu);
      (void)memcpy(req, &f->data[1], n);
    }
  } else {
    n = vecu_isotp_on_frame(&e->isotp, f, now, req);
  }
  if (n != 0u) {
    const uint16_t rl = vecu_uds_handle(e, req, n, functional, now, resp);
    if (rl != 0u) {
      (void)vecu_isotp_send(&e->isotp, resp, rl, now);
    }
  }
}

/* ---------- public API ---------- */

static void init_volatile(vecu_t *e, uint32_t now)
{
  e->state = VECU_INIT;
  e->state_since_ms = now;
  chan_reset(&e->steer);
  chan_reset(&e->speed);
  e->steer_out = 0;
  e->speed_out = 0;
  e->faults = 0u;
  e->next_cycle_ms = now + VECU_CYCLE_MS;
  e->next_status_ms = now + VECU_STATUS_PERIOD_MS;
  e->status_counter = 0u;
  vecu_uds_init(&e->uds, now);
  vecu_isotp_init(&e->isotp, VECU_ID_UDS_RESP, e->tx, e->tx_ctx);
}

void vecu_init(vecu_t *e, vecu_tx_fn tx, void *ctx, uint32_t now)
{
  (void)memset(e, 0, sizeof *e);
  e->tx = tx;
  e->tx_ctx = ctx;
  vecu_dtc_clear(&e->dtc);
  vecu_flash_init(&e->flash);
  init_volatile(e, now);
}

void vecu_reset(vecu_t *e, uint32_t now)
{
  vecu_swu_on_reset(&e->flash);
  vecu_dtc_new_cycle(&e->dtc);
  init_volatile(e, now);
}

void vecu_rx(vecu_t *e, const vecu_frame_t *f, uint32_t now)
{
  if (f->id == VECU_ID_STEER_CMD) {
    on_command(e, &e->steer, VECU_DATAID_STEER, f, now);
  } else if (f->id == VECU_ID_SPEED_CMD) {
    on_command(e, &e->speed, VECU_DATAID_SPEED, f, now);
  } else if (f->id == VECU_ID_UDS_PHYS) {
    on_diag(e, f, 0, now);
  } else if (f->id == VECU_ID_UDS_FUNC) {
    on_diag(e, f, 1, now);
  } else {
    /* not ours */
  }
}

static void check_timeout(vecu_t *e, vecu_chan_t *c, uint32_t now)
{
  const bool is_steer = (c == &e->steer);
  if ((c->seen != 0u) && (c->timed_out == 0u) && elapsed(now, c->last_rx_ms, VECU_CMD_TIMEOUT_MS)) {
    c->timed_out = 1u;
    c->streak = 0u;
    vecu_e2e_rx_reset(&c->e2e); /* resynchronise on the next frame (REQ-06) */
    set_fault(e, is_steer ? VECU_F_STEER_TIMEOUT : VECU_F_SPEED_TIMEOUT, true);
    vecu_dtc_report(&e->dtc, is_steer ? DTC_STEER_TIMEOUT : DTC_SPEED_TIMEOUT, true);
  }
}

void vecu_tick(vecu_t *e, uint32_t now)
{
  check_timeout(e, &e->steer, now);
  check_timeout(e, &e->speed, now);
  evaluate(e, now);
  if (vecu_time_reached(now, e->next_cycle_ms)) {
    e->next_cycle_ms += VECU_CYCLE_MS;
    control_cycle(e);
  }
  if (vecu_time_reached(now, e->next_status_ms)) {
    e->next_status_ms += VECU_STATUS_PERIOD_MS;
    send_status(e);
  }
  vecu_isotp_tick(&e->isotp, now);
  vecu_uds_tick(e, now);
}
