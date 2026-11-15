/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_IOS_LLE_H
#define WIILLR_IOS_LLE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wiillr/ipc.h"
#include "wiillr/hollywood.h"
#ifdef __cplusplus
extern "C" {
#endif
#include "di/di.h"
typedef struct CPUState CPUState;
typedef struct WiiIosLle WiiIosLle;
typedef struct {
    uint64_t instructions,arm_cycles,swi_entries,irq_entries,undefined_entries;
    uint32_t pc;
    bool stopped;
} WiiIosLleStatus;
/* Original instructions, shared physical MEM1/MEM2 and actual IPC registers.
 * No native service replies, captured initialization or synthetic fuse values.
 * OTP may be absent; the first actual OTP data read then stops explicitly.
 * Create before PPC execution: successful creation establishes the shared
 * Hollywood original-kernel clock/compatibility profile, not a title handoff.
 * The consumer retains CPU/RAM/IPC/hardware ownership until destroy. */
WiiIosLle* wii_ios_lle_create(CPUState*,WiiIpc*,WiiHollywood*,const uint8_t* ios,size_t ios_size,
    const uint8_t* otp,size_t otp_size,uint64_t ppc_origin);
/* Optional original 256-byte SEEPROM, copied before the first instruction.
 * Programming affects only this machine's private image. Invalid input or a
 * late attachment is rejected without altering the running context. */
bool wii_ios_lle_load_seeprom(WiiIosLle*,const uint8_t*,size_t);
/* Optional original raw 512MiB NAND+spare image, copied before execution.
 * An optional BootMii footer is ignored here; OTP/SEEPROM attach separately.
 * The caller's source bytes are never programmed or erased. */
bool wii_ios_lle_load_nand(WiiIosLle*,const uint8_t*,size_t);
/* Borrow the consumer's initialized shared DI before execution. The caller
 * retains DI and CPU lifetime; this neither resets the drive nor takes over
 * its clock/PPC interrupt callback. ARM transfers remain explicit failures. */
bool wii_ios_lle_attach_di(WiiIosLle*,GcnDi*);
bool wii_ios_lle_tick(WiiIosLle*,uint64_t ppc_cycles);
WiiIosLleStatus wii_ios_lle_status(const WiiIosLle*);
const char* wii_ios_lle_error(const WiiIosLle*);
void wii_ios_lle_destroy(WiiIosLle*);
#ifdef __cplusplus
}
#endif
#endif
