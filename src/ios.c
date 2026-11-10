/* SPDX-License-Identifier: MIT */
#include "wiillr/ios.h"
#include "memory/memory.h"
#include <stdio.h>
#include <string.h>

static u8* ios_memory(CPUState* cpu,u32 address,u32* available) {
    const u32 physical=gcn_reservation_key(address);
    if(!(physical<cpu->ram_size || (physical>=0x10000000u && physical-0x10000000u<cpu->mem2_size))) {
        if(available)*available=0;return NULL;
    }
    return gcn_mem_resolve(cpu,address,available);
}

void wii_ios_native_init(WiiIosNative* ios) {memset(ios,0,sizeof *ios);}
static void di_registers(WiiIosNative* ios,CPUState* cpu) {
    gcn_di_write(ios->di,cpu,GCN_DI_BASE+GCN_DI_SR,
        GCN_DI_SR_TCINT|GCN_DI_SR_DEINT|GCN_DI_SR_TCINTMASK|GCN_DI_SR_DEINTMASK,4);
    gcn_di_write(ios->di,cpu,GCN_DI_BASE+GCN_DI_CVR,0,4);
}
static int di_ioctl(WiiIosNative* ios,CPUState* cpu,const u8* request) {
    const u32 command=read_be32(request+12),input=read_be32(request+16),input_size=read_be32(request+20);
    const u32 output=read_be32(request+24),output_size=read_be32(request+28);
    u32 available;
    const u8* packet=ios_memory(cpu,input,&available);
    if(input_size!=32 || !packet || available<32) {ios->result=32;return 1;}
    u32 value;
    switch(command) {
    case 0x12:case 0x70:case 0xe0: {
        if(ios->di_waiting || ios->di->cmd_pending)return 0;
        const u32 need=command==0xe0?4:32;
        if((command==0xe0 ? output_size<4 || (output&3):output_size!=32 || (output&31)) ||
           !ios_memory(cpu,output,&available) || available<need ||
           read_be32(packet)>>24!=command) {ios->result=32;return 1;}
        di_registers(ios,cpu);
        for(unsigned n=0;n<3;n++)gcn_di_write(ios->di,cpu,GCN_DI_BASE+GCN_DI_CMDBUF0+4*n,read_be32(packet+4*n),4);
        if(command==0x70)gcn_di_write(ios->di,cpu,GCN_DI_BASE+GCN_DI_CMDBUF0,0xa8000040u,4);
        gcn_di_write(ios->di,cpu,GCN_DI_BASE+GCN_DI_MAR,output&0x1fffffffu,4);
        gcn_di_write(ios->di,cpu,GCN_DI_BASE+GCN_DI_LENGTH,32,4);
        gcn_di_write(ios->di,cpu,GCN_DI_BASE+GCN_DI_CR,GCN_DI_CR_TSTART|(command==0xe0?0:GCN_DI_CR_DMA),4);
        ios->di_immediate=command==0xe0;ios->di_output=output;
        ios->di_message=ios->message;ios->di_fd=read_be32(request+8);ios->di_waiting=true;ios->di_wait_kind=1;
        return 2; /* IPC ACK now, reply after actual shared-device completion */
    }
    case 0x79:
        if(ios->di_waiting || ios->di->cmd_pending)return 0;
        ios->di_message=ios->message;ios->di_fd=read_be32(request+8);ios->di_waiting=true;ios->di_wait_kind=2;
        return 2; /* stay pending while the shared drive's cover is open */
    case 0x7a:value=gcn_di_read(ios->di,cpu,GCN_DI_BASE+GCN_DI_CVR,4);break;
    case 0x84:value=gcn_di_read(ios->di,cpu,GCN_DI_BASE+GCN_DI_IMMBUF,4);break;
    case 0x88:value=ios->di->disc_present ? 2:1;break;
    case 0x95:value=gcn_di_read(ios->di,cpu,GCN_DI_BASE+GCN_DI_SR,4);break;
    case 0x96:value=gcn_di_read(ios->di,cpu,GCN_DI_BASE+GCN_DI_CR,4);break;
    case 0x7e:di_registers(ios,cpu);ios->result=1;return 1;
    case 0x85:case 0x86:case 0x89: {
        const u32 old=gcn_di_read(ios->di,cpu,GCN_DI_BASE+GCN_DI_CVR,4);
        value=old&GCN_DI_CVR_CVRINTMASK;
        if(command==0x85)value=0;
        if(command==0x86)value|=GCN_DI_CVR_CVRINT;
        if(command==0x89)value|=GCN_DI_CVR_CVRINTMASK;
        gcn_di_write(ios->di,cpu,GCN_DI_BASE+GCN_DI_CVR,value,4);
        ios->result=1;return 1;
    }
    case 0x8a:
        gcn_di_reset_drive(ios->di,read_be32(packet+4)!=0);
        di_registers(ios,cpu);ios->result=1;return 1;
    default:
        fprintf(stderr,"native IOS unsupported DI ioctl=%02x\n",command);return 0;
    }
    if(output_size<4 || !ios_memory(cpu,output,&available) || available<4) {
        ios->result=32;return 1;
    }
    mem_write32_cia(cpu,output,value,cpu->pc);ios->result=1;
    return 1;
}
static int execute(WiiIosNative* ios,CPUState* cpu) {
    u32 available;
    const u8* request=ios_memory(cpu,ios->message,&available);
    if(!request || available<32 || (ios->message&3))return false;
    ios->command=read_be32(request);
    const u32 fd=read_be32(request+8);
    if(ios->command==1) {
        const u8* path=ios_memory(cpu,read_be32(request+12),&available);
        if(!path || !memchr(path,0,available<64 ? available:64))return false;
        u8 device=0;
        if(!strcmp((const char*)path,"/dev/stm/immediate"))device=1;
        if(!strcmp((const char*)path,"/dev/stm/eventhook"))device=2;
        if(!strcmp((const char*)path,"/dev/di") && ios->di)device=3;
        if(!strcmp((const char*)path,"/dev/fs") && ios->fs)device=4;
        /* ES descriptors bind to this boot's real copied NAND. Individual ES
         * operations remain unsupported until their actual contracts exist. */
        if(!strcmp((const char*)path,"/dev/es") && ios->fs)device=6;
        if(path[0]=='/' && strncmp((const char*)path,"/dev/",5) && ios->fs)device=5;
        if(!device) {
            fprintf(stderr,"native IOS unsupported open path=%s\n",(const char*)path);
            return false;
        }
        if(read_be32(request+16)>3) {ios->result=-4;return true;}
        ios->result=-5;
        for(u32 n=0;n<24;n++)if(!ios->descriptors[n]) {
            if(device==5) {
                const s32 opened=wii_fs_open(ios->fs,(const char*)path,read_be32(request+16));
                if(opened<0) {ios->result=opened;return true;}
                ios->fs_handles[n]=(u32)opened;
            }
            if(device==3 && !ios->di_initialized) {di_registers(ios,cpu);ios->di_initialized=true;}
            ios->descriptors[n]=device;ios->result=(s32)n;break;
        }
        return true;
    }
    if(ios->command==6 && fd<24 && ios->descriptors[fd]==3 && ios->di)
        return di_ioctl(ios,cpu,request);
    if(ios->command==7 && fd<24 && ios->descriptors[fd]==6 && ios->fs && read_be32(request+12)==0x1d) {
        ios->result=-1017;
        if(read_be32(request+16)!=1 || read_be32(request+20)!=1)return 1;
        const u8* vectors=ios_memory(cpu,read_be32(request+24),&available);
        if(!vectors || available<16 || read_be32(vectors+4)!=8 || read_be32(vectors+12)<29)return 1;
        const u8* title=ios_memory(cpu,read_be32(vectors),&available);
        if(!title || available<8)return 1;
        const u32 high=read_be32(title),low=read_be32(title+4),output=read_be32(vectors+8);
        if(!ios_memory(cpu,output,&available) || available<29)return 1;
        char path[40];const int size=snprintf(path,sizeof path,"/title/%08x/%08x/data",high,low);
        for(int n=0;n<size;n++)mem_write8_cia(cpu,output+(u32)n,(u8)path[n],cpu->pc);
        ios->result=0;return 1;
    }
    if(ios->command==7 && fd<24 && ios->descriptors[fd]==6 && ios->fs && read_be32(request+12)==0x20) {
        ios->result=-1017; /* ES_EINVAL */
        if(read_be32(request+16)!=0 || read_be32(request+20)!=1)return 1;
        const u8* vector=ios_memory(cpu,read_be32(request+24),&available);
        if(!vector || available<8 || read_be32(vector+4)!=8)return 1;
        const u32 output=read_be32(vector);
        if(!ios_memory(cpu,output,&available) || available<8)return 1;
        u64 title;
        if(!wii_fs_boot_title(ios->fs,&title))return 0;
        mem_write32_cia(cpu,output,(u32)(title>>32),cpu->pc);
        mem_write32_cia(cpu,output+4,(u32)title,cpu->pc);ios->result=0;return 1;
    }
    if(fd<24 && ios->fs && (ios->descriptors[fd]==4 || ios->descriptors[fd]==5)) {
        if(ios->command==6) {
            const u32 input=read_be32(request+16),ni=read_be32(request+20);
            const u32 output=read_be32(request+24),no=read_be32(request+28);
            u32 in_available=0,out_available=0;
            const u8* in=ni ? ios_memory(cpu,input,&in_available):NULL;
            u8* out=no ? ios_memory(cpu,output,&out_available):NULL;
            if((ni && (!in || in_available<ni)) || (no && (!out || out_available<no))) {ios->result=-101;return 1;}
            if(!wii_fs_ioctl(ios->fs,ios->descriptors[fd]==5 ? (s32)ios->fs_handles[fd]:-1,
                read_be32(request+12),in,ni,out,no,&ios->result))return 0;
            /* Publish ARM-originated writes through the CPU bus so executable
             * caches and reservations observe the same changes as PPC stores.
             * Implemented FS ioctls write at most74 bytes. */
            if(!ios->result)for(u32 n=0;n<no && n<74;n++)mem_write8_cia(cpu,output+n,out[n],cpu->pc);
            return 1;
        }
        if(ios->descriptors[fd]==5 && (ios->command==3 || ios->command==4)) {
            const u32 size=read_be32(request+16),address=read_be32(request+12);
            u8* data=ios_memory(cpu,address,&available);
            if(size && (!data || available<size)) {ios->result=-101;return 1;}
            ios->result=ios->command==3 ? wii_fs_read(ios->fs,ios->fs_handles[fd],data,size):
                wii_fs_write(ios->fs,ios->fs_handles[fd],data,size);
            if(ios->command==3 && ios->result>0)
                for(u32 n=0;n<(u32)ios->result;n++)mem_write8_cia(cpu,address+n,data[n],cpu->pc);
            return 1;
        }
        if(ios->descriptors[fd]==5 && ios->command==5) {
            ios->result=wii_fs_seek(ios->fs,ios->fs_handles[fd],read_be32(request+12),read_be32(request+16));return 1;
        }
    }
    if(ios->command==2) {
        if(ios->event_waiting && fd==ios->event_fd)return false;
        if(ios->di_waiting && fd==ios->di_fd)return false;
        ios->result=-4;
        if(fd<24 && ios->descriptors[fd]) {
            ios->result=ios->descriptors[fd]==5 ? wii_fs_close(ios->fs,ios->fs_handles[fd]):0;
            if(!ios->result)ios->descriptors[fd]=0;
        }
        return true;
    }
    if(ios->command==6 && fd<24 && ios->descriptors[fd]==2 && read_be32(request+12)==0x1000) {
        if(ios->event_waiting) {ios->result=-2;return 1;}
        const u32 output=read_be32(request+24),size=read_be32(request+28);
        if(size<4 || !ios_memory(cpu,output,&available) || available<4) {ios->result=-4;return 1;}
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
    if(ios->di_waiting && ios->di_wait_kind==2 && !(ios->di->dicvr&GCN_DI_CVR_CVR) && !ipc->y) {
        reply(ios,ipc,cpu,ios->di_message,6,4);ios->di_waiting=false;return true;
    }
    if(ios->di_waiting && ios->di_wait_kind==1 && !ios->di->cmd_pending && !ipc->y) {
        const u32 status=gcn_di_read(ios->di,cpu,GCN_DI_BASE+GCN_DI_SR,4);
        if(!(status&(GCN_DI_SR_TCINT|GCN_DI_SR_DEINT)))return false;
        const s32 result=(status&GCN_DI_SR_DEINT)?2:1;
        if(result==1 && ios->di_immediate)mem_write32_cia(cpu,ios->di_output,
            gcn_di_read(ios->di,cpu,GCN_DI_BASE+GCN_DI_IMMBUF,4),cpu->pc);
        di_registers(ios,cpu);reply(ios,ipc,cpu,ios->di_message,6,result);ios->di_waiting=false;
        return true;
    }
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
