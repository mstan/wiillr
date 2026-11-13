/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_HOST_MENU_H
#define WIILLR_HOST_MENU_H
#include "wiillr/ios.h"
/* Optional policy for the one active standalone host session. Binding NULL
 * disables it. Guest IOS state/tickets remain unchanged, and other instances
 * are unaffected. The host consumes the request before further guest work. */
void wii_host_menu_bind(WiiIosNative*);
bool wii_host_menu_requested(const WiiIosNative*);
#endif
