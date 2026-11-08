/* UDS, ISO-TP, SHA-256 and software update unit tests on the simulated clock (REQ-14 to REQ-20). */
#include <stdlib.h>

#include "sim.h"
#include "vecu/sha256.h"
#include "vtest.h"

static sim_t S;
static uint8_t R[VECU_ISOTP_MAX];

static uint16_t uds(const uint8_t *q, uint16_t n) { return vecu_uds_handle(&S.e, q, n, 0, S.now, R); }
#define UDS(...) ({ static const uint8_t q_[] = {__VA_ARGS__}; uds(q_, (uint16_t)sizeof q_); })
#define IS_NRC(len, sid, code) ((len) == 3u && R[0] == 0x7Fu && R[1] == (sid) && R[2] == (code))

static void wait_ms(uint32_t ms)
{
  uint32_t i;
  for (i = 0; i < ms; ++i) sim_step(&S);
}

static void unlock(void)
{
  uint32_t seed, key;
  uint8_t q[6] = {0x27, 0x02, 0, 0, 0, 0};
  CHECK_EQ(UDS(0x27, 0x01), 6);
  seed = ((uint32_t)R[2] << 24) | ((uint32_t)R[3] << 16) | ((uint32_t)R[4] << 8) | R[5];
  key = vecu_uds_key(seed);
  q[2] = (uint8_t)(key >> 24); q[3] = (uint8_t)(key >> 16); q[4] = (uint8_t)(key >> 8); q[5] = (uint8_t)key;
  CHECK_EQ(uds(q, 6), 2);
  CHECK_EQ(R[0], 0x67);
}

/* ---------- REQ-14 services ---------- */

static void test_REQ_14_session_control(void)
{
  uint16_t n;
  sim_init(&S);
  n = UDS(0x10, 0x03);
  CHECK_EQ(n, 6);
  CHECK(R[0] == 0x50 && R[1] == 0x03 && R[2] == 0x00 && R[3] == 50 && R[4] == 0x01 && R[5] == 0xF4);
  CHECK_EQ(S.e.uds.session, UDS_SESSION_EXTENDED);
  CHECK(IS_NRC(UDS(0x10, 0x05), 0x10, 0x12));
  CHECK(IS_NRC(UDS(0x10), 0x10, 0x13));
  CHECK_EQ(UDS(0x10, 0x81), 0); /* suppressPosRspMsgIndicationBit */
  CHECK_EQ(S.e.uds.session, UDS_SESSION_DEFAULT);
  CHECK(IS_NRC(UDS(0x10, 0x02), 0x10, 0x22)); /* programming only from extended */
}

static void test_REQ_14_read_data_by_identifier(void)
{
  uint16_t n;
  sim_init(&S);
  sim_start(&S, -250, 1234);
  n = UDS(0x22, 0xF1, 0x90);
  CHECK_EQ(n, 3 + 17);
  CHECK(memcmp(&R[3], "VECUDEMO000000001", 17) == 0);
  n = UDS(0x22, 0xF1, 0x95, 0x01, 0x00, 0x01, 0x02, 0x01, 0x03);
  CHECK_EQ(n, 1 + 6 + 3 + 4 + 4);
  CHECK(R[3] == 0x00 && R[4] == 0x01 && R[5] == 0x00 && R[6] == 0x00); /* version 1.0.0 */
  CHECK(R[7] == 0x01 && R[8] == 0x00 && R[9] == VECU_NORMAL);
  CHECK_EQ((int16_t)((R[12] << 8) | R[13]), -250);
  CHECK_EQ((R[16] << 8) | R[17], 1234);
  CHECK(IS_NRC(UDS(0x22, 0x12, 0x34), 0x22, 0x31));
  CHECK(IS_NRC(UDS(0x22, 0xF1), 0x22, 0x13));
  n = UDS(0x22, 0xF1, 0x8C, 0x01, 0x01);
  CHECK_EQ(n, 3 + 7 + 3);
}

