/* SPDX-License-Identifier: MIT */
#include "wiillr/ios.h"
#include "memory/memory.h"
#include <stdio.h>
#include <string.h>

void wii_ios_native_init(WiiIosNative* ios) {memset(ios,0,sizeof *ios);}
static int execute(WiiIosNative* ios,CPUState* cpu) {
    u32 available;
    const u8* request=gcn_mem_resolve(cpu,ios->message,&available);
    if(!request || available<32 || (ios->message&3))return false;
    ios->command=read_be32(request);
    const u32 fd=read_be32(request+8);
    if(ios->command==1) {
        const u8* path=gcn_mem_resolve(cpu,read_be32(request+12),&available);
        if(!path || !memchr(path,0,available<64 ? available:64))return false;
        u8 device=0;
        if(!strcmp((const char*)path,"/dev/stm/immediate"))device=1;
        if(!strcmp((const char*)path,"/dev/stm/eventhook"))device=2;
        if(!device) {
            fprintf(stderr,"native IOS unsupported open path=%s\n",(const char*)path);
            return false;
        }
        if(read_be32(request+16)>3) {ios->result=-4;return true;}
        ios->result=-5;
        for(u32 n=0;n<24;n++)if(!ios->descriptors[n]) {
            ios->descriptors[n]=device;ios->result=(s32)n;break;
        }
        return true;
    }
    if(ios->command==2) {
        if(ios->event_waiting && fd==ios->event_fd)return false;
        ios->result=-4;
        if(fd<24 && ios->descriptors[fd]) {ios->descriptors[fd]=0;ios->result=0;}
        return true;
    }
    if(ios->command==6 && fd<24 && ios->descriptors[fd]==2 && read_be32(request+12)==0x1000) {
        if(ios->event_waiting) {ios->result=-2;return 1;}
        const u32 output=read_be32(request+24),size=read_be32(request+28);
        if(size<4 || !gcn_mem_resolve(cpu,output,&available) || available<4) {ios->result=-4;return 1;}
        ios->event_message=ios->message;ios->event_output=output;ios->event_fd=fd;
        ios->event_waiting=true;
        return 2; /* accepted asynchronous hook, no reply until a real event */
    }
    fprintf(stderr,"native IOS unsupported command=%u fd=%u arg=%08x\n",
            ios->command,fd,read_be32(request+12));
    return false;
}
static void reply(WiiIosNative* ios,WiiIpc* ipc,CPUState* cpu,u32 message,u32 command,s32 result) {
    mem_write32_cia(cpu,message+4,(u32)result,cpu->pc);
    mem_write32_cia(cpu,message+8,command,cpu->pc);
    mem_write32_cia(cpu,message,8,cpu->pc);
    wii_ipc_write(ipc,8,message,cpu->cycles);wii_ipc_write(ipc,12,1,cpu->cycles);
    ++ios->operations;
}
bool wii_ios_native_stm_event(WiiIosNative* ios,CPUState* cpu,u32 event) {
    if(!ios->event_waiting || ios->event_ready || !event || (event&~0x20800u))return false;
    mem_write32_cia(cpu,ios->event_output,event,cpu->pc);
    ios->event_ready=true;
    return true;
}
bool wii_ios_native_tick(WiiIosNative* ios,WiiIpc* ipc,CPUState* cpu) {
    if(ios->event_ready && !ipc->y) {
        reply(ios,ipc,cpu,ios->event_message,6,0);
        ios->event_ready=ios->event_waiting=false;
        return true;
    }
    if(!ios->phase) {
        if(!(ipc->x&1))return true;
        ios->message=ipc->ppc_message;ios->ready_at=cpu->cycles+6000;ios->phase=1;
    }
    if(cpu->cycles<ios->ready_at)return true;
    if(ios->phase==1) {
        /* PPC request/return layout follows IOS IPC. Hardware ACK latency is
         * modeled at500 TB ticks. Service latency remains development-only.
         * No ARM instructions execute in this explicitly native provider. */
        if(ipc->y)return true;
        const int status=execute(ios,cpu);
        if(!status)return false;
        wii_ipc_write(ipc,8,ios->message,cpu->cycles);
        wii_ipc_write(ipc,12,12,cpu->cycles); /* clear X1, raise Y2 ACK */
        ios->ready_at=cpu->cycles+6000;ios->phase=status==2 ? 0:2;
        return true;
    }
    if(ipc->y)return true; /* receiver must consume the previous mailbox */
    reply(ios,ipc,cpu,ios->message,ios->command,ios->result);ios->phase=0;
    return true;
}
