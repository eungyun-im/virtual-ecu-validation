#include "vecu/isotp.h"

#include <string.h>

#define N_SF 0x0u
#define N_FF 0x1u
#define N_CF 0x2u
#define N_FC 0x3u
#define CF_PER_TICK 16u /* with STmin 0, at most this many consecutive frames per 1 ms tick */

static void send_frame(vecu_isotp_t *t, const uint8_t *d, uint8_t n)
{
  vecu_frame_t f;
  f.id = t->tx_id;
  f.dlc = 8u;
  (void)memset(f.data, VECU_ISOTP_PAD, 8u);
  (void)memcpy(f.data, d, n);
  t->tx(t->tx_ctx, &f);
}

static void send_fc(vecu_isotp_t *t, uint8_t status)
{
  const uint8_t fc[3] = {(uint8_t)((N_FC << 4) | status), 0u, 0u};
  send_frame(t, fc, 3u);
}

void vecu_isotp_init(vecu_isotp_t *t, uint32_t tx_id, vecu_tx_fn tx, void *ctx)
{
  (void)memset(t, 0, sizeof *t);
  t->tx_id = tx_id;
  t->tx = tx;
  t->tx_ctx = ctx;
}

bool vecu_isotp_busy(const vecu_isotp_t *t) { return t->tx_state != 0u; }

static uint8_t stmin_ms(uint8_t raw)
{
  uint8_t ms;
  if (raw <= 0x7Fu) {
    ms = raw;
  } else if ((raw >= 0xF1u) && (raw <= 0xF9u)) {
    ms = 1u; /* 100-900 us: our tick is 1 ms */
  } else {
    ms = 0x7Fu; /* reserved values: use the maximum */
  }
  return ms;
}

static void on_fc(vecu_isotp_t *t, const vecu_frame_t *f, uint32_t now)
{
  const uint8_t fs = (uint8_t)(f->data[0] & 0x0Fu);
  if (t->tx_state != 1u) {
    /* no flow control expected: ignore */
  } else if (fs == 0u) { /* CTS */
    t->tx_bs = f->data[1];
    t->tx_bs_left = f->data[1];
    t->tx_stmin = stmin_ms(f->data[2]);
    t->tx_state = 2u;
    t->tx_next_ms = now;
  } else if (fs == 1u) { /* WAIT */
    t->tx_deadline = now + VECU_ISOTP_N_BS_MS;
  } else { /* overflow or invalid: abort */
    t->tx_state = 0u;
    ++t->errors;
  }
}

uint16_t vecu_isotp_on_frame(vecu_isotp_t *t, const vecu_frame_t *f, uint32_t now, uint8_t *out)
{
  uint16_t done = 0u;
  const uint8_t pci = (uint8_t)(f->data[0] >> 4);
  if (f->dlc < 1u) {
    /* empty frame: ignore */
  } else if (pci == N_SF) {
    const uint8_t n = (uint8_t)(f->data[0] & 0x0Fu);
    if ((n >= 1u) && (n <= 7u) && ((uint8_t)(n + 1u) <= f->dlc)) {
      t->rx_active = 0u;
      (void)memcpy(out, &f->data[1], n);
      done = n;
    } else {
      ++t->errors;
    }
  } else if (pci == N_FF) {
    const uint16_t len = (uint16_t)(((uint16_t)(f->data[0] & 0x0Fu) << 8) | f->data[1]);
    if ((f->dlc != 8u) || (len < 8u)) {
      ++t->errors;
    } else if (len > VECU_ISOTP_MAX) {
      send_fc(t, 2u);
      ++t->errors;
    } else {
      t->rx_len = len;
      (void)memcpy(t->rx_buf, &f->data[2], 6u);
      t->rx_got = 6u;
      t->rx_sn = 1u;
      t->rx_active = 1u;
      t->rx_deadline = now + VECU_ISOTP_N_CR_MS;
      send_fc(t, 0u);
    }
  } else if (pci == N_CF) {
    const uint8_t sn = (uint8_t)(f->data[0] & 0x0Fu);
    if ((t->rx_active == 0u) || (sn != t->rx_sn)) {
      t->rx_active = 0u;
      ++t->errors;
    } else {
      uint16_t n = (uint16_t)(t->rx_len - t->rx_got);
      if (n > 7u) {
        n = 7u;
      }
      if ((uint16_t)(n + 1u) > f->dlc) {
        t->rx_active = 0u;
        ++t->errors;
      } else {
        (void)memcpy(&t->rx_buf[t->rx_got], &f->data[1], n);
        t->rx_got = (uint16_t)(t->rx_got + n);
        t->rx_sn = (uint8_t)((t->rx_sn + 1u) & 0x0Fu);
        t->rx_deadline = now + VECU_ISOTP_N_CR_MS;
        if (t->rx_got == t->rx_len) {
          t->rx_active = 0u;
          (void)memcpy(out, t->rx_buf, t->rx_len);
          done = t->rx_len;
        }
      }
    }
  } else if (pci == N_FC) {
    on_fc(t, f, now);
  } else {
    ++t->errors;
  }
  return done;
}

