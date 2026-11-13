/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_MENU_TRANSFORM_H
#define WIILLR_MENU_TRANSFORM_H
#include <stdbool.h>
#include <stdint.h>
typedef struct CPUState CPUState;
#ifdef __cplusplus
extern "C" {
#endif
/* Provisional self-transform plan for retail Pane::CalculateMtx's zero-angle
 * case. No guest state is changed. Publishing the plan requires separate
 * certification of the original continuation, FP status and cycle effects. */
typedef struct WiiMenuTransformPlan {
    uint32_t pane,info,parent;
    uint8_t info_flags,pane_flags;
    float input_scale[2],aspect[2],translation[3];
    float scale[2],rotation[12],intermediate[12],composed[12],local[12],global[12];
} WiiMenuTransformPlan;
bool wii_menu_transform_plan(CPUState*,WiiMenuTransformPlan*);
/* Entry after the original save-register prologue, before the visible test.
 * False leaves the original native body selected and publishes no writes. */
bool wii_menu_transform_apply(CPUState*);
#ifdef __cplusplus
}
#endif
#endif
