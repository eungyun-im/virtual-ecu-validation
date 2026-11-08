/* Safety monitor unit tests on the simulated clock (REQ-01 to REQ-13). */
#include "sim.h"
#include "vtest.h"

static sim_t S;

static void test_REQ_01_steer_range_boundaries(void)
{
  static const struct { int32_t raw; int accepted; } cases[] = {
      {5400, 1}, {-5400, 1}, {5401, 0}, {-5401, 0}, {32767, 0}, {-32768, 0}, {0, 1}};
  unsigned i;
  for (i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
    sim_init(&S);
    sim_start(&S, 100, 5000);
    sim_drive(&S, 10u, 100, 5000, SIM_SPEED);
    sim_send_steer(&S, cases[i].raw);
    if (cases[i].accepted) {
      CHECK_EQ(S.e.steer.cmd_raw, cases[i].raw);
      CHECK((S.e.faults & VECU_F_STEER_RANGE) == 0u);
    } else {
      CHECK_EQ(S.e.steer.cmd_raw, 100); /* previous command kept */
      CHECK((S.e.faults & VECU_F_STEER_RANGE) != 0u);
      CHECK((S.e.dtc.status[DTC_STEER_RANGE] & DTC_CDTC) != 0u);
      CHECK_EQ(vecu_dtc_code(DTC_STEER_RANGE), 0x4A0011);
    }
    CHECK_EQ(S.e.state, VECU_NORMAL); /* a range fault alone does not change the state */
  }
}

static void test_REQ_01_output_unchanged_after_discard(void)
{
  sim_init(&S);
  sim_start(&S, 300, 5000);
  sim_drive(&S, 100u, 5401, 5000, SIM_BOTH);
  CHECK_EQ(S.e.steer_out, 300);
  CHECK_EQ(sim_last_status(&S).steer, 300);
}

static void test_REQ_02_speed_range_boundaries(void)
{
  static const struct { int32_t raw; int accepted; } cases[] = {{20000, 1}, {0, 1}, {20001, 0}, {65535, 0}};
  unsigned i;
  for (i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
    sim_init(&S);
    sim_start(&S, 0, 1234);
    sim_drive(&S, 10u, 0, 1234, SIM_STEER);
    sim_send_speed(&S, cases[i].raw);
    if (cases[i].accepted) {
      CHECK_EQ(S.e.speed.cmd_raw, cases[i].raw);
    } else {
      CHECK_EQ(S.e.speed.cmd_raw, 1234);
      CHECK((S.e.dtc.status[DTC_SPEED_RANGE] & DTC_CDTC) != 0u);
      CHECK_EQ(vecu_dtc_code(DTC_SPEED_RANGE), 0x4A0111);
    }
  }
}

static void test_REQ_03_rate_limit_steps(void)
{
  int32_t prev;
  int cycles = 0;
  sim_init(&S);
  sim_start(&S, 0, 0);
  CHECK_EQ(S.e.steer_out, 0);
  /* a command sent at a cycle boundary is applied in the next cycle (at most 10 ms of latency) */
  sim_drive(&S, 10u, 1000, 0, SIM_BOTH);
  CHECK_EQ(S.e.steer_out, 0);
  prev = 0;
  while (S.e.steer_out < 1000 && cycles < 100) {
    sim_drive(&S, 10u, 1000, 0, SIM_BOTH);
    CHECK_EQ(S.e.steer_out - prev, (S.e.steer_out < 1000) ? 50 : S.e.steer_out - prev);
    CHECK((S.e.steer_out - prev) <= 50);
    prev = S.e.steer_out;
    ++cycles;
  }
  CHECK_EQ(S.e.steer_out, 1000);
  CHECK_EQ(cycles, 20); /* 100.0 deg at 5.0 deg per cycle */
}

static void test_REQ_03_rate_limit_negative_and_flag(void)
{
  sim_init(&S);
  sim_start(&S, 0, 0);
  sim_drive(&S, 20u, -200, 0, SIM_BOTH);
  CHECK_EQ(S.e.steer_out, -50);
  CHECK((S.e.faults & VECU_F_RATE_LIMITED) != 0u);
  sim_drive(&S, 100u, -200, 0, SIM_BOTH);
  CHECK_EQ(S.e.steer_out, -200);
  CHECK((S.e.faults & VECU_F_RATE_LIMITED) == 0u);
}

