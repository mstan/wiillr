/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_MM9_INDEXED_H
#define WIILLR_MM9_INDEXED_H
#include <wiillr/mm9_screen.h>
#include <gcnllr/native_indexed.h>
#ifdef __cplusplus
extern "C" {
#endif
/* One source tile or sprite rectangle, not a CPU-composited image. Coordinates
 * are in the original 256x240 logical canvas; an actual queued screen supplies
 * its placement/view/tint. Pixel assets stay in their native CI4/RGB5551 form. */
typedef struct {
    uint32_t source,palette,source_width,source_height;
    uint32_t source_x,source_y,source_columns,source_rows,flips;
    float x,y,width,height;
} WiiMm9IndexedTile;
/* Pure preparation, no guest writes or pixel decode. Unsupported cropped or
 * filtered final-screen canvases reject; failure preserves output. */
int wii_mm9_indexed_prepare(const WiiMm9IndexedTile*,const WiiMm9Screen* canvas,
    WiiMm9Screen* output,char*,uint32_t);
/* Uses Render1 retained meshes and the explicit indexed material entry. */
WiiMm9ScreenNative* wii_mm9_indexed_create(WiiMm9Memory,const GcWiiNativeRenderApi*,
    const GcWiiNativeIndexedApi*);
/* Resolve retained geometry without drawing. A stock and an offscreen view
 * may share this handle while changing only their draw/view state. */
int wii_mm9_indexed_retain(WiiMm9ScreenNative*,uint32_t key,const WiiMm9Screen*,GcWiiNativeDraw*);
#ifdef __cplusplus
}
#endif
#endif
