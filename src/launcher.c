/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "wiillr/launcher.h"
#include "wiillr/launcher_input.h"
#include "wiillr/launcher_peripherals.h"
#include "wiillr/launcher_mods.h"
#include "wiillr/diagnostics.h"
#include "wiillr/package.h"
#include "wiillr/nand_setup.h"
#include "wiillr/mm9_mods.h"
#include "wiillr/menu_grid.h"
#include "wiillr/fs.h"
#include "wiillr/wad.h"
#include "recomp_launcher.h"
#include "launcher_profile.h"
#include "launcher_settings.h"
#include "sha256.h"
#include "host/host_audio.h"
#include <recomp_persistence/atomic_file.h>
#if WIILLR_MM9_RENDERER
#include "wiillr/mm9_renderer_hook.h"
#endif
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static RecompLauncherCSettings settings;
static RecompLauncherCSettings default_settings;
static bool active;
static RecompLauncherCGameInfo game_info;
static char config_path[32768],rom_cache_path[32768],boot_image[32768];
static bool settings_key_held,input_release;
static bool session_controls_held,session_input_releasing;
static char launcher_directory[32768];
static const char* const view_labels[]={"Original 4:3","16:9","21:9","Fit to screen"};
static const char* const resolution_labels[]={"Original (1x)","2x","3x","4x","8x","16x"};
static const int resolution_values[]={1,960,1440,1920,3840,7680};
static bool view_supported(void);
static bool product_pointing(const WiiGx*);
static bool product_default_fit(const RecompLauncherCGameInfo*);
static bool product_active(const WiiGx*);
static bool product_renderer_active(const WiiGx*);
static void product_extent(const WiiGx*,unsigned,unsigned,bool);
static const char* game_caption(const WiiGx*);
static void session_cursor(bool hide) {
    /* Native settings can also change SDL cursor state; query the actual state. */
    if(hide==!SDL_CursorVisible())return;
    if(hide)(void)SDL_HideCursor();else (void)SDL_ShowCursor();
}
static WiiGx* cursor_owner;
static bool session_cursor_event(void* user,SDL_Event* event) {
    /* SDL also pumps window events while guest execution is paused. Synthetic
     * events may originate on other threads; cursor APIs stay on the owner. */
    if(!SDL_IsMainThread())return true;
    if(event->type==SDL_EVENT_WINDOW_RESIZED) {
        SDL_Window* window=SDL_GetWindowFromID(event->window.windowID);
        if(window && !strcmp(SDL_GetWindowTitle(window),game_caption(user)))
            product_extent(user,event->window.data1,event->window.data2,settings.aspect_index==3);
    }
    if(event->type==SDL_EVENT_WINDOW_FOCUS_LOST || event->type==SDL_EVENT_WINDOW_MOUSE_LEAVE ||
       event->type==SDL_EVENT_WINDOW_HIDDEN) {
        SDL_Window* window=SDL_GetWindowFromID(event->window.windowID);
        if(window && !strcmp(SDL_GetWindowTitle(window),game_caption(user)))session_cursor(false);
    }
    return true;
}
static void session_stop(void* user) {
    if(cursor_owner==user) {
        SDL_RemoveEventWatch(session_cursor_event,user);cursor_owner=NULL;
    }
    session_cursor(false);wii_launcher_input_stop(user);
    wii_launcher_peripherals_unbind();
}

static bool set_env(const char* name,const char* value) {
#ifdef _WIN32
    return _putenv_s(name,value)==0;
#else
    return setenv(name,value,1)==0;
#endif
}

static void load_settings(RecompLauncherCGameInfo* game) {
    memset(&settings,0,sizeof settings);
    settings.window_scale=2;settings.enable_audio=1;settings.audio_freq=32000;
    settings.volume=100;settings.player_src[0]=1;
    settings.aspect_index=(product_default_fit(game) || view_supported())?3:0;
    settings.internal_resolution=1;
    if(SDL_InitSubSystem(SDL_INIT_GAMEPAD) && SDL_HasGamepad())settings.player_src[0]=2;
    wii_launcher_input_defaults(&settings);
    default_settings=settings;game->default_settings=&default_settings;
    launcher_settings_load(&settings,game);
    recomp_launcher_set_preserve_sdl(1);
}
static bool launch_devices_ready(const RecompLauncherCGameInfo* game) {
    for(int i=0;game->peripherals && i<game->num_peripherals;++i)
        if(!game->peripherals[i].ready)return false;
    return true;
}

