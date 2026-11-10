/* SPDX-License-Identifier: MIT */
#include "wiillr/ios.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do {if(!(x)) {fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}} while(0)
int main(void) {
    CPUState cpu;WiiIpc ipc;WiiIosNative ios;
    CHECK(cpu_init(&cpu) && cpu_alloc_mem2(&cpu,0x4000000));
    cpu.spr[287]=PPC_BROADWAY_PVR;
    wii_ipc_init(&ipc);wii_ios_native_init(&ios);
    const u32 message=0x10000200,path=0x800;
    memcpy(cpu.ram+path,"/dev/stm/immediate",19);
    mem_write32_cia(&cpu,message,1,0);
    mem_write32_cia(&cpu,message+12,path,0);
    wii_ipc_write(&ipc,0,message,0);wii_ipc_write(&ipc,4,0x31,0);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));
    cpu.cycles=5999;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && ipc.x==1 && !ipc.y && !ios.operations);
    cpu.cycles=6000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && !ipc.x && ipc.y==2);
    CHECK(mem_read32_cia(&cpu,message,0)==1 && ios.descriptors[0]==1);
    cpu.cycles=12000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && mem_read32_cia(&cpu,message,0)==1);
    wii_ipc_write(&ipc,4,0x32,cpu.cycles); /* consume ACK */
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && ipc.y==1 && ios.operations==1);
    CHECK(mem_read32_cia(&cpu,message,0)==8 && mem_read32_cia(&cpu,message+4,0)==0);
    CHECK(mem_read32_cia(&cpu,message+8,0)==1 && ipc.arm_message==message);
    wii_ipc_write(&ipc,4,0x34,cpu.cycles);
    /* Unsupported service behavior neither acknowledges nor overwrites its
     * request, including the caller's result sentinel. */
    mem_write32_cia(&cpu,message,6,0);mem_write32_cia(&cpu,message+4,0x11223344,0);
    mem_write32_cia(&cpu,message+8,0,0);mem_write32_cia(&cpu,message+12,0x7001,0);
    wii_ipc_write(&ipc,4,0x31,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));
    cpu.cycles+=6000;
    CHECK(!wii_ios_native_tick(&ios,&ipc,&cpu) && ipc.x==1 && !ipc.y && ios.operations==1);
    CHECK(mem_read32_cia(&cpu,message,0)==6 && mem_read32_cia(&cpu,message+4,0)==0x11223344);
    /* Close releases a real descriptor; a second close reports EINVAL. */
    ios.phase=0;wii_ipc_init(&ipc);
    mem_write32_cia(&cpu,message,2,0);
    wii_ipc_write(&ipc,0,message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && !ios.descriptors[0] && ios.result==0);
    cpu.cycles+=6000;wii_ipc_write(&ipc,4,0x32,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));wii_ipc_write(&ipc,4,0x34,cpu.cycles);
    mem_write32_cia(&cpu,message,2,0);mem_write32_cia(&cpu,message+8,0,0);
    wii_ipc_write(&ipc,4,0x31,cpu.cycles);CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));
    cpu.cycles+=6000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && ios.result==-4);
    /* Open the asynchronous resource through the same real request ABI. */
    wii_ios_native_init(&ios);wii_ipc_init(&ipc);
    memcpy(cpu.ram+path,"/dev/stm/eventhook",sizeof "/dev/stm/eventhook");
    mem_write32_cia(&cpu,message,1,0);mem_write32_cia(&cpu,message+12,path,0);
    mem_write32_cia(&cpu,message+16,0,0);
    wii_ipc_write(&ipc,0,message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));wii_ipc_write(&ipc,4,0x32,cpu.cycles);
    cpu.cycles+=6000;CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));
    wii_ipc_write(&ipc,4,0x34,cpu.cycles);
    mem_write32_cia(&cpu,message,6,0);mem_write32_cia(&cpu,message+4,0x12345678,0);
    mem_write32_cia(&cpu,message+8,0,0);mem_write32_cia(&cpu,message+12,0x1000,0);
    mem_write32_cia(&cpu,message+24,0x10000400,0);mem_write32_cia(&cpu,message+28,4,0);
    wii_ipc_write(&ipc,4,0x31,cpu.cycles);CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));
    cpu.cycles+=6000;CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));
    wii_ipc_write(&ipc,4,0x32,cpu.cycles);cpu.cycles+=60000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && !ipc.y && ios.event_waiting);
    CHECK(mem_read32_cia(&cpu,message,0)==6 && mem_read32_cia(&cpu,message+4,0)==0x12345678);
    CHECK(!wii_ios_native_stm_event(&ios,&cpu,1));
    CHECK(wii_ios_native_stm_event(&ios,&cpu,0x20000));
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && ipc.y==1 && !ios.event_waiting);
    CHECK(mem_read32_cia(&cpu,0x10000400,0)==0x20000 && mem_read32_cia(&cpu,message,0)==8);
    CHECK(mem_read32_cia(&cpu,message+8,0)==6 && ios.operations==2);
    /* Slot illumination changes real service state. Disabling it clears the
     * existing GPIO light while preserving other pins and output sentinels. */
    wii_ios_native_init(&ios);wii_ipc_init(&ipc);
    u32 gpio=0x320;ios.gpio_out=&gpio;ios.descriptors[0]=1;
    mem_write32_cia(&cpu,message,6,0);mem_write32_cia(&cpu,message+8,0,0);
    mem_write32_cia(&cpu,message+12,0x6002,0);
    mem_write32_cia(&cpu,message+16,0x10000400,0);mem_write32_cia(&cpu,message+20,32,0);
    mem_write32_cia(&cpu,message+24,0x10000440,0);mem_write32_cia(&cpu,message+28,32,0);
    mem_write32_cia(&cpu,0x10000400,2,0);mem_write32_cia(&cpu,0x10000440,0x12345678,0);
    wii_ipc_write(&ipc,0,message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && !ios.result && ios.led_mode==2 && gpio==0x320);
    ios.phase=0;wii_ipc_init(&ipc);mem_write32_cia(&cpu,0x10000400,0,0);
    wii_ipc_write(&ipc,0,message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && !ios.result && !ios.led_mode && gpio==0x300);
    CHECK(mem_read32_cia(&cpu,0x10000440,0)==0x12345678);
    ios.phase=0;wii_ipc_init(&ipc);mem_write32_cia(&cpu,message+20,3,0);
    wii_ipc_write(&ipc,0,message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && ios.result==-4 && !ios.led_mode && gpio==0x300);
    /* Offline KD scripts report the real disabled status, not successful
     * script activation. Invalid modes and short buffers remain errors. */
    wii_ios_native_init(&ios);wii_ipc_init(&ipc);ios.descriptors[0]=7;
    mem_write32_cia(&cpu,message+12,0x22,0);mem_write32_cia(&cpu,message+20,4,0);
    mem_write32_cia(&cpu,message+28,4,0);mem_write32_cia(&cpu,0x10000400,1,0);
    mem_write32_cia(&cpu,0x10000440,0x12345678,0);
    mem_write32_cia(&cpu,0x10000444,0xabcdef01,0);
    wii_ipc_write(&ipc,0,message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && !ios.result);
    CHECK((s32)mem_read32_cia(&cpu,0x10000440,0)==-39 && mem_read32_cia(&cpu,0x10000444,0)==0xabcdef01);
    ios.phase=0;wii_ipc_init(&ipc);mem_write32_cia(&cpu,0x10000400,2,0);
    wii_ipc_write(&ipc,0,message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && !ios.result && (s32)mem_read32_cia(&cpu,0x10000440,0)==-3);
    ios.phase=0;wii_ipc_init(&ipc);mem_write32_cia(&cpu,message+20,3,0);
    mem_write32_cia(&cpu,0x10000440,0x12345678,0);
    wii_ipc_write(&ipc,0,message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && ios.result==-4 && mem_read32_cia(&cpu,0x10000440,0)==0x12345678);
    ios.phase=0;wii_ipc_init(&ipc);mem_write32_cia(&cpu,message+12,1,0);
    mem_write32_cia(&cpu,message+20,0,0);mem_write32_cia(&cpu,message+28,32,0);
    wii_ipc_write(&ipc,0,message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && !ios.result && (s32)mem_read32_cia(&cpu,0x10000440,0)==-39);
    CHECK(mem_read32_cia(&cpu,0x10000444,0)==0xabcdef01);
    /* KD's RTC synchronization retains the guest's actual counter and epoch;
     * requesting the unimplemented persistent update must fail before ACK. */
    wii_ios_native_init(&ios);wii_ipc_init(&ipc);ios.descriptors[0]=8;
    mem_write32_cia(&cpu,message+12,0x17,0);mem_write32_cia(&cpu,message+20,8,0);
    mem_write32_cia(&cpu,message+28,4,0);mem_write32_cia(&cpu,0x10000400,1234,0);
    mem_write32_cia(&cpu,0x10000404,0,0);
    wii_ipc_write(&ipc,0,message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && !ios.result && ios.kd_rtc_set);
    CHECK(ios.kd_rtc==1234 && ios.kd_rtc_cycles==cpu.cycles && !mem_read32_cia(&cpu,0x10000440,0));
    ios.phase=0;wii_ipc_init(&ipc);mem_write32_cia(&cpu,0x10000404,1,0);
    mem_write32_cia(&cpu,message+4,0x11223344,0);
    wii_ipc_write(&ipc,0,message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
    CHECK(!wii_ios_native_tick(&ios,&ipc,&cpu) && ipc.x==1 && !ipc.y);
    CHECK(mem_read32_cia(&cpu,message+4,0)==0x11223344 && ios.kd_rtc==1234);
    /* The offline NCD profile reports a disconnected link and bounds the
     * complete output vector. Other NCD operations remain unsupported. */
    wii_ios_native_init(&ios);wii_ipc_init(&ipc);ios.descriptors[0]=10;
    mem_write32_cia(&cpu,message,7,0);mem_write32_cia(&cpu,message+8,0,0);mem_write32_cia(&cpu,message+12,7,0);
    mem_write32_cia(&cpu,message+16,0,0);mem_write32_cia(&cpu,message+20,1,0);mem_write32_cia(&cpu,message+24,0x1000,0);
    mem_write32_cia(&cpu,0x1000,0x10000400,0);mem_write32_cia(&cpu,0x1004,8,0);
    for(unsigned n=0;n<3;n++)mem_write32_cia(&cpu,0x10000400+4*n,0x11223344,0);
    wii_ipc_write(&ipc,0,message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && !ios.result);
    CHECK(!mem_read32_cia(&cpu,0x10000400,0) && mem_read32_cia(&cpu,0x10000404,0)==2 && mem_read32_cia(&cpu,0x10000408,0)==0x11223344);
    ios.phase=0;wii_ipc_init(&ipc);mem_write32_cia(&cpu,0x1004,7,0);mem_write32_cia(&cpu,0x10000400,0x11223344,0);
    wii_ipc_write(&ipc,0,message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && ios.result==-4 && mem_read32_cia(&cpu,0x10000400,0)==0x11223344);
    wii_ios_native_init(&ios);wii_ipc_init(&ipc);ios.descriptors[0]=1;
    mem_write32_cia(&cpu,message,6,0);
    mem_write32_cia(&cpu,message+16,0x10000400,0);mem_write32_cia(&cpu,message+24,0x10000440,0);
    WiiHollywood hollywood;wii_hollywood_init(&hollywood);ios.hollywood=&hollywood;
    hollywood.video_regs[0]=8;hollywood.video_regs[1]=0x12340000;hollywood.video_regs[2]=0x56780000;
    mem_write32_cia(&cpu,message+12,0x5001,0);mem_write32_cia(&cpu,message+20,32,0);mem_write32_cia(&cpu,message+28,32,0);
    for(unsigned n=0;n<8;n++)mem_write32_cia(&cpu,0x10000400+n*4,0,0);
    mem_write32_cia(&cpu,0x10000414,0xffffffff,0);mem_write32_cia(&cpu,0x10000418,0xffff0000,0);
    wii_ipc_write(&ipc,0,message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && !ios.result && !hollywood.video_regs[0]);
    CHECK(mem_read32_cia(&cpu,0x10000440,0)==8 && mem_read32_cia(&cpu,0x10000444,0)==0x12340000);
    CHECK(mem_read32_cia(&cpu,0x10000448,0)==0x56780000 && mem_read32_cia(&cpu,0x1000044c,0)==5);
    ios.phase=0;wii_ipc_init(&ipc);mem_write32_cia(&cpu,0x10000400,0x80,0);
    wii_ipc_write(&ipc,0,message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
    CHECK(!wii_ios_native_tick(&ios,&ipc,&cpu) && ipc.x==1 && !ipc.y && !hollywood.video_regs[0]);
    cpu_free(&cpu);return 0;
}
