/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_MENU_WELCOME_H
#define WIILLR_MENU_WELCOME_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum WiiMenuWelcomeResult {
    WII_MENU_WELCOME_DISABLED=0,
    WII_MENU_WELCOME_PRESENT=1,
    WII_MENU_WELCOME_CREATED=2,
    WII_MENU_WELCOME_NOT_READY=-1,
    WII_MENU_WELCOME_INVALID=-2,
    WII_MENU_WELCOME_UNSUPPORTED=-3,
    WII_MENU_WELCOME_FULL=-4,
    WII_MENU_WELCOME_IO_ERROR=-5
} WiiMenuWelcomeResult;
/* Hidden, default-enabled local mod. The caller supplies the Wii's CURRENT
 * local RTC seconds since 2000-01-01, not Unix time, and the active CDB Wii ID.
 * Run before the guest mounts cdb.vff. Existing stores are never reformatted.
 * A missing store is NOT_READY: retry at the next safe, unmounted boundary.
 * The receipt, CDB counter and real memo are committed in one VFF replacement.
 * No normal mod catalog entry or network operation is involved. */
WiiMenuWelcomeResult wii_menu_welcome(const char* copied_profile,
    uint64_t emulated_wii_seconds,uint64_t cdb_wii_id,bool enabled);
/* Bounded in-memory transaction used by the file provider and component test.
 * On any non-CREATED result, every input byte remains unchanged. */
WiiMenuWelcomeResult wii_menu_welcome_image(uint8_t* vff,size_t size,
    uint64_t emulated_wii_seconds,uint64_t cdb_wii_id,bool enabled);
#ifdef __cplusplus
}
#endif
#endif