static void test_REQ_14_read_and_clear_dtc(void)
{
  uint16_t n;
  sim_init(&S);
  sim_start(&S, 0, 0);
  sim_drive(&S, 30u, 6000, 0, SIM_BOTH); /* steering range fault */
  n = UDS(0x19, 0x01, 0x08);
  CHECK(n == 6 && R[0] == 0x59 && R[1] == 0x01 && R[2] == 0x7F && R[3] == 0x01 && R[4] == 0 && R[5] == 1);
  n = UDS(0x19, 0x02, 0x08);
  CHECK(n == 7 && R[3] == 0x4A && R[4] == 0x00 && R[5] == 0x11 && R[6] == 0x2F);
  n = UDS(0x19, 0x0A);
  CHECK_EQ(n, 3 + 4 * DTC_COUNT);
  CHECK(IS_NRC(UDS(0x19, 0x04), 0x19, 0x12));
  CHECK(IS_NRC(UDS(0x19, 0x02), 0x19, 0x13));
  CHECK(IS_NRC(UDS(0x19), 0x19, 0x13));
  CHECK(IS_NRC(UDS(0x14, 0x00, 0x00, 0x01), 0x14, 0x31));
  CHECK(IS_NRC(UDS(0x14, 0xFF), 0x14, 0x13));
  n = UDS(0x14, 0xFF, 0xFF, 0xFF);
  CHECK(n == 1 && R[0] == 0x54);
  CHECK_EQ(S.e.dtc.status[DTC_STEER_RANGE], 0x50);
  n = UDS(0x19, 0x01, 0x08);
  CHECK(n == 6 && R[5] == 0);
}

static void test_REQ_14_tester_present_and_unknown(void)
{
  sim_init(&S);
  CHECK(UDS(0x3E, 0x00) == 2 && R[0] == 0x7E);
  CHECK_EQ(UDS(0x3E, 0x80), 0);
  CHECK(IS_NRC(UDS(0x3E, 0x01), 0x3E, 0x12));
  CHECK(IS_NRC(UDS(0x3E), 0x3E, 0x13));
  CHECK(IS_NRC(UDS(0x2E, 0xF1, 0x90, 0x00), 0x2E, 0x11));
  CHECK(IS_NRC(UDS(0x31, 0x01, 0xFF, 0x00), 0x31, 0x11));
  CHECK(IS_NRC(UDS(0x11, 0x02), 0x11, 0x12));
  CHECK(IS_NRC(UDS(0x11), 0x11, 0x13));
}

static void test_REQ_14_functional_suppresses_nrc(void)
{
  static const uint8_t q1[] = {0x2E, 0x00};
  static const uint8_t q2[] = {0x3E, 0x00};
  sim_init(&S);
  CHECK_EQ(vecu_uds_handle(&S.e, q1, 2, 1, S.now, R), 0);
  CHECK_EQ(vecu_uds_handle(&S.e, q2, 2, 1, S.now, R), 2);
}

/* ---------- ISO-TP over frames, and REQ-15 P2 ---------- */

static uint16_t collect_response(int from, uint8_t *out)
{
  /* reassemble the ISO-TP response transmitted on 0x7E8 starting at tx index `from` */
  int k;
  uint16_t len = 0, got = 0;
  for (k = from; k < S.ntx; ++k) {
    const vecu_frame_t *f = &S.tx[k];
    if (f->id != VECU_ID_UDS_RESP) continue;
    if ((f->data[0] >> 4) == 0) {
      len = f->data[0] & 0x0F;
      memcpy(out, &f->data[1], len);
      return len;
    }
    if ((f->data[0] >> 4) == 1) {
      len = (uint16_t)(((f->data[0] & 0x0F) << 8) | f->data[1]);
      memcpy(out, &f->data[2], 6);
      got = 6;
    } else if ((f->data[0] >> 4) == 2) {
      uint16_t n = (uint16_t)((len - got) > 7 ? 7 : (len - got));
      memcpy(&out[got], &f->data[1], n);
      got = (uint16_t)(got + n);
      if (got == len) return len;
    }
  }
  return 0;
}