static void test_REQ_04_steer_timeout_timing(void)
{
  uint32_t last_rx, fault_ms = 0, safe_ms = 0;
  sim_init(&S);
  sim_start(&S, 0, 3000);
  last_rx = S.e.steer.last_rx_ms;
  while (S.now < last_rx + 200u) {
    sim_drive(&S, 1u, 0, 3000, SIM_SPEED); /* steering frames stop */
    if (fault_ms == 0u && (S.e.faults & VECU_F_STEER_TIMEOUT)) fault_ms = S.now;
    if (safe_ms == 0u && S.e.state == VECU_SAFE_STOP) safe_ms = S.now;
  }
  CHECK_EQ(fault_ms - last_rx, 30);
  CHECK(safe_ms != 0u);
  CHECK(safe_ms - fault_ms <= 50u);
  CHECK((S.e.dtc.status[DTC_STEER_TIMEOUT] & DTC_CDTC) != 0u);
}

static void test_REQ_04_no_timeout_at_29ms(void)
{
  uint32_t last_rx;
  sim_init(&S);
  sim_start(&S, 0, 0);
  last_rx = S.e.steer.last_rx_ms;
  while (S.now < last_rx + 29u) sim_drive(&S, 1u, 0, 0, SIM_SPEED);
  CHECK_EQ(S.e.state, VECU_NORMAL);
  sim_drive(&S, 1u, 0, 0, SIM_SPEED);
  CHECK_EQ(S.e.state, VECU_SAFE_STOP);
}

static void test_REQ_05_speed_timeout_degraded_and_ramp(void)
{
  uint32_t last_rx;
  int32_t before;
  sim_init(&S);
  sim_start(&S, 0, 5000);
  last_rx = S.e.speed.last_rx_ms;
  while (S.now < last_rx + 30u) sim_drive(&S, 1u, 0, 5000, SIM_STEER);
  CHECK_EQ(S.e.state, VECU_DEGRADED);
  CHECK_EQ(S.now - last_rx, 30);
  sim_drive(&S, 10u, 0, 5000, SIM_STEER);
  before = S.e.speed_out;
  sim_drive(&S, 100u, 0, 5000, SIM_STEER);
  CHECK_EQ(before - S.e.speed_out, 100); /* 10 cycles x 0.10 km/h */
}

static void test_REQ_06_counter_skip_repeat_replay(void)
{
  vecu_frame_t f;
  sim_init(&S);
  sim_start(&S, 100, 0);
  /* skip one counter value */
  f = sim_cmd(VECU_ID_STEER_CMD, VECU_DATAID_STEER, 900, (uint8_t)((S.sc + 1u) & 0x0Fu));
  vecu_rx(&S.e, &f, S.now);
  CHECK_EQ(S.e.steer.cmd_raw, 100);
  /* replay of the last accepted frame (same counter) */
  f = sim_cmd(VECU_ID_STEER_CMD, VECU_DATAID_STEER, 900, (uint8_t)((S.sc + 15u) & 0x0Fu));
  vecu_rx(&S.e, &f, S.now);
  CHECK_EQ(S.e.steer.cmd_raw, 100);
  CHECK_EQ(S.e.steer.invalid_run, 2);
  /* the expected counter is still accepted */
  sim_send_steer(&S, 900);
  CHECK_EQ(S.e.steer.cmd_raw, 900);
  CHECK_EQ(S.e.steer.invalid_run, 0);
}

static void test_REQ_06_resync_after_timeout(void)
{
  vecu_frame_t f;
  sim_init(&S);
  sim_start(&S, 0, 0);
  sim_drive(&S, 40u, 0, 0, SIM_SPEED); /* steering timeout */
  CHECK(S.e.steer.timed_out);
  f = sim_cmd(VECU_ID_STEER_CMD, VECU_DATAID_STEER, 77, 9u); /* any counter accepted once */
  vecu_rx(&S.e, &f, S.now);
  CHECK_EQ(S.e.steer.cmd_raw, 77);
  CHECK(!S.e.steer.timed_out);
}

static void test_REQ_07_crc_bit_flips_and_data_id(void)
{
  int byte, bit;
  vecu_frame_t f;
  sim_init(&S);
  sim_start(&S, 100, 0);
  for (byte = 1; byte < 8; ++byte) {
    for (bit = 0; bit < 8; ++bit) {
      f = sim_cmd(VECU_ID_STEER_CMD, VECU_DATAID_STEER, 700, S.sc);
      f.data[byte] ^= (uint8_t)(1u << bit);
      CHECK(vecu_e2e_check(&S.e.steer.e2e, VECU_DATAID_STEER, &f) != VECU_E2E_OK);
    }
  }
  /* a speed frame protected with the steering Data ID */
  f = sim_cmd(VECU_ID_SPEED_CMD, VECU_DATAID_STEER, 100, S.vc);
  vecu_rx(&S.e, &f, S.now);
  CHECK_EQ(S.e.speed.invalid_run, 1);
  /* known CRC8 SAE J1850 check value: "123456789" -> 0x4B */
  CHECK_EQ(vecu_crc8_j1850((const uint8_t *)"123456789", 9u, 0xFFu) ^ 0xFFu, 0x4B);
}

