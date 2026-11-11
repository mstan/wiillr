/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_MM9_RENDERER_HOOK_H
#define WIILLR_MM9_RENDERER_HOOK_H
#include <wiillr/gx.h>
#include <gcnllr/native_render.h>
#include <gcnllr/native_frame.h>
#include <gcnllr/native_output.h>
bool wii_mm9_renderer_hook_start(WiiGx*,const GcWiiNativeRenderApi*,const GcWiiNativeFrameApi*);
void wii_mm9_renderer_bind_output(const GcWiiNativeOutputApi*);
void wii_mm9_renderer_hook_stop(void);
void wii_mm9_renderer_hook_invalidate(u32,u32);
int wii_mm9_renderer_hook_call(CPUState*,u32);
int wii_mm9_renderer_hook_dispatch(CPUState*,u32);
void wii_mm9_renderer_frame_marker(CPUState*,u32);
#endif
