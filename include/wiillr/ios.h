/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_IOS_H
#define WIILLR_IOS_H
#include "cpu/cpu.h"
#include "wiillr/ipc.h"
#include "di/di.h"
#include "wiillr/fs.h"
#include "wiillr/bluetooth.h"
#include "wiillr/hollywood.h"
typedef struct {
    u8 descriptors[24],phase;
    u32 message,command;
    s32 result;
    u64 ready_at,operations;
    u32 event_message,event_output,event_fd;
    bool event_waiting,event_ready;
    u32 led_mode; /* slot illumination setting: off, dim, bright */
    u32* gpio_out; /* borrowed GPIO latch; off cancels slot illumination */
    u32 kd_rtc;
    u64 kd_rtc_cycles;
    bool kd_rtc_set;
    u64 kd_utc,kd_utc_cycles;
    bool kd_utc_set;
    u32 kd_suspend_count; /* local KD scheduler gate; no network workers installed */
    u32 kd_shutdown_event;
    bool kd_shutdown_requested; /* offline KD has no sockets or worker queue to drain */
    WiiTitleLaunch launch;
    bool launch_pending;
    bool (*validate_launch)(void*,const WiiTitleLaunch*);
    void* launch_user;
    GcnDi* di; /* borrowed device; consumer owns storage, time and disc paths */
    bool di_initialized;
    bool di_waiting;
    u8 di_wait_kind; /* 1: command completion, 2: actual cover closure */
    u32 di_message,di_fd;
    bool di_immediate;
    u32 di_output;
    WiiFs* fs; /* borrowed isolated NAND profile */
    u32 fs_handles[24];
    u32 es_uids[24]; /* identity captured when each ES descriptor opens */
    u32 keyboard_messages[24]; /* pending event reads; no keyboard attached */
    u32 sd_event_message,sd_event_fd; /* empty slot: pending insert notification */
    bool sd_event_cancelled;
    WiiBluetooth* bluetooth;
    WiiHollywood* hollywood;
} WiiIosNative;
/* Experimental native service boundary. Only implemented operations may be
 * acknowledged. False stops discovery with the original request intact. */
void wii_ios_native_init(WiiIosNative*);
void wii_ios_native_destroy(WiiIosNative*);
bool wii_ios_native_tick(WiiIosNative*,WiiIpc*,CPUState*);
/* Latch each PPC submission at the actual mailbox write, independently of
 * subsequent control-register writes and asynchronous completions. */
bool wii_ios_native_request(WiiIosNative*,u32 message,u64 cycles);
/* Broadway cycles until the next captured request, or UINT64_MAX if empty. */
u64 wii_ios_native_request_deadline(const WiiIosNative*,u64 cycles);
bool wii_ios_native_stm_event(WiiIosNative*,CPUState*,u32 event);
#endif
