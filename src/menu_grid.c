/* SPDX-License-Identifier: MIT */
/* Stock provider. A game consumer can supply WIILLR_MENU_GRID_SOURCE without
 * placing its experimental presentation or product policy in the engine. */
#include <wiillr/menu_grid.h>
#include <stdio.h>
bool wii_menu_grid_supported(void){return false;}
void wii_menu_grid_configure(bool enabled){(void)enabled;}
bool wii_menu_grid_enabled(void){return false;}
void wii_menu_grid_extent(unsigned width,unsigned height,bool fit){(void)width;(void)height;(void)fit;}
void wii_menu_grid_reset(void){}
bool wii_menu_grid_aspect(unsigned* width,unsigned* height){(void)width;(void)height;return false;}
int wii_menu_grid_entry(CPUState* cpu,uint32_t pc){(void)cpu;(void)pc;return 0;}
void wii_menu_grid_transform(CPUState* cpu,uint32_t pane){(void)cpu;(void)pane;}
bool wii_menu_grid_clip(CPUState* cpu,uint32_t scene,float* x,float* y){(void)cpu;(void)scene;(void)x;(void)y;return false;}
int wii_menu_grid_picture_tiles(CPUState* cpu,uint32_t pane){(void)cpu;(void)pane;return 0;}
void wii_menu_grid_picture_tile(CPUState* cpu,uint32_t pane,unsigned tile,WiiMenuPicture* out){(void)cpu;(void)pane;(void)tile;(void)out;}
int wii_menu_grid_status(char* out,size_t size){return snprintf(out,size,"{\"ok\":true,\"enabled\":false,\"ready\":false,\"columns\":4,\"rows\":3}");}
