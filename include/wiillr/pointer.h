/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_POINTER_H
#define WIILLR_POINTER_H
#include <stdint.h>
typedef struct {uint16_t x,y;} WiiPointerCamera;
static inline WiiPointerCamera wii_pointer_camera(uint16_t x,uint16_t y) {
    /* Virtual 4:3 screen with the bar below it. The camera sees mirrored X;
     * its bar position is below the screen's optical center. These calibrated
     * rays still pass through the real remote IR reports and guest KPAD math.
     * A 200-pixel dot separation remains inside the camera at every corner. */
    const WiiPointerCamera point={
        (uint16_t)(762u-((uint32_t)x*500u+32767u)/65535u),
        (uint16_t)(93u+((uint32_t)y*374u+32767u)/65535u)};
    return point;
}
#endif
