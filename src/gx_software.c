/* SPDX-License-Identifier: MIT */
#include "wiillr/gx.h"
#include "gx/gx.h"
#include "gx/gx_raster.h"
#include <string.h>

/* This facade shares only the host observation interface with the native
 * provider. Commands execute in the original CPU rasterizer; no GPU service
 * is loaded and copies are actual banked guest-RAM writes. */
static WiiMachine* machine;
static CPUState* cpu;
static u64 presents;
static int started;
static int start(const GcnllrNativeGxConfig* config) {
    if (!config || config->abi != GCNLLR_NATIVE_GX_ABI || !cpu || started) return 0;
    gcn_gx_init(cpu, &machine->cp, &machine->pe);
    presents = 0;
    started = 1;
    return 1;
}
static int fifo(const u8* bytes, u32 size) {
    return started && gcn_gx_submit(bytes, size);
}
static int drain(void) {
    if (!started) return 0;
    gcn_gx_pipeline_drain();
    return !cpu->halted;
}
static void status(GcnllrNativeGxStatus* out) {
    GcnGxCounters counters;
    gcn_gx_counters(&counters);
    memset(out, 0, sizeof *out);
    out->fifo_bytes = counters.fifo_bytes;
    out->draws = counters.draws;
    out->display_copies = counters.display_copies;
    out->texture_copies = counters.texture_copies;
    out->presents = presents;
    out->failed = !started || cpu->halted;
    out->pad.stick_x = out->pad.stick_y = 128;
    out->pad.substick_x = out->pad.substick_y = 128;
}
static int peek(u32 depth, u16 x, u16 y, u32* value) {
    return drain() && gx_raster_efb_cpu_read(
        0x08000000u | (depth ? 0x00400000u : 0) | ((u32)y << 12) | ((u32)x << 2), value);
}
static void stop(void) {
    if (started) gcn_gx_pipeline_shutdown();
    started = 0;
}
static int present(const u8* bytes, u32 width, u32 height, u32 stride) {
    if (!started || !bytes || !width || !height || stride < width * 2u) return 0;
    ++presents;
    return 1;
}
static const GcnllrNativeGxApi api = {
    .abi = GCNLLR_NATIVE_GX_ABI, .start = start, .fifo = fifo, .drain = drain,
    .status = status, .peek = peek, .stop = stop, .present = present,
    .capabilities = GCNLLR_NATIVE_GX_COMPLETED_SCANOUT
};
const GcnllrNativeGxApi* wii_gx_software_api(WiiMachine* owner, CPUState* state) {
    machine = owner;
    cpu = state;
    return &api;
}
