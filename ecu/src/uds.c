/* UDS server (ISO 14229-1), REQ-14 to REQ-20.
 * Services: 0x10 0x11 0x14 0x19 0x22 0x27 0x3E, and 0x34 0x36 0x37 in the programming session.
 * Response timing: every response is produced in the same call as the request, so P2 = 50 ms is
 * bounded by the bus and the tick loop, not by processing (REQ-15).
 * SecurityAccess key: key = rotl32(seed ^ 0x5A3C96E1, 7) + 0x1234. A demo algorithm only: a real
 * ECU would use an asymmetric scheme or a key held in a secure element. */
#include <string.h>

#include "vecu/ecu.h"

#define NRC_SNS 0x11u   /* serviceNotSupported */
#define NRC_SFNS 0x12u  /* subFunctionNotSupported */
#define NRC_IMLOIF 0x13u /* incorrectMessageLengthOrInvalidFormat */
#define NRC_CNC 0x22u   /* conditionsNotCorrect */
#define NRC_RSE 0x24u   /* requestSequenceError */
#define NRC_ROOR 0x31u  /* requestOutOfRange */
#define NRC_SAD 0x33u   /* securityAccessDenied */
#define NRC_IK 0x35u    /* invalidKey */
#define NRC_ENOA 0x36u  /* exceededNumberOfAttempts */
#define NRC_RTDNE 0x37u /* requiredTimeDelayNotExpired */
#define NRC_UDNA 0x70u  /* uploadDownloadNotAccepted */
#define NRC_TDS 0x71u   /* transferDataSuspended */
#define NRC_GPF 0x72u   /* generalProgrammingFailure */
#define NRC_WBSC 0x73u  /* wrongBlockSequenceCounter */
#define NRC_SNSIAS 0x7Fu /* serviceNotSupportedInActiveSession */

#define S3_MS 5000u
#define LOCKOUT_MS 10000u
#define MAX_ATTEMPTS 3u
#define P2_MS 50u
#define P2STAR_10MS 500u

static uint16_t nrc(uint8_t *resp, uint8_t sid, uint8_t code)
{
  resp[0] = 0x7Fu;
  resp[1] = sid;
  resp[2] = code;
  return 3u;
}

/* Exported for the unit tests and the tester tool, which compute the expected key. */
/* cppcheck-suppress misra-c2012-8.7 ; DEV-03 */
uint32_t vecu_uds_key(uint32_t seed)
{
  const uint32_t x = seed ^ 0x5A3C96E1u;
  return ((x << 7) | (x >> 25)) + 0x1234u;
}

void vecu_uds_init(vecu_uds_t *u, uint32_t now)
{
  const uint32_t prng = u->seed_state;
  (void)memset(u, 0, sizeof *u);
  u->session = UDS_SESSION_DEFAULT;
  u->last_req_ms = now;
  u->seed_state = (prng != 0u) ? prng : (0x9E3779B9u ^ now);
}

static uint32_t next_seed(vecu_uds_t *u, uint32_t now)
{
  uint32_t s;
  do {
    u->seed_state = (u->seed_state * 1664525u) + 1013904223u + now;
    s = u->seed_state;
  } while (s == 0u);
  return s;
}

static void to_default(vecu_t *e)
{
  e->uds.session = UDS_SESSION_DEFAULT;
  e->uds.unlocked = 0u;
  e->uds.seed_pending = 0u;
  vecu_swu_abort(&e->flash); /* interrupted download: running version stays (REQ-20) */
}

/* ---------- 0x10 DiagnosticSessionControl ---------- */
static uint16_t svc_session(vecu_t *e, const uint8_t *q, uint16_t n, uint8_t *r, int *suppress)
{
  uint8_t code = 0u;
  uint16_t rl = 0u;
  uint8_t sub = 0u;
  if (n != 2u) {
    code = NRC_IMLOIF;
  } else {
    sub = (uint8_t)(q[1] & 0x7Fu);
    *suppress = ((q[1] & 0x80u) != 0u) ? 1 : 0;
    if (sub == (uint8_t)UDS_SESSION_DEFAULT) {
      to_default(e);
    } else if (sub == (uint8_t)UDS_SESSION_EXTENDED) {
      if (e->uds.session == UDS_SESSION_PROGRAMMING) {
        vecu_swu_abort(&e->flash);
      }
      e->uds.session = UDS_SESSION_EXTENDED;
    } else if (sub == (uint8_t)UDS_SESSION_PROGRAMMING) {
      if (e->uds.session == UDS_SESSION_DEFAULT) {
        code = NRC_CNC; /* programming only from the extended session */
      } else {
        e->uds.session = UDS_SESSION_PROGRAMMING;
      }
    } else {
      code = NRC_SFNS;
    }
  }
  if (code != 0u) {
    rl = nrc(r, 0x10u, code);
  } else {
    r[0] = 0x50u;
    r[1] = sub;
    r[2] = 0u;
    r[3] = (uint8_t)P2_MS;
    r[4] = (uint8_t)(P2STAR_10MS >> 8);
    r[5] = (uint8_t)(P2STAR_10MS & 0xFFu);
    rl = 6u;
  }
  return rl;
}