static void corrupt_steer(sim_t *s)
{
  vecu_frame_t f = sim_cmd(VECU_ID_STEER_CMD, VECU_DATAID_STEER, 0, s->sc);
  f.data[0] ^= 0xFFu;
  vecu_rx(&s->e, &f, s->now);
}

static void test_REQ_08_three_invalid_degraded_ten_safe(void)
{
  int i;
  sim_init(&S);
  sim_start(&S, 0, 0);
  for (i = 1; i <= 10; ++i) {
    sim_drive(&S, 10u, 0, 0, SIM_SPEED);
    corrupt_steer(&S);
    if (i < 3) CHECK_EQ(S.e.state, VECU_NORMAL);
    if (i >= 3 && i < 10) CHECK_EQ(S.e.state, VECU_DEGRADED);
  }
  CHECK_EQ(S.e.state, VECU_SAFE_STOP);
  CHECK((S.e.dtc.status[DTC_STEER_E2E] & DTC_CDTC) != 0u);
}

static void test_REQ_08_speed_invalid_degraded_only(void)
{
  int i;
  vecu_frame_t f;
  sim_init(&S);
  sim_start(&S, 0, 0);
  for (i = 0; i < 20; ++i) {
    sim_drive(&S, 10u, 0, 0, SIM_STEER);
    f = sim_cmd(VECU_ID_SPEED_CMD, VECU_DATAID_SPEED, 0, S.vc);
    f.data[0] ^= 0x01u;
    vecu_rx(&S.e, &f, S.now);
  }
  CHECK_EQ(S.e.state, VECU_DEGRADED);
}

static void test_REQ_09_safe_stop_holds_steer_ramps_speed(void)
{
  int32_t steer_at_stop;
  sim_init(&S);
  sim_start(&S, 400, 3000);
  sim_drive(&S, 50u, 400, 3000, SIM_SPEED);
  CHECK_EQ(S.e.state, VECU_SAFE_STOP);
  steer_at_stop = S.e.steer_out;
  sim_drive(&S, 1000u, -400, 3000, SIM_SPEED); /* new steering command ignored */
  CHECK_EQ(S.e.steer_out, steer_at_stop);
  sim_drive(&S, 3000u, 0, 3000, SIM_SPEED);
  CHECK_EQ(S.e.speed_out, 0);
}

static void test_REQ_09_speed_ramp_rate(void)
{
  int32_t a;
  sim_init(&S);
  sim_start(&S, 0, 10000);
  sim_drive(&S, 40u, 0, 10000, SIM_SPEED);
  CHECK_EQ(S.e.state, VECU_SAFE_STOP);
  sim_drive(&S, 10u, 0, 10000, SIM_SPEED);
  a = S.e.speed_out;
  sim_drive(&S, 500u, 0, 10000, SIM_SPEED);
  CHECK_EQ(a - S.e.speed_out, 500); /* 50 cycles x 10 raw = 5.00 km/h in 0.5 s */
}

static void test_REQ_10_recovery_after_one_second(void)
{
  uint32_t resume, normal_ms = 0;
  sim_init(&S);
  sim_start(&S, 0, 0);
  sim_drive(&S, 50u, 0, 0, SIM_SPEED);
  CHECK_EQ(S.e.state, VECU_SAFE_STOP);
  resume = S.now + 10u - (S.now % 10u);
  while (S.now < resume + 1200u) {
    sim_drive(&S, 1u, 0, 0, SIM_BOTH);
    if (normal_ms == 0u && S.e.state == VECU_NORMAL) normal_ms = S.now;
  }
  CHECK(normal_ms != 0u);
  CHECK(normal_ms - resume >= 1000u);
  CHECK(normal_ms - resume <= 1010u);
  CHECK((S.e.dtc.status[DTC_STEER_TIMEOUT] & DTC_CDTC) != 0u); /* DTC kept */
  CHECK((S.e.dtc.status[DTC_STEER_TIMEOUT] & DTC_TF) == 0u);
}

