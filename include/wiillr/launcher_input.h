/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_LAUNCHER_INPUT_H
#define WIILLR_LAUNCHER_INPUT_H
#include "recomp_launcher.h"
#include "gcnllr/native_gx.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    const bool* keys;
    int key_count;
    uint32_t mouse,buttons;
    int16_t axes[6];
} WiiLauncherInput;
void wii_launcher_input_defaults(RecompLauncherCSettings*);
GcnllrPad wii_launcher_input_map(const RecompLauncherCSettings*,const WiiLauncherInput*);
void wii_launcher_input_poll(void*,GcnllrPad*);
void wii_launcher_input_stop(void*);
void wii_launcher_input_reset(void);
#endif
