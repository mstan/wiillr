/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_MENU_AUDIO_EFFECT_H
#define WIILLR_MENU_AUDIO_EFFECT_H
#include <stdint.h>
typedef struct CPUState CPUState;
#ifdef __cplusplus
extern "C" {
#endif
/* Process complete sample frames of the verified three-channel callback.
 * Zero preserves all guest state. Original native code handles unsupported
 * operands and a sample which cannot fit the current device deadline. */
uint32_t wii_menu_audio_effect_batch(CPUState*);
#ifdef __cplusplus
}
#endif
#endif
