/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef WIILLR_REMOTE_INPUT_H
#define WIILLR_REMOTE_INPUT_H
#include <stdbool.h>
#include <stdint.h>
#include "gcnllr/native_gx.h"

/* Host controls have a screen-relative D-pad and four face buttons. Remote
 * reports retain their physical wire layout; the title chooses its hold. */
static inline uint16_t wii_remote_buttons(const GcnllrPad* pad,bool sideways) {
    uint16_t buttons=0;
    if(pad->buttons&0x100)buttons|=sideways?0x100:0x800;
    if(pad->buttons&0x200)buttons|=sideways?0x200:0x400;
    if(pad->buttons&0x400)buttons|=sideways?0x800:0x100;
    if(pad->buttons&0x800)buttons|=sideways?0x400:0x200;
    if(pad->buttons&0x1000)buttons|=0x10;
    if(pad->buttons&0x10)buttons|=0x8000;
    if(pad->buttons&0x40)buttons|=0x1000;
    if(pad->stick_x<80 || (pad->buttons&1))buttons|=sideways?8:1;
    if(pad->stick_x>176 || (pad->buttons&2))buttons|=sideways?4:2;
    if(pad->stick_y>176 || (pad->buttons&8))buttons|=sideways?2:8;
    if(pad->stick_y<80 || (pad->buttons&4))buttons|=sideways?1:4;
    return buttons;
}
#endif
