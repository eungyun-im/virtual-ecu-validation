/* Software update over UDS 0x34/0x36/0x37 with integrity check, anti-rollback and rollback on
 * interruption (REQ-20). The "flash" is RAM: one running version and one staged image slot.
 *
 * Image = 64-byte header + payload. Header (big-endian):
 *   0  magic "VECU"   4  version u32   8  payload length u32   12  SHA-256 of payload (32 bytes)
 *   44 reserved (20 bytes, zero)
 * RequestDownload carries the image version in its memoryAddress field and the image size in
 * memorySize, so the version is checked before any data is transferred (NRC 0x70). */
#ifndef VECU_SWUPDATE_H
#define VECU_SWUPDATE_H

#include "vecu/vecu_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VECU_IMG_HDR 64u
#define VECU_IMG_MAX (VECU_IMG_HDR + 16384u)
#define VECU_BLOCK_DATA 1024u /* data bytes per TransferData */
#define VECU_FW_VERSION_INITIAL 0x00010000u /* 1.0.0 */

typedef struct {
  uint32_t running_version;
  uint32_t staged_version; /* 0 = nothing staged; activated by ECUReset */
  /* download in progress */
  uint8_t active;
  uint32_t announced_version;
  uint32_t size;
  uint32_t received;
  uint8_t next_seq;
  uint8_t buf[VECU_IMG_MAX];
} vecu_flash_t;

typedef enum {
  SWU_OK = 0,
  SWU_E_SEQUENCE,  /* NRC 0x24 */
  SWU_E_RANGE,     /* NRC 0x31 */
  SWU_E_NOT_ACCEPTED, /* NRC 0x70 */
  SWU_E_SUSPENDED, /* NRC 0x71 */
  SWU_E_PROGRAMMING, /* NRC 0x72 */
  SWU_E_WRONG_SEQ, /* NRC 0x73 */
  SWU_REPEAT       /* repeated block: positive response, data ignored */
} vecu_swu_result_t;

void vecu_flash_init(vecu_flash_t *f);
vecu_swu_result_t vecu_swu_request_download(vecu_flash_t *f, uint32_t version, uint32_t size);
vecu_swu_result_t vecu_swu_transfer(vecu_flash_t *f, uint8_t seq, const uint8_t *data, uint16_t n);
vecu_swu_result_t vecu_swu_exit(vecu_flash_t *f);
/* Interrupted transfer (session change, S3 timeout, reset): drop it, running version unchanged */
void vecu_swu_abort(vecu_flash_t *f);
/* ECUReset: activate a staged image */
void vecu_swu_on_reset(vecu_flash_t *f);

#ifdef __cplusplus
}
#endif

#endif
