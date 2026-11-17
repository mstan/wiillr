/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "wiillr/ave.h"
#include <string.h>
enum {IDLE,RX,ACK_SETUP,ACK_CLOCK,ACK_FINISH,TX,TX_RELEASE,MASTER_ACK,MASTER_ACK_FINISH,IGNORE};
enum {ADDRESS,REGISTER,DATA};
void wii_ave_init(WiiAve* a) {memset(a,0,sizeof *a);a->scl=a->sda=true;}
static void transmit(WiiAve* a) {
    if(!a->valid[a->index]) {a->failed=true;return;}
    a->byte=a->registers[a->index];a->bits=0;a->state=TX;
    a->drive_low=!(a->byte&0x80);
}
bool wii_ave_gpio(WiiAve* a,uint32_t out,uint32_t direction) {
    const bool scl=!(direction&0x4000) || (out&0x4000);
    const bool master_sda=!(direction&0x8000) || (out&0x8000);
    const bool sda=master_sda && !a->drive_low;
    if(a->scl && scl && sda!=a->sda) {
        a->drive_low=false;a->bits=a->byte=0;
        if(!sda) {a->state=RX;a->stage=ADDRESS;a->selected=false;++a->transactions;}
        else a->state=IDLE;
    } else if(!a->scl && scl) {
        if(a->state==RX) {
            a->byte=(uint8_t)((a->byte<<1)|sda);
            if(++a->bits==8) {
                if(a->stage==ADDRESS) {
                    a->selected=(a->byte>>1)==0x70;a->read=(a->byte&1)!=0;a->stage=REGISTER;
                } else if(a->selected && a->stage==REGISTER) {a->index=a->byte;a->stage=DATA;}
                else if(a->selected) {a->registers[a->index]=a->byte;a->valid[a->index++]=1;++a->writes;}
                a->ack=a->selected;a->state=ACK_SETUP;
            }
        } else if(a->state==ACK_CLOCK)a->state=ACK_FINISH;
        else if(a->state==TX) {if(++a->bits==8)a->state=TX_RELEASE;}
        else if(a->state==MASTER_ACK) {a->ack=!sda;a->state=MASTER_ACK_FINISH;}
    } else if(a->scl && !scl) {
        if(a->state==ACK_SETUP) {a->drive_low=a->ack;a->state=ACK_CLOCK;}
        else if(a->state==ACK_FINISH) {
            a->drive_low=false;a->bits=a->byte=0;
            if(!a->selected)a->state=IGNORE;
            else if(a->read)transmit(a);
            else a->state=RX;
        } else if(a->state==TX)a->drive_low=!(a->byte&(0x80u>>a->bits));
        else if(a->state==TX_RELEASE) {a->drive_low=false;a->state=MASTER_ACK;}
        else if(a->state==MASTER_ACK_FINISH) {
            if(a->ack) {++a->index;transmit(a);}else {a->drive_low=false;a->state=IGNORE;}
        }
    }
    a->scl=scl;a->sda=master_sda && !a->drive_low;
    return !a->failed;
}
uint32_t wii_ave_pins(const WiiAve* a) {return (a->scl?0x4000u:0)|(a->sda?0x8000u:0);}
