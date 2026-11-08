/* Simulated environment for unit tests: an ECU instance, a 1 ms clock and a capture of every
 * transmitted frame. sim_drive() plays the rest-bus: STEER_CMD and SPEED_CMD every 10 ms. */
#ifndef SIM_H
#define SIM_H

#include <string.h>

#include "vecu/ecu.h"

#define SIM_MAX_TX 20000

typedef struct {
  vecu_t e;
  uint32_t now;
  vecu_frame_t tx[SIM_MAX_TX];
  uint32_t tx_ms[SIM_MAX_TX];
  int ntx;
  uint8_t sc, vc;            /* alive counters of the two command channels */
  vecu_state_t trace_to[64];
  uint32_t trace_ms[64];
  int ntrace;
} sim_t;

#define SIM_STEER 0x1
#define SIM_SPEED 0x2
#define SIM_BOTH (SIM_STEER | SIM_SPEED)

static void sim_tx(void *ctx, const vecu_frame_t *f)
{
  sim_t *s = (sim_t *)ctx;
  if (s->ntx < SIM_MAX_TX) {
    s->tx_ms[s->ntx] = s->now;
    s->tx[s->ntx++] = *f;
  }
}

static void sim_trace(void *ctx, vecu_state_t from, vecu_state_t to, uint32_t now)
{
  sim_t *s = (sim_t *)ctx;
  (void)from;
  if (s->ntrace < 64) {
    s->trace_to[s->ntrace] = to;
    s->trace_ms[s->ntrace++] = now;
  }
}

static void sim_init(sim_t *s)
{
  memset(s, 0, sizeof *s);
  s->now = 1000u;
  vecu_init(&s->e, sim_tx, s, s->now);
  s->e.trace = sim_trace;
  s->e.trace_ctx = s;
}

static vecu_frame_t sim_cmd(uint32_t id, uint16_t data_id, int32_t raw, uint8_t counter)
{
  vecu_frame_t f;
  memset(&f, 0, sizeof f);
  f.id = id;
  f.dlc = 8u;
  f.data[2] = (uint8_t)((uint32_t)raw & 0xFFu);
  f.data[3] = (uint8_t)(((uint32_t)raw >> 8) & 0xFFu);
  vecu_e2e_protect(data_id, counter, f.data);
  return f;
}

static void sim_send_steer(sim_t *s, int32_t raw)
{
  const vecu_frame_t f = sim_cmd(VECU_ID_STEER_CMD, VECU_DATAID_STEER, raw, s->sc);
  s->sc = (uint8_t)((s->sc + 1u) & 0x0Fu);
  vecu_rx(&s->e, &f, s->now);
}

static void sim_send_speed(sim_t *s, int32_t raw)
{
  const vecu_frame_t f = sim_cmd(VECU_ID_SPEED_CMD, VECU_DATAID_SPEED, raw, s->vc);
  s->vc = (uint8_t)((s->vc + 1u) & 0x0Fu);
  vecu_rx(&s->e, &f, s->now);
}

static void sim_step(sim_t *s)
{
  ++s->now;
  vecu_tick(&s->e, s->now);
}

/* run for ms milliseconds; send the selected commands whenever now % 10 == 0 */
static void sim_drive(sim_t *s, uint32_t ms, int32_t steer, int32_t speed, int which)
{
  uint32_t i;
  for (i = 0u; i < ms; ++i) {
    sim_step(s);
    if ((s->now % 10u) == 0u) {
      if (which & SIM_STEER) sim_send_steer(s, steer);
      if (which & SIM_SPEED) sim_send_speed(s, speed);
    }
  }
}

typedef struct {
  int valid;
  int state, faults, dtc_count, counter;
  int32_t steer, speed;
  uint32_t ms;
} sim_status_t;

static sim_status_t sim_status_at(const sim_t *s, int k)
{
  sim_status_t r;
  const vecu_frame_t *f = &s->tx[k];
  memset(&r, 0, sizeof r);
  r.valid = vecu_e2e_crc(VECU_DATAID_STATUS, f->data) == f->data[0];
  r.counter = f->data[1] & 0x0F;
  r.state = (f->data[1] >> 4) & 0x03;
  r.steer = (int16_t)(uint16_t)(f->data[2] | (f->data[3] << 8));
  r.speed = (uint16_t)(f->data[4] | (f->data[5] << 8));
  r.faults = f->data[6];
  r.dtc_count = f->data[7];
  r.ms = s->tx_ms[k];
  return r;
}

static sim_status_t sim_last_status(const sim_t *s)
{
  int k;
  sim_status_t none;
  for (k = s->ntx - 1; k >= 0; --k) {
    if (s->tx[k].id == VECU_ID_STATUS) return sim_status_at(s, k);
  }
  memset(&none, 0, sizeof none);
  return none;
}

/* bring the ECU to NORMAL with steady commands */
static void sim_start(sim_t *s, int32_t steer, int32_t speed)
{
  sim_drive(s, 200u, steer, speed, SIM_BOTH);
}

#endif
