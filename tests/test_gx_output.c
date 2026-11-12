/* SPDX-License-Identifier: MIT */
#include <wiillr/gx_output.h>
#include <cpu/native_code.h>
#include <memory/write_epoch.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#if defined(_WIN32) && defined(_WIN64)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static struct {GcnllrNativeGxCopy copy;u8 value;} copies[16];
static WiiGx gx;
static volatile u32 materials[16],releases[16],dirty;
void gcn_native_code_content_dirty(u32 address,u32 size){(void)address;(void)size;++dirty;}
static int materialize(u64 token) {
    assert(token<16);++materials[token];const GcnllrNativeGxCopy* c=&copies[token].copy;
    u8* base=c->address>=0x10000000u?gx.cpu->mem2:gx.cpu->ram;
    const u32 offset=c->address>=0x10000000u?c->address-0x10000000u:c->address;
    for(u32 row=0;row<c->rows;row++)memset(base+offset+row*c->stride,copies[token].value,c->row_bytes);
    errno=EIO;SetLastError(ERROR_BUSY);
    return 1;
}
static void release(u64 token){assert(token<16);++releases[token];}
static int present(u64 token,u32 w,u32 h,u32 stride){(void)w;(void)h;(void)stride;return token==8;}
static void publish(u64 token,u32 address,u32 bytes,u32 rows,u32 stride,u8 value) {
    copies[token].copy=(GcnllrNativeGxCopy){token,address,bytes,rows,stride,GCNLLR_NATIVE_GX_COPY_DISPLAY};
    copies[token].value=value;assert(wii_gx_output_own(&gx,&copies[token].copy));
}
int main(void) {
    CPUState cpu={0};cpu.ram_size=0x10000;cpu.mem2_size=0x1000;
    cpu.ram=gcn_mem1_alloc(cpu.ram_size);assert(cpu.ram);
    cpu.mem2=calloc(1,cpu.mem2_size);assert(cpu.mem2);memset(cpu.ram,0xe5,cpu.ram_size);
    const GcnllrNativeGxApi api={.abi=GCNLLR_NATIVE_GX_ABI,.materialize_copy=materialize,.release_copy=release,.present_copy=present};
    gx=(WiiGx){.cpu=&cpu,.api=&api};assert(wii_gx_output_open(&gx));
    cpu.reserve_valid=true;cpu.reserve_addr=0x80001000;
    publish(1,0x1000,32,2,64,0x41);assert(!cpu.reserve_valid);
    assert(wii_gx_output_pending(&gx,0xc0001000,1));
    volatile u8* raw=cpu.ram;
    errno=ERANGE;SetLastError(ERROR_INVALID_PARAMETER);
    assert(raw[0x100f]==0x41);assert(materials[1]==1 && releases[1]==1);
    assert(errno==ERANGE && GetLastError()==ERROR_INVALID_PARAMETER);
    assert(raw[0x1021]==0xe5 && raw[0x1041]==0x41); // stride padding retained
    publish(2,0x1000,32,2,64,0x42);raw[0x1001]=0x88;
    assert(materials[2]==1 && raw[0x1001]==0x88 && raw[0x1002]==0x42 && raw[0x1041]==0x42);
    publish(3,0x2000,64,1,64,0x43);publish(4,0x2000,64,1,64,0x44);
    assert(!materials[3] && releases[3]==1);assert(raw[0x2003]==0x44 && materials[4]==1);
    publish(5,0x3000,64,1,64,0x45);publish(6,0x3020,64,1,64,0x46);
    assert(materials[5]==1);assert(raw[0x3001]==0x45 && raw[0x3021]==0x46 && materials[6]==1);
    publish(7,0x10000000,32,1,32,0x47);assert(cpu.mem2[3]==0x47 && materials[7]==1 && releases[7]==1);
    publish(8,0x4000,32,2,32,0x48);
    assert(wii_gx_output_present(&gx,0x4000,16,2,32)==1 && !materials[8]);
    /* Foreign graphics APIs may read padding while holding a device lock.
     * They must be told about the entire protected page before entry, so
     * their memcpy cannot reenter the GPU through our fault handler. */
    publish(9,0x5020,32,1,32,0x49);
    assert(wii_gx_output_pending(&gx,0x80005fff,1));
    assert(wii_gx_output_prepare(&gx,0x80005fff,1));
    MEMORY_BASIC_INFORMATION info;
    assert(VirtualQuery(cpu.ram+0x5000,&info,sizeof info) && info.Protect==PAGE_READWRITE);
    assert(materials[9]==1 && releases[9]==1 && raw[0x5020]==0x49 && raw[0x5fff]==0xe5);
    publish(10,0x6020,32,1,32,0x4a);
    assert(wii_gx_output_pending(&gx,0x80005fff,2));
    assert(wii_gx_output_prepare(&gx,0x80005fff,2));
    assert(VirtualQuery(cpu.ram+0x6000,&info,sizeof info) && info.Protect==PAGE_READWRITE);
    assert(materials[10]==1 && releases[10]==1 && raw[0x6020]==0x4a && raw[0x6000]==0xe5);
    assert(wii_gx_output_close(&gx));assert(raw[0x4003]==0x48 && materials[8]==1 && releases[8]==1);
    assert(gx.copies==10 && dirty>0 && !gx.output);
    gcn_mem1_release(cpu.ram);free(cpu.mem2);return 0;
}
#else
int main(void){WiiGx gx={0};assert(!wii_gx_output_open(&gx));assert(wii_gx_output_close(&gx));return 0;}
#endif
