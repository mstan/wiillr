/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_PACKAGE_H
#define WIILLR_PACKAGE_H
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct WiiPackage WiiPackage;
typedef bool (*WiiPackageValidate)(const char* image,const char* nand,unsigned uid,unsigned gid);
/* Read package-local game.ini. Seed user/nand atomically only when absent;
 * existing native progress, imported licenses and settings are never replaced.
 * Returned argument storage remains valid until close. No UI is opened. */
/* Menu packages additionally hold a nonblocking, same-profile lifetime lease.
 * Other hosts and direct CLI callers retain their own ownership contract. */
WiiPackage* wii_package_open(const char* base,const char* user,WiiPackageValidate,char* error,size_t capacity);
/* Package identity is independent of the installed game's mutable settings. */
const char* wii_package_name(const char* base);
bool wii_package_menu(WiiPackage*);
void wii_package_close(WiiPackage*);
void wii_package_arguments(WiiPackage*,const char* executable,const char* action,int* argc,char*** argv);
const char* wii_package_log(WiiPackage*);
bool wii_package_standalone(WiiPackage*);
#ifdef __cplusplus
}
#endif
#endif