/* ---------- 0x11 ECUReset ---------- */
static uint16_t svc_reset(vecu_t *e, const uint8_t *q, uint16_t n, uint8_t *r, int *suppress)
{
  uint16_t rl;
  if (n != 2u) {
    rl = nrc(r, 0x11u, NRC_IMLOIF);
  } else if ((q[1] & 0x7Fu) != 0x01u) {
    rl = nrc(r, 0x11u, NRC_SFNS);
  } else {
    *suppress = ((q[1] & 0x80u) != 0u) ? 1 : 0;
    e->uds.reset_pending = 1u; /* after the response has left */
    r[0] = 0x51u;
    r[1] = 0x01u;
    rl = 2u;
  }
  return rl;
}

/* ---------- 0x22 ReadDataByIdentifier ---------- */
/* Writes DID and data at r[at]; returns the bytes written, 0 for an unsupported DID. */
static uint16_t put_did(const vecu_t *e, uint16_t did, uint8_t *r, uint16_t at)
{
  static const uint8_t k_vin[17] = {(uint8_t)'V', (uint8_t)'E', (uint8_t)'C', (uint8_t)'U', (uint8_t)'D', (uint8_t)'E',
                                    (uint8_t)'M', (uint8_t)'O', (uint8_t)'0', (uint8_t)'0', (uint8_t)'0', (uint8_t)'0',
                                    (uint8_t)'0', (uint8_t)'0', (uint8_t)'0', (uint8_t)'0', (uint8_t)'1'};
  static const uint8_t k_serial[7] = {(uint8_t)'S', (uint8_t)'N', (uint8_t)'-', (uint8_t)'0',
                                      (uint8_t)'0', (uint8_t)'0', (uint8_t)'1'};
  uint16_t len = 0u;
  uint8_t v[17];
  switch (did) {
    case 0xF190u:
      len = (uint16_t)sizeof k_vin;
      (void)memcpy(v, k_vin, sizeof k_vin);
      break;
    case 0xF18Cu:
      len = (uint16_t)sizeof k_serial;
      (void)memcpy(v, k_serial, sizeof k_serial);
      break;
    case 0xF195u: /* software version, u32 big-endian (major << 16 | minor << 8 | patch) */
      len = 4u;
      v[0] = (uint8_t)(e->flash.running_version >> 24);
      v[1] = (uint8_t)(e->flash.running_version >> 16);
      v[2] = (uint8_t)(e->flash.running_version >> 8);
      v[3] = (uint8_t)e->flash.running_version;
      break;
    case 0x0100u: /* ECU state */
      len = 1u;
      v[0] = (uint8_t)e->state;
      break;
    case 0x0101u: /* active faults */
      len = 1u;
      v[0] = e->faults;
      break;
    case 0x0102u: /* steering output, 0.1 deg, int16 big-endian */
      len = 2u;
      v[0] = (uint8_t)((uint16_t)(int16_t)e->steer_out >> 8);
      v[1] = (uint8_t)((uint16_t)(int16_t)e->steer_out & 0xFFu);
      break;
    case 0x0103u: /* speed output, 0.01 km/h, uint16 big-endian */
      len = 2u;
      v[0] = (uint8_t)((uint16_t)e->speed_out >> 8);
      v[1] = (uint8_t)((uint16_t)e->speed_out & 0xFFu);
      break;
    default:
      len = 0u; /* not supported */
      break;
  }
  if (len != 0u) {
    r[at] = (uint8_t)(did >> 8);
    r[at + 1u] = (uint8_t)(did & 0xFFu);
    (void)memcpy(&r[at + 2u], v, len);
    len = (uint16_t)(len + 2u);
  }
  return len;
}

