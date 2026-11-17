/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef WIILLR_WAD_H
#define WIILLR_WAD_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct WiiWadInstallResult {
    uint64_t title;
    uint32_t uid;
    uint32_t contents;
    uint8_t ticket_common_key_index;
} WiiWadInstallResult;
/* Install a user-provided DLC WAD into a marked writable NAND copy. Paths are
 * UTF-8. expected_title must identify the caller's DLC title (00010005...).
 * The explicitly supplied 16-byte standard common key is accepted only when
 * every decrypted content matches its TMD SHA-1. Original ticket bytes,
 * including their key-selector field, are preserved. The current ES contract
 * supports a single RSA2048 version-0 ticket. No signature claim is
 * made. Conflicting installed content is rejected; saves are never replaced.
 * Imports are serialized per profile; publication is atomic per file and
 * resumable after an I/O failure. No UI or subprocess is opened. */
bool wii_wad_install(const char* wad_path,const char* common_key_path,
    const char* copied_nand,uint64_t expected_title,WiiWadInstallResult* result,
    char* error,size_t error_capacity);
typedef struct WiiWadBaseInstallResult {
    WiiWadInstallResult installed;
    uint64_t ios_title;
    uint32_t boot_content_id;
    uint16_t group_id,title_version,boot_index;
    /* Relative to copied_nand; ASCII and never an absolute or parent path. */
    char boot_path[64];
} WiiWadBaseInstallResult;
/* Same safe import/publication contract, for supported USA MM9 base title
 * 0001000157523945 only. Requires IOS37 and the supported boot SHA-1 in both
 * the TMD and decrypted boot content before any profile mutation. UID is
 * reused when present, otherwise allocated after existing users (4096 for
 * an empty uid.sys). Ticket/TMD, imported DLC and native saves are preserved.
 * Result is cleared on failure. No signing/authenticity claim is made. */
bool wii_wad_install_base(const char* wad_path,const char* common_key_path,
    const char* copied_nand,uint64_t expected_title,WiiWadBaseInstallResult* result,
    char* error,size_t error_capacity);
#ifdef __cplusplus
}
#endif
#endif
