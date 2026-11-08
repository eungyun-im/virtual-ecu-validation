/* SHA-256 (FIPS 180-4), used to check software images (REQ-20). */
#ifndef VECU_SHA256_H
#define VECU_SHA256_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t h[8];
  uint64_t len;
  uint8_t buf[64];
  size_t fill;
} vecu_sha256_t;

void vecu_sha256_init(vecu_sha256_t *c);
void vecu_sha256_update(vecu_sha256_t *c, const uint8_t *data, size_t n);
void vecu_sha256_final(vecu_sha256_t *c, uint8_t out[32]);
void vecu_sha256(const uint8_t *data, size_t n, uint8_t out[32]);

#ifdef __cplusplus
}
#endif

#endif
