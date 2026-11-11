/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_MENU_TEXT_H
#define WIILLR_MENU_TEXT_H
#include "wiillr/menu_picture.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    GcWiiNativeTexture texture;
    int32_t left,advance;
    uint32_t glyph_width,glyph_height,cell_x,cell_y;
    uint32_t font_width,font_height,font_ascent;
    int32_t line_feed;
} WiiMenuFontGlyph;

/* Read the Menu's relocated NW4R font resources. Unicode input is explicitly
 * one 16-bit code unit, independent of the host's wchar_t representation.
 * Archive-font sheet remapping is applied before resolving texture memory.
 * No guest call, GX construction or host rendering occurs here. */
int wii_menu_font_glyph(WiiMenuMemory,uint32_t font,uint16_t code_unit,
                        WiiMenuFontGlyph*,char* error,uint32_t error_size);

#ifdef __cplusplus
}
#endif
#endif
