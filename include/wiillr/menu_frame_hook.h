/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_MENU_FRAME_HOOK_H
#define WIILLR_MENU_FRAME_HOOK_H
#include <wiillr/menu_picture.h>
typedef struct CPUState CPUState;
#ifdef __cplusplus
extern "C" {
#endif
/* 0: no enclosing native owner, use the existing caller view.
 * 1: current typed native view. Negative: an owned view is unsupported;
 * callers must stop rather than resume through an omitted GX setup. */
int wii_menu_frame_current_view(CPUState*,WiiMenuView*);
#ifdef __cplusplus
}
#endif
#endif