static void test_REQ_15_isotp_single_and_multi_frame(void)
{
  vecu_frame_t f;
  uint8_t out[512];
  int from;
  uint16_t n;
  sim_init(&S);
  /* single frame request 22 F1 90 -> 20-byte response needs FF + CF after flow control */
  memset(&f, 0xCC, sizeof f);
  f.id = VECU_ID_UDS_PHYS; f.dlc = 8;
  f.data[0] = 0x03; f.data[1] = 0x22; f.data[2] = 0xF1; f.data[3] = 0x90;
  from = S.ntx;
  vecu_rx(&S.e, &f, S.now);
  CHECK(S.ntx > from);
  CHECK_EQ(S.tx[S.ntx - 1].data[0], 0x10); /* first frame sent in the same call: P2 = 0 ms */
  CHECK_EQ(S.tx_ms[S.ntx - 1], S.now);
  /* tester flow control: CTS, BS 0, STmin 0 */
  memset(&f, 0xCC, sizeof f);
  f.id = VECU_ID_UDS_PHYS; f.dlc = 8; f.data[0] = 0x30; f.data[1] = 0; f.data[2] = 0;
  vecu_rx(&S.e, &f, S.now);
  wait_ms(2);
  n = collect_response(from, out);
  CHECK_EQ(n, 20);
  CHECK(out[0] == 0x62 && memcmp(&out[3], "VECUDEMO000000001", 17) == 0);
  CHECK(!vecu_isotp_busy(&S.e.isotp));
}

static void test_REQ_15_isotp_multi_frame_request_and_stmin(void)
{
  vecu_frame_t f;
  uint8_t out[512];
  int from, k, cfs = 0;
  uint32_t first_cf = 0, last_cf = 0;
  sim_init(&S);
  /* multi-frame request: 22 + 8 DIDs (17 bytes) */
  static const uint8_t req[17] = {0x22, 0xF1, 0x90, 0xF1, 0x95, 0xF1, 0x8C, 0x01, 0x00, 0x01, 0x01, 0x01, 0x02, 0x01, 0x03, 0xF1, 0x95};
  memset(&f, 0xCC, sizeof f); f.id = VECU_ID_UDS_PHYS; f.dlc = 8;
  f.data[0] = 0x10; f.data[1] = 17; memcpy(&f.data[2], req, 6);
  from = S.ntx;
  vecu_rx(&S.e, &f, S.now);
  CHECK_EQ(S.tx[S.ntx - 1].data[0], 0x30); /* our flow control */
  memset(&f, 0xCC, sizeof f); f.id = VECU_ID_UDS_PHYS; f.dlc = 8;
  f.data[0] = 0x21; memcpy(&f.data[1], &req[6], 7); vecu_rx(&S.e, &f, S.now);
  f.data[0] = 0x22; memcpy(&f.data[1], &req[13], 4); memset(&f.data[5], 0xCC, 3); vecu_rx(&S.e, &f, S.now);
  /* response is long: tester asks for STmin 5 ms and block size 2 */
  memset(&f, 0xCC, sizeof f); f.id = VECU_ID_UDS_PHYS; f.dlc = 8; f.data[0] = 0x30; f.data[1] = 2; f.data[2] = 5;
  vecu_rx(&S.e, &f, S.now);
  wait_ms(20);
  CHECK(vecu_isotp_busy(&S.e.isotp)); /* waiting for the next flow control after 2 frames */
  vecu_rx(&S.e, &f, S.now);
  wait_ms(200);
  f.data[1] = 0; vecu_rx(&S.e, &f, S.now);
  wait_ms(200);
  for (k = from; k < S.ntx; ++k) {
    if (S.tx[k].id == VECU_ID_UDS_RESP && (S.tx[k].data[0] >> 4) == 2) {
      if (!cfs) first_cf = S.tx_ms[k];
      if (cfs == 1) CHECK(S.tx_ms[k] - last_cf >= 5u);
      last_cf = S.tx_ms[k];
      ++cfs;
    }
  }
  CHECK(cfs >= 4);
  CHECK(first_cf > 0u);
  CHECK(collect_response(from, out) > 40);
  CHECK_EQ(out[0], 0x62);
}

