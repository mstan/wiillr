/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef WIILLR_MENU_FRAME_HOOK_H
#define WIILLR_MENU_FRAME_HOOK_H
#include <wiillr/menu_picture.h>
#include <wiillr/gx.h>
#include <gcnllr/native_frame.h>
#include <gcnllr/native_display.h>
typedef struct CPUState CPUState;
#ifdef __cplusplus
extern "C" {
#endif
/* 0: no enclosing native owner, use the existing caller view.
 * 1: current typed native view. Negative: an owned view is unsupported;
 * callers must stop rather than resume through an omitted GX setup. */
int wii_menu_frame_current_view(CPUState*,WiiMenuView*);
int wii_menu_frame_current_model(CPUState*,float output[12]);
bool wii_menu_frame_keyboard_visible(const WiiGx*);
bool wii_menu_frame_widescreen(const WiiGx*);
/* Retire verified SDK cache setup owned by the native Menu renderer. Other
 * stores retain the machine's ordinary gather/ownership checks. */
bool wii_menu_frame_gather(WiiMachine*,CPUState*,uint32_t,uint64_t,unsigned);
bool wii_menu_frame_start(WiiGx*,const GcWiiNativeRenderApi*,const GcWiiNativeFrameApi*);
void wii_menu_frame_capture(const GcWiiNativeCaptureApi*);
void wii_menu_frame_display(const GcWiiNativeDisplayApi*);
void wii_menu_frame_stop(void);
int wii_menu_frame_entry(CPUState*,uint32_t);
void wii_menu_frame_invalidate(uint32_t,uint32_t);
#ifdef __cplusplus
}
#endif
#endif
