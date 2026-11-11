/* SPDX-License-Identifier: MIT */
#include "wiillr/ios.h"
#include "memory/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
    ES_GET_STORED_CONTENT_COUNT=0x10,ES_GET_STORED_CONTENTS=0x11,
    ES_GET_TICKET_VIEW_COUNT=0x12,ES_GET_TICKET_VIEWS=0x13,
    ES_GET_TMD_VIEW_SIZE=0x14,ES_GET_TMD_VIEW=0x15
};

static u8* ios_memory(CPUState* cpu,u32 address,u32* available) {
    const u32 physical=gcn_reservation_key(address);
    if(!(physical<cpu->ram_size || (physical>=0x10000000u && physical-0x10000000u<cpu->mem2_size))) {
        if(available)*available=0;return NULL;
    }
    return gcn_mem_resolve(cpu,address,available);
}

void wii_ios_native_init(WiiIosNative* ios) {
    memset(ios,0,sizeof *ios);
    // Sample the host clock once. Subsequent native KD time advances only
    // with retired Broadway cycles; a host pause also pauses this clock.
    const time_t now=time(NULL);
    if(now!=(time_t)-1) {
        ios->kd_utc=(u64)now;ios->kd_utc_set=true;ios->kd_utc_cycles=UINT64_MAX;
        if(getenv("WIILLR_IOS_TRACE"))fprintf(stderr,"native IOS UTC seed=%llu epoch=unix clock=729000000\n",(unsigned long long)ios->kd_utc);
    }
}
void wii_ios_native_destroy(WiiIosNative* ios){wii_bluetooth_destroy(ios->bluetooth);ios->bluetooth=NULL;}
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
    if(getenv("WIILLR_IOS_TRACE")) {
        fprintf(stderr,"IOS request command=%u fd=%u argument=%08x\n",ios->command,fd,read_be32(request+12));
        if(ios->command==7 && fd<24 && ios->descriptors[fd]==6) {
            const u32 inputs=read_be32(request+16),outputs=read_be32(request+20);
            const u8* vectors=ios_memory(cpu,read_be32(request+24),&available);
            if(inputs<=8 && outputs<=8 && vectors && available>=(inputs+outputs)*8)
                for(u32 n=0;n<inputs+outputs;n++)fprintf(stderr,"ES vector%u address=%08x size=%u\n",n,
                    read_be32(vectors+n*8),read_be32(vectors+n*8+4));
            if(inputs && vectors && available>=8 && read_be32(vectors+4)==8 &&
               (read_be32(request+12)==8 || read_be32(request+12)==0x17 || read_be32(request+12)==0x18)) {
                const u8* title=ios_memory(cpu,read_be32(vectors),&available);
                if(title && available>=8)fprintf(stderr,"ES %s title=%08x%08x\n",
                    read_be32(request+12)==8?"launch":"deletion",read_be32(title),read_be32(title+4));
            }
        }
    }
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
        if(!strcmp((const char*)path,"/dev/net/kd/request") && ios->fs)device=7;
        if(!strcmp((const char*)path,"/dev/net/kd/time") && ios->fs)device=8;
        if(!strcmp((const char*)path,"/dev/net/ncd/manage") && ios->fs)device=10;
        if(!strcmp((const char*)path,"/dev/usb/oh1/57e/305"))device=9;
        if(!strcmp((const char*)path,"/dev/usb/kbd"))device=11;
        if(!strcmp((const char*)path,"/dev/net/ip/top"))device=12;
        if(!strcmp((const char*)path,"/dev/sdio/slot0"))device=13;
        if(path[0]=='/' && strncmp((const char*)path,"/dev/",5) && ios->fs)device=5;
        if(!device) {
            fprintf(stderr,"native IOS unsupported open path=%s\n",(const char*)path);
            return false;
        }
        if(getenv("WIILLR_IOS_TRACE"))fprintf(stderr,"IOS open device=%u path=%s\n",device,(const char*)path);
        if(read_be32(request+16)>3) {ios->result=-4;return true;}
        ios->result=-5;
        for(u32 n=0;n<24;n++)if(!ios->descriptors[n]) {
            if(device==9 && !ios->bluetooth) {
                const WiiBluetoothStore store={ios->fs,wii_fs_bt_read,wii_fs_bt_write};
                ios->bluetooth=wii_bluetooth_create(cpu,ios->gpio_out,ios->fs?&store:NULL);if(!ios->bluetooth)return 0;
            }
            if(device==5) {
                const s32 opened=wii_fs_open(ios->fs,(const char*)path,read_be32(request+16));
                if(opened<0) {ios->result=opened;return true;}
                ios->fs_handles[n]=(u32)opened;
            }
            if(device==3 && !ios->di_initialized) {di_registers(ios,cpu);ios->di_initialized=true;}
            if(device==6)ios->es_uids[n]=wii_fs_uid(ios->fs);
            ios->descriptors[n]=device;ios->result=(s32)n;break;
        }
        return true;
    }
    if(ios->command==6 && fd<24 && ios->descriptors[fd]==3 && ios->di)
        return di_ioctl(ios,cpu,request);
    if(ios->command==7 && fd<24 && ios->descriptors[fd]==9 && ios->bluetooth)
        return wii_bluetooth_submit(ios->bluetooth,ios->message)?2:0;
    if(ios->command==6 && fd<24 && ios->descriptors[fd]==13 &&
       (read_be32(request+12)==0x0b || read_be32(request+12)==0x04)) {
        /* The front slot has no media attached: no card status bits and no
         * relative card address on reset. No image is opened or created. */
        ios->result=-4;
        const u32 output=read_be32(request+24),size=read_be32(request+28);
        if(read_be32(request+20) || size<4 || !ios_memory(cpu,output,&available) || available<size)return 1;
        mem_write32_cia(cpu,output,0,cpu->pc);
        ios->result=0;return 1;
    }
    if(ios->command==6 && fd<24 && ios->descriptors[fd]==13 && read_be32(request+12)==7) {
        const u32 size=read_be32(request+20),output=read_be32(request+24),out_size=read_be32(request+28);
        const u8* sd=ios_memory(cpu,read_be32(request+16),&available);
        ios->result=-4;
        if(!sd || size<36 || available<size || (out_size && (!ios_memory(cpu,output,&available) || available<out_size)))return 1;
        const u32 command=read_be32(sd),event=read_be32(sd+12);
        if(getenv("WIILLR_IOS_TRACE"))fprintf(stderr,"SDIO command=%02x event=%u\n",command,event);
        if(command==0x40) {
            if(event!=1 && event!=2)return 1;
            if(ios->sd_event_message){ios->result=-2;return 1;}
            if(event==2){ios->result=2;return 1;} /* removal condition already true */
            ios->sd_event_message=ios->message;ios->sd_event_fd=fd;
            return 2; /* insertion wait: ACK now, complete only on an event or cancellation */
        }
        if(command==0x41) {
            if(!ios->sd_event_message || ios->sd_event_fd!=fd)return 1;
            ios->sd_event_cancelled=true;ios->result=0;return 1;
        }
        fprintf(stderr,"native IOS unsupported SDIO command=%08x\n",command);
        return 0;
    }
    if(fd<24 && ios->descriptors[fd]==11) {
        if(ios->command==6) {
            // The keyboard bus exists without an attached USB keyboard.
            // A valid event read waits for a real connection/event; no empty
            // event or successful completion may be fabricated.
            ios->result=-4;
            const u32 output=read_be32(request+24),size=read_be32(request+28);
            if(size<16 || !ios_memory(cpu,output,&available) || available<size)return 1;
            if(ios->keyboard_messages[fd]){ios->result=-2;return 1;}
            ios->keyboard_messages[fd]=ios->message;return 2;
        }
        if(ios->command==3 || ios->command==5 || ios->command==7){ios->result=-4;return 1;}
    }
    if(ios->command==7 && fd<24 && ios->descriptors[fd]==10 && read_be32(request+12)==7) {
        // This development profile has no attached network interface. Report
        // NCD's disconnected link, with a successful status query only.
        ios->result=-4;
        if(read_be32(request+16)!=0 || read_be32(request+20)!=1)return 1;
        const u8* vector=ios_memory(cpu,read_be32(request+24),&available);
        if(!vector || available<8 || read_be32(vector+4)<8)return 1;
        const u32 output=read_be32(vector),size=read_be32(vector+4);
        if(!ios_memory(cpu,output,&available) || available<size)return 1;
        mem_write32_cia(cpu,output,0,cpu->pc);mem_write32_cia(cpu,output+4,2,cpu->pc); /* LINK_NONE */
        ios->result=0;return 1;
    }
    if(ios->command==7 && fd<24 && ios->descriptors[fd]==10 && read_be32(request+12)==8 && ios->fs) {
        ios->result=-4;
        if(read_be32(request+16)!=0 || read_be32(request+20)!=2)return 1;
        const u8* vectors=ios_memory(cpu,read_be32(request+24),&available);
        if(!vectors || available<16 || read_be32(vectors+4)<4 || read_be32(vectors+12)<6)return 1;
        const u32 output=read_be32(vectors),mac_output=read_be32(vectors+8);
        if(!ios_memory(cpu,output,&available) || available<read_be32(vectors+4) ||
           !ios_memory(cpu,mac_output,&available) || available<read_be32(vectors+12))return 1;
        u8 mac[6];if(!wii_fs_network_mac(ios->fs,mac)){ios->result=-117;return 1;}
        for(u32 n=0;n<6;n++)mem_write8_cia(cpu,mac_output+n,mac[n],cpu->pc);
        mem_write32_cia(cpu,output,0,cpu->pc);ios->result=0;return 1;
    }
    if(ios->command==7 && fd<24 && ios->descriptors[fd]==10 && read_be32(request+12)==5 && ios->fs) {
        ios->result=-4;
        if(read_be32(request+16)!=0 || read_be32(request+20)!=2)return 1;
        const u8* vectors=ios_memory(cpu,read_be32(request+24),&available);
        if(!vectors || available<16 || read_be32(vectors+4)!=WIILLR_NETWORK_CONFIG_SIZE || read_be32(vectors+12)<32)return 1;
        const u32 output=read_be32(vectors),status=read_be32(vectors+8);
        if(!ios_memory(cpu,output,&available) || available<WIILLR_NETWORK_CONFIG_SIZE ||
           !ios_memory(cpu,status,&available) || available<read_be32(vectors+12))return 1;
        u8 config[WIILLR_NETWORK_CONFIG_SIZE];ios->result=wii_fs_network_config(ios->fs,config);
        if(!ios->result) {
            for(u32 n=0;n<sizeof config;n++)mem_write8_cia(cpu,output+n,config[n],cpu->pc);
            mem_write32_cia(cpu,status,0,cpu->pc);
        }
        return 1;
    }
    if(ios->command==7 && fd<24 && ios->descriptors[fd]==10 && read_be32(request+12)==6 && ios->fs) {
        ios->result=-4;
        if(read_be32(request+16)!=1 || read_be32(request+20)!=1)return 1;
        const u8* vectors=ios_memory(cpu,read_be32(request+24),&available);
        if(!vectors || available<16 || read_be32(vectors+4)!=WIILLR_NETWORK_CONFIG_SIZE || read_be32(vectors+12)<4)return 1;
        const u32 address=read_be32(vectors),status=read_be32(vectors+8);
        const u8* config=ios_memory(cpu,address,&available);
        if(!config || available<WIILLR_NETWORK_CONFIG_SIZE ||
           !ios_memory(cpu,status,&available) || available<read_be32(vectors+12))return 1;
        ios->result=wii_fs_network_config_write(ios->fs,config);
        if(!ios->result)mem_write32_cia(cpu,status,0,cpu->pc);
        return 1;
    }
    if(ios->command==6 && fd<24 && ios->descriptors[fd]==7 &&
       (read_be32(request+12)==8 || read_be32(request+12)==9 || read_be32(request+12)==0x0d)) {
        // Socket and background mail workers are absent in this offline
        // profile. Never claim a socket lock or a worker's mail-cache flush.
        const u32 output=read_be32(request+24),size=read_be32(request+28);
        ios->result=-4;
        if(read_be32(request+20) || !output || (output&3) || size<4 ||
           !ios_memory(cpu,output,&available) || available<size)return 1;
        mem_write32_cia(cpu,output,(u32)-39,cpu->pc);ios->result=0;return 1;
    }
    if(ios->command==6 && fd<24 && ios->descriptors[fd]==7 && read_be32(request+12)==0x1e) {
        const u32 output=read_be32(request+24),size=read_be32(request+28);
        ios->result=-4;
        if(read_be32(request+20) || !output || (output&3) || size<16 ||
           !ios_memory(cpu,output,&available) || available<size)return 1;
        // An offline scheduler has no network permissions, active workers,
        // completed jobs or worker error history. The snapshot is 256 bytes.
        const u32 bytes=size<256 ? size:256;
        for(u32 n=0;n<bytes;n++)mem_write8_cia(cpu,output+n,0,cpu->pc);
        ios->result=0;return 1;
    }
    if(ios->command==6 && fd<24 && ios->descriptors[fd]==7 &&
       (read_be32(request+12)>=1 && read_be32(request+12)<=3)) {
        // KD returns its nesting count, including locks held while the PPC
        // accesses local WC24 files. This provider has an empty worker queue;
        // download/mail execution remains unsupported independently of this gate.
        ios->result=-4;
        const u32 operation=read_be32(request+12),input_size=read_be32(request+20);
        const u32 output=read_be32(request+24),size=read_be32(request+28);
        const u32 minimum=operation==2?4:32;
        if(!output || (output&3) || size<minimum ||
           !ios_memory(cpu,output,&available) || available<size)return 1;
        if(operation==2) {
            const u32 address=read_be32(request+16);const u8* input=ios_memory(cpu,address,&available);
            if(!address || (address&3) || input_size<4 || !input || available<input_size)return 1;
            if(read_be32(input))return 0; /* unimplemented try-suspend flags */
        }else if(input_size)return 1;
        if(operation==3) {
            if(ios->kd_suspend_count)--ios->kd_suspend_count;
        }else {
            if(ios->kd_suspend_count==0x7fffffffu) {
                mem_write32_cia(cpu,output,(u32)-3,cpu->pc);ios->result=0;return 1;
            }
            ++ios->kd_suspend_count;
        }
        mem_write32_cia(cpu,output,ios->kd_suspend_count,cpu->pc);ios->result=0;return 1;
    }
    if(ios->command==6 && fd<24 && ios->descriptors[fd]==1 && read_be32(request+12)==0x3002) {
        // Releasing the hook completes its original request with no event.
        // The mailbox delivers that completion before replying to this call.
        ios->result=-6; /* IPC_ENOENT */
        if(ios->event_waiting && !ios->event_ready) {
            mem_write32_cia(cpu,ios->event_output,0,cpu->pc);
            ios->event_ready=true;
            ios->result=0;
        }
        return 1;
    }
    if(ios->command==6 && fd<24 && ios->descriptors[fd]==1 && read_be32(request+12)==0x5001 && ios->hollywood) {
        ios->result=-4;
        const u32 input=read_be32(request+16),output=read_be32(request+24);
        const u8* data=ios_memory(cpu,input,&available);
        if(!input || (input&3) || read_be32(request+20)<32 || !data || available<32 ||
           !output || (output&3) || read_be32(request+28)<32 ||
           !ios_memory(cpu,output,&available) || available<32)return 1;
        u32 set[3],keep[3],old[3],flags;
        for(unsigned n=0;n<3;n++){set[n]=read_be32(data+n*4);keep[n]=read_be32(data+16+n*4);}
        if(!wii_hollywood_video_update(ios->hollywood,set,keep,old,&flags))return 0;
        for(unsigned n=0;n<3;n++)mem_write32_cia(cpu,output+n*4,old[n],cpu->pc);
        mem_write32_cia(cpu,output+12,flags,cpu->pc);ios->result=0;return 1;
    }
    if(ios->command==6 && fd<24 && ios->descriptors[fd]==7 && read_be32(request+12)==0x28) {
        const u32 address=read_be32(request+16),input_size=read_be32(request+20);
        const u32 output=read_be32(request+24),output_size=read_be32(request+28);
        const u8* input=ios_memory(cpu,address,&available);
        ios->result=-4;
        if(!address || (address&3) || input_size<4 || !input || available<input_size)return 1;
        const u32 event=read_be32(input);
        if(output_size && (!output || (output&3) ||
           !ios_memory(cpu,output,&available) || available<output_size))return 1;
        /* NWC24 shutdown preparation advertises the PPC's shutdown event and
         * drains network work; it does not reset the PPC itself. This offline
         * provider owns no sockets, network workers or keyboard LEDs. Its
         * queue is already empty. The completion is the IPC result, not a
         * service-result word in the unused caller output buffer. */
        ios->kd_shutdown_event=event;ios->kd_shutdown_requested=true;
        ios->result=0;return 1;
    }
    if(ios->command==6 && fd<24 && ios->descriptors[fd]==7 && read_be32(request+12)==0x22) {
        const u32 address=read_be32(request+16),output=read_be32(request+24);
        const u8* input=ios_memory(cpu,address,&available);
        ios->result=-4;
        if(!address || (address&3) || read_be32(request+20)<4 || !input || available<4 ||
           !output || (output&3) || read_be32(request+28)<4 ||
           !ios_memory(cpu,output,&available) || available<4)return 1;
        /* NWC24 SetScriptMode. This profile has no network script engine.
         * Report WC24_ERR_DISABLED through the service result, rather than
         * acknowledging script execution or silently enabling network work. */
        const s32 result=read_be32(input)<=1 ? -39 : -3;
        mem_write32_cia(cpu,output,(u32)result,cpu->pc);ios->result=0;return 1;
    }
    if(ios->command==6 && fd<24 && ios->descriptors[fd]==7 && read_be32(request+12)==0x0f && ios->fs) {
        const u32 output=read_be32(request+24);
        ios->result=-4;
        if(read_be32(request+20) || !output || (output&3) || read_be32(request+28)<32 ||
           !ios_memory(cpu,output,&available) || available<32)return 1;
        u64 id=0;u32 stage=0;
        const s32 status=wii_fs_wc24_generate_id(ios->fs,&id,&stage);
        mem_write32_cia(cpu,output,(u32)status,cpu->pc);
        if(!status || status==-35 || status==-36) {
            mem_write32_cia(cpu,output+4,(u32)(id>>32),cpu->pc);
            mem_write32_cia(cpu,output+8,(u32)id,cpu->pc);
            mem_write32_cia(cpu,output+12,stage,cpu->pc);
        }
        ios->result=0;return 1;
    }
    if(ios->command==6 && fd<24 && ios->descriptors[fd]==8 && read_be32(request+12)==0x17) {
        const u32 address=read_be32(request+16),output=read_be32(request+24);
        const u8* input=ios_memory(cpu,address,&available);
        ios->result=-4;
        if(!address || (address&3) || read_be32(request+20)<8 || !input || available<8 ||
           !output || (output&3) || read_be32(request+28)<4 ||
           !ios_memory(cpu,output,&available) || available<4)return 1;
        /* Synchronize the network service's RTC to the actual PPC-supplied
         * counter. Universal UTC is not inferred from this counter. The
         * persistent misc.bin update has no implemented contract yet. */
        if(read_be32(input+4)!=0)return 0;
        ios->kd_rtc=read_be32(input);ios->kd_rtc_cycles=cpu->cycles;ios->kd_rtc_set=true;
        mem_write32_cia(cpu,output,0,cpu->pc);ios->result=0;return 1;
    }
    if(ios->command==6 && fd<24 && ios->descriptors[fd]==8) {
        const u32 operation=read_be32(request+12),output=read_be32(request+24),extent=read_be32(request+28);
        if(operation==0x14 || operation==0x18) {
            ios->result=-4;
            if(read_be32(request+20) || !output || (output&3) || extent<12 ||
               !ios_memory(cpu,output,&available) || available<extent || !ios->kd_utc_set ||
               (operation==0x18 && !ios->kd_rtc_set) || cpu->cycles<ios->kd_utc_cycles ||
               (operation==0x18 && cpu->cycles<ios->kd_rtc_cycles))return 1;
            u64 value=ios->kd_utc+(cpu->cycles-ios->kd_utc_cycles)/729000000ull;
            if(operation==0x18)value-=(u32)(ios->kd_rtc+(cpu->cycles-ios->kd_rtc_cycles)/729000000ull);
            mem_write32_cia(cpu,output,0,cpu->pc);mem_write32_cia(cpu,output+4,(u32)(value>>32),cpu->pc);
            mem_write32_cia(cpu,output+8,(u32)value,cpu->pc);ios->result=0;return 1;
        }
        if(operation==0x15) {
            ios->result=-4;
            const u8* input=ios_memory(cpu,read_be32(request+16),&available);
            if(read_be32(request+20)<12 || !input || available<read_be32(request+20) || !output || (output&3) ||
               extent<4 || !ios_memory(cpu,output,&available) || available<extent)return 1;
            if(read_be32(input+8))return 0; /* persistent misc.bin update is not implemented */
            ios->kd_utc=(u64)read_be32(input)<<32|read_be32(input+4);ios->kd_utc_cycles=cpu->cycles;ios->kd_utc_set=true;
            mem_write32_cia(cpu,output,0,cpu->pc);ios->result=0;return 1;
        }
    }
    if(ios->command==6 && fd<24 && ios->descriptors[fd]==1 && read_be32(request+12)==0x6002) {
        /* STM_LEDMode is a setting, not a request to turn the LED on. The
         * PPC ABI is also exercised by libogc STM_SetLedMode. Flash/pattern
         * requests remain unsupported until their timing is implemented. */
        const u8* input=ios_memory(cpu,read_be32(request+16),&available);
        ios->result=-4;
        if(read_be32(request+20)!=32 || !input || available<32 ||
           read_be32(request+28)!=32 ||
           !ios_memory(cpu,read_be32(request+24),&available) || available<32)return 1;
        const u32 mode=read_be32(input);
        if(mode>2)return 1; /* invalid setting; keep the current LED state */
        ios->led_mode=mode;
        if(!mode && ios->gpio_out)*ios->gpio_out&=~0x20u;
        ios->result=0;return 1;
    }
    if(ios->command==7 && fd<24 && ios->descriptors[fd]==6 && ios->fs) {
        const u32 operation=read_be32(request+12);
        if(operation==0x45) {
            // This provider has no Korean common-key slot. IOS reports
            // ES_EINVAL for that provisioning query; it is not a region test
            // that should be answered with success for every title.
            ios->result=-1017;
            if(read_be32(request+16) || read_be32(request+20))return 1;
            return 1;
        }
        if(operation==0x24) {
            ios->result=-1017;
            if(read_be32(request+16)!=3 || read_be32(request+20)!=0)return 1;
            const u8* vectors=ios_memory(cpu,read_be32(request+24),&available);
            if(!vectors || available<24 || read_be32(vectors+4)!=8 || read_be32(vectors+12)!=0xd8 || read_be32(vectors+20)!=4)return 1;
            const u8* id=ios_memory(cpu,read_be32(vectors),&available);if(!id || available<8)return 1;
            const u64 title=(u64)read_be32(id)<<32|read_be32(id+4);
            const u8* view=ios_memory(cpu,read_be32(vectors+8),&available);if(!view || available<0xd8)return 1;
            const u8* index=ios_memory(cpu,read_be32(vectors+16),&available);if(!index || available<4)return 1;
            ios->result=wii_es_title_content_open(ios->fs,title,view,read_be32(index));return 1;
        }
        if(operation==0x21) {
            ios->result=-1017;
            if(read_be32(request+16)!=1 || read_be32(request+20)!=0)return 1;
            const u8* vector=ios_memory(cpu,read_be32(request+24),&available);
            if(!vector || available<8 || read_be32(vector+4)!=8)return 1;
            const u8* id=ios_memory(cpu,read_be32(vector),&available);
            if(!id || available<8)return 1;
            ios->result=wii_es_set_uid(ios->fs,ios->es_uids[fd],(u64)read_be32(id)<<32|read_be32(id+4));return 1;
        }
        if(operation==ES_GET_STORED_CONTENT_COUNT || operation==ES_GET_STORED_CONTENTS) {
            ios->result=-1017;
            const u32 inputs=operation==ES_GET_STORED_CONTENT_COUNT?1:2;
            if(read_be32(request+16)!=inputs || read_be32(request+20)!=1)return 1;
            const u8* vectors=ios_memory(cpu,read_be32(request+24),&available);
            if(!vectors || available<(inputs+1)*8 || read_be32(vectors+4)!=8)return 1;
            const u8* id=ios_memory(cpu,read_be32(vectors),&available);
            if(!id || available<8)return 1;const u64 title=(u64)read_be32(id)<<32|read_be32(id+4);
            u32 maximum=0;
            if(inputs==2) {
                if(read_be32(vectors+12)!=4)return 1;
                const u8* count=ios_memory(cpu,read_be32(vectors+8),&available);
                if(!count || available<4)return 1;maximum=read_be32(count);
                if(maximum>UINT32_MAX/4)return 1;
            }
            const u32 output=read_be32(vectors+inputs*8),size=inputs==2?maximum*4:4;
            if(read_be32(vectors+inputs*8+4)!=size)return 1;
            u8* data=size?ios_memory(cpu,output,&available):NULL;
            if(size && (!data || available<size))return 1;
            u32 actual=0;ios->result=wii_es_stored_contents(ios->fs,title,inputs==2?data:NULL,maximum,&actual);
            if(!ios->result) {
                if(inputs==1)mem_write32_cia(cpu,output,actual,cpu->pc);
                else for(u32 n=0;n<(actual<maximum?actual:maximum)*4;n++)mem_write8_cia(cpu,output+n,data[n],cpu->pc);
            }
            return 1;
        }
        if(operation==0x17) {
            ios->result=-1017;
            if(read_be32(request+16)!=1 || read_be32(request+20)!=0)return 1;
            const u8* vector=ios_memory(cpu,read_be32(request+24),&available);
            if(!vector || available<8 || read_be32(vector+4)!=8)return 1;
            const u8* id=ios_memory(cpu,read_be32(vector),&available);
            if(!id || available<8)return 1;
            ios->result=wii_es_delete_title(ios->fs,(u64)read_be32(id)<<32|read_be32(id+4));return 1;
        }
        if(operation==ES_GET_TMD_VIEW_SIZE || operation==ES_GET_TMD_VIEW) {
            ios->result=-1017;
            const u32 inputs=operation==ES_GET_TMD_VIEW_SIZE?1:2;
            if(read_be32(request+16)!=inputs || read_be32(request+20)!=1)return 1;
            const u8* vectors=ios_memory(cpu,read_be32(request+24),&available);
            if(!vectors || available<(inputs+1)*8 || read_be32(vectors+4)<8)return 1;
            const u8* id=ios_memory(cpu,read_be32(vectors),&available);
            if(!id || available<read_be32(vectors+4))return 1;
            const u64 title=(u64)read_be32(id)<<32|read_be32(id+4);
            u32 capacity=0;
            if(inputs==2) {
                if(read_be32(vectors+12)!=4)return 1;
                const u8* length=ios_memory(cpu,read_be32(vectors+8),&available);
                if(!length || available<4)return 1;capacity=read_be32(length);
            }
            const u32 output=read_be32(vectors+inputs*8),extent=read_be32(vectors+inputs*8+4);
            if((inputs==1 && extent<4) || (inputs==2 && extent!=capacity))return 1;
            u8* data=extent?ios_memory(cpu,output,&available):NULL;
            if(extent && (!data || available<extent))return 1;
            u32 size=0;ios->result=wii_es_tmd_view(ios->fs,title,inputs==2?data:NULL,capacity,&size);
            if(!ios->result) {
                if(inputs==1)mem_write32_cia(cpu,output,size,cpu->pc);
                else if(!extent){ios->result=-1017;return 1;}
                else for(u32 n=0;n<size;n++)mem_write8_cia(cpu,output+n,data[n],cpu->pc);
            }
            return 1;
        }
        if(operation==ES_GET_TICKET_VIEW_COUNT || operation==ES_GET_TICKET_VIEWS) {
            ios->result=-1017;
            const u32 inputs=operation==ES_GET_TICKET_VIEW_COUNT?1:2;
            if(read_be32(request+16)!=inputs || read_be32(request+20)!=1)return 1;
            const u8* vectors=ios_memory(cpu,read_be32(request+24),&available);
            if(!vectors || available<(inputs+1)*8 || read_be32(vectors+4)<8)return 1;
            const u8* id=ios_memory(cpu,read_be32(vectors),&available);
            if(!id || available<read_be32(vectors+4))return 1;const u64 title=(u64)read_be32(id)<<32|read_be32(id+4);
            u32 maximum=0;
            if(inputs==2) {
                if(read_be32(vectors+12)!=4)return 1;
                const u8* count=ios_memory(cpu,read_be32(vectors+8),&available);
                if(!count || available<4)return 1;maximum=read_be32(count);
                if(maximum>UINT32_MAX/0xd8)return 1;
            }
            const u32 output=read_be32(vectors+inputs*8),size=inputs==2?maximum*0xd8:4;
            const u32 extent=read_be32(vectors+inputs*8+4);
            if(extent<size)return 1;
            u8* data=extent?ios_memory(cpu,output,&available):NULL;
            if(extent && (!data || available<extent))return 1;
            u32 actual=0;ios->result=wii_es_ticket_views(ios->fs,title,inputs==2?data:NULL,maximum,&actual);
            if(!ios->result) {
                if(inputs==1)mem_write32_cia(cpu,output,actual,cpu->pc);
                else for(u32 n=0;n<(actual<maximum?actual:maximum)*0xd8;n++)mem_write8_cia(cpu,output+n,data[n],cpu->pc);
            }
            return 1;
        }
        if(operation==0x0e || operation==0x0f) {
            ios->result=-1017;
            const u32 inputs=operation==0x0f;
            if(read_be32(request+16)!=inputs || read_be32(request+20)!=1)return 1;
            const u8* vectors=ios_memory(cpu,read_be32(request+24),&available);
            if(!vectors || available<(inputs+1)*8)return 1;
            u32 maximum=0;
            if(inputs) {
                if(read_be32(vectors+4)!=4)return 1;
                const u8* count=ios_memory(cpu,read_be32(vectors),&available);
                if(!count || available<4)return 1;maximum=read_be32(count);
                if(maximum>UINT32_MAX/8)return 1;
            }
            const u32 output=read_be32(vectors+inputs*8),size=inputs?maximum*8:4;
            if(read_be32(vectors+inputs*8+4)!=size)return 1;
            u8* data=size?ios_memory(cpu,output,&available):NULL;
            if(size && (!data || available<size))return 1;
            u32 actual=0;ios->result=wii_es_titles(ios->fs,inputs?data:NULL,maximum,&actual);
            if(!ios->result) {
                if(!inputs)mem_write32_cia(cpu,output,actual,cpu->pc);
                else for(u32 n=0;n<(actual<maximum?actual:maximum)*8;n++)mem_write8_cia(cpu,output+n,data[n],cpu->pc);
            }
            return 1;
        }
        if(operation==0x16) {
            ios->result=-1017;
            if(read_be32(request+16)!=1 || read_be32(request+20)!=2)return 1;
            const u8* vectors=ios_memory(cpu,read_be32(request+24),&available);
            if(!vectors || available<24 || read_be32(vectors+4)!=8 ||
               read_be32(vectors+12)%8 || read_be32(vectors+20)!=4)return 1;
            const u32 output=read_be32(vectors+16),limits=read_be32(vectors+8),size=read_be32(vectors+12);
            const u8* input=ios_memory(cpu,read_be32(vectors),&available);
            if(!input || available<8)return 1;
            const u64 ticket_id=(u64)read_be32(input)<<32|read_be32(input+4);
            if(!ios_memory(cpu,output,&available) || available<4 ||
               (size && (!ios_memory(cpu,limits,&available) || available<size)))return 1;
            /* An actual unrestricted ticket has no applicable usage limits.
             * Never use this empty result to bypass a restricted ticket. */
            if(!wii_es_unlimited_ticket(ios->fs,ticket_id,&ios->result))return 0;
            if(!ios->result)mem_write32_cia(cpu,output,0,cpu->pc);
            return 1;
        }
        if(operation==0x1b) {
            ios->result=-1017;
            if(read_be32(request+16)!=1 || read_be32(request+20)!=1)return 1;
            const u8* vectors=ios_memory(cpu,read_be32(request+24),&available);
            if(!vectors || available<16 || read_be32(vectors+12)!=0xd8)return 1;
            const u32 size=read_be32(vectors+4),output=read_be32(vectors+8);
            const u8* ticket=size?ios_memory(cpu,read_be32(vectors),&available):NULL;
            if(size && (size!=0x2a4 || !ticket || available<size))return 1;
            if(!ios_memory(cpu,output,&available) || available<0xd8)return 1;
            u8 view[0xd8];ios->result=wii_es_ticket_view(ios->fs,ticket,size,view);
            if(!ios->result)for(u32 n=0;n<sizeof view;n++)mem_write8_cia(cpu,output+n,view[n],cpu->pc);
            return 1;
        }
        if(operation==9 || operation==10 || operation==11 || operation==0x23) {
            ios->result=-1017;
            const u32 count=operation==0x23?3:1,outputs=operation==10?1:0;
            if(read_be32(request+16)!=count || read_be32(request+20)!=outputs)return 1;
            const u8* vectors=ios_memory(cpu,read_be32(request+24),&available);
            if(!vectors || available<(count+outputs)*8)return 1;
            u32 args[3]={0};
            for(u32 n=0;n<count;n++) {
                if(read_be32(vectors+n*8+4)!=4)return 1;
                const u8* data=ios_memory(cpu,read_be32(vectors+n*8),&available);
                if(!data || available<4)return 1;args[n]=read_be32(data);
            }
            if(operation==9)ios->result=wii_es_content_open(ios->fs,args[0]);
            if(operation==11)ios->result=wii_es_content_close(ios->fs,args[0]);
            if(operation==0x23)ios->result=wii_es_content_seek(ios->fs,args[0],args[1],args[2]);
            if(operation==10) {
                const u32 output=read_be32(vectors+8),size=read_be32(vectors+12);
                u8* data=size?ios_memory(cpu,output,&available):NULL;
                if(size && (!data || available<size))return 1;
                ios->result=wii_es_content_read(ios->fs,args[0],data,size);
                if(ios->result>0)for(u32 n=0;n<(u32)ios->result;n++)
                    mem_write8_cia(cpu,output+n,data[n],cpu->pc);
            }
            return 1;
        }
    }
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
        if(ios->command==7 && ios->descriptors[fd]==4 && read_be32(request+12)==0x0c) {
            ios->result=-101;
            if(read_be32(request+16)!=1 || read_be32(request+20)!=2)return 1;
            const u8* vectors=ios_memory(cpu,read_be32(request+24),&available);
            if(!vectors || available<24 || read_be32(vectors+4)!=64 || read_be32(vectors+12)!=4 || read_be32(vectors+20)!=4)return 1;
            const u32 input=read_be32(vectors),out_clusters=read_be32(vectors+8),out_inodes=read_be32(vectors+16);
            const u8* path=ios_memory(cpu,input,&available);if(!path || available<64)return 1;
            if(!ios_memory(cpu,out_clusters,&available) || available<4 || !ios_memory(cpu,out_inodes,&available) || available<4)return 1;
            u32 clusters,inodes;ios->result=wii_fs_directory_usage(ios->fs,path,&clusters,&inodes);
            if(!ios->result){mem_write32_cia(cpu,out_clusters,clusters,cpu->pc);mem_write32_cia(cpu,out_inodes,inodes,cpu->pc);}
            return 1;
        }
        if(ios->command==7 && ios->descriptors[fd]==4 && read_be32(request+12)==4) {
            ios->result=-101;
            const u32 count=read_be32(request+16);
            if(count<1 || count>2 || read_be32(request+20)!=count)return 1;
            const u8* vectors=ios_memory(cpu,read_be32(request+24),&available);
            if(!vectors || available<count*16 || read_be32(vectors+4)!=64)return 1;
            const u8* path=ios_memory(cpu,read_be32(vectors),&available);
            if(!path || available<64)return 1;
            const u32 output=read_be32(vectors+count*8+(count-1)*8);
            if(read_be32(vectors+count*8+(count-1)*8+4)!=4 ||
               !ios_memory(cpu,output,&available) || available<4)return 1;
            u32 maximum=0,names_address=0;u8* names=NULL;
            if(count==2) {
                if(read_be32(vectors+12)!=4)return 1;
                const u8* limit=ios_memory(cpu,read_be32(vectors+8),&available);
                if(!limit || available<4)return 1;maximum=read_be32(limit);
                if(maximum>0xffffffffu/13 || read_be32(vectors+20)!=13*maximum)return 1;
                names_address=read_be32(vectors+16);
                names=ios_memory(cpu,names_address,&available);
                if(!names || available<13*maximum)return 1;
            }
            u32 actual=0;ios->result=wii_fs_read_directory(ios->fs,path,names,maximum,&actual);
            if(!ios->result) {
                for(u32 n=0;names && n<13*actual;n++)mem_write8_cia(cpu,names_address+n,names[n],cpu->pc);
                mem_write32_cia(cpu,output,actual,cpu->pc);
            }
            return 1;
        }
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
        if(fd<24 && ios->keyboard_messages[fd])return false; /* cancellation not implemented */
        if(ios->event_waiting && fd==ios->event_fd)return false;
        if(ios->di_waiting && fd==ios->di_fd)return false;
        if(ios->sd_event_message && fd==ios->sd_event_fd)return false;
        ios->result=-4;
        if(fd<24 && ios->descriptors[fd]) {
            if(ios->descriptors[fd]==9)wii_bluetooth_close(ios->bluetooth);
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
    if(getenv("WIILLR_IOS_TRACE"))fprintf(stderr,"IOS reply message=%08x command=%u result=%d\n",message,command,result);
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
    if(ios->kd_utc_set && ios->kd_utc_cycles==UINT64_MAX)ios->kd_utc_cycles=cpu->cycles;
    if(ios->bluetooth && !ipc->y) {
        u32 message;s32 result;bool ready;
        if(!wii_bluetooth_poll(ios->bluetooth,&message,&result,&ready))return false;
        if(ready){reply(ios,ipc,cpu,message,7,result);return true;}
    }
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
    if(ios->sd_event_cancelled && !ipc->y) {
        reply(ios,ipc,cpu,ios->sd_event_message,6,0x0c210000); /* EVENT_INVALID */
        ios->sd_event_message=0;ios->sd_event_cancelled=false;
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