static void test_isotp_errors(void)
{
  vecu_frame_t f;
  uint8_t out[16];
  vecu_isotp_t *t;
  sim_init(&S);
  t = &S.e.isotp;
  memset(&f, 0xCC, sizeof f); f.id = VECU_ID_UDS_PHYS; f.dlc = 8;
  f.data[0] = 0x21; CHECK_EQ(vecu_isotp_on_frame(t, &f, S.now, out), 0); /* CF without FF */
  f.data[0] = 0x00; CHECK_EQ(vecu_isotp_on_frame(t, &f, S.now, out), 0); /* SF length 0 */
  f.data[0] = 0x10; f.data[1] = 5; CHECK_EQ(vecu_isotp_on_frame(t, &f, S.now, out), 0); /* FF too short */
  f.data[0] = 0x40; CHECK_EQ(vecu_isotp_on_frame(t, &f, S.now, out), 0); /* reserved PCI */
  f.data[0] = 0x10; f.data[1] = 20; CHECK_EQ(vecu_isotp_on_frame(t, &f, S.now, out), 0);
  f.data[0] = 0x23; CHECK_EQ(vecu_isotp_on_frame(t, &f, S.now, out), 0); /* wrong sequence number */
  CHECK(!t->rx_active);
  f.data[0] = 0x10; f.data[1] = 20; (void)vecu_isotp_on_frame(t, &f, S.now, out);
  wait_ms(VECU_ISOTP_N_CR_MS + 1u); /* N_Cr timeout */
  CHECK(!t->rx_active);
  {
    uint8_t big[40] = {0};
    CHECK_EQ(vecu_isotp_send(t, big, 40, S.now), 0);
    CHECK_EQ(vecu_isotp_send(t, big, 40, S.now), -1); /* busy */
    wait_ms(VECU_ISOTP_N_BS_MS + 1u); /* no flow control -> N_Bs timeout */
    CHECK(!vecu_isotp_busy(t));
    CHECK_EQ(vecu_isotp_send(t, big, 0, S.now), -1);
  }
  CHECK(t->errors >= 7u);
}

/* ---------- REQ-16, REQ-17 security ---------- */

static void test_REQ_16_security_in_default_session(void)
{
  sim_init(&S);
  CHECK(IS_NRC(UDS(0x27, 0x01), 0x27, 0x7F));
  CHECK(IS_NRC(UDS(0x27, 0x02, 1, 2, 3, 4), 0x27, 0x7F));
}

static void test_REQ_17_attempt_limit_and_delay(void)
{
  int i;
  sim_init(&S);
  UDS(0x10, 0x03);
  for (i = 1; i <= 3; ++i) {
    CHECK_EQ(UDS(0x27, 0x01), 6);
    if (i < 3) CHECK(IS_NRC(UDS(0x27, 0x02, 0, 0, 0, 0), 0x27, 0x35));
    else CHECK(IS_NRC(UDS(0x27, 0x02, 0, 0, 0, 0), 0x27, 0x36));
  }
  for (i = 0; i < 9; ++i) {
    wait_ms(1000);
    UDS(0x3E, 0x00);
    CHECK(IS_NRC(UDS(0x27, 0x01), 0x27, 0x37));
  }
  wait_ms(1000);
  CHECK_EQ(UDS(0x27, 0x01), 6); /* 10 s later: a seed again */
  CHECK(R[0] == 0x67);
}

static void test_REQ_17_good_key_and_sequence(void)
{
  sim_init(&S);
  UDS(0x10, 0x03);
  CHECK(IS_NRC(UDS(0x27, 0x02, 1, 2, 3, 4), 0x27, 0x24)); /* key before seed */
  unlock();
  CHECK_EQ(UDS(0x27, 0x01), 6);
  CHECK(R[2] == 0 && R[3] == 0 && R[4] == 0 && R[5] == 0); /* already unlocked: zero seed */
  CHECK(IS_NRC(UDS(0x27, 0x03), 0x27, 0x12));
  CHECK(IS_NRC(UDS(0x27, 0x02, 1), 0x27, 0x13));
  CHECK(IS_NRC(UDS(0x27), 0x27, 0x13));
}

/* ---------- REQ-18 session timeout ---------- */

static void test_REQ_18_s3_timeout(void)
{
  sim_init(&S);
  UDS(0x10, 0x03);
  unlock();
  wait_ms(4990);
  CHECK_EQ(S.e.uds.session, UDS_SESSION_EXTENDED);
  wait_ms(20);
  CHECK_EQ(S.e.uds.session, UDS_SESSION_DEFAULT);
  CHECK_EQ(S.e.uds.unlocked, 0);
}

