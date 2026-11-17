/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef WIILLR_LAUNCHER_PERIPHERALS_H
#define WIILLR_LAUNCHER_PERIPHERALS_H
#include "wiillr/ios.h"
#include "recomp_launcher.h"
bool wii_launcher_peripherals_prepare(RecompLauncherCGameInfo*, const char* directory);
bool wii_launcher_peripherals_bind(WiiIosNative*);
void wii_launcher_peripherals_unbind(void);
#endif
