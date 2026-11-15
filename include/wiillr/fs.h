/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_FS_H
#define WIILLR_FS_H
#include <stdint.h>
#include <stdbool.h>
#include "wiillr/menu_welcome.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct WiiFs WiiFs;
typedef struct WiiTitleLaunch {
    uint64_t title,ios;
    uint32_t uid,size;
    uint16_t gid;
    uint8_t* image; /* owned immutable boot content; release after handoff */
} WiiTitleLaunch;
/* Validate an installed ticket view, UID, TMD and exact boot-content hash
 * before any launch effects. False means an unsupported ticket contract. */
bool wii_es_prepare_launch(WiiFs*,uint64_t title,const uint8_t view[0xd8],WiiTitleLaunch*,int32_t* result);
void wii_es_release_launch(WiiTitleLaunch*);
/* The marked profile must be an isolated writable copy, never the input NAND. */
WiiFs* wii_fs_mount(const char* profile,uint32_t uid,uint16_t gid);
void wii_fs_unmount(WiiFs*);
bool wii_fs_boot_title(WiiFs*,uint64_t* title);
/* Hidden local welcome mod: persistent backend switch (missing means enabled).
 * Run delivery only on an exclusively owned, stopped copied Menu profile,
 * before the original CDB mount. RTC is local Wii seconds since2000; persisted
 * IPL.CB is applied here exactly once. Existing mail is never reformatted. */
bool wii_fs_menu_welcome_enabled(WiiFs*,bool* enabled);
bool wii_fs_menu_welcome_set_enabled(WiiFs*,bool enabled);
WiiMenuWelcomeResult wii_fs_menu_welcome(WiiFs*,uint32_t rtc_seconds);
uint32_t wii_fs_uid(WiiFs*);
int32_t wii_es_set_uid(WiiFs*,uint32_t caller_uid,uint64_t title);
/* Actual /title directories, including titles whose content is absent.
 * A non-null output receives up to capacity big-endian title IDs; count is
 * the total. This query does not establish content or signature validity. */
int32_t wii_es_titles(WiiFs*,uint8_t* output,uint32_t capacity,uint32_t* count);
int32_t wii_es_delete_title(WiiFs*,uint64_t title);
/* Native Bluetooth module EEPROM, stored beside the copied NAND marker. */
bool wii_fs_bt_read(void*,uint8_t*,uint32_t capacity,uint32_t* size);
bool wii_fs_bt_write(void*,const uint8_t*,uint32_t size);
/* Persistent, locally administered identity of the offline virtual adapter. */
bool wii_fs_network_mac(WiiFs*,uint8_t mac[6]);
#define WIILLR_NETWORK_CONFIG_SIZE 7004u
int32_t wii_fs_network_config(WiiFs*,uint8_t data[WIILLR_NETWORK_CONFIG_SIZE]);
int32_t wii_fs_network_config_write(WiiFs*,const uint8_t data[WIILLR_NETWORK_CONFIG_SIZE]);
/* Persistent virtual chip identity and real local WC24 generation. No online
 * registration or console certificate provisioning is performed. */
int32_t wii_fs_wc24_generate_id(WiiFs*,uint64_t* id,uint32_t* stage);
/* Metadata view only; does not authenticate or import a ticket. */
int32_t wii_es_ticket_view(WiiFs*,const uint8_t* ticket,uint32_t size,uint8_t* view);
int32_t wii_es_ticket_views(WiiFs*,uint64_t title,uint8_t* views,uint32_t capacity,uint32_t* count);
int32_t wii_es_tmd_view(WiiFs*,uint64_t title,uint8_t* view,uint32_t capacity,uint32_t* size);
int32_t wii_es_stored_contents(WiiFs*,uint64_t title,uint8_t* ids,uint32_t capacity,uint32_t* count);
/* False rejects restricted tickets until real usage accounting is available. */
bool wii_es_unlimited_ticket(WiiFs*,uint64_t ticket_id,int32_t* result);
int32_t wii_es_content_open(WiiFs*,uint32_t index);
int32_t wii_es_title_content_open(WiiFs*,uint64_t title,const uint8_t view[0xd8],uint32_t index);
int32_t wii_es_content_read(WiiFs*,uint32_t handle,uint8_t*,uint32_t size);
int32_t wii_es_content_seek(WiiFs*,uint32_t handle,uint32_t offset,uint32_t mode);
int32_t wii_es_content_close(WiiFs*,uint32_t handle);
/* Check one installed content against its TMD SHA-1, without changing it. */
bool wii_es_verify_content(WiiFs*,uint64_t title,uint32_t index);
int32_t wii_fs_open(WiiFs*,const char* path,uint32_t mode);
int32_t wii_fs_close(WiiFs*,uint32_t fd);
int32_t wii_fs_read(WiiFs*,uint32_t fd,uint8_t*,uint32_t size);
int32_t wii_fs_write(WiiFs*,uint32_t fd,const uint8_t*,uint32_t size);
int32_t wii_fs_seek(WiiFs*,uint32_t fd,uint32_t offset,uint32_t mode);
int32_t wii_fs_read_directory(WiiFs*,const uint8_t* path,uint8_t* names,uint32_t maximum,uint32_t* count);
int32_t wii_fs_directory_usage(WiiFs*,const uint8_t* path,uint32_t* clusters,uint32_t* inodes);
/* False means unimplemented, requiring discovery to stop without an ACK. */
bool wii_fs_ioctl(WiiFs*,int32_t fd,uint32_t command,const uint8_t* input,uint32_t input_size,
                  uint8_t* output,uint32_t output_size,int32_t* result);
#ifdef __cplusplus
}
#endif
#endif
