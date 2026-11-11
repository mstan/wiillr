/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_MM9_SCREEN_H
#define WIILLR_MM9_SCREEN_H
#include <wiillr/mm9_compositor.h>
#include <gcnllr/native_render.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct WiiMm9Screen {
    GcWiiNativeVertex vertices[4];
    GcWiiNativeDraw draw;
} WiiMm9Screen;
/* Queued-quad family of WR9E 800D5E50: logical object rectangle/UV, native
 * base-level nonpaletted resources and RGB565 vertex brightness. The caller supplies the typed
 * camera and proven inherited blend/depth state, and orders preceding work.
 * Paletted/mipmapped texture families reject explicitly. No GX operation executes. */
int wii_mm9_screen_prepare(WiiMm9Memory,uint32_t object,uint32_t brightness,
    const GcWiiNativeDraw* camera,WiiMm9Screen*,char*,uint32_t);
typedef struct WiiMm9ScreenNative WiiMm9ScreenNative;
typedef struct WiiMm9ScreenStatus {
    uint64_t draws,mesh_uploads,rejected;
    char error[192];
} WiiMm9ScreenStatus;
WiiMm9ScreenNative* wii_mm9_screen_create(WiiMm9Memory,const GcWiiNativeRenderApi*);
void wii_mm9_screen_destroy(WiiMm9ScreenNative*);
int wii_mm9_screen_draw(WiiMm9ScreenNative*,uint32_t object,uint32_t brightness,const GcWiiNativeDraw* camera);
void wii_mm9_screen_status(const WiiMm9ScreenNative*,WiiMm9ScreenStatus*);
#ifdef __cplusplus
}
#endif
#endif