static void test_REQ_10_interrupted_recovery_restarts(void)
{
  sim_init(&S);
  sim_start(&S, 0, 0);
  sim_drive(&S, 50u, 0, 0, SIM_SPEED);
  sim_drive(&S, 900u, 0, 0, SIM_BOTH);
  corrupt_steer(&S); /* breaks the valid streak */
  sim_drive(&S, 200u, 0, 0, SIM_BOTH);
  CHECK_EQ(S.e.state, VECU_SAFE_STOP);
  sim_drive(&S, 850u, 0, 0, SIM_BOTH);
  CHECK_EQ(S.e.state, VECU_NORMAL);
}

static void test_REQ_11_degraded_recovery_and_no_safe_to_degraded(void)
{
  int i, k;
  sim_init(&S);
  sim_start(&S, 0, 0);
  for (i = 0; i < 3; ++i) {
    sim_drive(&S, 10u, 0, 0, SIM_SPEED);
    corrupt_steer(&S);
  }
  CHECK_EQ(S.e.state, VECU_DEGRADED);
  sim_drive(&S, 90u, 0, 0, SIM_BOTH);
  CHECK_EQ(S.e.state, VECU_DEGRADED);
  sim_drive(&S, 20u, 0, 0, SIM_BOTH);
  CHECK_EQ(S.e.state, VECU_NORMAL);
  /* SAFE_STOP, then a DEGRADED condition: must stay in SAFE_STOP */
  sim_drive(&S, 50u, 0, 0, SIM_SPEED);
  CHECK_EQ(S.e.state, VECU_SAFE_STOP);
  sim_drive(&S, 50u, 0, 0, SIM_STEER);
  for (k = 0; k < S.ntrace; ++k) {
    if (k > 0 && S.trace_to[k - 1] == VECU_SAFE_STOP) CHECK(S.trace_to[k] != VECU_DEGRADED);
  }
  CHECK_EQ(S.e.state, VECU_SAFE_STOP);
}

static void test_REQ_12_status_period_and_event(void)
{
  int k, n = 0;
  uint32_t prev = 0;
  sim_init(&S);
  sim_start(&S, 0, 0);
  S.ntx = 0;
  sim_drive(&S, 1000u, 0, 0, SIM_BOTH);
  for (k = 0; k < S.ntx; ++k) {
    if (S.tx[k].id != VECU_ID_STATUS) continue;
    CHECK(sim_status_at(&S, k).valid);
    if (prev) CHECK_EQ(S.tx_ms[k] - prev, 20);
    prev = S.tx_ms[k];
    ++n;
  }
  CHECK_EQ(n, 50);
  /* event frame in the tick of the change */
  S.ntx = 0;
  sim_drive(&S, 60u, 0, 0, SIM_SPEED);
  CHECK(S.ntrace > 0);
  {
    const uint32_t t = S.trace_ms[S.ntrace - 1];
    int found = 0;
    for (k = 0; k < S.ntx; ++k)
      if (S.tx[k].id == VECU_ID_STATUS && S.tx_ms[k] == t && sim_status_at(&S, k).state == VECU_SAFE_STOP) found = 1;
    CHECK(found);
  }
}

static void test_REQ_12_status_counter_and_signals(void)
{
  sim_status_t a, b;
  sim_init(&S);
  sim_start(&S, -123, 4567);
  a = sim_last_status(&S);
  sim_drive(&S, 20u, -123, 4567, SIM_BOTH);
  b = sim_last_status(&S);
  CHECK_EQ(b.counter, (a.counter + 1) & 0x0F);
  CHECK_EQ(b.steer, -123);
  CHECK_EQ(b.speed, 4567);
  CHECK_EQ(b.state, VECU_NORMAL);
}

static void test_REQ_13_dtc_status_byte(void)
{
  vecu_dtc_mem_t m;
  vecu_dtc_clear(&m);
  CHECK_EQ(m.status[DTC_STEER_RANGE], 0x50);
  vecu_dtc_report(&m, DTC_STEER_RANGE, 1);
  CHECK_EQ(m.status[DTC_STEER_RANGE], 0x2F);
  vecu_dtc_report(&m, DTC_STEER_RANGE, 0);
  CHECK_EQ(m.status[DTC_STEER_RANGE], 0x2E);
  vecu_dtc_new_cycle(&m);
  CHECK_EQ(m.status[DTC_STEER_RANGE], 0x6C);
  CHECK_EQ(vecu_dtc_confirmed_count(&m), 1);
  vecu_dtc_report(&m, DTC_COUNT, 1); /* out of range index ignored */
  CHECK_EQ(vecu_dtc_code(DTC_COUNT), 0);
}

