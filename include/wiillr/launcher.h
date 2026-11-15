/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_LAUNCHER_H
#define WIILLR_LAUNCHER_H
#include <stdbool.h>
#include <stddef.h>
#include "wiillr/gx.h"
/* Optional native-build launcher. No UI or file picker in check-only mode. */
int wii_launcher_start(const char* image,const char* nand,unsigned uid,unsigned gid,
    bool force,bool check_only,char* selected,size_t capacity);
bool wii_launcher_verify(const void* image,size_t size);
/* Installed System Menu package: actual startup launcher (Launch/Quit/Skip),
 * separate from the direct CLI session attachment below. Check mode has no UI. */
int wii_launcher_menu_start(const char* image,const char* nand,unsigned uid,unsigned gid,
    bool force,bool check_only,char* selected,size_t capacity,unsigned ios_profile);
bool wii_launcher_menu_verify(const void* image,size_t size);
/* Attach shared controls/settings to an explicitly windowed Menu boot.
 * The caller has already loaded the original boot image and copied NAND. */
bool wii_launcher_menu_session(const char* image);
bool wii_launcher_window(WiiGx*);
#endif
