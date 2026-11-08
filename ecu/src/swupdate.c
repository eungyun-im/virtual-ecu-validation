#include "vecu/swupdate.h"

#include <string.h>

#include "vecu/sha256.h"

static uint32_t be32(const uint8_t *p)
{
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

void vecu_flash_init(vecu_flash_t *f)
{
  memset(f, 0, sizeof *f);
  f->running_version = VECU_FW_VERSION_INITIAL;
}

void vecu_swu_abort(vecu_flash_t *f)
{
  f->active = 0u;
  f->received = 0u;
  f->size = 0u;
}

vecu_swu_result_t vecu_swu_request_download(vecu_flash_t *f, uint32_t version, uint32_t size)
{
  vecu_swu_result_t r = SWU_OK;
  vecu_swu_abort(f); /* a new request replaces an unfinished one */
  if ((size <= VECU_IMG_HDR) || (size > VECU_IMG_MAX)) {
    r = SWU_E_RANGE;
  } else if (version <= f->running_version) {
    r = SWU_E_NOT_ACCEPTED; /* anti-rollback */
  } else {
    f->active = 1u;
    f->announced_version = version;
    f->size = size;
    f->received = 0u;
    f->next_seq = 1u;
  }
  return r;
}

vecu_swu_result_t vecu_swu_transfer(vecu_flash_t *f, uint8_t seq, const uint8_t *data, uint16_t n)
{
  vecu_swu_result_t r = SWU_OK;
  if (f->active == 0u) {
    r = SWU_E_SEQUENCE;
  } else if ((f->received > 0u) && ((uint8_t)(seq + 1u) == f->next_seq)) {
    r = SWU_REPEAT; /* ISO 14229-1: a repeated block is acknowledged and not written again */
  } else if (seq != f->next_seq) {
    r = SWU_E_WRONG_SEQ;
  } else if ((n == 0u) || (n > VECU_BLOCK_DATA) || ((f->received + n) > f->size)) {
    r = SWU_E_SUSPENDED;
  } else {
    memcpy(&f->buf[f->received], data, n);
    f->received += n;
    f->next_seq = (uint8_t)(f->next_seq + 1u);
  }
  return r;
}

vecu_swu_result_t vecu_swu_exit(vecu_flash_t *f)
{
  vecu_swu_result_t r = SWU_OK;
  uint8_t digest[32];
  if ((f->active == 0u) || (f->received != f->size)) {
    r = SWU_E_SEQUENCE;
  } else if ((memcmp(f->buf, "VECU", 4u) != 0) || (be32(&f->buf[4]) != f->announced_version) ||
             (be32(&f->buf[8]) != (f->size - VECU_IMG_HDR))) {
    r = SWU_E_PROGRAMMING;
  } else {
    vecu_sha256(&f->buf[VECU_IMG_HDR], f->size - VECU_IMG_HDR, digest);
    if (memcmp(digest, &f->buf[12], 32u) != 0) {
      r = SWU_E_PROGRAMMING;
    } else {
      f->staged_version = f->announced_version;
    }
  }
  vecu_swu_abort(f);
  return r;
}

void vecu_swu_on_reset(vecu_flash_t *f)
{
  vecu_swu_abort(f);
  if (f->staged_version > f->running_version) {
    f->running_version = f->staged_version;
  }
  f->staged_version = 0u;
}
