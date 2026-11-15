/* SPDX-License-Identifier: MIT */
#define main discover_program_main
#define wii_gx_start fixture_gx_start
#define wii_gx_stop fixture_gx_stop
#define wii_fs_mount fixture_fs_mount
#define wii_fs_unmount fixture_fs_unmount
#include "../src/discover.c"
#undef main
#undef wii_gx_start
#undef wii_gx_stop
#undef wii_fs_mount
#undef wii_fs_unmount
#include "wiillr/pointer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
static unsigned polls,starts,stops,mounts;
static bool pointer_active;
static int drain(void) {return 1;}
static void status(GcnllrNativeGxStatus* out) {
    memset(out,0,sizeof *out);
    if(pointer_active) {
        out->pointer.valid=1;out->pointer.x=16384;out->pointer.y=49152;out->pointer.buttons=1;
    }
}
static void input(void* user,GcnllrPad* pad) {
    (void)user;++polls;
    *pad=(GcnllrPad){.buttons=0x100,.stick_x=128,.stick_y=128,.substick_x=128,.substick_y=128};
}
/* Exercise the real bootstrap without opening a host window/device or NAND.
 * Only those external resources are replaced; CPU reset, boot-memory/DOL
 * loading, machine reset, Bluetooth and subsequent input polling are real. */
WiiFs* fixture_fs_mount(const char* profile,u32 uid,u16 gid) {
    if(strcmp(profile,"synthetic-profile") || uid!=0x1234 || gid!=0x5678)return NULL;
    ++mounts;return (WiiFs*)(uintptr_t)1;
}
void fixture_fs_unmount(WiiFs* fs) {(void)fs;}
bool fixture_gx_start(WiiGx* gx,WiiMachine* machine,CPUState* cpu,
    const char* library,const char* data,const char* cache) {
    (void)library;(void)data;(void)cache;++starts;
    static const GcnllrNativeGxApi api={.status=status,.drain=drain};
    memset(gx,0,sizeof *gx);
    gx->machine=machine;gx->cpu=cpu;gx->api=&api;gx->visible=true;
    gx->scripted_input=getenv("WIILLR_INPUT_SCRIPT")!=NULL;
    gx->host_input=input;gx->input=(WiiBluetoothInput){0,{512,512,616},512,280,false,true,false};
    gx->scanouts=machine->fields;
    machine->ios.bluetooth=wii_bluetooth_create(cpu,&machine->gpio_out,NULL);
    return machine->ios.bluetooth!=NULL;
}
void fixture_gx_stop(WiiGx* gx) {++stops;gx->api=NULL;}
static int title_handoff_input(bool scripted) {
    static CPUState cpu;static WiiMachine machine;WiiGx gx={0};
    CHECK(cpu_init(&cpu) && cpu_alloc_mem2(&cpu,WII_MEM2_SIZE));
    cpu.cycles=65536;cpu.timebase=1234;wii_machine_init(&machine,&cpu);
    machine.exi.rtc=(GcnRtc){.counter=0x12345678,.anchor_cycles=12345,.running=scripted?1:0};
    CHECK(fixture_gx_start(&gx,&machine,&cpu,"synthetic-gx",NULL,NULL));
    gx.scripted_input=scripted;gx.input_sent=true;pointer_active=true;
    u8* image=calloc(1,0x120);CHECK(image);
    write_be32(image,0x100);write_be32(image+0x48,0x80003400);
    write_be32(image+0x90,0x20);write_be32(image+0xe0,0x3400);
    memset(image+0x100,0xa5,0x20);
    machine.ios.launch=(WiiTitleLaunch){0x0001000141424344ull,0x100000025ull,0x1234,0x120,0x5678,image};
    machine.ios.launch_pending=true;
    const unsigned before=polls,old_starts=starts,old_stops=stops,old_mounts=mounts;
    CHECK(bootstrap_launch(&cpu,&machine,&gx,"synthetic-profile","synthetic-gx",NULL,NULL,NULL,NULL));
    CHECK(starts==old_starts+1 && stops==old_stops+1 && mounts==old_mounts+1);
    CHECK(cpu.pc==0x3400 && cpu.cycles==65536 && cpu.timebase==1234 && cpu.ram[0x3400]==0xa5);
    CHECK(machine.exi.rtc.counter==0x12345678 && machine.exi.rtc.anchor_cycles==12345 &&
        machine.exi.rtc.running==(scripted?1:0));
    CHECK(!machine.ios.launch_pending && !machine.ios.launch.image);
    CHECK(gx.scripted_input==scripted && !gx.input_sent);
    cpu.cycles+=16384;CHECK(wii_gx_tick(&gx));
    CHECK(polls==before+(scripted?0:1));
    if(!scripted)CHECK(gx.input_sent && gx.input.pointing && (gx.input.buttons&0x800));
    else {
        gx.scripted_input=false;cpu.cycles+=16384;CHECK(wii_gx_tick(&gx));
        CHECK(polls==before+1 && gx.input_sent && gx.input.pointing && (gx.input.buttons&0x800));
    }
    const WiiPointerCamera point=wii_pointer_camera(16384,49152);
    CHECK(gx.input.pointer_x==point.x && gx.input.pointer_y==point.y);
    wii_ios_native_destroy(&machine.ios);cpu_free(&cpu);pointer_active=false;
    return 0;
}
int main(void) {
    CPUState cpu;WiiMachine machine;
    CHECK(cpu_init(&cpu));wii_machine_init(&machine,&cpu);
    machine.ios.bluetooth=wii_bluetooth_create(&cpu,&machine.gpio_out,NULL);
    CHECK(machine.ios.bluetooth);
    const GcnllrNativeGxApi api={.status=status};
    WiiGx gx={.api=&api,.machine=&machine,.cpu=&cpu,.visible=true,
        .title=0x0001000157523945ull,.host_input=input,.scripted_input=true};
#ifdef _WIN32
    CHECK(!_putenv_s("WIILLR_INPUT_SCRIPT","startup-journal"));
#else
    CHECK(!setenv("WIILLR_INPUT_SCRIPT","startup-journal",1));
#endif
    cpu.cycles=16384;CHECK(wii_gx_tick(&gx));CHECK(!polls);
    /* Review releases ownership while the startup environment still exists. */
    gx.scripted_input=false;cpu.cycles+=16384;CHECK(wii_gx_tick(&gx));
    CHECK(polls==1 && gx.input_sent && gx.input.buttons==0x100);
    cpu.cycles+=16384;CHECK(wii_gx_tick(&gx));CHECK(polls==1);
    /* A new automated setup must suppress physical input again. */
    gx.scripted_input=true;machine.fields=1;cpu.cycles+=16384;
    CHECK(wii_gx_tick(&gx));CHECK(polls==1);
    gx.scripted_input=false;cpu.cycles+=16384;CHECK(wii_gx_tick(&gx));CHECK(polls==2);
    wii_bluetooth_destroy(machine.ios.bluetooth);cpu_free(&cpu);
    CHECK(!title_handoff_input(false));
    CHECK(!title_handoff_input(true));
    puts("review input: scripted isolation, physical handoff and title restart PASS");return 0;
}
