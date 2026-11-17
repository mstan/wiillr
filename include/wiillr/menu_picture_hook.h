/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef WIILLR_MENU_PICTURE_HOOK_H
#define WIILLR_MENU_PICTURE_HOOK_H
#include "wiillr/gx.h"
#include "gcnllr/native_render.h"
#include "gcnllr/native_gui.h"
#include "gcnllr/native_gui_effects.h"
#include "gcnllr/native_gui_program.h"
/* Build-selected partial renderer. Slice mode counts original-renderer
 * declines; strict mode stops. Both reject failed native submissions. */
bool wii_menu_picture_hook_start(WiiGx*,const GcWiiNativeRenderApi*,const GcWiiNativeGuiApi*);
bool wii_menu_picture_hook_layers(const GcWiiNativeGuiLayersApi*);
bool wii_menu_picture_hook_mask(const GcWiiNativeGuiMaskApi*);
bool wii_menu_picture_hook_effects(const GcWiiNativeGuiEffectsApi*);
bool wii_menu_picture_hook_program(const GcWiiNativeGuiProgramApi*);
void wii_menu_picture_hook_stop(void);
int wii_menu_picture_hook_call(CPUState*,u32 address);
int wii_menu_picture_hook_window(CPUState*);
void wii_menu_picture_hook_invalidate(u32 address,u32 size);
/* Shared recording boundary for native Menu pane providers. */
bool wii_menu_native_boundary(WiiGx*);
#endif
