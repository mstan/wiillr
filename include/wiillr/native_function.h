/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_NATIVE_FUNCTION_H
#define WIILLR_NATIVE_FUNCTION_H
#include "cpu/overlay_region.h"
#include "cpu/function_unit.h"

typedef bool (*WiiNativeFunctionBody)(CPUState*,const DrFunctionUnitOwner*);
/* The region is first so its existing nested invocation identifies this unit.
 * Private generated descriptors name original catalog members, never aliases.
 * False from body guarantees no architectural, cycle or bus effects; only then
 * may the adapter execute the attested original member directly. */
typedef struct {
    GcnOverlayRegion region;
    const DrFunctionUnitInfo* info;
    WiiNativeFunctionBody body;
    u64 handled_root,handled_continuation,compatibility;
    /* Charged before original-page compatibility runs. A true first fault or
     * immediate helper delegation is handled, but is not optimized progress. */
    u64 progressed_root,progressed_continuation;
    u64 root_guest_cycles,continuation_guest_cycles,zero_progress_delegations;
} WiiNativeFunctionUnit;

void wii_native_function_enable(WiiNativeFunctionUnit*,bool,
                                const u8* excluded_pages,u32 excluded_count);
bool wii_native_function_call(GcnOverlayModule*,CPUState*,
                              WiiNativeFunctionUnit*,u32 address);
void wii_native_function_invalidate(WiiNativeFunctionUnit*,u32 address,u32 size);
#endif
