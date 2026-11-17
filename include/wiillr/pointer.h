/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef WIILLR_POINTER_H
#define WIILLR_POINTER_H
#include <stdbool.h>
#include <stdint.h>
typedef struct {uint16_t x,y;} WiiPointerCamera;
static inline WiiPointerCamera wii_pointer_camera_view(uint16_t x,uint16_t y,bool widescreen) {
    /* Virtual 4:3 screen with the bar below it. The camera sees mirrored X;
     * its bar position is below the screen's optical center. These calibrated
     * rays still pass through the real remote IR reports and guest KPAD math.
     * Native widescreen Menu projection needs a wider horizontal ray range
     * so edge buttons are reachable before the mouse leaves the window.
     * Both ranges retain a 200-pixel dot separation inside the camera. */
    const uint32_t span=widescreen?600u:500u;
    const WiiPointerCamera point={
        (uint16_t)(512u+span/2u-((uint32_t)x*span+32767u)/65535u),
        (uint16_t)(93u+((uint32_t)y*374u+32767u)/65535u)};
    return point;
}
static inline WiiPointerCamera wii_pointer_camera(uint16_t x,uint16_t y) {
    return wii_pointer_camera_view(x,y,false);
}
#endif
