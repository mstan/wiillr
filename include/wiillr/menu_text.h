/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef WIILLR_MENU_TEXT_H
#define WIILLR_MENU_TEXT_H
#include "wiillr/menu_picture.h"
#include <gcnllr/native_gui.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    GcWiiNativeTexture texture;
    int32_t left,advance;
    uint32_t glyph_width,glyph_height,cell_x,cell_y;
    uint32_t font_width,font_height,font_ascent;
    int32_t line_feed,baseline;
} WiiMenuFontGlyph;

/* Read the Menu's relocated NW4R font resources. Unicode input is explicitly
 * one 16-bit code unit, independent of the host's wchar_t representation.
 * Archive-font sheet remapping is applied before resolving texture memory.
 * No guest call, GX construction or host rendering occurs here. */
int wii_menu_font_glyph(WiiMenuMemory,uint32_t font,uint16_t code_unit,
                        WiiMenuFontGlyph*,char* error,uint32_t error_size);
/* Shared relocated RFNT resource reader. The title binding owns text decoding
 * and font-class validation. Zero sheet_offsets selects an ordinary font. */
int wii_resource_font_glyph(WiiMenuMemory,uint32_t font_info,uint16_t code_unit,
                           uint32_t sheet_offsets,WiiMenuFontGlyph*,char*,uint32_t);


/* Original NW4R Glyph value produced by Font::GetGlyph. This preserves
 * font lookup/metrics in the guest and resolves only the supplied bitmap. */
typedef struct { GcWiiNativeGuiMaterial color; uint32_t intensity; } WiiMenuCharWriterStyle;
int wii_menu_charwriter_style(WiiMenuMemory,uint32_t writer,WiiMenuCharWriterStyle*,char*,uint32_t);
int wii_menu_charwriter_glyph(WiiMenuMemory,uint32_t glyph,WiiMenuFontGlyph*,char*,uint32_t);

typedef struct WiiMenuTextNative WiiMenuTextNative;
typedef struct {
    uint64_t entries,glyphs,draws,mesh_uploads,rejected;
    char error[192];
} WiiMenuTextStatus;
WiiMenuTextNative* wii_menu_text_create(WiiMenuMemory,const GcWiiNativeRenderApi*,const GcWiiNativeGuiApi*);
void wii_menu_text_destroy(WiiMenuTextNative*);
/* Validate a scene's text resources/layout before any native submission. */
int wii_menu_text_validate(WiiMenuMemory,uint32_t pane,uint32_t draw_info,
    const WiiMenuView*,char* error,uint32_t error_size);
/* 1: submitted; 0: unsupported input before any upload/submission;
 * -1: resource/backend failure, which must not invoke the original renderer. */
int wii_menu_text_draw(WiiMenuTextNative*,uint32_t pane,uint32_t draw_info,const WiiMenuView*);
/* Standalone CharWriter GPU service. Geometry input is the original Glyph
 * and explicit XYZ, with the owned model/view. No writer or glyph bytes are
 * changed; Print retains glyph lookup, controls and cursor advancement. */
int wii_menu_charwriter_validate(WiiMenuMemory,uint32_t writer,char*,uint32_t);
int wii_menu_charwriter_prepare(WiiMenuMemory,uint32_t writer,uint32_t glyph,
    const float position[3],const WiiMenuView*,const float model[12],
    const WiiMenuCharWriterStyle*,WiiMenuPicture*,char*,uint32_t);
int wii_menu_charwriter_draw(WiiMenuTextNative*,uint32_t writer,uint32_t glyph,
    const float position[3],const WiiMenuView*,const float model[12],const WiiMenuCharWriterStyle*);
void wii_menu_text_forget(WiiMenuTextNative*,uint32_t pane);
void wii_menu_text_status(const WiiMenuTextNative*,WiiMenuTextStatus*);

#ifdef __cplusplus
}
#endif
#endif
