/* SPDX-License-Identifier: MIT */
#include "wiillr/launcher_input.h"
#include "launcher_input.h"
#include <SDL3/SDL.h>
#include <string.h>
#include <stdio.h>

static uint32_t selected_id;
static char selected_guid[40],selected_path[4096];

static const int keys[11]={SDL_SCANCODE_UP,SDL_SCANCODE_DOWN,SDL_SCANCODE_LEFT,
    SDL_SCANCODE_RIGHT,SDL_SCANCODE_Z,SDL_SCANCODE_X,SDL_SCANCODE_RETURN,
    SDL_SCANCODE_S,SDL_SCANCODE_C,SDL_SCANCODE_V,SDL_SCANCODE_A};
static const int buttons[11]={SDL_GAMEPAD_BUTTON_DPAD_UP,SDL_GAMEPAD_BUTTON_DPAD_DOWN,
    SDL_GAMEPAD_BUTTON_DPAD_LEFT,SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
    SDL_GAMEPAD_BUTTON_EAST,SDL_GAMEPAD_BUTTON_SOUTH,SDL_GAMEPAD_BUTTON_START,
    SDL_GAMEPAD_BUTTON_BACK,SDL_GAMEPAD_BUTTON_WEST,SDL_GAMEPAD_BUTTON_NORTH,
    SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER};
static const uint16_t pad_bits[11]={8,4,1,2,0x200,0x100,0x1000,0x40,0x400,0x800,0x10};

void wii_launcher_input_defaults(RecompLauncherCSettings* s) {
    s->deadzone[0]=12;
    for(int b=0;b<11;++b) {
        s->player_key_bind[0][b]=keys[b];
        s->player_pad_bind[0][b]=RECOMP_LAUNCHER_PAD_BUTTON(buttons[b]);
    }
}

static bool key_down(const WiiLauncherInput* input,int binding) {
    if(binding>512 && binding<=517)return (input->mouse&(1u<<(binding-513)))!=0;
    return binding>0 && binding<input->key_count && input->keys && input->keys[binding];
}

static bool axis_down(const WiiLauncherInput* input,int axis,bool positive,int threshold) {
    return axis>=0 && axis<6 &&
        (positive?input->axes[axis]>=threshold:input->axes[axis]<=-threshold);
}

static bool pad_down(const WiiLauncherInput* input,int binding,int threshold) {
    if(RECOMP_LAUNCHER_PAD_IS_BUTTON(binding)) {
        int button=RECOMP_LAUNCHER_PAD_BUTTON_CODE(binding);
        return button<SDL_GAMEPAD_BUTTON_COUNT && (input->buttons&(1u<<button));
    }
    if(RECOMP_LAUNCHER_PAD_IS_AXIS(binding))
        return axis_down(input,RECOMP_LAUNCHER_PAD_AXIS_CODE(binding),
            RECOMP_LAUNCHER_PAD_AXIS_POSITIVE(binding),threshold);
    if(RECOMP_LAUNCHER_PAD_IS_BUTTON_COMBO(binding)) {
        uint32_t mask=(uint32_t)RECOMP_LAUNCHER_PAD_BUTTON_COMBO_MASK(binding);
        return mask && (mask>>SDL_GAMEPAD_BUTTON_COUNT)==0 && (input->buttons&mask)==mask;
    }
    return false;
}

GcnllrPad wii_launcher_input_map(const RecompLauncherCSettings* s,const WiiLauncherInput* input) {
    GcnllrPad pad={0};pad.stick_x=pad.stick_y=pad.substick_x=pad.substick_y=128;
    int deadzone=s->deadzone[0];if(deadzone<0)deadzone=0;if(deadzone>100)deadzone=100;
    int threshold=deadzone*32767/100;if(threshold<1)threshold=1;
    for(int b=0;b<11;++b) {
        bool held=false;
        if(s->player_src[0]==1)held=key_down(input,s->player_key_bind[0][b]);
        if(s->player_src[0]==2) {
            int binding=s->player_pad_bind[0][b];
            held=pad_down(input,binding,threshold);
            /* Conventional left-stick alias only while the original D-pad
             * assignment remains. A remap/unbind removes this alias too. */
            if(b<4 && binding==RECOMP_LAUNCHER_PAD_BUTTON(buttons[b])) {
                int axis=b<2?SDL_GAMEPAD_AXIS_LEFTY:SDL_GAMEPAD_AXIS_LEFTX;
                held=held || axis_down(input,axis,b==1 || b==3,threshold);
            }
        }
        if(held)pad.buttons|=pad_bits[b];
    }
    /* Console Sync is separate from the Remote's gameplay button bindings.
     * Keep a way to pair a fresh copied NAND even with a gamepad selected. */
    if(key_down(input,SDL_SCANCODE_F10))pad.buttons|=0x20;
    if(s->player_src[0]==1) {
        pad.substick_x=(uint8_t)(128+100*((int)key_down(input,SDL_SCANCODE_L)-(int)key_down(input,SDL_SCANCODE_J)));
        pad.substick_y=(uint8_t)(128+100*((int)key_down(input,SDL_SCANCODE_I)-(int)key_down(input,SDL_SCANCODE_K)));
    } else if(s->player_src[0]==2) {
        int x=input->axes[SDL_GAMEPAD_AXIS_RIGHTX],y=-input->axes[SDL_GAMEPAD_AXIS_RIGHTY];
        if(x>-threshold && x<threshold)x=0;
        if(y>-threshold && y<threshold)y=0;
        x=128+x/256;y=128+y/256;
        pad.substick_x=(uint8_t)(x<0?0:x>255?255:x);
        pad.substick_y=(uint8_t)(y<0?0:y>255?255:y);
    }
    return pad;
}

