/* SPDX-License-Identifier: MIT */
#include "wiillr/keyboard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#ifndef KEYBOARD_WIRE_ONLY
#include "wiillr/ios.h"
#include "wiillr/gx.h"
static void consume(WiiIpc* ipc,u32 bits,u64 cycles) {
    wii_ipc_write(ipc,4,bits,cycles);wii_ipc_write(ipc,0x30,0x40000000u,cycles);
}
static void request(WiiIosNative* ios,WiiIpc* ipc,CPUState* cpu,u32 command,u32 fd,u32 size) {
    const u32 message=command==2?0x10000280:0x10000200;
    for(unsigned n=0;n<8;n++)mem_write32_cia(cpu,message+4*n,0,cpu->pc);
    mem_write32_cia(cpu,message,command,cpu->pc);mem_write32_cia(cpu,message+8,fd,cpu->pc);
    mem_write32_cia(cpu,message+12,0x800,cpu->pc);
    if(command==4){mem_write32_cia(cpu,message+12,0x900,cpu->pc);mem_write32_cia(cpu,message+16,size,cpu->pc);}
    mem_write32_cia(cpu,message+24,0x10000400,cpu->pc);mem_write32_cia(cpu,message+28,size,cpu->pc);
    wii_ipc_write(ipc,0,message,cpu->cycles);wii_ipc_write(ipc,4,0x31,cpu->cycles);
    CHECK(wii_ios_native_tick(ios,ipc,cpu));cpu->cycles+=6000;
    CHECK(wii_ios_native_tick(ios,ipc,cpu) && ipc->y==2);consume(ipc,0x32,cpu->cycles);
    cpu->cycles+=6000;CHECK(wii_ios_native_tick(ios,ipc,cpu));
}
static void ios_cases(void) {
    CPUState cpu;CHECK(cpu_init(&cpu) && cpu_alloc_mem2(&cpu,0x4000000));
    WiiIosNative ios;WiiIpc ipc;wii_ios_native_init(&ios);wii_ipc_init(&ipc);
    memcpy(cpu.ram+0x800,"/dev/usb/kbd",sizeof "/dev/usb/kbd");
    request(&ios,&ipc,&cpu,1,0,0);CHECK(ipc.y==1 && ios.descriptors[0]==11);consume(&ipc,0x34,cpu.cycles);
    memcpy(cpu.ram+0x900,(const u8[5]){0,0,0,0,0x80},5);
    request(&ios,&ipc,&cpu,4,0,5);CHECK(ipc.y==1 && !mem_read32_cia(&cpu,0x10000204,0));consume(&ipc,0x34,cpu.cycles);
    request(&ios,&ipc,&cpu,4,0,4);CHECK(ipc.y==1 && (s32)mem_read32_cia(&cpu,0x10000204,0)==-4);consume(&ipc,0x34,cpu.cycles);
    mem_write32_cia(&cpu,0x10000400,0xabcdef01,0);request(&ios,&ipc,&cpu,6,0,16);
    CHECK(!ipc.y && ios.keyboard_messages[0] && mem_read32_cia(&cpu,0x10000400,0)==0xabcdef01);
    const WiiKeyboardReport shifted_a={true,2,{4,0,0,0,0,0}};
    CHECK(wii_ios_native_keyboard_input(&ios,&shifted_a));CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));
    CHECK(ipc.y==1 && mem_read32_cia(&cpu,0x10000400,0)==0 && !ios.keyboard_messages[0]);consume(&ipc,0x34,cpu.cycles);
    request(&ios,&ipc,&cpu,6,0,16);CHECK(ipc.y==1 && mem_read32_cia(&cpu,0x10000400,0)==2);
    CHECK(mem_read8_cia(&cpu,0x10000408,0)==2 && mem_read8_cia(&cpu,0x1000040a,0)==4);consume(&ipc,0x34,cpu.cycles);
    for(unsigned n=0;n<100;n++)CHECK(wii_ios_native_keyboard_input(&ios,&shifted_a));
    request(&ios,&ipc,&cpu,6,0,16);CHECK(!ipc.y); /* no held-key/connect storm */
    const WiiKeyboardReport released={true,0,{0}};
    CHECK(wii_ios_native_keyboard_input(&ios,&released));CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && ipc.y==1);
    CHECK(mem_read32_cia(&cpu,0x10000400,0)==2 && !mem_read32_cia(&cpu,0x10000408,0));consume(&ipc,0x34,cpu.cycles);
    request(&ios,&ipc,&cpu,6,0,15);CHECK(ipc.y==1 && (s32)mem_read32_cia(&cpu,0x10000204,0)==-4);consume(&ipc,0x34,cpu.cycles);
    request(&ios,&ipc,&cpu,6,0,16);CHECK(!ipc.y);
    /* Closing a pending read retires that real request before the close reply. */
    request(&ios,&ipc,&cpu,2,0,0);CHECK(ipc.y==1 && (s32)mem_read32_cia(&cpu,0x10000204,0)==-4);
    consume(&ipc,0x34,cpu.cycles);CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && ipc.y==1 && !ios.descriptors[0]);
    CHECK(mem_read32_cia(&cpu,0x10000280,0)==8 && !mem_read32_cia(&cpu,0x10000284,0));
    consume(&ipc,0x34,cpu.cycles);
    wii_ios_native_destroy(&ios);wii_ios_native_init(&ios);wii_ipc_init(&ipc);
    request(&ios,&ipc,&cpu,1,0,0);consume(&ipc,0x34,cpu.cycles);
    request(&ios,&ipc,&cpu,6,0,16);CHECK(!ipc.y); /* no event leak across handoff */
    wii_ios_native_destroy(&ios);cpu_free(&cpu);
}
typedef struct {bool focused;uint8_t key;} HostKeys;
static void host_keys(void* user,GcnllrPad* pad) {
    (void)pad;const HostKeys* host=user;bool keys[256]={0};keys[host->key]=true;
    wii_keyboard_host_keys(host->focused?keys:NULL,host->focused?256:0,true);
}
static void status(GcnllrNativeGxStatus* out) {
    memset(out,0,sizeof *out);out->pad.stick_x=out->pad.stick_y=out->pad.substick_x=out->pad.substick_y=128;
}
static void service_gx(WiiGx* gx) {gx->cpu->cycles+=16384;CHECK(wii_gx_tick(gx) && !gx->cpu->halted);}
static void gx_backpressure_cases(void) {
    CPUState cpu;CHECK(cpu_init(&cpu) && cpu_alloc_mem2(&cpu,0x4000000));
    WiiMachine* machine=calloc(1,sizeof *machine);CHECK(machine);
    WiiIosNative* ios=&machine->ios;WiiIpc* ipc=&machine->ipc;wii_ios_native_init(ios);wii_ipc_init(ipc);
    memcpy(cpu.ram+0x800,"/dev/usb/kbd",sizeof "/dev/usb/kbd");
    request(ios,ipc,&cpu,1,0,0);consume(ipc,0x34,cpu.cycles);
    HostKeys host={true,4};const GcnllrNativeGxApi api={.status=status};
    WiiGx gx={.api=&api,.machine=machine,.cpu=&cpu,.visible=true,.host_input=host_keys,.host_input_user=&host};
    service_gx(&gx);
    for(unsigned n=1;n<=41;n++){host.key=(uint8_t)(4+n%2);service_gx(&gx);}
    const WiiKeyboardReport last={true,0,{5}};
    CHECK(wii_ios_native_keyboard_input(ios,&last)==0); /* actual caller filled bounded queue */
    host.focused=false;service_gx(&gx); /* latest held-key release retained while full */
    gx.visible=false;service_gx(&gx); /* latest disconnect replaces pending state, never old events */
    unsigned events=0;u32 type;
    do {
        request(ios,ipc,&cpu,6,0,16);CHECK(ipc->y==1);
        type=mem_read32_cia(&cpu,0x10000400,0);
        CHECK(type==(events?events==32?1u:2u:0u));consume(ipc,0x34,cpu.cycles);++events;
    }while(type!=1 && events<40);
    CHECK(type==1 && events==33); /* all32 queued events, then retained latest disconnect */
    gx.visible=true;service_gx(&gx);request(ios,ipc,&cpu,6,0,16);
    CHECK(ipc->y==1 && mem_read32_cia(&cpu,0x10000400,0)==0);consume(ipc,0x34,cpu.cycles);
    host.focused=true;service_gx(&gx);request(ios,ipc,&cpu,6,0,16);
    CHECK(ipc->y==1 && mem_read32_cia(&cpu,0x10000400,0)==2 && mem_read8_cia(&cpu,0x1000040a,0)==5);consume(ipc,0x34,cpu.cycles);
    host.focused=false;service_gx(&gx);request(ios,ipc,&cpu,6,0,16);
    CHECK(ipc->y==1 && mem_read32_cia(&cpu,0x10000400,0)==2 && !mem_read32_cia(&cpu,0x10000408,0));consume(ipc,0x34,cpu.cycles);
    gx.scripted_input=true;service_gx(&gx);request(ios,ipc,&cpu,6,0,16);
    CHECK(ipc->y==1 && mem_read32_cia(&cpu,0x10000400,0)==1);consume(ipc,0x34,cpu.cycles);
    for(unsigned n=0;n<40;n++)service_gx(&gx);
    request(ios,ipc,&cpu,6,0,16);CHECK(!ipc->y); /* no disconnect storm */
    CHECK(wii_ios_native_keyboard_input(NULL,&last)<0 && wii_ios_native_keyboard_input(ios,NULL)<0);
    wii_ios_native_destroy(ios);free(machine);cpu_free(&cpu);wii_keyboard_host_reset();
}
#endif
int main(void) {
    WiiKeyboard keyboard={0};uint8_t event[16];const WiiKeyboardReport absent={0},connected={true,0,{0}},a={true,2,{4}};
    CHECK(wii_keyboard_update(&keyboard,&absent) && !wii_keyboard_pop(&keyboard,event));
    CHECK(wii_keyboard_update(&keyboard,&a) && wii_keyboard_pop(&keyboard,event));CHECK(!event[3]);
    CHECK(wii_keyboard_pop(&keyboard,event) && event[3]==2 && event[8]==2 && event[10]==4);
    CHECK(!wii_keyboard_pop(&keyboard,event));CHECK(wii_keyboard_update(&keyboard,&a) && !keyboard.count);
    CHECK(wii_keyboard_update(&keyboard,&connected) && wii_keyboard_pop(&keyboard,event));CHECK(event[3]==2 && !event[8] && !event[10]);
    for(unsigned n=0;n<32;n++){WiiKeyboardReport r={true,0,{(uint8_t)(4+n%2)}};CHECK(wii_keyboard_update(&keyboard,&r));}
    WiiKeyboard before=keyboard;CHECK(!wii_keyboard_update(&keyboard,&absent) && !memcmp(&before,&keyboard,sizeof before));
    WiiKeyboard invalid={.head=32,.count=1};CHECK(!wii_keyboard_update(&invalid,&a));
    memset(event,0xa5,sizeof event);WiiKeyboard invalid_before=invalid;
    CHECK(!wii_keyboard_pop(&invalid,event) && !memcmp(&invalid,&invalid_before,sizeof invalid));
    for(unsigned n=0;n<16;n++)CHECK(event[n]==0xa5);
    invalid=(WiiKeyboard){.count=33};invalid_before=invalid;
    CHECK(!wii_keyboard_pop(&invalid,event) && !memcmp(&invalid,&invalid_before,sizeof invalid));
    for(unsigned n=0;n<16;n++)CHECK(event[n]==0xa5);
    while(wii_keyboard_pop(&keyboard,event)){}CHECK(wii_keyboard_update(&keyboard,&absent));CHECK(wii_keyboard_pop(&keyboard,event) && event[3]==1);
    bool keys[256]={0};WiiKeyboardReport sample;
    wii_keyboard_host_begin(true);keys[4]=keys[225]=true;wii_keyboard_host_keys(keys,256,true);CHECK(wii_keyboard_host_captured());
    wii_keyboard_host_end(true,&sample);CHECK(sample.connected && sample.modifiers==2 && sample.keys[0]==4);
    wii_keyboard_host_begin(false);wii_keyboard_host_keys(keys,256,true);CHECK(wii_keyboard_host_captured()); /* held through Back */
    memset(keys,0,sizeof keys);wii_keyboard_host_keys(keys,256,true);CHECK(!wii_keyboard_host_captured());
    for(unsigned n=4;n<11;n++)keys[n]=true;wii_keyboard_host_keys(keys,256,true);wii_keyboard_host_end(true,&sample);
    for(unsigned n=0;n<6;n++)CHECK(sample.keys[n]==1);
    wii_keyboard_host_begin(false);wii_keyboard_host_keys(NULL,0,true);wii_keyboard_host_end(true,&sample);CHECK(sample.connected && !sample.keys[0]); /* focus loss release */
    wii_keyboard_host_begin(false);wii_keyboard_host_keys(keys,256,true);wii_keyboard_host_end(false,&sample);CHECK(!sample.connected); /* host settings own input */
    wii_keyboard_host_begin(false);wii_keyboard_host_end(true,&sample);CHECK(!sample.connected); /* portable/absent publisher */
    wii_keyboard_host_reset();CHECK(!wii_keyboard_host_captured());
#ifndef KEYBOARD_WIRE_ONLY
    ios_cases();
    gx_backpressure_cases();
#endif
    puts("USB keyboard wire, focus/release/arbitration, queue and IOS lifecycle PASS");return 0;
}