static void test_REQ_18_tester_present_keeps_session(void)
{
  int i;
  sim_init(&S);
  UDS(0x10, 0x03);
  for (i = 0; i < 5; ++i) {
    wait_ms(2000);
    CHECK_EQ(UDS(0x3E, 0x80), 0);
  }
  CHECK_EQ(S.e.uds.session, UDS_SESSION_EXTENDED);
}

/* ---------- REQ-20 software update ---------- */

static uint8_t IMG[VECU_IMG_MAX];

static uint32_t make_image(uint32_t version, uint32_t payload, int corrupt)
{
  uint32_t i;
  memset(IMG, 0, VECU_IMG_HDR);
  memcpy(IMG, "VECU", 4);
  IMG[4] = (uint8_t)(version >> 24); IMG[5] = (uint8_t)(version >> 16); IMG[6] = (uint8_t)(version >> 8); IMG[7] = (uint8_t)version;
  IMG[8] = (uint8_t)(payload >> 24); IMG[9] = (uint8_t)(payload >> 16); IMG[10] = (uint8_t)(payload >> 8); IMG[11] = (uint8_t)payload;
  for (i = 0; i < payload; ++i) IMG[VECU_IMG_HDR + i] = (uint8_t)(i * 7u + 3u);
  vecu_sha256(&IMG[VECU_IMG_HDR], payload, &IMG[12]);
  if (corrupt) IMG[VECU_IMG_HDR + payload / 2u] ^= 0x01u;
  return VECU_IMG_HDR + payload;
}

static uint16_t request_download(uint32_t version, uint32_t size)
{
  uint8_t q[11] = {0x34, 0x00, 0x44};
  q[3] = (uint8_t)(version >> 24); q[4] = (uint8_t)(version >> 16); q[5] = (uint8_t)(version >> 8); q[6] = (uint8_t)version;
  q[7] = (uint8_t)(size >> 24); q[8] = (uint8_t)(size >> 16); q[9] = (uint8_t)(size >> 8); q[10] = (uint8_t)size;
  return uds(q, 11);
}

static int transfer_all(uint32_t size, uint32_t stop_after)
{
  static uint8_t q[2 + VECU_BLOCK_DATA];
  uint32_t off = 0;
  uint8_t seq = 1;
  while (off < size && off < stop_after) {
    const uint32_t n = (size - off) > VECU_BLOCK_DATA ? VECU_BLOCK_DATA : (size - off);
    q[0] = 0x36; q[1] = seq;
    memcpy(&q[2], &IMG[off], n);
    if (uds(q, (uint16_t)(n + 2u)) != 2 || R[0] != 0x76 || R[1] != seq) return -1;
    off += n;
    ++seq;
  }
  return 0;
}

static void programming_unlocked(void)
{
  UDS(0x10, 0x03);
  unlock();
  CHECK_EQ(UDS(0x10, 0x02), 6);
}

static uint32_t running_version(void)
{
  CHECK_EQ(UDS(0x22, 0xF1, 0x95), 7);
  return ((uint32_t)R[3] << 24) | ((uint32_t)R[4] << 16) | ((uint32_t)R[5] << 8) | R[6];
}

static void test_REQ_20_good_image_activated_after_reset(void)
{
  uint32_t size;
  sim_init(&S);
  programming_unlocked();
  size = make_image(0x00010100u, 5000u, 0);
  CHECK(request_download(0x00010100u, size) == 4 && R[0] == 0x74 && R[1] == 0x20);
  CHECK_EQ(transfer_all(size, size), 0);
  CHECK(UDS(0x37) == 1 && R[0] == 0x77);
  CHECK_EQ(running_version(), 0x00010000u); /* not yet active */
  CHECK(UDS(0x11, 0x01) == 2 && R[0] == 0x51);
  wait_ms(2);
  CHECK_EQ(S.e.uds.session, UDS_SESSION_DEFAULT);
  CHECK_EQ(running_version(), 0x00010100u);
}