static uint16_t svc_rdbi(const vecu_t *e, const uint8_t *q, uint16_t n, uint8_t *r)
{
  uint16_t rl;
  if ((n < 3u) || (((n - 1u) % 2u) != 0u) || (n > (1u + (2u * 8u)))) {
    rl = nrc(r, 0x22u, NRC_IMLOIF);
  } else {
    uint16_t at = 1u;
    uint16_t i = 1u;
    bool ok = true;
    r[0] = 0x62u;
    while (ok && (i < n)) {
      const uint16_t did = (uint16_t)(((uint16_t)q[i] << 8) | q[i + 1u]);
      const uint16_t w = put_did(e, did, r, at);
      if (w == 0u) {
        ok = false;
      } else {
        at = (uint16_t)(at + w);
        i = (uint16_t)(i + 2u);
      }
    }
    rl = ok ? at : nrc(r, 0x22u, NRC_ROOR);
  }
  return rl;
}

/* ---------- 0x19 ReadDTCInformation ---------- */
static uint16_t put_dtc(const vecu_t *e, uint8_t *r, uint16_t at, uint8_t i)
{
  const uint32_t code = vecu_dtc_code((vecu_dtc_idx_t)i);
  r[at] = (uint8_t)(code >> 16);
  r[at + 1u] = (uint8_t)(code >> 8);
  r[at + 2u] = (uint8_t)code;
  r[at + 3u] = (uint8_t)(e->dtc.status[i] & DTC_AVAILABILITY_MASK);
  return (uint16_t)(at + 4u);
}

static uint16_t svc_rdtc(const vecu_t *e, const uint8_t *q, uint16_t n, uint8_t *r, int *suppress)
{
  uint8_t code = 0u;
  uint16_t at = 0u;
  uint8_t sub;
  uint8_t i;
  if (n < 2u) {
    code = NRC_IMLOIF;
  } else {
    sub = (uint8_t)(q[1] & 0x7Fu);
    *suppress = ((q[1] & 0x80u) != 0u) ? 1 : 0;
    r[0] = 0x59u;
    r[1] = sub;
    r[2] = DTC_AVAILABILITY_MASK;
    if ((sub == 0x01u) || (sub == 0x02u)) {
      if (n != 3u) {
        code = NRC_IMLOIF;
      } else {
        const uint8_t mask = q[2];
        uint16_t count = 0u;
        at = (sub == 0x01u) ? (uint16_t)6u : (uint16_t)3u;
        for (i = 0u; i < (uint8_t)DTC_COUNT; ++i) {
          if ((e->dtc.status[i] & mask & DTC_AVAILABILITY_MASK) != 0u) {
            ++count;
            if (sub == 0x02u) {
              at = put_dtc(e, r, at, i);
            }
          }
        }
        if (sub == 0x01u) {
          r[3] = 0x01u; /* DTCFormatIdentifier: ISO 14229-1 */
          r[4] = (uint8_t)(count >> 8);
          r[5] = (uint8_t)(count & 0xFFu);
        }
      }
    } else if (sub == 0x0Au) {
      if (n != 2u) {
        code = NRC_IMLOIF;
      } else {
        at = 3u;
        for (i = 0u; i < (uint8_t)DTC_COUNT; ++i) {
          at = put_dtc(e, r, at, i);
        }
      }
    } else {
      code = NRC_SFNS;
    }
  }
  return (code != 0u) ? nrc(r, 0x19u, code) : at;
}

/* ---------- 0x14 ClearDiagnosticInformation ---------- */
static uint16_t svc_clear(vecu_t *e, const uint8_t *q, uint16_t n, uint8_t *r)
{
  uint16_t rl;
  if (n != 4u) {
    rl = nrc(r, 0x14u, NRC_IMLOIF);
  } else if ((q[1] != 0xFFu) || (q[2] != 0xFFu) || (q[3] != 0xFFu)) {
    rl = nrc(r, 0x14u, NRC_ROOR);
  } else {
    vecu_dtc_clear(&e->dtc);
    r[0] = 0x54u;
    rl = 1u;
  }
  return rl;
}

/* ---------- 0x27 SecurityAccess (REQ-16, REQ-17) ---------- */
static uint8_t security_seed(vecu_uds_t *u, uint16_t n, uint8_t *r, uint32_t now)
{
  uint8_t code = 0u;
  if (n != 2u) {
    code = NRC_IMLOIF;
  } else {
    u->seed = (u->unlocked != 0u) ? 0u : next_seed(u, now);
    u->seed_pending = (u->unlocked == 0u) ? 1u : 0u;
    r[0] = 0x67u;
    r[1] = 0x01u;
    r[2] = (uint8_t)(u->seed >> 24);
    r[3] = (uint8_t)(u->seed >> 16);
    r[4] = (uint8_t)(u->seed >> 8);
    r[5] = (uint8_t)u->seed;
  }
  return code;
}

