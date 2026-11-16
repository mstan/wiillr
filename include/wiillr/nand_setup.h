/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_NAND_SETUP_H
#define WIILLR_NAND_SETUP_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum WiiNandSetupStatus {
    WII_NAND_SETUP_READY=0,
    WII_NAND_SETUP_NEEDS_KEYS=1,
    WII_NAND_SETUP_MISSING=2,
    WII_NAND_SETUP_UNSUPPORTED=3,
    WII_NAND_SETUP_INVALID=4,
    WII_NAND_SETUP_DEST_EXISTS=5,
    WII_NAND_SETUP_IO_ERROR=6
} WiiNandSetupStatus;
typedef enum WiiNandSourceKind {
    WII_NAND_SOURCE_UNKNOWN=0,
    WII_NAND_SOURCE_EXTRACTED=1,
    WII_NAND_SOURCE_BOOTMII=2
} WiiNandSourceKind;
typedef struct WiiNandSetupInfo {
    WiiNandSetupStatus status;
    WiiNandSourceKind source_kind;
    bool ready;
    uint32_t menu_version,ios_profile,uid;
    uint16_t gid;
    uint64_t files,bytes;
    char menu_label[64],region_label[32];
    char boot_relative[192];
    char details[1024];
} WiiNandSetupInfo;
/* UTF-8 paths. Inspect never changes source, destination, settings or keys.
 * keys may be NULL: BootMii's genuine appended footer is used when present.
 * Explicit backup formats are BootMii keys.bin (1024 bytes) or OTP (128).
 * Readiness is for qualified Wii Menu 4.3U with the included native IOS80
 * provider. Original IOS80 files are optional, and integrity-checked if any
 * are supplied. This neither requires nor qualifies original-IOS startup and
 * does not claim Nintendo RSA-signature authentication. Startup Menu v1 and
 * other unqualified Menu identities are reported as UNSUPPORTED. */
WiiNandSetupStatus wii_nand_setup_inspect(const char* source,const char* keys,
    WiiNandSetupInfo*,char* error,size_t capacity);
/* Publish a complete managed writable copy to a NEW destination atomically.
 * Existing destinations are never merged/replaced; all source saves, tickets
 * and settings are preserved byte-for-byte. No dialogs or guest execution. */
WiiNandSetupStatus wii_nand_setup_import(const char* source,const char* keys,
    const char* destination,WiiNandSetupInfo*,char* error,size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