static SDL_Gamepad* selected_gamepad(const RecompLauncherCSettings* s) {
    LauncherPad pads[LNG_MAX_PADS];
    int count=launcher_input_poll(pads,LNG_MAX_PADS,0);
    if(count<0)count=0;if(count>LNG_MAX_PADS)count=LNG_MAX_PADS;
    const char* guid=s->player_gamepad_guid[0];
    /* Shared C ABI reserves zero and returns the live SDL ID plus one. */
    uint32_t encoded_instance=s->player_gamepad_instance[0];
    uint32_t instance=encoded_instance?encoded_instance-1:0;
    if(selected_id) {
        for(int i=0;i<count;++i)
            if(pads[i].id==selected_id && !strcmp(selected_guid,pads[i].guid))
                return SDL_GetGamepadFromID(pads[i].id);
        SDL_Gamepad* match=NULL;uint32_t matched_id=0;
        for(int i=0;i<count;++i) {
            if(strcmp(selected_guid,pads[i].guid))continue;
            SDL_Gamepad* pad=SDL_GetGamepadFromID(pads[i].id);
            const char* path=pad?SDL_GetGamepadPath(pad):NULL;
            if(*selected_path && path && !strcmp(path,selected_path)) {
                if(match)return NULL;
                match=pad;matched_id=pads[i].id;
            }
        }
        if(match)selected_id=matched_id;
        return match; /* A different connected pad must not take over. */
    }
    for(int i=0;i<count;++i) {
        if(*guid && strcmp(guid,pads[i].guid))continue;
        if(instance && pads[i].id!=instance)continue;
        SDL_Gamepad* pad=SDL_GetGamepadFromID(pads[i].id);
        if(!pad)return NULL;
        selected_id=pads[i].id;snprintf(selected_guid,sizeof selected_guid,"%s",pads[i].guid);
        const char* path=SDL_GetGamepadPath(pad);
        if(path && snprintf(selected_path,sizeof selected_path,"%s",path)>=(int)sizeof selected_path)
            selected_path[0]=0;
        /* Some virtual/driver devices have no unique path. Keep their live
         * instance selection, without guessing after disconnection. */
        for(int j=0;*selected_path && j<count;++j)if(j!=i && !strcmp(pads[j].guid,selected_guid)) {
            SDL_Gamepad* other=SDL_GetGamepadFromID(pads[j].id);
            const char* other_path=other?SDL_GetGamepadPath(other):NULL;
            if(other_path && !strcmp(other_path,selected_path))selected_path[0]=0;
        }
        return pad;
    }
    return NULL;
}

void wii_launcher_input_poll(void* user,GcnllrPad* pad) {
    const RecompLauncherCSettings* s=user;
    WiiLauncherInput input={0};
    SDL_Window* focus=SDL_GetKeyboardFocus();
    if(focus && !strcmp(SDL_GetWindowTitle(focus),"Mega Man 9")) {
        input.keys=SDL_GetKeyboardState(&input.key_count);
        if(SDL_GetMouseFocus()==focus)input.mouse=SDL_GetMouseState(NULL,NULL);
    }
    if(s->player_src[0]==2) {
        SDL_Gamepad* gamepad=selected_gamepad(s);
        if(gamepad) {
            for(int b=0;b<SDL_GAMEPAD_BUTTON_COUNT;++b)
                if(SDL_GetGamepadButton(gamepad,(SDL_GamepadButton)b))input.buttons|=1u<<b;
            for(int a=0;a<6;++a)input.axes[a]=SDL_GetGamepadAxis(gamepad,(SDL_GamepadAxis)a);
        }
    }
    *pad=wii_launcher_input_map(s,&input);
}

void wii_launcher_input_stop(void* user) {
    (void)user;launcher_input_shutdown();
    /* Retain the device path through original title reset/SDL reinit. */
}

void wii_launcher_input_reset(void) {
    launcher_input_shutdown();selected_id=0;
    selected_guid[0]=selected_path[0]=0;
}
