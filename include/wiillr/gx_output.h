/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_GX_OUTPUT_H
#define WIILLR_GX_OUTPUT_H
#include <wiillr/gx.h>
/* Build-selected guarded MEM1 output. Unsupported hosts retain eager output.
 * All publication/materialization/release occurs on this machine thread. */
bool wii_gx_output_open(WiiGx*);
bool wii_gx_output_close(WiiGx*);
/* Isolate host-guarded output allocations from unrelated guest heap data.
 * Eager output retains the guest's original 32-byte alignment. */
u32 wii_gx_output_allocation_alignment(const WiiGx*);
int wii_gx_output_own(void*,const GcnllrNativeGxCopy*);
int wii_gx_output_pending(void*,u32,u32);
int wii_gx_output_prepare(void*,u32,u32);
/* Owner-thread, read-only check for normalized physical MEM1/MEM2 bytes.
 * True means this access cannot enter the resident fault/materialize path.
 * Unlike prepare(), this never observes/publishes RAM or calls the GPU. */
bool wii_gx_output_access_unobserved(const CPUState*,u32 physical,u32 size);
int wii_gx_output_present(WiiGx*,u32,u32,u32,u32);
#endif