static void test_REQ_20_bad_hash_rejected(void)
{
  uint32_t size;
  sim_init(&S);
  programming_unlocked();
  size = make_image(0x00020000u, 3000u, 1);
  CHECK_EQ(request_download(0x00020000u, size), 4);
  CHECK_EQ(transfer_all(size, size), 0);
  CHECK(IS_NRC(UDS(0x37), 0x37, 0x72));
  UDS(0x11, 0x01);
  wait_ms(2);
  CHECK_EQ(running_version(), 0x00010000u);
}

static void test_REQ_20_old_version_rejected(void)
{
  sim_init(&S);
  programming_unlocked();
  CHECK(IS_NRC(request_download(0x00010000u, 1000u), 0x34, 0x70));
  CHECK(IS_NRC(request_download(0x00000900u, 1000u), 0x34, 0x70));
  CHECK(IS_NRC(request_download(0x00020000u, 10u), 0x34, 0x31));
  CHECK(IS_NRC(request_download(0x00020000u, VECU_IMG_MAX + 1u), 0x34, 0x31));
}

static void test_REQ_20_interrupted_transfer_rollback(void)
{
  uint32_t size;
  sim_init(&S);
  programming_unlocked();
  size = make_image(0x00030000u, 8000u, 0);
  CHECK_EQ(request_download(0x00030000u, size), 4);
  CHECK_EQ(transfer_all(size, 4096u), 0);
  wait_ms(5001); /* tester lost: S3 timeout ends the programming session */
  CHECK_EQ(S.e.uds.session, UDS_SESSION_DEFAULT);
  CHECK_EQ(S.e.flash.active, 0);
  UDS(0x11, 0x01);
  wait_ms(2);
  CHECK_EQ(running_version(), 0x00010000u);
}

static void test_REQ_20_interrupted_by_reset(void)
{
  uint32_t size;
  sim_init(&S);
  programming_unlocked();
  size = make_image(0x00030000u, 8000u, 0);
  CHECK_EQ(request_download(0x00030000u, size), 4);
  CHECK_EQ(transfer_all(size, 2048u), 0);
  UDS(0x11, 0x01);
  wait_ms(2);
  CHECK_EQ(running_version(), 0x00010000u);
  CHECK(IS_NRC(UDS(0x36, 0x03, 0x00), 0x36, 0x7F)); /* back in default session */
}

static void test_REQ_20_sequence_and_access_rules(void)
{
  uint32_t size;
  sim_init(&S);
  CHECK(IS_NRC(request_download(0x00020000u, 1000u), 0x34, 0x7F)); /* default session */
  UDS(0x10, 0x03);
  CHECK(UDS(0x10, 0x02) == 6);
  CHECK(IS_NRC(request_download(0x00020000u, 1000u), 0x34, 0x33)); /* locked */
  sim_init(&S);
  programming_unlocked();
  CHECK(IS_NRC(UDS(0x36, 0x01, 0xAA), 0x36, 0x24)); /* no download active */
  CHECK(IS_NRC(UDS(0x37), 0x37, 0x24));
  CHECK(IS_NRC(UDS(0x34, 0x00, 0x44), 0x34, 0x13));
  CHECK(IS_NRC(UDS(0x34, 0x11, 0x44, 0, 2, 0, 0, 0, 0, 4, 0), 0x34, 0x31));
  size = make_image(0x00020000u, 2000u, 0);
  CHECK_EQ(request_download(0x00020000u, size), 4);
  CHECK(IS_NRC(UDS(0x36, 0x02, 0xAA), 0x36, 0x73)); /* wrong block sequence counter */
  CHECK_EQ(transfer_all(size, 1024u), 0);
  {
    static uint8_t q[2 + VECU_BLOCK_DATA];
    q[0] = 0x36; q[1] = 0x01; memcpy(&q[2], IMG, VECU_BLOCK_DATA);
    CHECK(uds(q, 2 + VECU_BLOCK_DATA) == 2 && R[1] == 0x01); /* repeated block acknowledged */
    CHECK_EQ(S.e.flash.received, 1024u);
  }
  CHECK(IS_NRC(UDS(0x37), 0x37, 0x24)); /* incomplete */
  CHECK(IS_NRC(UDS(0x36), 0x36, 0x13));
  CHECK(IS_NRC(UDS(0x37, 0x00), 0x37, 0x13));
}