static uint8_t security_key(vecu_uds_t *u, const uint8_t *q, uint16_t n, uint8_t *r, uint32_t now)
{
  uint8_t code = 0u;
  if (n != 6u) {
    code = NRC_IMLOIF;
  } else if (u->seed_pending == 0u) {
    code = NRC_RSE;
  } else {
    const uint32_t key = ((uint32_t)q[2] << 24) | ((uint32_t)q[3] << 16) | ((uint32_t)q[4] << 8) | (uint32_t)q[5];
    u->seed_pending = 0u;
    if (key == vecu_uds_key(u->seed)) {
      u->unlocked = 1u;
      u->failed_attempts = 0u;
      r[0] = 0x67u;
      r[1] = 0x02u;
    } else {
      ++u->failed_attempts;
      if (u->failed_attempts >= MAX_ATTEMPTS) {
        u->locked_out = 1u;
        u->lockout_until_ms = now + LOCKOUT_MS;
        code = NRC_ENOA;
      } else {
        code = NRC_IK;
      }
    }
  }
  return code;
}

static uint16_t svc_security(vecu_t *e, const uint8_t *q, uint16_t n, uint8_t *r, uint32_t now)
{
  vecu_uds_t *u = &e->uds;
  uint16_t rl;
  uint8_t code = 0u;
  uint8_t sub = 0u;
  if (u->session == UDS_SESSION_DEFAULT) {
    code = NRC_SNSIAS;
  } else if (n < 2u) {
    code = NRC_IMLOIF;
  } else {
    sub = (uint8_t)(q[1] & 0x7Fu);
    if ((sub != 0x01u) && (sub != 0x02u)) {
      code = NRC_SFNS;
    } else if ((u->locked_out != 0u) && !vecu_time_reached(now, u->lockout_until_ms)) {
      code = NRC_RTDNE;
    } else {
      if (u->locked_out != 0u) { /* delay expired */
        u->locked_out = 0u;
        u->failed_attempts = 0u;
      }
      code = (sub == 0x01u) ? security_seed(u, n, r, now) : security_key(u, q, n, r, now);
    }
  }
  if (code != 0u) {
    rl = nrc(r, 0x27u, code);
  } else {
    rl = (sub == 0x01u) ? (uint16_t)6u : (uint16_t)2u; /* seed response or key accepted */
  }
  return rl;
}

/* ---------- 0x3E TesterPresent ---------- */
static uint16_t svc_tester(const uint8_t *q, uint16_t n, uint8_t *r, int *suppress)
{
  uint16_t rl;
  if (n != 2u) {
    rl = nrc(r, 0x3Eu, NRC_IMLOIF);
  } else if ((q[1] & 0x7Fu) != 0u) {
    rl = nrc(r, 0x3Eu, NRC_SFNS);
  } else {
    *suppress = ((q[1] & 0x80u) != 0u) ? 1 : 0;
    r[0] = 0x7Eu;
    r[1] = 0x00u;
    rl = 2u;
  }
  return rl;
}

/* ---------- 0x34 / 0x36 / 0x37 software download (REQ-20) ---------- */
static uint8_t swu_nrc(vecu_swu_result_t s)
{
  static const uint8_t map[] = {0u, NRC_RSE, NRC_ROOR, NRC_UDNA, NRC_TDS, NRC_GPF, NRC_WBSC, 0u};
  return map[s];
}