static void test_REQ_13_status_reports_confirmed_count(void)
{
  sim_init(&S);
  sim_start(&S, 0, 0);
  sim_drive(&S, 10u, 6000, 0, SIM_BOTH);
  sim_drive(&S, 30u, 0, 30000, SIM_BOTH);
  CHECK_EQ(sim_last_status(&S).dtc_count, 2);
}

static void test_init_waits_for_both_channels(void)
{
  sim_init(&S);
  sim_drive(&S, 200u, 0, 0, SIM_STEER);
  CHECK_EQ(S.e.state, VECU_INIT);
  CHECK_EQ(S.e.steer_out, 0);
  sim_drive(&S, 20u, 0, 0, SIM_BOTH);
  CHECK_EQ(S.e.state, VECU_NORMAL);
}

static void test_init_steer_timeout_goes_safe(void)
{
  sim_init(&S);
  sim_drive(&S, 20u, 0, 0, SIM_STEER);
  sim_drive(&S, 40u, 0, 0, 0);
  CHECK_EQ(S.e.state, VECU_SAFE_STOP);
}

static void test_wrong_dlc_is_invalid(void)
{
  vecu_frame_t f;
  sim_init(&S);
  sim_start(&S, 0, 0);
  f = sim_cmd(VECU_ID_STEER_CMD, VECU_DATAID_STEER, 0, S.sc);
  f.dlc = 7u;
  vecu_rx(&S.e, &f, S.now);
  CHECK_EQ(S.e.steer.invalid_run, 1);
  f.id = 0x123u; /* unrelated frame ignored */
  vecu_rx(&S.e, &f, S.now);
  CHECK_EQ(S.e.steer.invalid_run, 1);
}

int main(int argc, char **argv)
{
  static const vt_case_t cases[] = {
      {"test_REQ_01_steer_range_boundaries", test_REQ_01_steer_range_boundaries},
      {"test_REQ_01_output_unchanged_after_discard", test_REQ_01_output_unchanged_after_discard},
      {"test_REQ_02_speed_range_boundaries", test_REQ_02_speed_range_boundaries},
      {"test_REQ_03_rate_limit_steps", test_REQ_03_rate_limit_steps},
      {"test_REQ_03_rate_limit_negative_and_flag", test_REQ_03_rate_limit_negative_and_flag},
      {"test_REQ_04_steer_timeout_timing", test_REQ_04_steer_timeout_timing},
      {"test_REQ_04_no_timeout_at_29ms", test_REQ_04_no_timeout_at_29ms},
      {"test_REQ_05_speed_timeout_degraded_and_ramp", test_REQ_05_speed_timeout_degraded_and_ramp},
      {"test_REQ_06_counter_skip_repeat_replay", test_REQ_06_counter_skip_repeat_replay},
      {"test_REQ_06_resync_after_timeout", test_REQ_06_resync_after_timeout},
      {"test_REQ_07_crc_bit_flips_and_data_id", test_REQ_07_crc_bit_flips_and_data_id},
      {"test_REQ_08_three_invalid_degraded_ten_safe", test_REQ_08_three_invalid_degraded_ten_safe},
      {"test_REQ_08_speed_invalid_degraded_only", test_REQ_08_speed_invalid_degraded_only},
      {"test_REQ_09_safe_stop_holds_steer_ramps_speed", test_REQ_09_safe_stop_holds_steer_ramps_speed},
      {"test_REQ_09_speed_ramp_rate", test_REQ_09_speed_ramp_rate},
      {"test_REQ_10_recovery_after_one_second", test_REQ_10_recovery_after_one_second},
      {"test_REQ_10_interrupted_recovery_restarts", test_REQ_10_interrupted_recovery_restarts},
      {"test_REQ_11_degraded_recovery_and_no_safe_to_degraded", test_REQ_11_degraded_recovery_and_no_safe_to_degraded},
      {"test_REQ_12_status_period_and_event", test_REQ_12_status_period_and_event},
      {"test_REQ_12_status_counter_and_signals", test_REQ_12_status_counter_and_signals},
      {"test_REQ_13_dtc_status_byte", test_REQ_13_dtc_status_byte},
      {"test_REQ_13_status_reports_confirmed_count", test_REQ_13_status_reports_confirmed_count},
      {"test_init_waits_for_both_channels", test_init_waits_for_both_channels},
      {"test_init_steer_timeout_goes_safe", test_init_steer_timeout_goes_safe},
      {"test_wrong_dlc_is_invalid", test_wrong_dlc_is_invalid},
  };
  return vt_main(argc, argv, cases, (int)(sizeof cases / sizeof cases[0]), "monitor");
}
