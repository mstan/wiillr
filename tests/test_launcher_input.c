/* SPDX-License-Identifier: MIT */
#include "wiillr/launcher_input.h"
#include "wiillr/keyboard.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s (%s)\n",__LINE__,#x,SDL_GetError());return 1;}}while(0)

int main(void) {
    RecompLauncherCSettings s={0};wii_launcher_input_defaults(&s);
    bool keys[SDL_SCANCODE_COUNT]={0};WiiLauncherInput input={.keys=keys,.key_count=SDL_SCANCODE_COUNT};
    s.player_src[0]=1;keys[SDL_SCANCODE_X]=true;keys[SDL_SCANCODE_RIGHT]=true;
    CHECK(wii_launcher_input_map(&s,&input).buttons==0x102);
    wii_keyboard_host_begin(true);wii_keyboard_host_keys(keys,SDL_SCANCODE_COUNT,true);
    CHECK(!wii_launcher_input_map(&s,&input).buttons);
    s.player_src[0]=2;input.buttons=1u<<SDL_GAMEPAD_BUTTON_SOUTH;
    CHECK(wii_launcher_input_map(&s,&input).buttons==0x100); /* gamepad remains independent */
    wii_keyboard_host_reset();s.player_src[0]=1;input.buttons=0;
    s.player_key_bind[0][5]=SDL_SCANCODE_F;
    CHECK(!(wii_launcher_input_map(&s,&input).buttons&0x100));
    keys[SDL_SCANCODE_F]=true;CHECK(wii_launcher_input_map(&s,&input).buttons&0x100);
    s.player_key_bind[0][5]=0;CHECK(!(wii_launcher_input_map(&s,&input).buttons&0x100));
    s.player_src[0]=0;input.buttons=~0u;CHECK(!wii_launcher_input_map(&s,&input).buttons);
    s.player_src[0]=2;input.buttons=0;
    CHECK(!wii_launcher_input_map(&s,&input).buttons); /* keyboard source isolated */
    input.buttons=1u<<SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
    CHECK(!(wii_launcher_input_map(&s,&input).buttons&0x10)); /* R1 is no longer HOME */
    CHECK(wii_launcher_input_actions(&s,&input)==2);
    input.buttons=1u<<SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
    CHECK(wii_launcher_input_actions(&s,&input)==1);
    input.buttons=0;input.axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER]=12000;
    CHECK(wii_launcher_input_map(&s,&input).buttons&0x10); /* L2 HOME */
    CHECK(!wii_launcher_input_actions(&s,&input));
    input.axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER]=0;
    s.assist_pad_bind[0]=0;input.buttons=1u<<SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
    CHECK(!wii_launcher_input_actions(&s,&input)); /* explicit unbind */
    input.buttons=0;
    keys[SDL_SCANCODE_F10]=true;
    CHECK(wii_launcher_input_map(&s,&input).buttons==0x20); /* physical console Sync */
    keys[SDL_SCANCODE_F10]=false;
    s.player_pad_bind[0][5]=RECOMP_LAUNCHER_PAD_AXIS(SDL_GAMEPAD_AXIS_LEFT_TRIGGER,1);
    input.axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER]=1000;
    CHECK(!(wii_launcher_input_map(&s,&input).buttons&0x100));
    input.axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER]=12000;
    CHECK(wii_launcher_input_map(&s,&input).buttons&0x100);
    s.player_pad_bind[0][5]=999;CHECK(!(wii_launcher_input_map(&s,&input).buttons&0x100));
    const unsigned chord=(1u<<SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)|(1u<<SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    s.player_pad_bind[0][10]=RECOMP_LAUNCHER_PAD_BUTTON_COMBO(chord);
    input.buttons=1u<<SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
    CHECK(!(wii_launcher_input_map(&s,&input).buttons&0x10));
    input.buttons=chord;CHECK(wii_launcher_input_map(&s,&input).buttons&0x10);
    input.buttons=0;input.axes[SDL_GAMEPAD_AXIS_LEFTX]=16000;
    CHECK(wii_launcher_input_map(&s,&input).buttons&2);
    s.player_pad_bind[0][3]=0;CHECK(!(wii_launcher_input_map(&s,&input).buttons&2));
    input.axes[SDL_GAMEPAD_AXIS_RIGHTX]=16000;
    CHECK(wii_launcher_input_map(&s,&input).substick_x>128);
    s.player_src[0]=1;s.player_key_bind[0][7]=513;input.mouse=SDL_BUTTON_LMASK;
    CHECK(wii_launcher_input_map(&s,&input).buttons&0x40);

    // Per-player keyboard and IR bindings do not inherit P1's keys.
    s.player_src[1]=1;keys[SDL_SCANCODE_F]=true;
    CHECK(!wii_launcher_input_map_player(&s,&input,1).buttons);
    s.player_key_bind[1][5]=SDL_SCANCODE_G;keys[SDL_SCANCODE_G]=true;
    CHECK(wii_launcher_input_map_player(&s,&input,1).buttons==0x100);
    s.player_key_bind[1][11]=SDL_SCANCODE_T;keys[SDL_SCANCODE_T]=true;
    CHECK(wii_launcher_input_map_player(&s,&input,1).substick_y>128);
    CHECK(!wii_launcher_input_map_player(&s,&input,4).buttons);

    /* Real SDL virtual controllers, no video subsystem, window or picker. */
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    CHECK(SDL_Init(SDL_INIT_GAMEPAD));
    /* System key state exists even without enumerated physical keyboards or
     * a focused video window. The virtual USB device remains attached while
     * unfocused and sends a neutral report rather than a disconnect. */
    CHECK(!SDL_HasKeyboard() && !SDL_GetKeyboardFocus());
    wii_keyboard_host_begin(false);
    GcnllrPad presence_pad;wii_launcher_input_poll(&s,&presence_pad);
    WiiKeyboardReport keyboard;wii_keyboard_host_end(true,&keyboard);
    CHECK(keyboard.connected && !keyboard.modifiers);
    CHECK(!memcmp(keyboard.keys,(unsigned char[6]){0},6));
    SDL_VirtualJoystickDesc desc;SDL_INIT_INTERFACE(&desc);
    desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;desc.naxes=6;desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
    desc.axis_mask=63;desc.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;
    desc.vendor_id=0xf00d;desc.product_id=1;desc.name="wiillr input test";
    SDL_JoystickID one=SDL_AttachVirtualJoystick(&desc);CHECK(one);
    SDL_JoystickID two=SDL_AttachVirtualJoystick(&desc);CHECK(two);
    SDL_Joystick* first=SDL_OpenJoystick(one);SDL_Joystick* second=SDL_OpenJoystick(two);
    CHECK(first && second);
    memset(&s,0,sizeof s);wii_launcher_input_defaults(&s);s.player_src[0]=2;
    SDL_GUIDToString(SDL_GetJoystickGUID(second),s.player_gamepad_guid[0],sizeof s.player_gamepad_guid[0]);
    char first_guid[40];SDL_GUIDToString(SDL_GetJoystickGUID(first),first_guid,sizeof first_guid);
    CHECK(!strcmp(first_guid,s.player_gamepad_guid[0])); /* identical controller models */
    s.player_gamepad_instance[0]=two+1;
    s.player_pad_bind[0][5]=RECOMP_LAUNCHER_PAD_BUTTON(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    CHECK(SDL_SetJoystickVirtualButton(first,SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,true));
    SDL_UpdateJoysticks();SDL_PumpEvents();
    GcnllrPad pad;wii_launcher_input_poll(&s,&pad);
    CHECK(!(pad.buttons&0x100)); /* first pad must not control player one */
    CHECK(SDL_SetJoystickVirtualButton(second,SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,true));
    SDL_UpdateJoysticks();SDL_PumpEvents();wii_launcher_input_poll(&s,&pad);
    CHECK(pad.buttons&0x100); /* remapped button on the selected pad */
    SDL_JoystickID three=SDL_AttachVirtualJoystick(&desc),four=SDL_AttachVirtualJoystick(&desc);CHECK(three && four);
    SDL_Joystick* third=SDL_OpenJoystick(three);SDL_Joystick* fourth=SDL_OpenJoystick(four);CHECK(third && fourth);
    SDL_Joystick* sticks[4]={second,first,third,fourth};SDL_JoystickID ids[4]={two,one,three,four};
    for(unsigned n=0;n<4;++n) {
        s.player_src[n]=2;s.player_gamepad_instance[n]=ids[n]+1;
        snprintf(s.player_gamepad_guid[n],sizeof s.player_gamepad_guid[n],"%s",first_guid);
        s.player_pad_bind[n][5]=RECOMP_LAUNCHER_PAD_BUTTON(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
        CHECK(SDL_SetJoystickVirtualButton(sticks[n],SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,false));
    }
    for(unsigned active=0;active<4;++active) {
        CHECK(SDL_SetJoystickVirtualButton(sticks[active],SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,true));
        SDL_UpdateJoysticks();SDL_PumpEvents();
        for(unsigned n=0;n<4;++n) {
            unsigned flags=wii_launcher_input_poll_player(&s,n,&pad);CHECK(flags&WIILLR_INPUT_PRESENT);
            CHECK(((pad.buttons&0x100)!=0)==(n==active));
        }
        CHECK(wii_launcher_input_held_actions()==(active==0?1u:0u));
        CHECK(SDL_SetJoystickVirtualButton(sticks[active],SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,false));
    }
    CHECK(SDL_SetJoystickVirtualAxis(third,SDL_GAMEPAD_AXIS_RIGHTX,16000));
    SDL_UpdateJoysticks();SDL_PumpEvents();
    for(unsigned n=0;n<4;++n) {wii_launcher_input_poll_player(&s,n,&pad);CHECK((pad.substick_x>128)==(n==2));}
    // A duplicated physical assignment fails closed for the later slot.
    s.player_gamepad_instance[3]=one+1;
    CHECK(!(wii_launcher_input_poll_player(&s,3,&pad)&WIILLR_INPUT_PRESENT) && !pad.buttons);
    s.player_gamepad_instance[3]=four+1;
    CHECK(wii_launcher_input_poll_player(&s,3,&pad)&WIILLR_INPUT_PRESENT);
    // Dummy video cannot create a desktop window. Losing video focus keeps
    // assigned devices present while releasing every player's buttons/IR.
    CHECK(SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"dummy"));CHECK(SDL_InitSubSystem(SDL_INIT_VIDEO));
    CHECK(!strcmp(SDL_GetCurrentVideoDriver(),"dummy") && !SDL_GetKeyboardFocus());
    for(unsigned n=0;n<4;++n) {
        const unsigned flags=wii_launcher_input_poll_player(&s,n,&pad);
        CHECK((flags&WIILLR_INPUT_PRESENT) && !(flags&WIILLR_INPUT_FOCUSED));
        CHECK(!pad.buttons && pad.substick_x==128 && pad.substick_y==128);
    }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    SDL_CloseJoystick(third);SDL_CloseJoystick(fourth);
    CHECK(SDL_DetachVirtualJoystick(three) && SDL_DetachVirtualJoystick(four));
    SDL_CloseJoystick(second);CHECK(SDL_DetachVirtualJoystick(two));
    SDL_UpdateJoysticks();SDL_PumpEvents();wii_launcher_input_poll(&s,&pad);
    CHECK(!(pad.buttons&0x100) && !(wii_launcher_input_player_flags(0)&WIILLR_INPUT_PRESENT)); /* no takeover when selected pad disconnects */
    wii_launcher_input_stop(&s);
    SDL_CloseJoystick(first);CHECK(SDL_DetachVirtualJoystick(one));SDL_Quit();
    puts("launcher input: remaps, source isolation, axes, unbinds and SDL device selection PASS");
    return 0;
}
