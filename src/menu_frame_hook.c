/* SPDX-License-Identifier: MIT */
#include <wiillr/menu_frame_hook.h>

/* The original build owns its view until the enclosing frame provider is
 * selected. Keeping this seam in one source also leaves standalone material
 * adapters independent of guest SDK cache representation. */
int wii_menu_frame_current_view(CPUState* cpu,WiiMenuView* view) {
    (void)cpu;(void)view;return 0;
}
