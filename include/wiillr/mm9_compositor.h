/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_MM9_COMPOSITOR_H
#define WIILLR_MM9_COMPOSITOR_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define WII_MM9_TEXTURE_BYTES (256u*240u*2u)
#define WII_MM9_VIEW_MAX_WIDTH 864u
typedef struct WiiMm9Memory {
    void* user;
    /* Returned spans must remain valid and immutable until preparation returns. */
    const uint8_t* (*read)(void*,uint32_t,uint32_t);
} WiiMm9Memory;
typedef struct WiiMm9Frame {
    /* Actual final-screen texture layout: GX RGB565, 4x4 tiled, big endian.
     * The machine publishes this to the real 0x8039b2c0 target atomically. */
    uint8_t texture[WII_MM9_TEXTURE_BYTES];
    uint32_t final_sprite_count;
    uint32_t background_pixels,sprite_pixels,limited_sprite_rows;
} WiiMm9Frame;
/* Native animation components omitted by the original horizontal crop. The
 * insertion address preserves their order relative to the guest-visible queue.
 * Coordinates stay signed host integers rather than packed ten-bit values.
 * Margin-only draws preserve the original center and counters; when enabled,
 * their per-row sprite budget is separate from the original queue's budget. */
typedef struct WiiMm9MarginSprite {
    uint32_t before,tile,width,height,palette,priority,flags;
    int32_t x,y;
} WiiMm9MarginSprite;
typedef struct WiiMm9SpriteBatch {
    uint32_t address,end,size,margin_count,admitted;
    uint8_t packed[1024];
    WiiMm9MarginSprite margins[255];
} WiiMm9SpriteBatch;
/* Host layout provenance for an actual span of the original packed queue.
 * The queue bytes stay unchanged. Edge -1/+1 preserves the left/right inset
 * when producing an enhanced view; width256 retains original coordinates. */
typedef struct WiiMm9SpriteAnchor {
    uint32_t begin,end;
    int32_t edge;
} WiiMm9SpriteAnchor;
typedef struct WiiMm9ViewLayout {
    const WiiMm9MarginSprite* margins;
    uint32_t margin_count;
    const WiiMm9SpriteAnchor* anchors;
    uint32_t anchor_count,color_level;
} WiiMm9ViewLayout;
/* Original-canvas source rectangles for native GPU composition. These carry
 * the game's CI4 assets and raw RGB5551 palette addresses, never a rasterized
 * frame. Preparation is atomic and leaves the prior scene intact on failure. */
typedef struct WiiMm9SceneTile {
    uint32_t source,palette,source_width,source_height;
    uint32_t source_x,source_y,source_columns,source_rows,flips;
    float x,y,width,height;
} WiiMm9SceneTile;
typedef struct WiiMm9SceneSolid {
    float x,y,width,height;
    uint32_t rgb565;
} WiiMm9SceneSolid;
typedef struct WiiMm9Scene WiiMm9Scene;
WiiMm9Scene* wii_mm9_scene_create(void);
void wii_mm9_scene_destroy(WiiMm9Scene*);
int wii_mm9_scene_prepare(WiiMm9Memory,WiiMm9Scene*,char*,uint32_t);
/* Enhanced scene coordinates retain the original center at x=0..255. The
 * complete view spans -(width-256)/2..256+(width-256)/2. Solids describe opaque
 * coverage; the GPU view target must start transparent outside these regions. */
int wii_mm9_scene_layout_prepare(WiiMm9Memory,uint32_t,const WiiMm9ViewLayout*,
    WiiMm9Scene*,char*,uint32_t);
uint32_t wii_mm9_scene_width(const WiiMm9Scene*);
uint32_t wii_mm9_scene_count(const WiiMm9Scene*);
const WiiMm9SceneTile* wii_mm9_scene_tile(const WiiMm9Scene*,uint32_t);
uint32_t wii_mm9_scene_solid_count(const WiiMm9Scene*);
const WiiMm9SceneSolid* wii_mm9_scene_solid(const WiiMm9Scene*,uint32_t);
uint32_t wii_mm9_scene_backdrop(const WiiMm9Scene*);
uint32_t wii_mm9_scene_sprite_count(const WiiMm9Scene*);
/* Prepare the original WR9E animation emitter's CPU-visible queue writes and
 * uncropped components in one pass. No guest memory is written. */
int wii_mm9_sprites_prepare(WiiMm9Memory,uint32_t object,uint32_t camera,
    WiiMm9SpriteBatch*,char*,uint32_t);
/* Prepare the WR9E compositor's logical tile/sprite output directly, without
 * executing its atlas construction, scanline GX assembly or EFB readbacks.
 * No guest RAM, GPU state, queues or synchronization are changed here.
 * The integration must retain the final-screen queue and completion effects.
 * This is an unqualified candidate until the original entry/copy gate passes. */
int wii_mm9_composite_prepare(WiiMm9Memory,WiiMm9Frame*,char*,uint32_t);
/* Optional host view, centered on the unchanged 256-column guest image.
 * The standard world loader's margins use mutable source metatiles with level
 * bounds, bypassing stale rolling-cache columns. Other layers use their live
 * maps, and sprites use queued positions. This does not publish a wider guest
 * texture or change simulation.
 * Width must be even, 256..864. The output is tightly packed RGBA8, 240 rows.
 * Zero alpha marks excluded finite-canvas/room pixels for presentation clipping;
 * visible world/HUD pixels have full coverage. The guest texture stays opaque.
 * Caller owns width*240*4 bytes. Failure leaves that output unchanged. */
int wii_mm9_composite_view_prepare(WiiMm9Memory,uint32_t width,uint8_t* rgba,
    uint32_t capacity,char*,uint32_t);
/* Produces the original tiled texture and the host view in one traversal.
 * All original counters and pixels are preserved. Stock may be null.
 * Failure publishes neither output. */
int wii_mm9_composite_present_prepare(WiiMm9Memory,uint32_t width,WiiMm9Frame*,
    uint8_t* rgba,uint32_t capacity,char*,uint32_t);
int wii_mm9_composite_present_sprites_prepare(WiiMm9Memory,uint32_t width,
    const WiiMm9MarginSprite*,uint32_t count,WiiMm9Frame*,uint8_t* rgba,
    uint32_t capacity,char*,uint32_t);
/* Explicit enhanced layout. Stock is the 256-column crop of that layout;
 * its untinted texture feeds the original screen draw/copy. The wide pixels
 * receive the screen's color level, while Original leaves layout unchanged. */
int wii_mm9_composite_layout_prepare(WiiMm9Memory,uint32_t width,
    const WiiMm9ViewLayout*,WiiMm9Frame*,uint8_t* rgba,uint32_t capacity,char*,uint32_t);
#ifdef __cplusplus
}
#endif
#endif
