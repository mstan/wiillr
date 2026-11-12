/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_MM9_RENDERER_HOOK_H
#define WIILLR_MM9_RENDERER_HOOK_H
#include <wiillr/gx.h>
#include <gcnllr/native_render.h>
#include <gcnllr/native_frame.h>
#include <gcnllr/native_output.h>
#include <gcnllr/native_gui_effects.h>
bool wii_mm9_renderer_hook_start(WiiGx*,const GcWiiNativeRenderApi*,const GcWiiNativeFrameApi*);
bool wii_mm9_renderer_bind_hbm(const GcWiiNativeRenderApi*,const GcWiiNativeGuiApi*,
    const GcWiiNativeGuiLayersApi*,const GcWiiNativeGuiMaskApi*,const GcWiiNativeGuiEffectsApi*);
void wii_mm9_renderer_bind_output(const GcWiiNativeOutputApi*);
typedef enum {
    WII_MM9_VIEW_ORIGINAL,
    WII_MM9_VIEW_16_9,
    WII_MM9_VIEW_21_9,
    WII_MM9_VIEW_FIT
} WiiMm9View;
bool wii_mm9_renderer_view_supported(void);
bool wii_mm9_renderer_set_view(WiiGx*,WiiMm9View);
void wii_mm9_renderer_hook_stop(void);
void wii_mm9_renderer_hook_invalidate(u32,u32);
int wii_mm9_renderer_hook_call(CPUState*,u32);
int wii_mm9_renderer_hook_dispatch(CPUState*,u32);
void wii_mm9_renderer_frame_marker(CPUState*,u32);
u32 wii_mm9_renderer_spawn_radius(void);
#endif
