/* Runtime for the virtual ECU: a 1 ms tick loop on the monotonic clock around the core.
 * usage: vecu --bus socketcan:vcan0 | --bus vbus:127.0.0.1:29536  [--trace]
 * SIGINT/SIGTERM end the loop cleanly (so coverage data is written). */

#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "bus.h"
#include "vecu/ecu.h"

static volatile sig_atomic_t g_stop;
static vecu_bus_t g_bus;
static unsigned long g_tx_errors;

static void on_signal(int s)
{
  (void)s;
  g_stop = 1;
}

static uint64_t mono_us(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static void tx(void *ctx, const vecu_frame_t *f)
{
  (void)ctx;
  if (vecu_bus_send(&g_bus, f) != 0) {
    ++g_tx_errors;
  }
}

static const char *state_name(vecu_state_t s)
{
  static const char *const n[] = {"NORMAL", "DEGRADED", "SAFE_STOP", "INIT"};
  return n[(unsigned)s & 3u];
}

static void trace(void *ctx, vecu_state_t from, vecu_state_t to, uint32_t now)
{
  (void)ctx;
  fprintf(stderr, "[vecu] %u ms %s -> %s\n", (unsigned)now, state_name(from), state_name(to));
}

int main(int argc, char **argv)
{
  static vecu_t ecu;
  const char *spec = "vbus:127.0.0.1:29536";
  int do_trace = 0, i;
  uint64_t t0, next_tick_us, last_keepalive;
  struct sigaction sa;

  for (i = 1; i < argc; ++i) {
    if ((strcmp(argv[i], "--bus") == 0) && (i + 1 < argc)) {
      spec = argv[++i];
    } else if (strcmp(argv[i], "--trace") == 0) {
      do_trace = 1;
    } else {
      fprintf(stderr, "usage: %s [--bus socketcan:<if>|vbus:<host>:<port>] [--trace]\n", argv[0]);
      return 2;
    }
  }
  if (vecu_bus_open(&g_bus, spec) != 0) {
    return 1;
  }
  memset(&sa, 0, sizeof sa);
  sa.sa_handler = on_signal;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);

  t0 = mono_us();
  vecu_init(&ecu, tx, NULL, 0u);
  if (do_trace) {
    ecu.trace = trace;
  }
  fprintf(stderr, "[vecu] running on %s\n", spec);
  next_tick_us = t0 + 1000u;
  last_keepalive = t0;
  while (!g_stop) {
    struct pollfd p = {g_bus.fd, POLLIN, 0};
    uint64_t now_us = mono_us();
    int wait_ms = (now_us >= next_tick_us) ? 0 : (int)((next_tick_us - now_us + 999u) / 1000u);
    vecu_frame_t f;
    (void)poll(&p, 1, wait_ms);
    now_us = mono_us();
    while (now_us >= next_tick_us) { /* catch up if the process was delayed */
      vecu_tick(&ecu, (uint32_t)((next_tick_us - t0) / 1000u));
      next_tick_us += 1000u;
    }
    /* frames are stamped with the last processed tick, so the core never sees time go backwards */
    while (vecu_bus_recv(&g_bus, &f) == 1) {
      vecu_rx(&ecu, &f, (uint32_t)((next_tick_us - 1000u - t0) / 1000u));
    }
    if (now_us - last_keepalive > 500000u) {
      vecu_bus_keepalive(&g_bus);
      last_keepalive = now_us;
    }
  }
  vecu_bus_close(&g_bus);
  fprintf(stderr, "[vecu] stopped, %lu tx errors, %u state changes\n", g_tx_errors, (unsigned)ecu.transitions);
  return 0;
}
