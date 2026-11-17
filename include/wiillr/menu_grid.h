/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef WIILLR_MENU_GRID_H
#define WIILLR_MENU_GRID_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef struct CPUState CPUState;
typedef struct WiiMenuPicture WiiMenuPicture;
typedef struct WiiFs WiiFs;
#ifdef __cplusplus
extern "C" {
#endif
/* Consumer-owned layout/library. Stock providers decline these enhancements. */
void wii_menu_grid_configure(bool enabled);
/* Capacity is latched at profile attachment, never changed beneath live guest
 * objects. 1 is stock; supported expanded multipliers are powers of two <=128.
 * The borrowed filesystem remains valid until attachment with NULL. */
bool wii_menu_grid_library_configure(unsigned multiplier);
void wii_menu_grid_library_attach(WiiFs*);
const char* wii_menu_grid_library_error(void);
bool wii_menu_grid_supported(void);
bool wii_menu_grid_enabled(void);
void wii_menu_grid_extent(unsigned width,unsigned height,bool fit);
void wii_menu_grid_reset(void);
bool wii_menu_grid_aspect(unsigned* width,unsigned* height);
int wii_menu_grid_entry(CPUState*,uint32_t);
void wii_menu_grid_transform(CPUState*,uint32_t pane);
bool wii_menu_grid_clip(CPUState*,uint32_t scene,float* half_x,float* half_y);
int wii_menu_grid_status(char*,size_t);
/* Reuse the original grid artwork, subdivided at its authored cell boundaries.
 * -1 skips a neighboring stock sheet, 0 keeps an ordinary picture. */
int wii_menu_grid_picture_tiles(CPUState*,uint32_t pane);
void wii_menu_grid_picture_tile(CPUState*,uint32_t pane,unsigned tile,WiiMenuPicture*);
#ifdef __cplusplus
}
#endif
#endif
