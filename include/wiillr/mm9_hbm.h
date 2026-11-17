/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef WIILLR_MM9_HBM_H
#define WIILLR_MM9_HBM_H
#include "menu_text.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct WiiMm9Hbm WiiMm9Hbm;
typedef struct WiiMm9HbmStatus {
    uint64_t scenes,pictures,windows,texts,rejected;
    char error[192];
} WiiMm9HbmStatus;
WiiMm9Hbm* wii_mm9_hbm_create(WiiMenuMemory,const GcWiiNativeRenderApi*,
    const GcWiiNativeGuiApi*,const GcWiiNativeGuiLayersApi*,
    const GcWiiNativeGuiMaskApi*,const GcWiiNativeGuiEffectsApi*);
void wii_mm9_hbm_destroy(WiiMm9Hbm*);
/* Original HBM Calc/input remains guest code. Replace its entire draw scene,
 * including ordered layouts, Window borders, text and the final fade. */
int wii_mm9_hbm_draw(WiiMm9Hbm*,uint32_t singleton,const WiiMenuView*);
void wii_mm9_hbm_status(const WiiMm9Hbm*,WiiMm9HbmStatus*);
#ifdef __cplusplus
}
#endif
#endif