static uint32_t rd_be32(const uint8_t *p)
{
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* 0x34 RequestDownload: format 0x00 (no compression or encryption), 4-byte address and size.
 * The memoryAddress field carries the image version. */
static uint8_t req_download(vecu_t *e, const uint8_t *q, uint16_t n, uint8_t *r)
{
  uint8_t code = 0u;
  if (n != 11u) {
    code = NRC_IMLOIF;
  } else if ((q[1] != 0x00u) || (q[2] != 0x44u)) {
    code = NRC_ROOR;
  } else {
    const vecu_swu_result_t s = vecu_swu_request_download(&e->flash, rd_be32(&q[3]), rd_be32(&q[7]));
    if (s != SWU_OK) {
      code = swu_nrc(s);
    } else {
      r[0] = 0x74u;
      r[1] = 0x20u; /* maxNumberOfBlockLength in 2 bytes */
      r[2] = (uint8_t)((VECU_BLOCK_DATA + 2u) >> 8);
      r[3] = (uint8_t)((VECU_BLOCK_DATA + 2u) & 0xFFu);
    }
  }
  return code;
}

/* 0x36 TransferData */
static uint8_t transfer_data(vecu_t *e, const uint8_t *q, uint16_t n, uint8_t *r)
{
  uint8_t code = 0u;
  if (n < 2u) {
    code = NRC_IMLOIF;
  } else {
    const vecu_swu_result_t s = vecu_swu_transfer(&e->flash, q[1], &q[2], (uint16_t)(n - 2u));
    if ((s != SWU_OK) && (s != SWU_REPEAT)) {
      if (s != SWU_E_WRONG_SEQ) {
        vecu_swu_abort(&e->flash);
      }
      code = swu_nrc(s);
    } else {
      r[0] = 0x76u;
      r[1] = q[1];
    }
  }
  return code;
}

/* 0x37 RequestTransferExit */
static uint8_t transfer_exit(vecu_t *e, uint16_t n, uint8_t *r)
{
  uint8_t code = 0u;
  if (n != 1u) {
    code = NRC_IMLOIF;
  } else {
    const vecu_swu_result_t s = vecu_swu_exit(&e->flash);
    if (s != SWU_OK) {
      code = swu_nrc(s);
    } else {
      r[0] = 0x77u;
    }
  }
  return code;
}

static uint16_t svc_download(vecu_t *e, uint8_t sid, const uint8_t *q, uint16_t n, uint8_t *r)
{
  uint8_t code;
  uint16_t rl;
  if (e->uds.session != UDS_SESSION_PROGRAMMING) {
    code = NRC_SNSIAS;
  } else if (e->uds.unlocked == 0u) {
    code = NRC_SAD;
  } else if (sid == 0x34u) {
    code = req_download(e, q, n, r);
  } else if (sid == 0x36u) {
    code = transfer_data(e, q, n, r);
  } else {
    code = transfer_exit(e, n, r);
  }
  if (code != 0u) {
    rl = nrc(r, sid, code);
  } else if (sid == 0x34u) {
    rl = 4u;
  } else if (sid == 0x36u) {
    rl = 2u;
  } else {
    rl = 1u;
  }
  return rl;
}

/* ---------- dispatcher ---------- */
uint16_t vecu_uds_handle(vecu_t *e, const uint8_t *req, uint16_t len, int functional, uint32_t now, uint8_t *resp)
{
  uint16_t rl;
  int suppress = 0;
  const uint8_t sid = req[0];
  e->uds.last_req_ms = now;
  switch (sid) {
    case 0x10u: rl = svc_session(e, req, len, resp, &suppress); break;
    case 0x11u: rl = svc_reset(e, req, len, resp, &suppress); break;
    case 0x14u: rl = svc_clear(e, req, len, resp); break;
    case 0x19u: rl = svc_rdtc(e, req, len, resp, &suppress); break;
    case 0x22u: rl = svc_rdbi(e, req, len, resp); break;
    case 0x27u: rl = svc_security(e, req, len, resp, now); break;
    case 0x3Eu: rl = svc_tester(req, len, resp, &suppress); break;
    case 0x34u:
    case 0x36u:
    case 0x37u: rl = svc_download(e, sid, req, len, resp); break;
    default: rl = nrc(resp, sid, NRC_SNS); break;
  }
  if (resp[0] == 0x7Fu) {
    /* ISO 14229-1: functional requests get no NRC 0x11, 0x12, 0x31, 0x7E, 0x7F */
    if ((functional != 0) && ((resp[2] == NRC_SNS) || (resp[2] == NRC_SFNS) || (resp[2] == NRC_ROOR) ||
                              (resp[2] == 0x7Eu) || (resp[2] == NRC_SNSIAS))) {
      rl = 0u;
    }
  } else if (suppress != 0) {
    rl = 0u;
  } else {
    /* positive response */
  }
  return rl;
}

void vecu_uds_tick(vecu_t *e, uint32_t now)
{
  if ((e->uds.session != UDS_SESSION_DEFAULT) && ((uint32_t)(now - e->uds.last_req_ms) >= S3_MS)) {
    to_default(e); /* REQ-18 */
  }
  if ((e->uds.reset_pending != 0u) && !vecu_isotp_busy(&e->isotp)) {
    vecu_reset(e, now);
  }
}