static void test_REQ_20_oversized_block_suspends(void)
{
  uint32_t size;
  static uint8_t q[2 + VECU_BLOCK_DATA + 1];
  sim_init(&S);
  programming_unlocked();
  size = make_image(0x00020000u, 500u, 0);
  CHECK_EQ(request_download(0x00020000u, size), 4);
  q[0] = 0x36; q[1] = 1;
  CHECK(IS_NRC(uds(q, (uint16_t)sizeof q), 0x36, 0x71));
  CHECK_EQ(S.e.flash.active, 0);
}

static void test_REQ_20_header_mismatch(void)
{
  uint32_t size;
  sim_init(&S);
  programming_unlocked();
  size = make_image(0x00020000u, 1000u, 0);
  CHECK_EQ(request_download(0x00020001u, size), 4); /* announced version differs from the header */
  CHECK_EQ(transfer_all(size, size), 0);
  CHECK(IS_NRC(UDS(0x37), 0x37, 0x72));
}

/* ---------- SHA-256 ---------- */

static void test_sha256_vectors(void)
{
  static const uint8_t abc_hash[32] = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
                                      0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
  static const uint8_t empty_hash[4] = {0xe3, 0xb0, 0xc4, 0x42};
  static const uint8_t two_block[4] = {0x24, 0x8d, 0x6a, 0x61};
  uint8_t h[32];
  vecu_sha256((const uint8_t *)"abc", 3, h);
  CHECK(memcmp(h, abc_hash, 32) == 0);
  vecu_sha256((const uint8_t *)"", 0, h);
  CHECK(memcmp(h, empty_hash, 4) == 0);
  vecu_sha256((const uint8_t *)"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, h);
  CHECK(memcmp(h, two_block, 4) == 0);
}

int main(int argc, char **argv)
{
  static const vt_case_t cases[] = {
      {"test_REQ_14_session_control", test_REQ_14_session_control},
      {"test_REQ_14_read_data_by_identifier", test_REQ_14_read_data_by_identifier},
      {"test_REQ_14_read_and_clear_dtc", test_REQ_14_read_and_clear_dtc},
      {"test_REQ_14_tester_present_and_unknown", test_REQ_14_tester_present_and_unknown},
      {"test_REQ_14_functional_suppresses_nrc", test_REQ_14_functional_suppresses_nrc},
      {"test_REQ_15_isotp_single_and_multi_frame", test_REQ_15_isotp_single_and_multi_frame},
      {"test_REQ_15_isotp_multi_frame_request_and_stmin", test_REQ_15_isotp_multi_frame_request_and_stmin},
      {"test_isotp_errors", test_isotp_errors},
      {"test_REQ_16_security_in_default_session", test_REQ_16_security_in_default_session},
      {"test_REQ_17_attempt_limit_and_delay", test_REQ_17_attempt_limit_and_delay},
      {"test_REQ_17_good_key_and_sequence", test_REQ_17_good_key_and_sequence},
      {"test_REQ_18_s3_timeout", test_REQ_18_s3_timeout},
      {"test_REQ_18_tester_present_keeps_session", test_REQ_18_tester_present_keeps_session},
      {"test_REQ_20_good_image_activated_after_reset", test_REQ_20_good_image_activated_after_reset},
      {"test_REQ_20_bad_hash_rejected", test_REQ_20_bad_hash_rejected},
      {"test_REQ_20_old_version_rejected", test_REQ_20_old_version_rejected},
      {"test_REQ_20_interrupted_transfer_rollback", test_REQ_20_interrupted_transfer_rollback},
      {"test_REQ_20_interrupted_by_reset", test_REQ_20_interrupted_by_reset},
      {"test_REQ_20_sequence_and_access_rules", test_REQ_20_sequence_and_access_rules},
      {"test_REQ_20_oversized_block_suspends", test_REQ_20_oversized_block_suspends},
      {"test_REQ_20_header_mismatch", test_REQ_20_header_mismatch},
      {"test_sha256_vectors", test_sha256_vectors},
  };
  return vt_main(argc, argv, cases, (int)(sizeof cases / sizeof cases[0]), "uds");
}
