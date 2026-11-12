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
#ifdef __cplusplus
}
#endif
#endif
