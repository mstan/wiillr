/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../src/gx.c"
#include <assert.h>
static GcnllrPad pads[4];
static unsigned flags[4],polls[4],sends[4],pairs[4],keyboards,begins,ends,legacy;
static WiiBluetoothInput reports[4];
static int service(void){return 1;}
static void status(GcnllrNativeGxStatus* s){memset(s,0,sizeof *s);s->pointer=(GcnllrNativeGxPointer){1,1,16384,49152};}
static unsigned poll_player(void* user,unsigned i,GcnllrPad* out){assert(user==pads && i<4);++polls[i];*out=pads[i];return flags[i];}
static void old_input(void* user,GcnllrPad* out){(void)user;++legacy;*out=pads[0];}
int gcn_vi_xfb_info(u32* a,u32* w,u32* h,u32* s){(void)a;(void)w;(void)h;(void)s;return 0;}
u8* gcn_mem_resolve(CPUState* c,u32 a,u32* available){(void)c;(void)a;*available=0;return NULL;}
int wii_gx_output_present(WiiGx* g,u32 a,u32 w,u32 h,u32 s){(void)g;(void)a;(void)w;(void)h;(void)s;return 0;}
void wii_keyboard_host_reset(void){}
void wii_keyboard_host_begin(bool typing){(void)typing;++begins;}
void wii_keyboard_host_end(bool enabled,WiiKeyboardReport* r){++ends;memset(r,0,sizeof *r);r->connected=enabled;}
int wii_ios_native_keyboard_input(WiiIosNative* ios,const WiiKeyboardReport* r){(void)ios;(void)r;++keyboards;return 1;}
bool wii_bluetooth_set_input_indexed(WiiBluetooth* b,u32 i,const WiiBluetoothInput* input){assert(b && i<4);reports[i]=*input;++sends[i];return true;}
bool wii_bluetooth_set_input(WiiBluetooth* b,const WiiBluetoothInput* input){return wii_bluetooth_set_input_indexed(b,0,input);}
bool wii_bluetooth_pair_remote(WiiBluetooth* b,u32 i){assert(b && i<4);++pairs[i];return true;}
bool wii_bluetooth_press_sync(WiiBluetooth* b){assert(b);return true;}
int main(void) {
    static WiiMachine m;static CPUState c;const GcnllrNativeGxApi api={.service=service,.status=status};
    m.ios.bluetooth=(WiiBluetooth*)(uintptr_t)1;
    WiiGx g={.cpu=&c,.machine=&m,.api=&api,.visible=true,.host_input=old_input};
    g.input=(WiiBluetoothInput){0,{512,512,616},512,384,false,false,false};
    for(unsigned i=0;i<4;++i) {
        flags[i]=3;pads[i].stick_x=pads[i].stick_y=pads[i].substick_x=pads[i].substick_y=128;
        pads[i].buttons=1u<<i;
    }
    pads[2].substick_x=228;
    g.input_sent=true; /* A live launcher can bind after the legacy P1 poll. */
    assert(wii_gx_set_player_input(&g,poll_player,pads));
    c.cycles=20000;m.fields=1;assert(wii_gx_tick(&g));
    assert(begins==1 && ends==1 && keyboards==1 && !legacy);
    for(unsigned i=0;i<4;++i)assert(polls[i]==1 && sends[i]==1 && pairs[i]==1 && reports[i].present);
    assert(reports[0].buttons==(1|0x800) && reports[1].buttons==2 && reports[2].buttons==4 && reports[3].buttons==8);
    assert(reports[0].pointing && !reports[1].pointing && reports[2].pointing && !reports[3].pointing);
    assert(reports[2].pointer_x<512 && reports[1].pointer_x==512 && reports[3].pointer_x==512);
    c.cycles+=20000;assert(wii_gx_tick(&g));assert(polls[0]==1); /* Once per actual VI. */
    flags[1]=1;flags[2]=0;m.fields++;c.cycles+=20000;assert(wii_gx_tick(&g));
    assert(reports[1].present && !reports[1].buttons && !reports[1].pointing);
    assert(!reports[2].present && !reports[2].buttons && !reports[2].pointing && reports[3].buttons==8);
    g.host_input_capture=true;m.fields++;c.cycles+=20000;assert(wii_gx_tick(&g));
    for(unsigned i=0;i<4;++i)assert(!reports[i].buttons && !reports[i].pointing);
    g.host_input_capture=false;g.scripted_input=true;m.fields++;c.cycles+=20000;assert(wii_gx_tick(&g));
    const unsigned before=polls[0];c.cycles+=20000;m.fields++;assert(wii_gx_tick(&g));assert(polls[0]==before);
    assert(wii_gx_set_player_input(&g,NULL,NULL));
    for(unsigned i=0;i<4;++i)assert(!reports[i].present);
    g.scripted_input=false;g.input_sent=false;c.cycles+=20000;m.fields++;assert(wii_gx_tick(&g));
    assert(legacy==1 && reports[0].present);return 0;
}
