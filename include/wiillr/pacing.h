/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_PACING_H
#define WIILLR_PACING_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Host-only state. VI supplies the period; guest clocks remain untouched. */
typedef struct {
    uint64_t origin_ns;
    double elapsed_seconds;
    int armed;
} WiiPacing;
void wii_pacing_reset(WiiPacing*);
void wii_pacing_on_field(WiiPacing*, double period_seconds);

#ifdef __cplusplus
}
#endif
#endif
