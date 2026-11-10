/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_FS_H
#define WIILLR_FS_H
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct WiiFs WiiFs;
/* The marked profile must be an isolated writable copy, never the input NAND. */
WiiFs* wii_fs_mount(const char* profile,uint32_t uid,uint16_t gid);
void wii_fs_unmount(WiiFs*);
bool wii_fs_boot_title(WiiFs*,uint64_t* title);
/* Metadata view only; does not authenticate or import a ticket. */
int32_t wii_es_ticket_view(WiiFs*,const uint8_t* ticket,uint32_t size,uint8_t* view);
/* False rejects restricted tickets until real usage accounting is available. */
bool wii_es_unlimited_ticket(WiiFs*,uint64_t ticket_id,int32_t* result);
int32_t wii_es_content_open(WiiFs*,uint32_t index);
int32_t wii_es_content_read(WiiFs*,uint32_t handle,uint8_t*,uint32_t size);
int32_t wii_es_content_seek(WiiFs*,uint32_t handle,uint32_t offset,uint32_t mode);
int32_t wii_es_content_close(WiiFs*,uint32_t handle);
int32_t wii_fs_open(WiiFs*,const char* path,uint32_t mode);
int32_t wii_fs_close(WiiFs*,uint32_t fd);
int32_t wii_fs_read(WiiFs*,uint32_t fd,uint8_t*,uint32_t size);
int32_t wii_fs_write(WiiFs*,uint32_t fd,const uint8_t*,uint32_t size);
int32_t wii_fs_seek(WiiFs*,uint32_t fd,uint32_t offset,uint32_t mode);
int32_t wii_fs_read_directory(WiiFs*,const uint8_t* path,uint8_t* names,uint32_t maximum,uint32_t* count);
/* False means unimplemented, requiring discovery to stop without an ACK. */
bool wii_fs_ioctl(WiiFs*,int32_t fd,uint32_t command,const uint8_t* input,uint32_t input_size,
                  uint8_t* output,uint32_t output_size,int32_t* result);
#ifdef __cplusplus
}
#endif
#endif
