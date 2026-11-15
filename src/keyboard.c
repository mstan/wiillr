/* SPDX-License-Identifier: MIT */
#include "wiillr/keyboard.h"
#include <string.h>
static unsigned changes(const WiiKeyboard* keyboard,const WiiKeyboardReport* report) {
    if(!report->connected)return keyboard->report.connected?1u:0u;
    const bool changed=report->modifiers!=keyboard->report.modifiers ||
        memcmp(report->keys,keyboard->report.keys,6)!=0;
    return !keyboard->report.connected?1u+(report->modifiers || memcmp(report->keys,(uint8_t[6]){0},6)!=0):changed;
}
bool wii_keyboard_can_update(const WiiKeyboard* keyboard,const WiiKeyboardReport* report) {
    return keyboard && report && keyboard->head<32 && keyboard->count<=32 && changes(keyboard,report)<=32-keyboard->count;
}
static void enqueue(WiiKeyboard* keyboard,uint8_t type,const WiiKeyboardReport* report) {
    uint8_t* wire=keyboard->events[(keyboard->head+keyboard->count)%32];memset(wire,0,16);wire[3]=type;
    if(type==2){wire[8]=report->modifiers;memcpy(wire+10,report->keys,6);}++keyboard->count;
}
bool wii_keyboard_update(WiiKeyboard* keyboard,const WiiKeyboardReport* report) {
    if(!wii_keyboard_can_update(keyboard,report))return false;
    const unsigned count=changes(keyboard,report);
    if(count) {
        if(!report->connected)enqueue(keyboard,1,report);
        else {if(!keyboard->report.connected)enqueue(keyboard,0,report);
            if(keyboard->report.connected || count==2)enqueue(keyboard,2,report);}
    }
    keyboard->report=*report;
    if(!report->connected)memset(&keyboard->report,0,sizeof keyboard->report);
    return true;
}
bool wii_keyboard_pop(WiiKeyboard* keyboard,uint8_t event[16]) {
    if(!keyboard || !event || keyboard->head>=32 || keyboard->count>32 || !keyboard->count)return false;
    memcpy(event,keyboard->events[keyboard->head],16);keyboard->head=(keyboard->head+1)%32;--keyboard->count;return true;
}
static WiiKeyboardReport host;
static bool sampled,typing,captured;
void wii_keyboard_host_begin(bool active) {memset(&host,0,sizeof host);sampled=false;typing=active;if(active)captured=true;}
void wii_keyboard_host_keys(const bool* keys,int count,bool connected) {
    memset(&host,0,sizeof host);sampled=true;host.connected=connected;unsigned n=0;bool held=false;
    if(keys && connected) {
        for(int key=4;key<count && key<224;key++)if(keys[key]) {
            held=true;if(n<6)host.keys[n]=(uint8_t)key;++n;
        }
        for(int bit=0;bit<8;bit++)if(count>224+bit && keys[224+bit])host.modifiers|=(uint8_t)(1u<<bit);
        held|=host.modifiers!=0;
        if(n>6)memset(host.keys,1,6); /* USB boot keyboard ErrorRollOver */
    }
    if(!typing && !held)captured=false;
}
bool wii_keyboard_host_captured(void){return captured;}
void wii_keyboard_host_end(bool enabled,WiiKeyboardReport* out) {
    if(out){memset(out,0,sizeof *out);if(enabled && sampled)*out=host;}
    sampled=false;
}
void wii_keyboard_host_reset(void){memset(&host,0,sizeof host);sampled=typing=captured=false;}
