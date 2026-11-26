/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "wiillr/bluetooth.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Configuration and producer calls belong to the simulation/UI owner thread. */
bool wii_diagnostics_configure(const char* directory);
bool wii_diagnostics_enabled(void);
bool wii_diagnostics_set_enabled(bool enabled);
const char* wii_diagnostics_error(void);
void wii_diagnostics_begin(const char* catalog_identity);
void wii_diagnostics_input(u64 cycles,u32 remote,const WiiBluetoothInput* input);
void wii_diagnostics_end(void);
#ifdef __cplusplus
}
#endif
