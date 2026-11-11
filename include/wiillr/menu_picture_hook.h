/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_MENU_PICTURE_HOOK_H
#define WIILLR_MENU_PICTURE_HOOK_H
#include "wiillr/gx.h"
#include "gcnllr/native_render.h"
#include "gcnllr/native_gui.h"
/* Build-selected partial renderer. Slice mode counts original-renderer
 * declines; strict mode stops. Both reject failed native submissions. */
bool wii_menu_picture_hook_start(WiiGx*,const GcWiiNativeRenderApi*,const GcWiiNativeGuiApi*);
bool wii_menu_picture_hook_layers(const GcWiiNativeGuiLayersApi*);
void wii_menu_picture_hook_stop(void);
int wii_menu_picture_hook_call(CPUState*,u32 address);
void wii_menu_picture_hook_invalidate(u32 address,u32 size);
/* Shared recording boundary for native Menu pane providers. */
bool wii_menu_native_boundary(WiiGx*);
#endif
