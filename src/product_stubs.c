/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "wiillr/mm9_mods.h"
#include "wiillr/mm9_dlc.h"
#include <stdio.h>
void wii_mm9_dlc_bind(WiiGx* g){(void)g;}
void wii_mm9_dlc_invalidate(u32 a,u32 s){(void)a;(void)s;}
bool wii_mm9_dlc_dispatch(CPUState* c,u32 p){(void)c;(void)p;return false;}
void wii_mm9_mods_configure(bool a,bool b){(void)a;(void)b;}
void wii_mm9_mods_bind(WiiGx* g){(void)g;}
void wii_mm9_mods_invalidate(u32 a,u32 s){(void)a;(void)s;}
bool wii_mm9_mods_dispatch(CPUState* c,u32 p){(void)c;(void)p;return false;}
bool wii_mm9_mods_stage_frozen(CPUState* c){(void)c;return false;}
void wii_mm9_mods_host_actions(unsigned a){(void)a;}
void wii_mm9_mods_test_actions(unsigned a){(void)a;}
int wii_mm9_mods_status(char* p,size_t n){return snprintf(p,n,"{\"ok\":false,\"error\":\"No gameplay provider selected\"}");}
