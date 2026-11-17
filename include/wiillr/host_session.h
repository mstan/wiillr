/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef WIILLR_HOST_SESSION_H
#define WIILLR_HOST_SESSION_H
#include "wiillr/ios.h"
/* Optional policy for the one active standalone host session: pair its first
 * virtual controller through normal Bluetooth authentication when unpaired,
 * and return to the host launcher if the original Menu is absent. Binding
 * NULL disables both policies. Other IOS instances remain unaffected. */
#ifdef __cplusplus
extern "C" {
#endif
void wii_host_session_bind(WiiIosNative*);
bool wii_host_session_menu_requested(const WiiIosNative*);
#ifdef __cplusplus
}
#endif
#endif
