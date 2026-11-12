/* SPDX-License-Identifier: MIT */
#include "wiillr/gx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
static unsigned polls;
static void status(GcnllrNativeGxStatus* out) {memset(out,0,sizeof *out);}
static void input(void* user,GcnllrPad* pad) {
    (void)user;++polls;
    *pad=(GcnllrPad){.buttons=0x100,.stick_x=128,.stick_y=128,.substick_x=128,.substick_y=128};
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
    puts("review input: scripted isolation and physical handoff PASS");return 0;
}
