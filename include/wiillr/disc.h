/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef WIILLR_DISC_H
#define WIILLR_DISC_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct WiiDisc WiiDisc;
/* Original raw ISO, read-only. No title key leaves this object. Accessed data
 * sectors are H0 checked. Ticket signatures and H1-H3 remain unqualified. */
WiiDisc* wii_disc_open(const char* iso,const char* common_key);
void wii_disc_close(WiiDisc*);
bool wii_disc_read(WiiDisc*,uint64_t offset,void* output,size_t size);
uint32_t wii_disc_ios(const WiiDisc*);
uint64_t wii_disc_partition(const WiiDisc*);
uint64_t wii_disc_size(const WiiDisc*);
bool wii_disc_boot_header(WiiDisc*,void* output,size_t size);
#ifdef __cplusplus
}
#endif
#endif
