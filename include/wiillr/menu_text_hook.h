/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef WIILLR_MENU_TEXT_HOOK_H
#define WIILLR_MENU_TEXT_HOOK_H
#include "wiillr/gx.h"
#include "gcnllr/native_render.h"
#include "gcnllr/native_gui.h"
bool wii_menu_text_hook_start(WiiGx*,const GcWiiNativeRenderApi*,const GcWiiNativeGuiApi*);
void wii_menu_text_hook_stop(void);
int wii_menu_text_hook_call(CPUState*,u32 address);
void wii_menu_text_hook_invalidate(u32 address,u32 size);
#endif
