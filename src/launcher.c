/* SPDX-License-Identifier: MIT */
#include "wiillr/launcher.h"
#include "wiillr/fs.h"
#include "recomp_launcher.h"
#include "launcher_profile.h"
#include "launcher_settings.h"
#include "sha256.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static RecompLauncherCSettings settings;
static unsigned char expected[1][32];
static bool active;

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
    char config[32768];
    const char* directory=getenv("WIILLR_USER_DIR");
    char* pref=NULL;
    if(!directory || !*directory) {
        pref=SDL_GetPrefPath("RetroPortingToolKit","Mega Man 9");directory=pref;
    }
    if(!directory || snprintf(config,sizeof config,"%s/config.ini",directory)>=(int)sizeof config) {
        SDL_free(pref);return 2;
    }
    RecompLauncherCGameInfo game={0};
    launcher_profile_apply("wii",&game);
    game.name="Mega Man 9";game.region="USA";
    game.known_sha256=(const uint8_t(*)[32])expected;game.num_known_sha256=1;
    game.num_players=1;game.lock_device=1;game.config_path=config;
    /* Hide binding cards until the gameplay input path consumes edits. */
    game.settings_bindings=0;
    memset(&settings,0,sizeof settings);
    settings.window_scale=2;settings.enable_audio=1;settings.audio_freq=32000;
    settings.volume=100;settings.player_src[0]=1;
    launcher_settings_load(&settings,&game);
    int action=RECOMP_LAUNCHER_RESULT_LAUNCH;
    if(force || !settings.skip_launcher)
        action=recomp_launcher_run_window("Mega Man 9",&settings,&game,NULL,
            image,selected,capacity);
    SDL_free(pref);
    if(action==RECOMP_LAUNCHER_RESULT_QUIT)return 1;
    if(action!=RECOMP_LAUNCHER_RESULT_LAUNCH || !verify_file(selected)) {
        fprintf(stderr,"launcher: launch unavailable or selected content is unsupported\n");
        return 2;
    }
    char volume[8];snprintf(volume,sizeof volume,"%d",settings.volume);
    if(!set_env("WIILLR_WINDOW","1") || !set_env("GCN_WINDOW","1") ||
       !set_env("WIILLR_WINDOW_TITLE","Mega Man 9") ||
       !set_env("GCN_AUDIO_VOLUME",volume))return 2;
    /* An explicit automation override remains authoritative. */
    if(!getenv("GCN_AUDIO") && !set_env("GCN_AUDIO",settings.enable_audio?"1":"0"))return 2;
    active=true;return 0;
}

bool wii_launcher_window(void) {
    if(!active)return true;
    int count=0;SDL_Window** windows=SDL_GetWindows(&count);
    bool ok=false;
    for(int i=0;windows && i<count;++i) {
        SDL_Window* window=windows[i];
        if(strcmp(SDL_GetWindowTitle(window),"Mega Man 9"))continue;
        if(SDL_GetWindowFlags(window)&SDL_WINDOW_HIDDEN)continue;
        if(settings.fullscreen==2) {
            SDL_DisplayMode mode;
            if(!SDL_GetClosestFullscreenDisplayMode(SDL_GetDisplayForWindow(window),
                640*settings.window_scale,480*settings.window_scale,0,false,&mode) ||
               !SDL_SetWindowFullscreenMode(window,&mode))break;
        } else if(!SDL_SetWindowFullscreenMode(window,NULL))break;
        ok=SDL_SetWindowSize(window,640*settings.window_scale,480*settings.window_scale) &&
            SDL_SetWindowFullscreen(window,settings.fullscreen!=0);
        break;
    }
    SDL_free(windows);
    if(!ok)fprintf(stderr,"launcher: unable to apply game window settings: %s\n",SDL_GetError());
    return ok;
}
