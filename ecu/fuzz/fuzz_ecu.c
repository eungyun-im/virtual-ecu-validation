/* libFuzzer harness: random frame sequences into the ECU core (commands, ISO-TP, UDS, functional
 * requests) with random time steps. Built with ASan and UBSan; any memory error, UB or abort is a
 * finding (REQ-19). The first input byte picks how many 1 ms ticks pass before each frame.
 * Record per frame: [ticks u8][selector u8][dlc u8][8 data bytes] = 11 bytes. */
#include <string.h>

#include "vecu/ecu.h"

static unsigned long g_frames;

static void tx(void *ctx, const vecu_frame_t *f)
{
  (void)ctx;
  if (f->dlc > 8u) {
    __builtin_trap();
  }
  ++g_frames;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  static vecu_t e;
  static const uint32_t ids[] = {VECU_ID_STEER_CMD, VECU_ID_SPEED_CMD, VECU_ID_UDS_PHYS, VECU_ID_UDS_FUNC, 0x123u};
  uint32_t now = 0u;
  size_t i;
  vecu_init(&e, tx, NULL, now);
  for (i = 0u; i + 11u <= size; i += 11u) {
    vecu_frame_t f;
    uint8_t t;
    for (t = 0u; t < data[i]; ++t) {
      vecu_tick(&e, ++now);
    }
    f.id = ids[data[i + 1u] % (sizeof ids / sizeof ids[0])];
    f.dlc = (uint8_t)(data[i + 2u] % 9u);
    memcpy(f.data, &data[i + 3u], 8u);
    vecu_rx(&e, &f, now);
  }
  vecu_tick(&e, ++now);
  return 0;
}