#include WIILLR_LAUNCHER_POLICY
static bool session_review(void* user,bool visible,u32 volume) {
    WiiGx* gx=user;
    if(!product_active(gx) || volume>100)return false;
    if(!visible)session_cursor(false);
    int count=0;SDL_Window** windows=SDL_GetWindows(&count);bool ok=false;
    for(int i=0;windows && i<count;++i) {
        if(strcmp(SDL_GetWindowTitle(windows[i]),game_caption(gx)))continue;
        ok=visible?(SDL_SetWindowFullscreen(windows[i],false) && SDL_ShowWindow(windows[i])):
            SDL_HideWindow(windows[i]);
        break;
    }
    SDL_free(windows);
    if(!ok || !gcn_host_audio_set_volume(volume))return false;
    char gain[8];snprintf(gain,sizeof gain,"%u",volume);
    if(!set_env("GCNLLR_RENDER_HIDDEN",visible?"0":"1") ||
       !set_env("GCN_AUDIO_VOLUME",gain))return false;
    settings.enable_audio=1;settings.volume=(int)volume;
    wii_pacing_reset(&gx->machine->pacing);return true;
}
static bool session_settings(void* user) {
    WiiGx* gx=user;session_cursor(false);
    /* This callback runs between fields on the simulation/graphics owner.
     * The shared in-session launcher freezes that owner until RESUME. */
    if(!gx->api->drain()) {gx->cpu->halted=true;return false;}
    SDL_Window* gl_window=SDL_GL_GetCurrentWindow();
    SDL_GLContext gl_context=SDL_GL_GetCurrentContext();
    RecompLauncherCGameInfo in_session=game_info;in_session.in_session=1;
    in_session.name=game_caption(gx);
    const RecompLauncherCSettings before=settings;
    product_in_session();
    char selected[32768];
    const int action=recomp_launcher_run_window(game_caption(gx),&settings,
        &in_session,NULL,boot_image,selected,sizeof selected);
    product_mod_finish(&in_session,&before);
    if(gl_context && !SDL_GL_MakeCurrent(gl_window,gl_context))gx->cpu->halted=true;
    if(action==RECOMP_LAUNCHER_RESULT_UNAVAILABLE)return false;
    wii_launcher_input_reset();
    char volume[8];snprintf(volume,sizeof volume,"%d",settings.enable_audio?settings.volume:0);
    if(!set_env("GCN_AUDIO_VOLUME",volume) ||
       !gcn_host_audio_set_volume((u32)(settings.enable_audio?settings.volume:0)) ||
       !wii_launcher_window(gx))gx->cpu->halted=true;
    wii_pacing_reset(&gx->machine->pacing);
    input_release=true;
    if(action==RECOMP_LAUNCHER_RESULT_QUIT)gx->quit=true;
    return !gx->cpu->halted;
}

static void session_input(void* user,GcnllrPad* pad) {
    WiiGx* gx=user;
    SDL_Window* focus=SDL_GetKeyboardFocus();
    const bool focused=focus && !strcmp(SDL_GetWindowTitle(focus),game_caption(gx));
    int count=0;const bool* keys=SDL_GetKeyboardState(&count);
    const bool pressed=product_active(gx) && focused &&
        keys && count>SDL_SCANCODE_F1 && keys[SDL_SCANCODE_F1];
    const bool open=pressed && !settings_key_held;
    settings_key_held=pressed;
    if(open)(void)session_settings(gx);
    wii_launcher_input_poll(&settings,pad);
    const bool controls_held=pressed || pad->buttons || pad->stick_x!=128 ||
        pad->stick_y!=128 || pad->substick_x!=128 || pad->substick_y!=128 ||
        wii_launcher_input_held_actions() || (focused && SDL_GetMouseState(NULL,NULL));
    session_controls_held=controls_held;session_input_releasing=input_release;
    if(input_release && !controls_held)input_release=false;
    gx->host_input_capture=input_release;
    if(input_release) {
        memset(pad,0,sizeof *pad);
        pad->stick_x=pad->stick_y=pad->substick_x=pad->substick_y=128;
    }
    SDL_Window* mouse=SDL_GetMouseFocus();
    session_cursor(product_pointing(gx) && focused && mouse &&
        !strcmp(SDL_GetWindowTitle(mouse),game_caption(gx)) &&
        gx->input.pointing && !input_release && !gx->scripted_input);
    product_host_actions(product_renderer_active(gx) && !gx->scripted_input && !input_release &&
        !(pad->buttons&0x10)?wii_launcher_input_held_actions():0);
}

static unsigned session_player_input(void* user,unsigned player,GcnllrPad* pad) {
    static bool releasing,held;
    WiiGx* gx=user;
    if(player==0) {
        session_input(user,pad);
        releasing=session_input_releasing;held=session_controls_held;
    } else {
        const unsigned flags=wii_launcher_input_poll_player(&settings,player,pad);
        held=held || pad->buttons || pad->stick_x!=128 || pad->stick_y!=128 ||
              pad->substick_x!=128 || pad->substick_y!=128;
        if(player==3) {
            input_release=releasing && held;
            gx->host_input_capture=input_release;
            if(input_release)session_cursor(false);
        }
        return flags;
    }
    return wii_launcher_input_player_flags(0);
}

