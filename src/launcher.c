/* SPDX-License-Identifier: MIT */
#include "wiillr/launcher.h"
#include "wiillr/launcher_input.h"
#include "wiillr/fs.h"
#include "wiillr/wad.h"
#include "recomp_launcher.h"
#include "launcher_profile.h"
#include "launcher_settings.h"
#include "sha256.h"
#include "host/host_audio.h"
#if WIILLR_MM9_RENDERER
#include "wiillr/mm9_renderer_hook.h"
#endif
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static RecompLauncherCSettings settings;
static RecompLauncherCSettings default_settings;
static unsigned char expected[1][32];
static bool active;
static RecompLauncherCGameInfo game_info;
static char config_path[32768],rom_cache_path[32768],boot_image[32768];
static struct {char nand[32768],key[32768],cache[32768];} dlc;
static char dlc_path[32768],dlc_status[128];
static const char* const dlc_patterns[]={"*.wad"};
static bool settings_key_held,input_release;
static const char* const view_labels[]={"Original 4:3","16:9","21:9","Fit to screen"};
static const char* const resolution_labels[]={"Original (1x)","2x","3x","4x","8x","16x"};
static const int resolution_values[]={1,960,1440,1920,3840,7680};
static int import_dlc(void* context,const char* path,char* error,size_t capacity) {
    (void)context;WiiWadInstallResult result={0};
    if(!wii_wad_install(path,dlc.key,dlc.nand,0x0001000577523945ull,&result,error,capacity))return 0;
    char temporary[32864];
    snprintf(temporary,sizeof temporary,"%s.%llu.tmp",dlc.cache,(unsigned long long)SDL_GetTicksNS());
    if(!SDL_SaveFile(temporary,path,strlen(path)) || !SDL_RenamePath(temporary,dlc.cache)) {
        SDL_RemovePath(temporary);fprintf(stderr,"DLC installed, but its source path could not be saved\n");
    }
    snprintf(dlc_status,sizeof dlc_status,"Installed");return 1;
}
static bool prepare_dlc(RecompLauncherCGameInfo* game,const char* nand,const char* directory) {
    const char* key=getenv("WIILLR_COMMON_KEY");const char* base=SDL_GetBasePath();
    if(!base || strlen(nand)>=sizeof dlc.nand ||
       snprintf(dlc.cache,sizeof dlc.cache,"%s/dlc.cfg",directory)>=(int)sizeof dlc.cache)return false;
    strcpy(dlc.nand,nand);
    const int length=key && *key?snprintf(dlc.key,sizeof dlc.key,"%s",key):
        snprintf(dlc.key,sizeof dlc.key,"%ssystem/wii-common-key.bin",base);
    if(length<0 || length>=(int)sizeof dlc.key)return false;
    dlc_path[0]=0;size_t size=0;void* saved=SDL_LoadFile(dlc.cache,&size);
    if(saved && size<sizeof dlc_path && !memchr(saved,0,size)){memcpy(dlc_path,saved,size);dlc_path[size]=0;}
    SDL_free(saved);snprintf(dlc_status,sizeof dlc_status,"Optional");
    WiiFs* fs=wii_fs_mount(nand,0,0);
    if(fs) {
        unsigned count=0;
        if(!wii_es_ticket_views(fs,0x0001000577523945ull,NULL,0,&count) && count) {
            unsigned valid=0;for(unsigned n=1;n<=5;n++)if(wii_es_verify_content(fs,0x0001000577523945ull,n))valid++;
            if(valid)snprintf(dlc_status,sizeof dlc_status,"Installed (%u packs)",valid);
        }
        wii_fs_unmount(fs);
    }
    game->import_dlc=import_dlc;game->dlc_path=dlc_path;game->dlc_status=dlc_status;
    game->dlc_patterns=dlc_patterns;game->num_dlc_patterns=1;game->dlc_filter_desc="Wii downloadable content (*.wad)";
    return true;
}
static bool view_supported(void) {
#if WIILLR_MM9_RENDERER
    return wii_mm9_renderer_view_supported();
#else
    return false;
#endif
}
static const char* game_caption(const WiiGx* gx) {
    return gx->title==0x0000000100000002ull?"Wii Menu":"Mega Man 9";
}

static bool set_env(const char* name,const char* value) {
#ifdef _WIN32
    return _putenv_s(name,value)==0;
#else
    return setenv(name,value,1)==0;
#endif
}

bool wii_launcher_verify(const void* image,size_t size) {
    unsigned char actual[32];
    recompui_sha256_compute(image,size,actual);
    return !memcmp(actual,expected[0],sizeof actual);
}

static bool verify_file(const char* path) {
    FILE* file=fopen(path,"rb");
    if(!file)return false;
    bool ok=false;
    if(!fseek(file,0,SEEK_END)) {
        long size=ftell(file);
        if(size>=0x100 && size<=0x5800000 && !fseek(file,0,SEEK_SET)) {
            void* image=malloc((size_t)size);
            if(image) {
                ok=fread(image,1,(size_t)size,file)==(size_t)size &&
                    wii_launcher_verify(image,(size_t)size);
                free(image);
            }
        }
    }
    fclose(file);return ok;
}

