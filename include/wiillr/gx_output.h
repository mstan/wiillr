/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef WIILLR_GX_OUTPUT_H
#define WIILLR_GX_OUTPUT_H
#include <wiillr/gx.h>
#include <gcnllr/native_frame.h>
#include <gcnllr/native_output.h>
/* Build-selected guarded MEM1 output. Unsupported hosts retain eager output.
 * All publication/materialization/release occurs on this machine thread. */
bool wii_gx_output_open(WiiGx*);
bool wii_gx_output_close(WiiGx*);
/* Bind only to this live guarded owner. NULL leaves the physical floor.
 * The borrowed extension outlives the owner and is retired by close(). */
bool wii_gx_output_bind_completion(WiiGx*,const GcWiiNativeOutputApi*);
/* Zero-event ordering barrier: retain immutable guarded output when bound,
 * otherwise use NativeFrame's physical completion. A failed owner/submit
 * returns failure; it never falls back after attempting submission. */
int wii_gx_output_complete(WiiGx*,const GcWiiNativeFrameApi*);
/* Isolate host-guarded output allocations from unrelated guest heap data.
 * Eager output retains the guest's original 32-byte alignment. */
u32 wii_gx_output_allocation_alignment(const WiiGx*);
int wii_gx_output_own(void*,const GcnllrNativeGxCopy*);
int wii_gx_output_pending(void*,u32,u32);
int wii_gx_output_prepare(void*,u32,u32);
int wii_gx_output_present(WiiGx*,u32,u32,u32,u32);
#endif
