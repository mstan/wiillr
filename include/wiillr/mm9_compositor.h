/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_MM9_COMPOSITOR_H
#define WIILLR_MM9_COMPOSITOR_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define WII_MM9_TEXTURE_BYTES (256u*240u*2u)
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
/* Prepare the WR9E compositor's logical tile/sprite output directly, without
 * executing its atlas construction, scanline GX assembly or EFB readbacks.
 * No guest RAM, GPU state, queues or synchronization are changed here.
 * The integration must retain the final-screen queue and completion effects.
 * This is an unqualified candidate until the original entry/copy gate passes. */
int wii_mm9_composite_prepare(WiiMm9Memory,WiiMm9Frame*,char*,uint32_t);
#ifdef __cplusplus
}
#endif
#endif