int wii_launcher_start(const char* image,const char* nand,unsigned uid,unsigned gid,
    bool force,bool check_only,char* selected,size_t capacity) {
    WiiFs* fs=wii_fs_mount(nand,uid,(uint16_t)gid);
    uint64_t title=0;uint32_t count=0;unsigned char view[0xd8];
    WiiTitleLaunch launch={0};int32_t result=0;
    /* Derive the accepted boot hash from the installed ticket/TMD contract,
     * never from an arbitrary file chosen in the launcher. */
    bool valid=fs && wii_fs_boot_title(fs,&title) &&
        wii_es_ticket_views(fs,title,view,1,&count)==0 && count==1 &&
        wii_es_prepare_launch(fs,title,view,&launch,&result);
    if(valid) {
        recompui_sha256_compute(launch.image,launch.size,expected[0]);
        /* This first launcher consumer is Mega Man 9 (USA), IOS37. */
        valid=title==0x0001000157523945ull && launch.ios==0x100000025ull;
    }
    wii_es_release_launch(&launch);
    if(fs)wii_fs_unmount(fs);
    if(!valid || !verify_file(image)) {
        fprintf(stderr,"launcher: installed Mega Man 9 boot content could not be verified\n");
        return 2;
    }
    if(strlen(image)>=capacity)return 2;
    strcpy(selected,image);
    if(check_only) {
        puts("{\"launcher_title_verified\":true,\"window_opened\":false}");
        return 0;
    }
    const char* directory=getenv("WIILLR_USER_DIR");
    char* pref=NULL;
    if(!directory || !*directory) {
        pref=SDL_GetPrefPath("RetroPortingToolKit","Mega Man 9");directory=pref;
    }
    if(!directory || snprintf(config_path,sizeof config_path,"%s/config.ini",directory)>=(int)sizeof config_path ||
       snprintf(rom_cache_path,sizeof rom_cache_path,"%s/boot.cfg",directory)>=(int)sizeof rom_cache_path) {
        SDL_free(pref);return 2;
    }
    RecompLauncherCGameInfo game={0};
    launcher_profile_apply("wii",&game);
    game.name="Mega Man 9";game.region="USA";
    game.known_sha256=(const uint8_t(*)[32])expected;game.num_known_sha256=1;
    game.num_players=1;game.config_path=config_path;game.rom_cache_path=rom_cache_path;
    if(!prepare_dlc(&game,nand,directory)){SDL_free(pref);return 2;}
    game.settings_bindings=1;
    if(view_supported()) {
        game.has_supersampling=1;
        game.internal_resolution_labels=resolution_labels;
        game.internal_resolution_values=resolution_values;
        game.num_internal_resolutions=6;
        game.internal_resolution_note="Raster resolution for geometry and effects. Bitmap artwork retains its original detail.";
        game.aspect_labels=view_labels;game.num_aspect_labels=4;
        game.aspect_setting_label="View";
        game.aspect_setting_help="Fit to screen adapts the gameplay view to the window. Original 4:3 keeps the original view.";
    }
    memset(&settings,0,sizeof settings);
    settings.window_scale=2;settings.enable_audio=1;settings.audio_freq=32000;
    settings.volume=100;settings.player_src[0]=1;
    settings.aspect_index=view_supported()?3:0;
    settings.internal_resolution=1;
    if(SDL_InitSubSystem(SDL_INIT_GAMEPAD) && SDL_HasGamepad())settings.player_src[0]=2;
    wii_launcher_input_defaults(&settings);
    default_settings=settings;
    game.default_settings=&default_settings;
    launcher_settings_load(&settings,&game);
    int action=RECOMP_LAUNCHER_RESULT_LAUNCH;
    /* Keep the launcher's selected SDL instance valid for the game. */
    recomp_launcher_set_preserve_sdl(1);
    if(force || !settings.skip_launcher)
        action=recomp_launcher_run_window("Mega Man 9",&settings,&game,NULL,
            image,selected,capacity);
    SDL_free(pref);
    if(action==RECOMP_LAUNCHER_RESULT_QUIT)return 1;
    if(action!=RECOMP_LAUNCHER_RESULT_LAUNCH || !verify_file(selected)) {
        fprintf(stderr,"launcher: launch unavailable or selected content is unsupported\n");
        return 2;
    }
    if(strlen(selected)>=sizeof boot_image)return 2;
    strcpy(boot_image,selected);game_info=game;
    char volume[8];snprintf(volume,sizeof volume,"%d",settings.enable_audio?settings.volume:0);
    if(!set_env("WIILLR_WINDOW","1") || !set_env("GCN_WINDOW","1") ||
       !set_env("WIILLR_WINDOW_TITLE","Mega Man 9") ||
       !set_env("GCN_AUDIO_VOLUME",volume))return 2;
    /* Keep the host session available for live unmute; the user's audio
     * switch controls host gain. Guest audio and explicit QA overrides stay. */
    if(!getenv("GCN_AUDIO") && !set_env("GCN_AUDIO","1"))return 2;
    wii_launcher_input_reset();active=true;return 0;
}

