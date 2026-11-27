/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "wiillr/launcher_input.h"
#include <recomp_input_effects.h>
#include "wiillr/keyboard.h"
#include "launcher_input.h"
#include <SDL3/SDL.h>
#include <string.h>
#include <stdio.h>

static uint32_t selected_id[4],requested_id[4];
static char selected_identity[4][65],requested_identity[4][65],requested_guid[4][40];
static unsigned player_flags[4];
static unsigned held_actions;

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
    for(unsigned player=0;player<4;++player) {
        s->deadzone[player]=12;
        for(int b=0;b<11;++b) {
            s->player_key_bind[player][b]=player?0:keys[b];
            s->player_pad_bind[player][b]=RECOMP_LAUNCHER_PAD_BUTTON(buttons[b]);
        }
        s->player_pad_bind[player][10]=RECOMP_LAUNCHER_PAD_AXIS(SDL_GAMEPAD_AXIS_LEFT_TRIGGER,1);
        const int pointer_keys[4]={SDL_SCANCODE_I,SDL_SCANCODE_K,SDL_SCANCODE_J,SDL_SCANCODE_L};
        for(int b=0;b<4;++b) {
            s->player_key_bind[player][11+b]=player?0:pointer_keys[b];
            s->player_pad_bind[player][11+b]=RECOMP_LAUNCHER_PAD_AXIS(b<2?SDL_GAMEPAD_AXIS_RIGHTY:SDL_GAMEPAD_AXIS_RIGHTX,b==1 || b==3);
        }
    }
    s->assist_key_bind[0]=SDL_SCANCODE_Q;s->assist_key_bind[1]=SDL_SCANCODE_E;
    s->assist_pad_bind[0]=RECOMP_LAUNCHER_PAD_BUTTON(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    s->assist_pad_bind[1]=RECOMP_LAUNCHER_PAD_BUTTON(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
}

static bool key_down(const WiiLauncherInput* input,int binding) {
    if(binding>512 && binding<=517)return (input->mouse&(1u<<(binding-513)))!=0;
    if(wii_keyboard_host_captured())return false;
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

unsigned wii_launcher_input_actions(const RecompLauncherCSettings* s,const WiiLauncherInput* input) {
    int deadzone=s->deadzone[0];if(deadzone<0)deadzone=0;if(deadzone>100)deadzone=100;
    int threshold=deadzone*32767/100;if(threshold<1)threshold=1;
    unsigned result=0;
    for(int n=0;n<2;n++) {
        bool held=s->player_src[0]==1?key_down(input,s->assist_key_bind[n]):
            s->player_src[0]==2?pad_down(input,s->assist_pad_bind[n],threshold):false;
        if(held)result|=1u<<n;
    }
    return result;
}
unsigned wii_launcher_input_held_actions(void){return held_actions;}

GcnllrPad wii_launcher_input_map(const RecompLauncherCSettings* s,const WiiLauncherInput* input) {return wii_launcher_input_map_player(s,input,0);}
GcnllrPad wii_launcher_input_map_player(const RecompLauncherCSettings* s,const WiiLauncherInput* input,unsigned player) {
    GcnllrPad pad={0};pad.stick_x=pad.stick_y=pad.substick_x=pad.substick_y=128;
    if(!s || !input || player>=4)return pad;
    int deadzone=s->deadzone[player];if(deadzone<0)deadzone=0;if(deadzone>100)deadzone=100;
    int threshold=deadzone*32767/100;if(threshold<1)threshold=1;
    for(int b=0;b<11;++b) {
        bool held=false;
        if(s->player_src[player]==1)held=key_down(input,s->player_key_bind[player][b]);
        if(s->player_src[player]==2) {
            int binding=s->player_pad_bind[player][b];
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
    if(player==0 && key_down(input,SDL_SCANCODE_F10))pad.buttons|=0x20;
    bool movement[4]={0};
    for(int b=0;b<4;++b)movement[b]=s->player_src[player]==1?key_down(input,s->player_key_bind[player][11+b]):
        s->player_src[player]==2?pad_down(input,s->player_pad_bind[player][11+b],threshold):false;
    pad.substick_x=(uint8_t)(128+100*((int)movement[3]-(int)movement[2]));
    pad.substick_y=(uint8_t)(128+100*((int)movement[0]-(int)movement[1]));
    if(s->player_src[player]==2) {
        // Keep the original analog pointer while these defaults remain; a
        // remap/unbind controls only this Remote's pointer direction.
        const int bx=s->player_pad_bind[player][13],by=s->player_pad_bind[player][11];
        if(bx==RECOMP_LAUNCHER_PAD_AXIS(SDL_GAMEPAD_AXIS_RIGHTX,0) && s->player_pad_bind[player][14]==RECOMP_LAUNCHER_PAD_AXIS(SDL_GAMEPAD_AXIS_RIGHTX,1)) {
            pad.substick_x=ri_axis_u8(input->axes[SDL_GAMEPAD_AXIS_RIGHTX],(unsigned)deadzone,RI_AXIS_U8_CLIPPED_256);
        }
        if(by==RECOMP_LAUNCHER_PAD_AXIS(SDL_GAMEPAD_AXIS_RIGHTY,0) && s->player_pad_bind[player][12]==RECOMP_LAUNCHER_PAD_AXIS(SDL_GAMEPAD_AXIS_RIGHTY,1)) {
            pad.substick_y=ri_axis_u8(-(int32_t)input->axes[SDL_GAMEPAD_AXIS_RIGHTY],(unsigned)deadzone,RI_AXIS_U8_CLIPPED_256);
        }
    }
    return pad;
}

static bool claimed(const RecompLauncherCSettings* s,unsigned player,uint32_t id) {
    for(unsigned n=0;n<player;++n)if(s->player_src[n]==2 && selected_id[n]==id &&
       requested_id[n]==s->player_gamepad_instance[n] && !strcmp(requested_guid[n],s->player_gamepad_guid[n]) &&
       !strcmp(requested_identity[n],s->player_gamepad_identity[n]))return true;
    return false;
}
static SDL_Gamepad* selected_gamepad(const RecompLauncherCSettings* s,unsigned player) {
    LauncherPad pads[LNG_MAX_PADS];int count=launcher_input_poll(pads,LNG_MAX_PADS,0);
    if(count<0)count=0;if(count>LNG_MAX_PADS)count=LNG_MAX_PADS;
    const char* guid=s->player_gamepad_guid[player];
    const char* identity=s->player_gamepad_identity[player];
    const uint32_t instance=s->player_gamepad_instance[player];
    if(requested_id[player]!=instance || strcmp(requested_guid[player],guid) || strcmp(requested_identity[player],identity)) {
        selected_id[player]=0;selected_identity[player][0]=0;
        requested_id[player]=instance;snprintf(requested_guid[player],sizeof requested_guid[player],"%s",guid);
        snprintf(requested_identity[player],sizeof requested_identity[player],"%s",identity);
    }
    if(!*identity && !instance && !selected_id[player])
        for(unsigned n=0;n<4;++n)if(n!=player && s->player_src[n]==2 && !strcmp(guid,s->player_gamepad_guid[n]))return NULL;
    const int index=launcher_input_resolve(pads,count,guid,*identity?identity:selected_identity[player],
                                           selected_id[player]?selected_id[player]+1:instance);
    if(index<0 || claimed(s,player,pads[index].id))return NULL;
    SDL_Gamepad* pad=SDL_GetGamepadFromID(pads[index].id);if(!pad)return NULL;
    if(selected_id[player]!=pads[index].id)fprintf(stderr,"[input] Wii Remote %u controller: %s\n",player+1,pads[index].name);
    selected_id[player]=pads[index].id;
    snprintf(selected_identity[player],sizeof selected_identity[player],"%s",pads[index].identity[0]?pads[index].identity:"manual");
    return pad;
}
unsigned wii_launcher_input_poll_player(void* user,unsigned player,GcnllrPad* pad) {
    const RecompLauncherCSettings* s=user;
    if(!pad)return 0;
    *pad=(GcnllrPad){0};pad->stick_x=pad->stick_y=pad->substick_x=pad->substick_y=128;
    if(!s || player>=4)return 0;
    WiiLauncherInput input={0};int keyboard_count=0;
    const bool* keyboard_keys=SDL_GetKeyboardState(&keyboard_count);
    SDL_Window* focus=SDL_GetKeyboardFocus();
    const bool focused=focus && (!strcmp(SDL_GetWindowTitle(focus),"Mega Man 9") || !strcmp(SDL_GetWindowTitle(focus),"Wii Menu"));
    // Without a video subsystem there is no host window/focus to lose (e.g.
    // input-only components). Actual runtime windows always enforce focus.
    const bool active=focused || !(SDL_WasInit(SDL_INIT_VIDEO)&SDL_INIT_VIDEO);
    if(focused) {
        input.keys=keyboard_keys;input.key_count=keyboard_count;
        if(player==0 && SDL_GetMouseFocus()==focus)input.mouse=SDL_GetMouseState(NULL,NULL);
    }
    if(player==0)wii_keyboard_host_keys(input.keys,input.key_count,keyboard_keys && keyboard_count>0);
    bool present=s->player_src[player]==1 && keyboard_keys && keyboard_count>0;
    if(s->player_src[player]==2) {
        SDL_Gamepad* gamepad=selected_gamepad(s,player);present=gamepad!=NULL;
        if(gamepad && active) {
            for(int b=0;b<SDL_GAMEPAD_BUTTON_COUNT;++b)
                if(SDL_GetGamepadButton(gamepad,(SDL_GamepadButton)b))input.buttons|=1u<<b;
            for(int axis=0;axis<6;++axis)input.axes[axis]=SDL_GetGamepadAxis(gamepad,(SDL_GamepadAxis)axis);
        }
    }
    *pad=wii_launcher_input_map_player(s,&input,player);
    if(player==0)held_actions=wii_launcher_input_actions(s,&input);
    return player_flags[player]=present?(WIILLR_INPUT_PRESENT|(focused?WIILLR_INPUT_FOCUSED:0)):0;
}
unsigned wii_launcher_input_player_flags(unsigned player){return player<4?player_flags[player]:0;}
void wii_launcher_input_poll(void* user,GcnllrPad* pad){(void)wii_launcher_input_poll_player(user,0,pad);}
void wii_launcher_input_stop(void* user) {
    (void)user;launcher_input_shutdown();held_actions=0;memset(player_flags,0,sizeof player_flags);
    /* Retain each physical identity through original title reset/SDL reinit. */
}
void wii_launcher_input_reset(void) {
    launcher_input_shutdown();held_actions=0;memset(player_flags,0,sizeof player_flags);
    memset(selected_id,0,sizeof selected_id);memset(selected_identity,0,sizeof selected_identity);memset(requested_identity,0,sizeof requested_identity);
    memset(requested_id,0,sizeof requested_id);memset(requested_guid,0,sizeof requested_guid);
}
