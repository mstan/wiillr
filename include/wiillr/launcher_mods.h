/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef WIILLR_LAUNCHER_MODS_H
#define WIILLR_LAUNCHER_MODS_H
#include "recomp_launcher.h"
#include <stdbool.h>
bool wii_launcher_mods_prepare(RecompLauncherCGameInfo*,const char* directory,const char* nand);
void wii_launcher_mods_start(void);
void wii_launcher_mods_in_session(void);
void wii_launcher_mods_dlc_installed(void);
#endif
