/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "wiillr/remote_input.h"
#include <stdio.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void) {
    const GcnllrPad neutral={.stick_x=128,.stick_y=128,.substick_x=128,.substick_y=128};
    GcnllrPad pad=neutral;
    CHECK(!wii_remote_buttons(&pad,false));CHECK(!wii_remote_buttons(&pad,true));
    /* The Menu needs A to confirm; MM9 needs Remote 2 to jump. */
    pad.buttons=0x100;CHECK(wii_remote_buttons(&pad,false)==0x800);CHECK(wii_remote_buttons(&pad,true)==0x100);
    pad.buttons=0x200;CHECK(wii_remote_buttons(&pad,false)==0x400);CHECK(wii_remote_buttons(&pad,true)==0x200);
    pad.buttons=0x400;CHECK(wii_remote_buttons(&pad,false)==0x100);CHECK(wii_remote_buttons(&pad,true)==0x800);
    pad.buttons=0x800;CHECK(wii_remote_buttons(&pad,false)==0x200);CHECK(wii_remote_buttons(&pad,true)==0x400);
    /* Actual Remote left/right/down/up wire bits are 1/2/4/8. */
    pad.buttons=8;CHECK(wii_remote_buttons(&pad,false)==8);CHECK(wii_remote_buttons(&pad,true)==2);
    pad.buttons=4;CHECK(wii_remote_buttons(&pad,false)==4);CHECK(wii_remote_buttons(&pad,true)==1);
    pad.buttons=1;CHECK(wii_remote_buttons(&pad,false)==1);CHECK(wii_remote_buttons(&pad,true)==8);
    pad.buttons=2;CHECK(wii_remote_buttons(&pad,false)==2);CHECK(wii_remote_buttons(&pad,true)==4);
    pad=neutral;pad.stick_x=0;pad.stick_y=255;
    CHECK(wii_remote_buttons(&pad,false)==9);CHECK(wii_remote_buttons(&pad,true)==10);
    pad=neutral;pad.buttons=0x1050;CHECK(wii_remote_buttons(&pad,false)==0x9010);CHECK(wii_remote_buttons(&pad,true)==0x9010);
    pad=neutral;pad.buttons=0x20;CHECK(!wii_remote_buttons(&pad,false)); /* console Sync is separate */
    WiiBluetoothInput first={0},second={0};first.nunchuk=wii_nunchuk_neutral();second.nunchuk=wii_nunchuk_neutral();
    pad=neutral;pad.stick_x=0;pad.stick_y=255;pad.buttons=0x20;
    wii_remote_host_controls(&first,&pad,false);
    pad=neutral;pad.buttons=0x100;wii_remote_host_controls(&second,&pad,false);
    CHECK(first.nunchuk.stick_x==32 && first.nunchuk.stick_y==223 && first.nunchuk.buttons==1 && !first.buttons);
    CHECK(second.nunchuk.stick_x==128 && second.nunchuk.stick_y==128 && !second.nunchuk.buttons && second.buttons==0x800);
    puts("Remote layouts: upright Menu and sideways MM9 wire controls PASS");return 0;
}
