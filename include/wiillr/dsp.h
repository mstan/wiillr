/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_DSP_H
#define WIILLR_DSP_H
#include "dsp/dsp.h"
#include "gcnllr/native_dsp.h"
typedef struct {
    GcnDsp registers;
    GcnDspEngine* engine;
    CPUState* cpu;
    u8* aram;
    u32 cycle_phase;
    u64 audio_frames,audio_nonzero_bytes;
} WiiDsp;
bool wii_dsp_start(WiiDsp*, CPUState*, const u8* irom, const u8* coef);
void wii_dsp_stop(WiiDsp*);
bool wii_dsp_read(WiiDsp*, u32 offset, u8 size, u64* value);
bool wii_dsp_write(WiiDsp*, u32 offset, u8 size, u64 value);
bool wii_dsp_tick(WiiDsp*, u32 cpu_cycles,u32 audio_cycles_per_block);
#endif
