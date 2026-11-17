/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef WIILLR_MM9_OVERLAY_H
#define WIILLR_MM9_OVERLAY_H
#include "wiillr/mm9_compositor.h"
#include <gcnllr/native_render.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct WiiMm9OverlayNative WiiMm9OverlayNative;
typedef struct {
    uint64_t entries,nodes,glyphs,draws,mesh_uploads,rejected;
    char error[192];
} WiiMm9OverlayStatus;
WiiMm9OverlayNative* wii_mm9_overlay_create(WiiMm9Memory,
    void (*write_byte)(void*,uint32_t,uint8_t),const GcWiiNativeRenderApi*);
void wii_mm9_overlay_destroy(WiiMm9OverlayNative*);
/* Whole ordered text-list submission. Unsupported input rejects before any
 * RAM effect, mesh upload or draw. No original font/GX operation executes. */
int wii_mm9_overlay_draw(WiiMm9OverlayNative*,uint32_t head,uint32_t fonts,
    uint32_t encodings,uint32_t brightness,const GcWiiNativeDraw* view);
void wii_mm9_overlay_status(const WiiMm9OverlayNative*,WiiMm9OverlayStatus*);
#ifdef __cplusplus
}
#endif
#endif