static bool session_review(void* user,bool visible,u32 volume) {
    WiiGx* gx=user;
    if(gx->title!=0x0001000157523945ull || volume>100)return false;
    int count=0;SDL_Window** windows=SDL_GetWindows(&count);bool ok=false;
    for(int i=0;windows && i<count;++i) {
        if(strcmp(SDL_GetWindowTitle(windows[i]),game_caption(gx)))continue;
        ok=visible?(SDL_SetWindowFullscreen(windows[i],false) && SDL_ShowWindow(windows[i])):
            SDL_HideWindow(windows[i]);
        break;
    }
    SDL_free(windows);
    if(!ok || !gcn_host_audio_set_volume(volume))return false;
    settings.enable_audio=1;settings.volume=(int)volume;
    wii_pacing_reset(&gx->machine->pacing);return true;
}
static bool session_settings(void* user) {
    WiiGx* gx=user;
    /* This callback runs between fields on the simulation/graphics owner.
     * The shared in-session launcher freezes that owner until RESUME. */
    if(!gx->api->drain()) {gx->cpu->halted=true;return false;}
    SDL_Window* gl_window=SDL_GL_GetCurrentWindow();
    SDL_GLContext gl_context=SDL_GL_GetCurrentContext();
    RecompLauncherCGameInfo in_session=game_info;in_session.in_session=1;
    char selected[32768];
    const int action=recomp_launcher_run_window("Mega Man 9 Settings",&settings,
        &in_session,NULL,boot_image,selected,sizeof selected);
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
    const bool pressed=gx->title==0x0001000157523945ull && focused &&
        keys && count>SDL_SCANCODE_F1 && keys[SDL_SCANCODE_F1];
    const bool open=pressed && !settings_key_held;
    settings_key_held=pressed;
    if(open)(void)session_settings(gx);
    wii_launcher_input_poll(&settings,pad);
    const bool controls_held=pressed || pad->buttons || pad->stick_x!=128 ||
        pad->stick_y!=128 || pad->substick_x!=128 || pad->substick_y!=128 ||
        (focused && SDL_GetMouseState(NULL,NULL));
    if(input_release && !controls_held)input_release=false;
    gx->host_input_capture=input_release;
    if(input_release) {
        memset(pad,0,sizeof *pad);
        pad->stick_x=pad->stick_y=pad->substick_x=pad->substick_y=128;
    }
}

bool wii_launcher_window(WiiGx* gx) {
    if(!active)return true;
    static int applied_scale,applied_fullscreen,applied_view;
    const bool mm9=gx->title==0x0001000157523945ull;
    const int view=mm9 && view_supported()?settings.aspect_index:0;
    const unsigned raster_scale=mm9 && view_supported() && settings.internal_resolution>1?
        (unsigned)settings.internal_resolution/480:1;
    if(gx->resolution) {
        if(!gx->resolution->set_scale(raster_scale))return false;
    } else if(raster_scale!=1) {
        fprintf(stderr,"launcher: this graphics service does not support internal resolution\n");return false;
    }
    const bool resize=gx->host_input!=session_input || applied_scale!=settings.window_scale ||
        applied_fullscreen!=settings.fullscreen || applied_view!=view;
#if WIILLR_MM9_RENDERER
    if(mm9 && view_supported() && !wii_mm9_renderer_set_view(gx,(WiiMm9View)view))return false;
#endif
    gx->host_input_user=gx;gx->host_input=session_input;
    gx->host_settings=gx->title==0x0001000157523945ull?session_settings:NULL;
    gx->host_review=gx->title==0x0001000157523945ull?session_review:NULL;
    gx->host_input_stop=wii_launcher_input_stop;
    /* Audio/input edits and Resume preserve a manually resized Fit window. */
    if(!resize)return true;
    const int width=640*settings.window_scale;
    const int height=view==2?2*((width*9/21+1)/2):
        view?2*((width*9/16+1)/2):480*settings.window_scale;
    int count=0;SDL_Window** windows=SDL_GetWindows(&count);
    bool ok=false;
    for(int i=0;windows && i<count;++i) {
        SDL_Window* window=windows[i];
        if(strcmp(SDL_GetWindowTitle(window),game_caption(gx)))continue;
        if(settings.fullscreen==2) {
            SDL_DisplayMode mode;
            if(!SDL_GetClosestFullscreenDisplayMode(SDL_GetDisplayForWindow(window),
                width,height,0,false,&mode) ||
               !SDL_SetWindowFullscreenMode(window,&mode))break;
        } else if(!SDL_SetWindowFullscreenMode(window,NULL))break;
        ok=SDL_SetWindowSize(window,width,height) &&
            SDL_SetWindowFullscreen(window,settings.fullscreen!=0);
        break;
    }
    SDL_free(windows);
    if(ok) {
        applied_scale=settings.window_scale;applied_fullscreen=settings.fullscreen;applied_view=view;
    }
    if(!ok)fprintf(stderr,"launcher: unable to apply game window settings: %s\n",SDL_GetError());
    return ok;
}
