/* SPDX-License-Identifier: MIT */
/* Offline, installed-content EC queries for the verified WR9E runtime.
 * Original archive opening, resource registration and gameplay remain callers.
 * Tickets/TMD/content are read unchanged; no online account is synthesized. */
#include "wiillr/mm9_dlc.h"
#include "wiillr/fs.h"
#include "memory/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static WiiGx* owner;
static bool checked,available,active,verified[3];
static unsigned records;
static u8 tmd[0x5c+6*16];
static u32 flags[6];
static const u32 entries[3]={0x8024c5f4,0x8024d424,0x8024c6f0};
static const u32 hashes[3]={0xdc45e2c9,0x5e0c1989,0xb0a948a3};
static const u64 title=0x0001000577523945ull;
static u32 be32(const u8* p){return (u32)p[0]<<24|(u32)p[1]<<16|(u32)p[2]<<8|p[3];}
static u16 be16(const u8* p){return (u16)p[0]<<8|p[1];}
static bool ram(CPUState* cpu,u32 address,u32 size) {
    const u32 p=address&0x1fffffffu;
    if(!((p<cpu->ram_size && size<=cpu->ram_size-p) ||
         (p>=0x10000000u && p-0x10000000u<cpu->mem2_size && size<=cpu->mem2_size-(p-0x10000000u))))return false;
    u32 capacity;return gcn_mem_resolve(cpu,address,&capacity) && capacity>=size;
}
void wii_mm9_dlc_bind(WiiGx* gx) {
    owner=gx;checked=available=active=false;records=0;memset(verified,0,sizeof verified);
}
void wii_mm9_dlc_invalidate(u32 address,u32 size) {
    const u32 start=address&0x1fffffffu;
    for(unsigned n=0;n<3;n++)if((u64)start+size>(entries[n]&0x1fffffffu) && start<(entries[n]&0x1fffffffu)+64)verified[n]=false;
}
static bool installed(void) {
    if(checked)return available;checked=true;
    if(!owner || !owner->machine->ios.fs)return false;
    WiiFs* fs=owner->machine->ios.fs;u32 count=0,size=0;
    if(wii_es_ticket_views(fs,title,NULL,0,&count) || !count ||
       wii_es_tmd_view(fs,title,tmd,sizeof tmd,&size) || size<0x5c)return false;
    records=be16(tmd+0x5a);
    if(records!=6 || size!=0x5c+records*16 || be16(tmd+0x58)!=0)return false;
    if(count>256)return false;
    u8* views=malloc(count*0xd8u);if(!views)return false;
    if(wii_es_ticket_views(fs,title,views,count,&count)){free(views);return false;}
    unsigned present=0,usable=0;
    for(unsigned n=0;n<records;n++) {
        const u8* record=tmd+0x5c+n*16;
        if(be16(record+4)!=n || be32(record)!=n){free(views);return false;}
        bool permitted=false;
        for(unsigned ticket=0;ticket<count;ticket++)
            if(views[ticket*0xd8u+0x56+(n>>3)]&(1u<<(n&7)))permitted=true;
        const bool stored=wii_es_verify_content(fs,title,n);
        flags[n]=(stored?1u:0u)|(permitted?2u:0u);
        if(stored)present++;if(stored && permitted)usable++;
    }
    free(views);available=usable!=0;
    fprintf(stderr,"{\"native_mm9_dlc\":1,\"verified_contents\":%u,\"records\":%u,\"signature_authentication\":false}\n",present,records);
    return available;
}
bool wii_mm9_dlc_dispatch(CPUState* cpu,u32 address) {
#if WIILLR_MM9_DLC
    unsigned n=0;while(n<3 && entries[n]!=address)n++;
    if(n==3 || !owner || owner->cpu!=cpu || owner->title!=0x0001000157523945ull || !installed())return false;
    if(!verified[n]) {
        if(!ram(cpu,address,64))return false;
        u32 hash=2166136261u;
        for(unsigned i=0;i<64;i++)hash=(hash^mem_read8(cpu,address+i))*16777619u;
        if(hash!=hashes[n]) {fprintf(stderr,"MM9 DLC unsupported code at %08x\n",address);cpu->halted=true;return true;}
        verified[n]=true;
    }
    s32 result=0;
    if(n==0) {result=active?-0xfc7:0;active=true;}
    else if(n==2)active=false;
    else {
        const u64 requested=(u64)cpu->gpr[3]<<32|cpu->gpr[4];
        const u32 output=cpu->gpr[5],count=cpu->gpr[6];
        if(!active)result=-0xfc9;
        else if(requested!=title || !ram(cpu,count,4))result=-0xfa4;
        else {
            const u32 capacity=mem_read32(cpu,count);
            mem_write32(cpu,count,records);
            if(output && capacity<records)result=-0xfb2;
            else if(output && !ram(cpu,output,records*16))result=-0xfa4;
            else {
                if(output)for(unsigned i=0;i<records;i++) {
                    const u8* record=tmd+0x5c+i*16;
                    mem_write32(cpu,output+i*16,flags[i]);
                    mem_write32(cpu,output+i*16+4,((u32)be16(record+4)<<16)|be16(record+6));
                    mem_write32(cpu,output+i*16+8,be32(record+8));
                    mem_write32(cpu,output+i*16+12,be32(record+12));
                }
                mem_write32(cpu,count,records);
            }
        }
    }
    cpu->gpr[3]=(u32)result;cpu->pc=cpu->lr;++cpu->cycles;return true;
#else
    (void)cpu;(void)address;return false;
#endif
}