bool wii_launcher_window(WiiGx* gx) {
    if(!active)return true;
    static SDL_WindowID applied_window;
    static int applied_scale,applied_fullscreen,applied_view;
    const bool mm9=product_renderer_active(gx);
    const bool menu=product_pointing(gx);
    /* The shared raster target also renders Wii Menu. Retain the selected
     * resolution across HOME/Start while its original layout stays unchanged. */
    const unsigned raster_scale=(mm9 || menu) && view_supported() && settings.internal_resolution>1?
        (unsigned)settings.internal_resolution/480:1;
    if(gx->resolution) {
        if(!gx->resolution->set_scale(raster_scale))return false;
    } else if(raster_scale!=1) {
        fprintf(stderr,"launcher: this graphics service does not support internal resolution\n");return false;
    }
    int count=0;SDL_Window** windows=SDL_GetWindows(&count);
    SDL_Window* window=NULL;
    for(int i=0;windows && i<count;++i)
        if(!strcmp(SDL_GetWindowTitle(windows[i]),game_caption(gx))){window=windows[i];break;}
    SDL_free(windows);
    if(!window)return false;
    if(menu) {
        int w=0,h=0;
        if(SDL_GetWindowSize(window,&w,&h))product_extent(gx,w,h,settings.aspect_index==3);
    }
    const SDL_WindowID window_id=SDL_GetWindowID(window);
    if(!window_id)return false;
    /* Title handoffs rebind controls on the retained physical window. Only a
     * new window or an explicit settings edit may replace its geometry. */
    const bool resize=applied_window!=window_id || applied_scale!=settings.window_scale ||
        applied_fullscreen!=settings.fullscreen || applied_view!=settings.aspect_index;
    if(mm9 && view_supported() && !product_set_view(gx,settings.aspect_index))return false;
    gx->host_input_user=gx;gx->host_input=session_input;
    gx->host_settings=mm9 || menu?session_settings:NULL;
    gx->host_review=mm9 || menu?session_review:NULL;
    gx->host_input_stop=session_stop;
    if(!wii_gx_set_player_input(gx,session_player_input,gx))return false;
    if(!wii_launcher_peripherals_bind(&gx->machine->ios))return false;
    if(menu && cursor_owner!=gx) {
        if(cursor_owner)SDL_RemoveEventWatch(session_cursor_event,cursor_owner);
        cursor_owner=NULL;
        if(!SDL_AddEventWatch(session_cursor_event,gx))return false;
        cursor_owner=gx;
    }
    /* Audio/input edits and Resume preserve a manually resized Fit window. */
    if(!resize)return true;
    const int width=640*settings.window_scale;
    const int height=settings.aspect_index==2?2*((width*9/21+1)/2):
        settings.aspect_index?2*((width*9/16+1)/2):480*settings.window_scale;
    bool ok=false;
    if(settings.fullscreen==2) {
        SDL_DisplayMode mode;
        if(SDL_GetClosestFullscreenDisplayMode(SDL_GetDisplayForWindow(window),
            width,height,0,false,&mode) && SDL_SetWindowFullscreenMode(window,&mode))
            ok=true;
    } else ok=SDL_SetWindowFullscreenMode(window,NULL);
    if(ok) {
        ok=SDL_SetWindowSize(window,width,height) &&
            SDL_SetWindowFullscreen(window,settings.fullscreen!=0);
    }
    if(ok) {
        applied_window=window_id;applied_scale=settings.window_scale;
        applied_fullscreen=settings.fullscreen;applied_view=settings.aspect_index;
    }
    if(!ok)fprintf(stderr,"launcher: unable to apply game window settings: %s\n",SDL_GetError());
    return ok;
}
bool wii_launcher_window_extent(WiiGx* gx,unsigned* w,unsigned* h,bool resize) {
    if(!gx || !w || !h || !SDL_IsMainThread())return false;
    int count=0;SDL_Window** windows=SDL_GetWindows(&count);SDL_Window* window=NULL;
    for(int i=0;windows && i<count;++i)
        if(!strcmp(SDL_GetWindowTitle(windows[i]),game_caption(gx))){window=windows[i];break;}
    SDL_free(windows);if(!window)return false;
    if(resize && (*w<240 || *h<240 || *w>8192 || *h>8192 || !SDL_SetWindowSize(window,*w,*h)))return false;
    int width=0,height=0;if(!SDL_GetWindowSize(window,&width,&height))return false;
    *w=width;*h=height;
    if(product_pointing(gx))product_extent(gx,*w,*h,settings.aspect_index==3);
    return true;
}
