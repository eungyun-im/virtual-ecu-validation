/* DTC memory with the ISO 14229-1 status byte (REQ-13).
 * Confirmation threshold is 1: the first failed test confirms the DTC. Memory survives an ECUReset
 * (it lives outside vecu_t's volatile part) and is cleared only by ClearDiagnosticInformation. */
#ifndef VECU_DTC_H
#define VECU_DTC_H

#include "vecu/vecu_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DTC_TF 0x01u      /* testFailed */
#define DTC_TFTOC 0x02u   /* testFailedThisOperationCycle */
#define DTC_PDTC 0x04u    /* pendingDTC */
#define DTC_CDTC 0x08u    /* confirmedDTC */
#define DTC_TNCSLC 0x10u  /* testNotCompletedSinceLastClear */
#define DTC_TFSLC 0x20u   /* testFailedSinceLastClear */
#define DTC_TNCTOC 0x40u  /* testNotCompletedThisOperationCycle */
/* bit 7 warningIndicatorRequested is not supported (always 0) */
#define DTC_AVAILABILITY_MASK 0x7Fu

typedef enum {
  DTC_STEER_RANGE = 0,
  DTC_SPEED_RANGE,
  DTC_STEER_TIMEOUT,
  DTC_SPEED_TIMEOUT,
  DTC_STEER_E2E,
  DTC_SPEED_E2E,
  DTC_COUNT
} vecu_dtc_idx_t;

typedef struct {
  uint8_t status[DTC_COUNT];
} vecu_dtc_mem_t;

uint32_t vecu_dtc_code(vecu_dtc_idx_t i);
void vecu_dtc_clear(vecu_dtc_mem_t *m);
/* Start of an operation cycle (power-up or ECUReset): clears TFTOC, sets TNCTOC */
void vecu_dtc_new_cycle(vecu_dtc_mem_t *m);
void vecu_dtc_report(vecu_dtc_mem_t *m, vecu_dtc_idx_t i, bool failed);
uint8_t vecu_dtc_confirmed_count(const vecu_dtc_mem_t *m);

#ifdef __cplusplus
}
#endif

#endif