int vecu_isotp_send(vecu_isotp_t *t, const uint8_t *data, uint16_t len, uint32_t now)
{
  int rc = 0;
  if ((t->tx_state != 0u) || (len == 0u) || (len > VECU_ISOTP_MAX)) {
    rc = -1;
  } else if (len <= 7u) {
    uint8_t sf[8];
    sf[0] = (uint8_t)len;
    (void)memcpy(&sf[1], data, len);
    send_frame(t, sf, (uint8_t)(len + 1u));
  } else {
    uint8_t ff[8];
    (void)memcpy(t->tx_buf, data, len);
    t->tx_len = len;
    ff[0] = (uint8_t)((N_FF << 4) | (uint8_t)(len >> 8));
    ff[1] = (uint8_t)(len & 0xFFu);
    (void)memcpy(&ff[2], data, 6u);
    send_frame(t, ff, 8u);
    t->tx_sent = 6u;
    t->tx_sn = 1u;
    t->tx_state = 1u;
    t->tx_deadline = now + VECU_ISOTP_N_BS_MS;
  }
  return rc;
}

void vecu_isotp_tick(vecu_isotp_t *t, uint32_t now)
{
  unsigned burst = 0u;
  if ((t->rx_active != 0u) && vecu_time_reached(now, t->rx_deadline)) {
    t->rx_active = 0u;
    ++t->errors;
  }
  if ((t->tx_state == 1u) && vecu_time_reached(now, t->tx_deadline)) {
    t->tx_state = 0u;
    ++t->errors;
  }
  while ((t->tx_state == 2u) && vecu_time_reached(now, t->tx_next_ms) && (burst < CF_PER_TICK)) {
    uint8_t cf[8];
    uint16_t n = (uint16_t)(t->tx_len - t->tx_sent);
    if (n > 7u) {
      n = 7u;
    }
    cf[0] = (uint8_t)((N_CF << 4) | t->tx_sn);
    (void)memcpy(&cf[1], &t->tx_buf[t->tx_sent], n);
    send_frame(t, cf, (uint8_t)(n + 1u));
    t->tx_sent = (uint16_t)(t->tx_sent + n);
    t->tx_sn = (uint8_t)((t->tx_sn + 1u) & 0x0Fu);
    ++burst;
    if (t->tx_sent >= t->tx_len) {
      t->tx_state = 0u;
    } else if (t->tx_bs != 0u) {
      --t->tx_bs_left;
      if (t->tx_bs_left == 0u) {
        t->tx_state = 1u;
        t->tx_deadline = now + VECU_ISOTP_N_BS_MS;
      }
    } else {
      /* block size 0: no further flow control */
    }
    if ((t->tx_state == 2u) && (t->tx_stmin != 0u)) {
      t->tx_next_ms = now + t->tx_stmin; /* the loop condition stops here until STmin has passed */
    }
  }
}
