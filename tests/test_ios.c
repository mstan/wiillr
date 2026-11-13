/* SPDX-License-Identifier: MIT */
#include "wiillr/ios.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do {if(!(x)) {fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}} while(0)
static void consume_mailbox(WiiIpc* ipc,u32 control,u64 cycles) {
    wii_ipc_write(ipc,4,control,cycles);
    wii_ipc_write(ipc,0x30,0x40000000u,cycles); /* receiver completes its ISR */
}
typedef struct {bool enabled,reject;u32 calls;} VideoTest;
static bool set_idle_dimming(void* user,bool enabled) {
    VideoTest* video=user;++video->calls;if(video->reject)return false;
    video->enabled=enabled;return true;
}
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
    CHECK(ipc.arm_message==0); /* an ACK has no reply pointer */
    cpu.cycles=12000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && mem_read32_cia(&cpu,message,0)==1);
    wii_ipc_write(&ipc,4,0x32,cpu.cycles); /* consume ACK, ISR still active */
    CHECK(!wii_ipc_ready(&ipc) && wii_ios_native_tick(&ios,&ipc,&cpu));
    CHECK(!ipc.y && mem_read32_cia(&cpu,message,0)==1);
    wii_ipc_write(&ipc,0x30,0x40000000u,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && ipc.y==1 && ios.operations==1);
    CHECK(mem_read32_cia(&cpu,message,0)==8 && mem_read32_cia(&cpu,message+4,0)==0);
    CHECK(mem_read32_cia(&cpu,message+8,0)==1 && ipc.arm_message==message);
    consume_mailbox(&ipc,0x34,cpu.cycles);
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
    cpu.cycles+=6000;consume_mailbox(&ipc,0x32,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));consume_mailbox(&ipc,0x34,cpu.cycles);
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
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));consume_mailbox(&ipc,0x32,cpu.cycles);
    cpu.cycles+=6000;CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));
    consume_mailbox(&ipc,0x34,cpu.cycles);
    mem_write32_cia(&cpu,message,6,0);mem_write32_cia(&cpu,message+4,0x12345678,0);
    mem_write32_cia(&cpu,message+8,0,0);mem_write32_cia(&cpu,message+12,0x1000,0);
    mem_write32_cia(&cpu,message+24,0x10000400,0);mem_write32_cia(&cpu,message+28,4,0);
    wii_ipc_write(&ipc,4,0x31,cpu.cycles);CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));
    cpu.cycles+=6000;CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));
    consume_mailbox(&ipc,0x32,cpu.cycles);cpu.cycles+=60000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && !ipc.y && ios.event_waiting);
    CHECK(mem_read32_cia(&cpu,message,0)==6 && mem_read32_cia(&cpu,message+4,0)==0x12345678);
    CHECK(!wii_ios_native_stm_event(&ios,&cpu,1));
    CHECK(wii_ios_native_stm_event(&ios,&cpu,0x20000));
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && ipc.y==1 && !ios.event_waiting);
    CHECK(mem_read32_cia(&cpu,0x10000400,0)==0x20000 && mem_read32_cia(&cpu,message,0)==8);
    CHECK(mem_read32_cia(&cpu,message+8,0)==6 && ios.operations==2);
    /* Release completes the pending hook before the release request. The
     * same operation without an installed hook returns IPC_ENOENT. */
    consume_mailbox(&ipc,0x34,cpu.cycles);
    mem_write32_cia(&cpu,message,6,0);mem_write32_cia(&cpu,message+8,0,0);
    wii_ipc_write(&ipc,4,0x31,cpu.cycles);CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));
    cpu.cycles+=6000;CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && ipc.y==2 && ios.event_waiting);
    consume_mailbox(&ipc,0x32,cpu.cycles);
    const u32 release=message+64;
    ios.descriptors[1]=1;
    for(unsigned n=0;n<2;n++) {
        mem_write32_cia(&cpu,release,6,0);mem_write32_cia(&cpu,release+8,1,0);
        mem_write32_cia(&cpu,release+12,0x3002,0);
        wii_ipc_write(&ipc,0,release,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
        CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
        CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && ipc.y==2);
        consume_mailbox(&ipc,0x32,cpu.cycles);cpu.cycles+=6000;
        CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));
        if(!n) {
            CHECK(ipc.y==1 && ipc.arm_message==message && !ios.event_waiting);
            CHECK(!mem_read32_cia(&cpu,0x10000400,0) && !mem_read32_cia(&cpu,message+4,0));
            CHECK(mem_read32_cia(&cpu,release,0)==6);
            consume_mailbox(&ipc,0x34,cpu.cycles);
            CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));
        }
        CHECK(ipc.y==1 && ipc.arm_message==release && (s32)mem_read32_cia(&cpu,release+4,0)==(n ? -6:0));
        consume_mailbox(&ipc,0x34,cpu.cycles);
    }
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
    ios.phase=0;wii_ipc_init(&ipc);mem_write32_cia(&cpu,0x10000400,246,0);
    wii_ipc_write(&ipc,0,message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && ios.result==-4 && ios.led_mode==2 && gpio==0x320);
    CHECK(mem_read32_cia(&cpu,0x10000440,0)==0x12345678);
    ios.phase=0;wii_ipc_init(&ipc);mem_write32_cia(&cpu,0x10000400,0,0);
    wii_ipc_write(&ipc,0,message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && !ios.result && !ios.led_mode && gpio==0x300);
    CHECK(mem_read32_cia(&cpu,0x10000440,0)==0x12345678);
    ios.phase=0;wii_ipc_init(&ipc);mem_write32_cia(&cpu,message+20,3,0);
    wii_ipc_write(&ipc,0,message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && ios.result==-4 && !ios.led_mode && gpio==0x300);
    /* Real asynchronous shutdown preparation accepts the original event4
     * packet without fabricating a service-result word or resetting PPC.
     * Invalid declared extents cannot advertise an event or damage output. */
    wii_ios_native_init(&ios);ios.descriptors[0]=7;ios.kd_suspend_count=2;
    for(unsigned n=0;n<9;n++) {
        wii_ipc_init(&ipc);ios.phase=0;
        ios.kd_shutdown_requested=false;ios.kd_shutdown_event=0x1234;
        mem_write32_cia(&cpu,message,6,0);mem_write32_cia(&cpu,message+8,0,0);
        mem_write32_cia(&cpu,message+12,0x28,0);
        mem_write32_cia(&cpu,message+16,0x10000400,0);mem_write32_cia(&cpu,message+20,32,0);
        mem_write32_cia(&cpu,message+24,0x10000440,0);mem_write32_cia(&cpu,message+28,32,0);
        mem_write32_cia(&cpu,0x10000400,n==8 ? 3:4,0);
        memset(cpu.mem2+0x440,0x55,32);
        if(n==1)mem_write32_cia(&cpu,message+20,3,0);
        if(n==2)mem_write32_cia(&cpu,message+16,0x10000401,0);
        if(n==3)mem_write32_cia(&cpu,message+16,0x13fffffc,0);
        if(n==4)mem_write32_cia(&cpu,message+24,0,0);
        if(n==5)mem_write32_cia(&cpu,message+24,0x10000441,0);
        if(n==6)mem_write32_cia(&cpu,message+28,0xffffffff,0);
        if(n==7){mem_write32_cia(&cpu,message+24,0,0);mem_write32_cia(&cpu,message+28,0,0);}
        wii_ipc_write(&ipc,0,message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
        CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
        CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && ios.result==((n>0 && n<7) ? -4:0));
        const bool valid=n==0 || n>=7;
        CHECK(ios.kd_shutdown_requested==valid && ios.kd_shutdown_event==(valid ? (n==8 ? 3:4):0x1234));
        CHECK(ios.kd_suspend_count==2 && !cpu.halted);
        for(unsigned i=0;i<32;i++)CHECK(cpu.mem2[0x440+i]==0x55);
    }
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
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && !ios.result && mem_read32_cia(&cpu,0x10000440,0)==1 && ios.kd_suspend_count==1);
    CHECK(mem_read32_cia(&cpu,0x10000444,0)==0xabcdef01);
    ios.phase=0;wii_ipc_init(&ipc);mem_write32_cia(&cpu,message+12,2,0);mem_write32_cia(&cpu,message+28,4,0);
    mem_write32_cia(&cpu,message+20,32,0);mem_write32_cia(&cpu,0x10000400,0,0);
    wii_ipc_write(&ipc,0,message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && !ios.result && mem_read32_cia(&cpu,0x10000440,0)==2 && ios.kd_suspend_count==2);
    CHECK(mem_read32_cia(&cpu,0x10000444,0)==0xabcdef01);
    mem_write32_cia(&cpu,message+28,32,0);
    ios.phase=0;wii_ipc_init(&ipc);mem_write32_cia(&cpu,message+12,3,0);
    mem_write32_cia(&cpu,message+20,0,0);
    mem_write32_cia(&cpu,0x10000440,0x12345678,0);
    wii_ipc_write(&ipc,0,message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && !ios.result && mem_read32_cia(&cpu,0x10000440,0)==1 && ios.kd_suspend_count==1);
    CHECK(mem_read32_cia(&cpu,0x10000444,0)==0xabcdef01);
    ios.phase=0;wii_ipc_init(&ipc);
    wii_ipc_write(&ipc,0,message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && !ios.result && !ios.kd_suspend_count && !mem_read32_cia(&cpu,0x10000440,0));
    ios.phase=0;wii_ipc_init(&ipc);mem_write32_cia(&cpu,message+12,8,0);
    wii_ipc_write(&ipc,0,message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && !ios.result && (s32)mem_read32_cia(&cpu,0x10000440,0)==-39);
    CHECK(mem_read32_cia(&cpu,0x10000444,0)==0xabcdef01 && !ios.kd_suspend_count);
    ios.phase=0;wii_ipc_init(&ipc);mem_write32_cia(&cpu,message+12,0x0d,0);
    wii_ipc_write(&ipc,0,message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && !ios.result && (s32)mem_read32_cia(&cpu,0x10000440,0)==-39);
    CHECK(mem_read32_cia(&cpu,0x10000444,0)==0xabcdef01);
    for(unsigned n=0;n<2;n++) {
        ios.phase=0;wii_ipc_init(&ipc);mem_write32_cia(&cpu,message+12,0x1e,0);
        mem_write32_cia(&cpu,message+28,n ? 260:15,0);
        memset(cpu.mem2+0x440,0x55,260);
        wii_ipc_write(&ipc,0,message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
        CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
        CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && ios.result==(n ? 0:-4));
        for(unsigned i=0;i<260;i++)CHECK(cpu.mem2[0x440+i]==(n && i<256 ? 0:0x55));
    }
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
    ios.phase=0;wii_ipc_init(&ipc);ios.kd_utc=12345;ios.kd_utc_cycles=cpu.cycles;ios.kd_utc_set=true;
    ios.kd_rtc_cycles=cpu.cycles;cpu.cycles+=3*729000000ull;
    mem_write32_cia(&cpu,message+12,0x18,0);mem_write32_cia(&cpu,message+20,0,0);mem_write32_cia(&cpu,message+28,16,0);
    mem_write32_cia(&cpu,0x1000044c,0xabcdef01,0);
    wii_ipc_write(&ipc,0,message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && !ios.result && !mem_read32_cia(&cpu,0x10000440,0));
    CHECK(!mem_read32_cia(&cpu,0x10000444,0) && mem_read32_cia(&cpu,0x10000448,0)==11111);
    CHECK(mem_read32_cia(&cpu,0x1000044c,0)==0xabcdef01);
    ios.phase=0;wii_ipc_init(&ipc);mem_write32_cia(&cpu,message+12,0x14,0);
    wii_ipc_write(&ipc,0,message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && !ios.result && mem_read32_cia(&cpu,0x10000448,0)==12348);
    ios.phase=0;wii_ipc_init(&ipc);mem_write32_cia(&cpu,message+12,0x15,0);
    mem_write32_cia(&cpu,message+20,12,0);mem_write32_cia(&cpu,message+28,4,0);
    mem_write32_cia(&cpu,0x10000400,1,0);mem_write32_cia(&cpu,0x10000404,5,0);mem_write32_cia(&cpu,0x10000408,0,0);
    wii_ipc_write(&ipc,0,message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && !ios.result && ios.kd_utc==0x100000005ull && ios.kd_utc_cycles==cpu.cycles);
    ios.phase=0;wii_ipc_init(&ipc);mem_write32_cia(&cpu,0x10000408,1,0);mem_write32_cia(&cpu,message+4,0x12345678,0);
    wii_ipc_write(&ipc,0,message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
    CHECK(!wii_ios_native_tick(&ios,&ipc,&cpu) && ipc.x==1 && !ipc.y && ios.kd_utc==0x100000005ull);
    CHECK(mem_read32_cia(&cpu,message+4,0)==0x12345678);
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
    /* The original idle request must have a real presentation effect. Missing
     * or failing providers leave the request, register and output untouched. */
    VideoTest video={0};hollywood.video_user=&video;
    for(unsigned n=0;n<4;n++) {
        ios.phase=0;wii_ipc_init(&ipc);
        hollywood.idle_dimming=n?set_idle_dimming:NULL;video.reject=n==1;
        mem_write32_cia(&cpu,0x10000400,n==3?0:0x92,0);
        mem_write32_cia(&cpu,0x10000440,0x11223344,0);
        wii_ipc_write(&ipc,0,message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
        CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
        const bool handled=wii_ios_native_tick(&ios,&ipc,&cpu);
        CHECK(handled==(n>=2));
        if(n<2)CHECK(ipc.x==1 && !ipc.y && !hollywood.video_regs[0] && !video.enabled &&
            mem_read32_cia(&cpu,0x10000440,0)==0x11223344);
        else CHECK(ipc.y==2 && !ios.result && hollywood.video_regs[0]==(n==2?0x92:0) &&
            video.enabled==(n==2) && mem_read32_cia(&cpu,0x10000440,0)==(n==3?0x92:0));
    }
    CHECK(video.calls==3 && !video.enabled);
    CHECK(wii_hollywood_write(&hollywood,0x0d80001c,0x92) && video.enabled);
    u32 dimming=0;CHECK(wii_hollywood_read(&hollywood,0x0d00001c,&dimming) && dimming==0x92);
    CHECK(!wii_hollywood_write(&hollywood,0x0d80001c,0x80) && video.enabled && hollywood.video_regs[0]==0x92);
    CHECK(wii_hollywood_write(&hollywood,0x0d00001c,0) && !video.enabled && video.calls==5);
    /* An empty keyboard bus accepts event reads asynchronously and leaves the
     * caller's message/output untouched while no USB keyboard is attached. */
    wii_ios_native_init(&ios);wii_ipc_init(&ipc);
    memcpy(cpu.ram+path,"/dev/usb/kbd",sizeof "/dev/usb/kbd");
    mem_write32_cia(&cpu,message,1,0);mem_write32_cia(&cpu,message+12,path,0);mem_write32_cia(&cpu,message+16,0,0);
    wii_ipc_write(&ipc,0,message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));consume_mailbox(&ipc,0x32,cpu.cycles);
    cpu.cycles+=6000;CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && ios.descriptors[0]==11);
    consume_mailbox(&ipc,0x34,cpu.cycles);
    mem_write32_cia(&cpu,message,6,0);mem_write32_cia(&cpu,message+4,0x12345678,0);mem_write32_cia(&cpu,message+8,0,0);
    mem_write32_cia(&cpu,message+24,0x10000400,0);mem_write32_cia(&cpu,message+28,16,0);
    mem_write32_cia(&cpu,0x10000400,0xabcdef01,0);
    wii_ipc_write(&ipc,4,0x31,cpu.cycles);CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));consume_mailbox(&ipc,0x32,cpu.cycles);
    cpu.cycles+=60000;CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && !ipc.y && ios.keyboard_messages[0]==message);
    CHECK(mem_read32_cia(&cpu,message,0)==6 && mem_read32_cia(&cpu,message+4,0)==0x12345678);
    CHECK(mem_read32_cia(&cpu,0x10000400,0)==0xabcdef01);
    /* IP control has a real descriptor lifecycle; opening it must not turn
     * an unimplemented network operation into a successful reply. */
    wii_ios_native_init(&ios);wii_ipc_init(&ipc);
    memcpy(cpu.ram+path,"/dev/net/ip/top",sizeof "/dev/net/ip/top");
    mem_write32_cia(&cpu,message,1,0);mem_write32_cia(&cpu,message+12,path,0);mem_write32_cia(&cpu,message+16,0,0);
    wii_ipc_write(&ipc,0,message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && !ios.result && ios.descriptors[0]==12);
    ios.phase=0;wii_ipc_init(&ipc);mem_write32_cia(&cpu,message,6,0);mem_write32_cia(&cpu,message+8,0,0);
    mem_write32_cia(&cpu,message+12,0x1234,0);mem_write32_cia(&cpu,message+4,0x12345678,0);
    wii_ipc_write(&ipc,0,message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
    CHECK(!wii_ios_native_tick(&ios,&ipc,&cpu) && ipc.x==1 && !ipc.y && mem_read32_cia(&cpu,message+4,0)==0x12345678);
    ios.phase=0;wii_ipc_init(&ipc);mem_write32_cia(&cpu,message,2,0);
    wii_ipc_write(&ipc,0,message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && !ios.result && !ios.descriptors[0]);
    /* The SD slot can be queried while physically empty. Both status and
     * reset report no card; a short output cannot modify the buffer. */
    wii_ios_native_init(&ios);wii_ipc_init(&ipc);
    memcpy(cpu.ram+path,"/dev/sdio/slot0",sizeof "/dev/sdio/slot0");
    mem_write32_cia(&cpu,message,1,0);mem_write32_cia(&cpu,message+12,path,0);mem_write32_cia(&cpu,message+16,0,0);
    wii_ipc_write(&ipc,0,message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && !ios.result && ios.descriptors[0]==13);
    for(unsigned n=0;n<3;n++) {
        ios.phase=0;wii_ipc_init(&ipc);
        mem_write32_cia(&cpu,message,6,0);mem_write32_cia(&cpu,message+8,0,0);
        mem_write32_cia(&cpu,message+12,n==1 ? 0x04:0x0b,0);mem_write32_cia(&cpu,message+20,0,0);
        mem_write32_cia(&cpu,message+24,0x10000400,0);mem_write32_cia(&cpu,message+28,n==2 ? 3:8,0);
        mem_write32_cia(&cpu,0x10000400,0x11223344,0);mem_write32_cia(&cpu,0x10000404,0xabcdef01,0);
        wii_ipc_write(&ipc,0,message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
        CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
        CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && ios.result==(n==2 ? -4:0));
        CHECK(mem_read32_cia(&cpu,0x10000400,0)==(n==2 ? 0x11223344:0) && mem_read32_cia(&cpu,0x10000404,0)==0xabcdef01);
    }
    ios.phase=0;wii_ipc_init(&ipc);
    for(unsigned n=0;n<3;n++) {
        const u32 request=message+64*n;
        mem_write32_cia(&cpu,request,6,0);mem_write32_cia(&cpu,request+4,0x11223344,0);
        mem_write32_cia(&cpu,request+8,0,0);mem_write32_cia(&cpu,request+12,7,0);
        mem_write32_cia(&cpu,request+16,0x10000400,0);mem_write32_cia(&cpu,request+20,36,0);
        mem_write32_cia(&cpu,request+24,0,0);mem_write32_cia(&cpu,request+28,0,0);
        mem_write32_cia(&cpu,0x10000400,n==1 ? 0x41:0x40,0);
        mem_write32_cia(&cpu,0x1000040c,n==2 ? 2:1,0);
        wii_ipc_write(&ipc,0,request,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
        CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;
        CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && ipc.y==2);
        consume_mailbox(&ipc,0x32,cpu.cycles);cpu.cycles+=60000;
        CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));
        if(!n) {
            CHECK(!ipc.y && mem_read32_cia(&cpu,message,0)==6 && mem_read32_cia(&cpu,message+4,0)==0x11223344);
            continue; /* no unsolicited insertion */
        }
        if(n==1) {
            CHECK(ipc.y==1 && ipc.arm_message==message && mem_read32_cia(&cpu,message+4,0)==0x0c210000);
            consume_mailbox(&ipc,0x34,cpu.cycles);
            CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));
        }
        CHECK(ipc.y==1 && ipc.arm_message==request && mem_read32_cia(&cpu,request+4,0)==(n==1 ? 0:2));
        consume_mailbox(&ipc,0x34,cpu.cycles);
    }
    /* Submission is an event, not a later sample of X1 or PPCMSG. Retain
     * both pointers even after the caller clears X1 or changes the register. */
    wii_ios_native_init(&ios);wii_ipc_init(&ipc);
    memcpy(cpu.ram+path,"/dev/stm/immediate",sizeof "/dev/stm/immediate");
    const u32 second=message+64;
    for(unsigned n=0;n<2;n++) {
        const u32 request=message+n*64;
        mem_write32_cia(&cpu,request,1,0);mem_write32_cia(&cpu,request+12,path,0);
        mem_write32_cia(&cpu,request+16,0,0);
        CHECK(wii_ios_native_request(&ios,request,cpu.cycles));
    }
    CHECK(wii_ios_native_request_deadline(&ios,cpu.cycles)==6000);
    wii_ipc_write(&ipc,0,message+128,cpu.cycles);wii_ipc_write(&ipc,4,0x30,cpu.cycles);
    cpu.cycles+=5999;CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && !ipc.y);
    ++cpu.cycles;CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && ipc.y==2 && ios.message==message);
    CHECK(!ipc.arm_message && ios.descriptors[0]==1);
    consume_mailbox(&ipc,0x32,cpu.cycles);cpu.cycles+=6000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && ipc.y==1 && ipc.arm_message==message);
    consume_mailbox(&ipc,0x34,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && ipc.y==2 && ios.message==second);
    CHECK(ipc.arm_message==message && ios.descriptors[1]==1);
    consume_mailbox(&ipc,0x32,cpu.cycles);cpu.cycles+=6000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && ipc.y==1 && ipc.arm_message==second);
    consume_mailbox(&ipc,0x34,cpu.cycles);
    CHECK(wii_ios_native_request_deadline(&ios,cpu.cycles)==UINT64_MAX);
    wii_ios_native_init(&ios);
    for(unsigned n=0;n<64;n++)CHECK(wii_ios_native_request(&ios,message+n*64,cpu.cycles));
    CHECK(!wii_ios_native_request(&ios,message,cpu.cycles));
    WiiIosNative other;wii_ios_native_init(&other);
    CHECK(wii_ios_native_request(&other,message,cpu.cycles+100));
    CHECK(wii_ios_native_request_deadline(&other,cpu.cycles)==6100);
    CHECK(wii_ios_native_request_deadline(&ios,cpu.cycles)==6000);
    wii_ios_native_destroy(&other);wii_ios_native_destroy(&ios);
    CHECK(wii_ios_native_request_deadline(&ios,cpu.cycles)==UINT64_MAX);
    cpu_free(&cpu);return 0;
}
