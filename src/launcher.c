/* SPDX-License-Identifier: MIT */
#include "wiillr/launcher.h"
#include "wiillr/launcher_input.h"
#include "wiillr/launcher_peripherals.h"
#include "wiillr/launcher_mods.h"
#include "wiillr/package.h"
#include "wiillr/nand_setup.h"
#include "wiillr/mm9_mods.h"
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
static const unsigned char expected[1][32]={{
    0x67,0x8c,0x9e,0x1f,0x67,0xa9,0xb3,0x4c,0xd9,0x90,0x49,0xe2,0x99,0x75,0x1e,0x8d,
    0xc3,0x89,0x0b,0x1c,0xe8,0xfd,0x0e,0x09,0x17,0x9b,0x7e,0x1d,0x3c,0x71,0xfc,0x05}};
static bool active;
static RecompLauncherCGameInfo game_info;
static char config_path[32768],rom_cache_path[32768],boot_image[32768];
static struct {char nand[32768],key[32768],cache[32768];} dlc;
static char dlc_path[32768],dlc_status[128];
static const char* const dlc_patterns[]={"*.wad"};
static bool settings_key_held,input_release;
static bool session_controls_held,session_input_releasing;
static unsigned char menu_sha256[1][32];
static unsigned char menu_boot_sha256[32];
static bool menu_boot_verified;
static bool menu_package_started;
static WiiPackage* menu_package;
static char menu_system_status[1024];
static WiiNandSetupInfo menu_import_info;
static unsigned char menu_import_sha256[32];
static char menu_keys_path[1024];
static char launcher_directory[32768];
static const char* const view_labels[]={"Original 4:3","16:9","21:9","Fit to screen"};
static const char* const resolution_labels[]={"Original (1x)","2x","3x","4x","8x","16x"};
static const int resolution_values[]={1,960,1440,1920,3840,7680};
static int import_dlc(void* context,const char* path,char* error,size_t capacity) {
    (void)context;WiiWadInstallResult result={0};
    if(!wii_wad_install(path,dlc.key,dlc.nand,0x0001000577523945ull,&result,error,capacity))return 0;
    wii_launcher_mods_dlc_installed();
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
#include "menu_launcher_mods.inc"
static const char* game_caption(const WiiGx* gx) {
    return gx->title==0x0000000100000002ull?"Wii Menu":"Mega Man 9";
}

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
    settings.aspect_index=((game->name && !strcmp(game->name,"Wii Menu")) || view_supported())?3:0;
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

bool wii_launcher_menu_session(const char* image) {
    const char* directory=getenv("WIILLR_USER_DIR");char* pref=NULL;
    if(!directory || !*directory) {
        pref=SDL_GetPrefPath("RetroPortingToolKit","Wii Menu");directory=pref;
    }
    if(!directory || strlen(directory)>=sizeof launcher_directory ||
       !image || strlen(image)>=sizeof boot_image ||
       snprintf(config_path,sizeof config_path,"%s/config.ini",directory)>=(int)sizeof config_path ||
       snprintf(rom_cache_path,sizeof rom_cache_path,"%s/boot.cfg",directory)>=(int)sizeof rom_cache_path) {
        SDL_free(pref);return false;
    }
    strcpy(launcher_directory,directory);
    memset(menu_sha256,0,sizeof menu_sha256);
    if(*image) {
        size_t size=0;void* original=SDL_LoadFile(image,&size);
        if(!original){SDL_free(pref);return false;}
        recompui_sha256_compute(original,size,menu_sha256[0]);SDL_free(original);
    }
    RecompLauncherCGameInfo game={0};launcher_profile_apply("wii",&game);
    game.name="Wii Menu";game.num_players=4;game.config_path=config_path;
    game.rom_noun="Wii system";game.channel_grid_art=1;
    game.system_media_title="Wii system files";
    game.system_media_status=menu_system_status;
    game.system_media_note="Import a Wii Menu 4.3U NAND backup or an extracted NAND folder. System files and saved data use a managed copy; your original backup is preserved.";
    game.system_media_folders=1;game.host_persists_paths=1;
    game.rom_cache_path=rom_cache_path;game.settings_bindings=1;
    game.known_sha256=(const uint8_t(*)[32])menu_sha256;game.num_known_sha256=1;
    if(view_supported()) {
        game.has_supersampling=1;game.internal_resolution_labels=resolution_labels;
        game.internal_resolution_values=resolution_values;game.num_internal_resolutions=6;
        game.internal_resolution_note="Raster resolution for geometry and effects. Bitmap artwork retains its original detail.";
    }
    load_settings(&game);menu_mod_prepare(&game);
    if(!wii_launcher_peripherals_prepare(&game,directory)){SDL_free(pref);return false;}
    game_info=game;strcpy(boot_image,image);SDL_free(pref);
    char volume[8];snprintf(volume,sizeof volume,"%d",settings.enable_audio?settings.volume:0);
    if(!set_env("WIILLR_WINDOW_TITLE","Wii Menu") || !set_env("GCN_WINDOW","1") ||
       !set_env("GCN_AUDIO_VOLUME",volume))return false;
    wii_launcher_input_reset();
    GcnllrPad initial_input;wii_launcher_input_poll(&settings,&initial_input);
    active=true;return true;
}

static void menu_identity(const WiiNandSetupInfo* info) {
    snprintf(menu_system_status,sizeof menu_system_status,"%s%s%s",
        info->menu_label,info->region_label[0]?" · ":"",info->region_label);
}
static int import_menu_system(const char* source,char* out,size_t out_capacity,
                              char* error,size_t error_capacity) {
    if(!menu_package) {
        snprintf(error,error_capacity,"Use the packaged Wii Menu launcher to import system files.");return 0;
    }
    WiiNandSetupInfo info={0};
    if(!wii_package_import_system(menu_package,source,*menu_keys_path?menu_keys_path:NULL,&info,error,error_capacity))return 0;
    const int n=snprintf(out,out_capacity,"%s/%s",wii_package_nand(menu_package),info.boot_relative);
    if(n<0 || (size_t)n>=out_capacity) {
        snprintf(error,error_capacity,"The managed system path is too long.");return 0;
    }
    size_t size=0;void* data=SDL_LoadFile(out,&size);
    if(!data) {snprintf(error,error_capacity,"Could not read the imported Wii Menu.");return 0;}
    recompui_sha256_compute(data,size,menu_import_sha256);SDL_free(data);
    menu_import_info=info;return 1;
}
static void menu_imported(const char* boot_path) {
    (void)boot_path;
    memcpy(menu_sha256[0],menu_import_sha256,sizeof menu_import_sha256);
    menu_identity(&menu_import_info);
}

int wii_launcher_menu_package(WiiPackage* package,bool force,bool check_only) {
    if(!package || !wii_package_menu(package))return 2;
    char error[1024]={0},image[32768]={0};WiiNandSetupInfo info={0};
    const bool ready=wii_package_ready(package);
    if(ready) {
        if(!wii_package_refresh(package,&info,error,sizeof error)) {
            fprintf(stderr,"launcher: %s\n",error);return 2;
        }
        const int n=snprintf(image,sizeof image,"%s/%s",wii_package_nand(package),info.boot_relative);
        if(n<0 || n>=(int)sizeof image)return 2;
        menu_identity(&info);
    } else {
        (void)wii_package_refresh(package,&info,error,sizeof error);
        snprintf(menu_system_status,sizeof menu_system_status,"%s",
            *error?error:"System files have not been imported.");
    }
    if(check_only)return ready?0:2;
    if(!wii_launcher_menu_session(image))return 2;
    menu_package=package;
    static const char* const patterns[]={"*.bin"};
    game_info.rom_patterns=patterns;game_info.num_rom_patterns=1;
    game_info.rom_filter_desc="Wii NAND backup (nand.bin)";
    game_info.prepare_disc=import_menu_system;
    game_info.system_media_imported=menu_imported;
    game_info.system_media_keys_path=menu_keys_path;
    game_info.system_media_keys_capacity=sizeof menu_keys_path;
    game_info.prepare_busy_status="Checking and importing Wii system files...";
    game_info.prepare_success_status="Wii system files are ready.";
    const RecompLauncherCSettings before=settings;
    int action=RECOMP_LAUNCHER_RESULT_LAUNCH;
    char selected[32768];
    if(!ready || force || !settings.skip_launcher || !launch_devices_ready(&game_info))
        action=recomp_launcher_run_window("Wii Menu",&settings,&game_info,NULL,
                                          *image?image:NULL,selected,sizeof selected);
    menu_mod_finish(&game_info,&before);
    if(action==RECOMP_LAUNCHER_RESULT_QUIT)return 1;
    if(action!=RECOMP_LAUNCHER_RESULT_LAUNCH ||
       !wii_package_refresh(package,&info,error,sizeof error)) {
        fprintf(stderr,"launcher: %s\n",error);return 2;
    }
    menu_package_started=true;return 0;
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

static bool installed_valid(const char* image,const char* nand,unsigned uid,unsigned gid) {
    WiiFs* fs=wii_fs_mount(nand,uid,(uint16_t)gid);
    uint64_t title=0;uint32_t count=0;unsigned char view[0xd8];
    WiiTitleLaunch launch={0};int32_t result=0;
    /* Derive the accepted boot hash from the installed ticket/TMD contract,
     * never from an arbitrary file chosen in the launcher. */
    bool valid=fs && wii_fs_boot_title(fs,&title) &&
        wii_es_ticket_views(fs,title,view,1,&count)==0 && count==1 &&
        wii_es_prepare_launch(fs,title,view,&launch,&result);
    if(valid) {
        /* This first launcher consumer is Mega Man 9 (USA), IOS37. */
        valid=title==0x0001000157523945ull && launch.ios==0x100000025ull &&
            wii_launcher_verify(launch.image,launch.size);
    }
    wii_es_release_launch(&launch);
    if(fs)wii_fs_unmount(fs);
    return valid && verify_file(image);
}
static bool installed_menu_valid(const char* image,const char* nand,unsigned uid,unsigned gid,unsigned ios_profile) {
    menu_boot_verified=false;
    if(!image || !nand || gid>0xffffu)return false;
    WiiFs* fs=wii_fs_mount(nand,uid,(uint16_t)gid);
    uint64_t title=0;uint32_t count=0;unsigned char view[0xd8];
    WiiTitleLaunch launch={0};int32_t status=-1;
    bool valid=fs && wii_fs_boot_title(fs,&title) && title==0x0000000100000002ull &&
        wii_es_ticket_views(fs,title,view,1,&count)==0 && count==1 &&
        wii_es_prepare_launch(fs,title,view,&launch,&status) && status==0 &&
        launch.title==title && launch.uid==uid && launch.gid==gid &&
        (launch.ios==0x100000004ull || launch.ios==0x100000050ull) &&
        (!ios_profile || launch.ios==(0x100000000ull|ios_profile)) &&
        launch.image && launch.size>=0x100 && launch.size<=0x5800000;
    if(valid) {
        FILE* file=fopen(image,"rb");valid=false;
        if(file) {
            if(!fseek(file,0,SEEK_END) && ftell(file)==(long)launch.size && !fseek(file,0,SEEK_SET)) {
                unsigned char part[16384];size_t offset=0;valid=true;
                while(offset<launch.size) {
                    const size_t remaining=launch.size-offset,n=remaining<sizeof part?remaining:sizeof part;
                    if(fread(part,1,n,file)!=n || memcmp(part,launch.image+offset,n)){valid=false;break;}
                    offset+=n;
                }
                valid=valid && fgetc(file)==EOF && !ferror(file);
            }
            fclose(file);
        }
        if(valid){recompui_sha256_compute(launch.image,launch.size,menu_boot_sha256);menu_boot_verified=true;}
    }
    wii_es_release_launch(&launch);if(fs)wii_fs_unmount(fs);return valid;
}
bool wii_launcher_menu_verify(const void* image,size_t size) {
    if(!menu_boot_verified || !image || !size)return false;
    unsigned char hash[32];recompui_sha256_compute(image,size,hash);
    return !memcmp(hash,menu_boot_sha256,sizeof hash);
}
int wii_launcher_menu_start(const char* image,const char* nand,unsigned uid,unsigned gid,
    bool force,bool check_only,char* selected,size_t capacity,unsigned ios_profile) {
    if(!selected || !image || strlen(image)>=capacity ||
       !installed_menu_valid(image,nand,uid,gid,ios_profile)) {
        fprintf(stderr,"launcher: the installed Wii Menu title, content or IOS profile could not be verified\n");return 2;
    }
    strcpy(selected,image);
    if(check_only){puts("{\"launcher_title_verified\":true,\"window_opened\":false}");return 0;}
    if(menu_package_started) {
        menu_package_started=false;
        goto menu_selected;
    }
    if(!wii_launcher_menu_session(image))return 2;
    const RecompLauncherCSettings before=settings;
    int action=RECOMP_LAUNCHER_RESULT_LAUNCH;
    if(force || !settings.skip_launcher || !launch_devices_ready(&game_info))
        action=recomp_launcher_run_window("Wii Menu",&settings,&game_info,NULL,image,selected,capacity);
    menu_mod_finish(&game_info,&before);
    if(action==RECOMP_LAUNCHER_RESULT_QUIT)return 1;
    if(action!=RECOMP_LAUNCHER_RESULT_LAUNCH || !installed_menu_valid(selected,nand,uid,gid,ios_profile) ||
       strlen(selected)>=sizeof boot_image)return 2;
menu_selected:
    strcpy(boot_image,selected);
    char volume[8];snprintf(volume,sizeof volume,"%d",settings.enable_audio?settings.volume:0);
    if(!set_env("WIILLR_WINDOW","1") || !set_env("GCN_WINDOW","1") ||
       !set_env("WIILLR_WINDOW_TITLE","Wii Menu") || !set_env("GCN_AUDIO_VOLUME",volume))return 2;
    if(!getenv("GCN_AUDIO") && !set_env("GCN_AUDIO","1"))return 2;
    wii_launcher_input_reset();active=true;return 0;
}
static int import_base(const char* path,char* out,size_t capacity,char* error,size_t error_capacity) {
    WiiWadBaseInstallResult result={0};
    if(!wii_wad_install_base(path,dlc.key,dlc.nand,0x0001000157523945ull,&result,error,error_capacity))return 0;
    const int count=snprintf(out,capacity,"%s/%s",dlc.nand,result.boot_path);
    if(count<0 || (size_t)count>=capacity ||
       !installed_valid(out,dlc.nand,result.installed.uid,result.group_id)) {
        snprintf(error,error_capacity,"The installed Mega Man 9 game could not be verified.");return 0;
    }
    SDL_SaveFile(rom_cache_path,out,strlen(out));
    return 1;
}

int wii_launcher_start(const char* image,const char* nand,unsigned uid,unsigned gid,
    bool force,bool check_only,char* selected,size_t capacity) {
    const bool valid=installed_valid(image,nand,uid,gid);
    if(check_only && !valid) {
        fprintf(stderr,"launcher: import the supported Mega Man 9 (USA) WAD first\n");return 2;
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
    static const char* const wad_patterns[]={"*.wad"};
    game.rom_patterns=wad_patterns;game.num_rom_patterns=1;game.rom_filter_desc="Mega Man 9 (USA) game WAD (*.wad)";
    game.setup_wizard_supported=1;game.needs_setup=!valid;
    game.prepare_disc=import_base;game.prepare_use_selected_rom=1;
    game.prepare_required_before_continue=1;game.host_persists_paths=1;
    game.prepare_section_title="Import game";game.prepare_disc_label="Import game WAD";
    game.prepare_disc_note="Select your Mega Man 9 (USA) game WAD and import it. Add the DLC WAD from the main menu afterward.";
    game.prepare_busy_status="Importing Mega Man 9...";game.prepare_success_status="Mega Man 9 is ready.";
    if(!prepare_dlc(&game,nand,directory)){SDL_free(pref);return 2;}
    if(!wii_launcher_mods_prepare(&game,directory,nand)){SDL_free(pref);return 2;}
    if(!wii_launcher_peripherals_prepare(&game,directory)){SDL_free(pref);return 2;}
    game.settings_bindings=1;
    static const char* const actions[]={"Previous weapon (L)","Next weapon (R)"};
    game.assist_binding_labels=actions;game.assist_binding_count=2;
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
    load_settings(&game);
    int action=RECOMP_LAUNCHER_RESULT_LAUNCH;
    /* Keep the launcher's selected SDL instance valid for the game. */
    recomp_launcher_set_preserve_sdl(1);
    if(!valid || force || !settings.skip_launcher || !launch_devices_ready(&game))
        action=recomp_launcher_run_window("Mega Man 9",&settings,&game,NULL,
            valid?image:NULL,selected,capacity);
    SDL_free(pref);
    if(action==RECOMP_LAUNCHER_RESULT_QUIT)return 1;
    if(action!=RECOMP_LAUNCHER_RESULT_LAUNCH || !installed_valid(selected,nand,uid,gid)) {
        fprintf(stderr,"launcher: launch unavailable or selected content is unsupported\n");
        return 2;
    }
    if(strlen(selected)>=sizeof boot_image)return 2;
    strcpy(boot_image,selected);game_info=game;
    wii_launcher_mods_start();
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
    if((gx->title!=0x0001000157523945ull && gx->title!=0x0000000100000002ull) || volume>100)return false;
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
    wii_launcher_mods_in_session();
    char selected[32768];
    const int action=recomp_launcher_run_window(game_caption(gx),&settings,
        &in_session,NULL,boot_image,selected,sizeof selected);
    menu_mod_finish(&in_session,&before);
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
    const bool pressed=(gx->title==0x0001000157523945ull || gx->title==0x0000000100000002ull) && focused &&
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
    session_cursor(gx->title==0x0000000100000002ull && focused && mouse &&
        !strcmp(SDL_GetWindowTitle(mouse),game_caption(gx)) &&
        gx->input.pointing && !input_release && !gx->scripted_input);
    wii_mm9_mods_host_actions(gx->title==0x0001000157523945ull && !gx->scripted_input && !input_release &&
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
    const bool mm9=gx->title==0x0001000157523945ull;
    const bool menu=gx->title==0x0000000100000002ull;
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
    const SDL_WindowID window_id=SDL_GetWindowID(window);
    if(!window_id)return false;
    /* Title handoffs rebind controls on the retained physical window. Only a
     * new window or an explicit settings edit may replace its geometry. */
    const bool resize=applied_window!=window_id || applied_scale!=settings.window_scale ||
        applied_fullscreen!=settings.fullscreen || applied_view!=settings.aspect_index;
#if WIILLR_MM9_RENDERER
    if(mm9 && view_supported() && !wii_mm9_renderer_set_view(gx,(WiiMm9View)settings.aspect_index))return false;
#endif
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
