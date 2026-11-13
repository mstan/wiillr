/* SPDX-License-Identifier: MIT */
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
#ifdef __cplusplus
}
#endif
#endif
