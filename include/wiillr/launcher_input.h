/* SPDX-License-Identifier: GPL-3.0-or-later */
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
#define WIILLR_REMOTE_PLAYERS 4u
#define WIILLR_INPUT_PRESENT 1u
#define WIILLR_INPUT_FOCUSED 2u
void wii_launcher_input_defaults(RecompLauncherCSettings*);
GcnllrPad wii_launcher_input_map(const RecompLauncherCSettings*,const WiiLauncherInput*);
GcnllrPad wii_launcher_input_map_player(const RecompLauncherCSettings*,const WiiLauncherInput*,unsigned player);
unsigned wii_launcher_input_actions(const RecompLauncherCSettings*,const WiiLauncherInput*);
unsigned wii_launcher_input_held_actions(void);
void wii_launcher_input_poll(void*,GcnllrPad*);
unsigned wii_launcher_input_poll_player(void*,unsigned player,GcnllrPad*);
unsigned wii_launcher_input_player_flags(unsigned player);
void wii_launcher_input_stop(void*);
void wii_launcher_input_reset(void);
#endif
