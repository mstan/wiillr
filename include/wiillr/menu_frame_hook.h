/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_MENU_FRAME_HOOK_H
#define WIILLR_MENU_FRAME_HOOK_H
#include <wiillr/menu_picture.h>
#include <wiillr/gx.h>
#include <gcnllr/native_frame.h>
typedef struct CPUState CPUState;
#ifdef __cplusplus
extern "C" {
#endif
/* 0: no enclosing native owner, use the existing caller view.
 * 1: current typed native view. Negative: an owned view is unsupported;
 * callers must stop rather than resume through an omitted GX setup. */
int wii_menu_frame_current_view(CPUState*,WiiMenuView*);
bool wii_menu_frame_start(WiiGx*,const GcWiiNativeRenderApi*,const GcWiiNativeFrameApi*);
void wii_menu_frame_capture(const GcWiiNativeCaptureApi*);
void wii_menu_frame_stop(void);
int wii_menu_frame_entry(CPUState*,uint32_t);
void wii_menu_frame_invalidate(uint32_t,uint32_t);
#ifdef __cplusplus
}
#endif
#endif
