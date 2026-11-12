/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_LAUNCHER_H
#define WIILLR_LAUNCHER_H
#include <stdbool.h>
#include <stddef.h>
/* Optional native-build launcher. No UI or file picker in check-only mode. */
int wii_launcher_start(const char* image,const char* nand,unsigned uid,unsigned gid,
    bool force,bool check_only,char* selected,size_t capacity);
bool wii_launcher_verify(const void* image,size_t size);
bool wii_launcher_window(void);
#endif
