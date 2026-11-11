/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_MENU_PICTURE_H
#define WIILLR_MENU_PICTURE_H
#include <gcnllr/native_render.h>
#include <gcnllr/native_gui.h>
#include <gcnllr/native_gui_effects.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Version-verified NW4R class bindings belong to the title provider. Null
 * selects the existing retail Menu binding; resources retain the same ABI. */
typedef struct WiiMenuClasses {
    uint32_t picture,window,material,textbox,resource_font,archive_font;
} WiiMenuClasses;
typedef struct WiiMenuMemory {
    void* user;
    const uint8_t* (*read)(void*,uint32_t address,uint32_t size);
    const WiiMenuClasses* classes;
} WiiMenuMemory;
static inline const WiiMenuClasses* wii_menu_classes(WiiMenuMemory memory) {
    static const WiiMenuClasses original={0x816717f4,0x81671a20,0x81671b48,
                                         0x8167192c,0x8166fe88,0x8166ff70};
    return memory.classes?memory.classes:&original;
}
typedef struct WiiMenuView {
    float projection[16];
    float x,y,width,height,near_depth,far_depth;
    int32_t clip_x,clip_y,clip_width,clip_height;
    uint32_t depth_flags,depth_func;
} WiiMenuView;
typedef struct WiiMenuPicture {
    GcWiiNativeVertex vertices[4];
    GcWiiNativeDraw draw;
    uint32_t pane,material;
    uint8_t skipped;
    GcWiiNativeGuiMaterial color;
    uint8_t needs_gui;
    GcWiiNativeGuiLayersDraw layers;
    uint8_t needs_layers;
    GcWiiNativeGuiMaskDraw mask;
    uint8_t needs_mask;
    uint8_t effect; /* 1 cutoff, 2 additive pair, 3 alpha product */
    GcWiiNativeGuiCutoutDraw cutout;
    GcWiiNativeGuiAddDraw addition;
    float extra_uv[4][2],primary_uv[2][3];
    uint8_t paired_uv;
} WiiMenuPicture;
typedef struct WiiMenuWindow {
    WiiMenuPicture quads[9]; /* content, then the authored four/eight borders */
    uint32_t count;
} WiiMenuWindow;
int wii_menu_window_prepare(WiiMenuMemory,uint32_t window,uint32_t draw_info,
                            const WiiMenuView*,WiiMenuWindow*,char* error,uint32_t error_size);
/* Wii Menu 4.3U NW4R Picture::DrawSelf replacement inputs.
 * This reads live pane/material data; it does not execute guest routines or
 * construct/parse a display list. Failure publishes no partial output. */
int wii_menu_picture_prepare(WiiMenuMemory,uint32_t picture,uint32_t draw_info,
                             const WiiMenuView*,WiiMenuPicture*,char* error,uint32_t error_size);
/* Extract the current Menu SDK view after the original caller established it.
 * A fully native layout caller may instead supply its own typed view. */
int wii_menu_picture_sdk_view(WiiMenuMemory,WiiMenuView*,char* error,uint32_t error_size);
typedef struct WiiMenuNative WiiMenuNative;
typedef struct WiiMenuNativeStatus {
    uint64_t draws,skipped,mesh_uploads,rejected,live_meshes;
    char error[192];
} WiiMenuNativeStatus;
WiiMenuNative* wii_menu_native_create(WiiMenuMemory,const GcWiiNativeRenderApi*);
int wii_menu_native_gui(WiiMenuNative*,const GcWiiNativeGuiApi*);
int wii_menu_native_gui_layers(WiiMenuNative*,const GcWiiNativeGuiLayersApi*);
int wii_menu_native_gui_mask(WiiMenuNative*,const GcWiiNativeGuiMaskApi*);
int wii_menu_native_gui_effects(WiiMenuNative*,const GcWiiNativeGuiEffectsApi*);
void wii_menu_native_destroy(WiiMenuNative*);
int wii_menu_native_draw_picture(WiiMenuNative*,uint32_t picture,uint32_t draw_info,const WiiMenuView*);
int wii_menu_native_draw_window(WiiMenuNative*,uint32_t window,uint32_t draw_info,const WiiMenuView*);
/* Scene owners can validate a complete ordered plan before submitting it. */
int wii_menu_native_submit_prepared(WiiMenuNative*,uint64_t key,const WiiMenuPicture*);
void wii_menu_native_forget_picture(WiiMenuNative*,uint32_t picture);
void wii_menu_native_status(const WiiMenuNative*,WiiMenuNativeStatus*);
#ifdef __cplusplus
}
#endif
#endif
