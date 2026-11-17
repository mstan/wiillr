/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Headless boundary test: real launcher code/SHA, synthetic installed-title
 * provider, and an observing replacement for the UI window call. No SDL
 * initialization, controller access, window, dialog or original data. */
#include "wiillr/launcher.h"
#include "wiillr/menu_grid.h"
#include "wiillr/launcher_input.h"
#include "wiillr/fs.h"
#include "launcher_settings.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"Menu launcher failure line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned char installed[256];
static uint64_t title=0x100000002ull,ios=0x100000004ull;
static unsigned views=1,ui_calls,pref_calls,settings_calls,init_calls;
static unsigned mod_save_calls;
static int mod_saved_resolution,mod_before_resolution;
static bool rejected,skip;
static bool grid_enabled;
static unsigned grid_width,grid_height;
void wii_menu_grid_configure(bool enabled){grid_enabled=enabled;}
bool wii_menu_grid_supported(void){return true;}
static unsigned library_multiplier=1;
const char* wii_menu_grid_library_error(void){return "";}
bool wii_menu_grid_library_configure(unsigned multiplier){
    if(!multiplier || multiplier>128 || (multiplier&(multiplier-1)))return false;
    library_multiplier=multiplier;return true;
}
void wii_menu_grid_extent(unsigned w,unsigned h,bool fit){(void)fit;grid_width=w;grid_height=h;}
static bool peripheral_missing;
static RecompLauncherCPeripheral test_peripheral;
static int ui_action,launch_status;
static char directory[4096],pref_name[64],alternate[4096];
struct WiiFs {int unused;};static struct WiiFs filesystem;
WiiFs* wii_fs_mount(const char* nand,uint32_t uid,uint16_t gid){return nand && !strcmp(nand,"synthetic NAND") && uid==0 && gid==1?&filesystem:NULL;}
void wii_fs_unmount(WiiFs* fs){CHECK(fs==&filesystem);}
bool wii_fs_boot_title(WiiFs* fs,uint64_t* out){CHECK(fs==&filesystem);*out=title;return !rejected;}
int32_t wii_es_ticket_views(WiiFs* fs,uint64_t id,uint8_t* out,uint32_t capacity,uint32_t* count){
    CHECK(fs==&filesystem && id==title && capacity==1);*count=views;if(out)memset(out,0,0xd8);return 0;
}
bool wii_es_prepare_launch(WiiFs* fs,uint64_t id,const uint8_t* view,WiiTitleLaunch* launch,int32_t* result){
    CHECK(fs==&filesystem && id==title && view);*result=launch_status;
    *launch=(WiiTitleLaunch){title,ios,0,sizeof installed,1,malloc(sizeof installed)};
    CHECK(launch->image);memcpy(launch->image,installed,sizeof installed);return true;
}
void wii_es_release_launch(WiiTitleLaunch* launch){free(launch->image);launch->image=NULL;}
void* SDL_LoadFile(const char* path,size_t* size){
    FILE* f=fopen(path,"rb");if(!f)return NULL;CHECK(!fseek(f,0,SEEK_END));const long n=ftell(f);rewind(f);
    CHECK(n>=0);void* p=malloc((size_t)n+1);CHECK(p);CHECK(fread(p,1,(size_t)n,f)==(size_t)n);fclose(f);*size=(size_t)n;return p;
}
void SDL_free(void* p){free(p);}
char* SDL_GetPrefPath(const char* organization,const char* app){
    CHECK(!strcmp(organization,"RetroPortingToolKit"));++pref_calls;snprintf(pref_name,sizeof pref_name,"%s",app);
    char* p=malloc(strlen(directory)+1);CHECK(p);strcpy(p,directory);return p;
}
bool SDL_InitSubSystem(SDL_InitFlags flags){CHECK(flags==SDL_INIT_GAMEPAD);++init_calls;return false;}
bool SDL_HasGamepad(void){return false;}
void recomp_launcher_set_preserve_sdl(int keep){CHECK(keep==1);}
void wii_launcher_input_defaults(RecompLauncherCSettings* value){(void)value;}
void wii_launcher_input_reset(void){}
void wii_launcher_input_poll(void* user,GcnllrPad* pad){CHECK(user);memset(pad,0,sizeof *pad);pad->stick_x=pad->stick_y=pad->substick_x=pad->substick_y=128;}
bool wii_launcher_peripherals_prepare(RecompLauncherCGameInfo* game,const char* path){
    CHECK(game && path);test_peripheral.ready=!peripheral_missing;
    game->peripherals=&test_peripheral;game->num_peripherals=1;return true;
}
void launcher_settings_load(RecompLauncherCSettings* value,const RecompLauncherCGameInfo* game){
    ++settings_calls;CHECK(!strcmp(game->name,"Wii Menu"));CHECK(value->aspect_index==3);value->skip_launcher=skip;
}
void launcher_settings_save(const RecompLauncherCSettings* value,const RecompLauncherCSettings* before,
    const RecompLauncherCGameInfo* game){
    CHECK(game->has_supersampling && game->num_internal_resolutions==6);
    ++mod_save_calls;mod_saved_resolution=value->internal_resolution;mod_before_resolution=before->internal_resolution;
}
int recomp_launcher_run_window(const char* caption,RecompLauncherCSettings* value,const RecompLauncherCGameInfo* game,
    const char* assets,const char* initial,char* selected,size_t capacity){
    ++ui_calls;CHECK(value && !assets && initial && !strcmp(caption,"Wii Menu") && !strcmp(game->name,"Wii Menu"));
    CHECK(game->num_players==4 && game->settings_bindings && game->num_known_sha256==1);
    CHECK(game->channel_grid_art && game->system_media_title && game->system_media_folders);
    CHECK(!game->setup_wizard_supported && !game->prepare_disc && !game->import_dlc && !game->mods);
    CHECK(strstr(game->config_path,directory)==game->config_path);
    /* The user repairs an unavailable saved attachment before choosing Launch. */
    if(game->peripherals)game->peripherals[0].ready=1;
    CHECK(strlen(*alternate?alternate:initial)<capacity);strcpy(selected,*alternate?alternate:initial);return ui_action;
}
static void file(const char* path,const void* p,size_t size){FILE* f=fopen(path,"wb");CHECK(f);CHECK(fwrite(p,1,size,f)==size);CHECK(!fclose(f));}
int main(int argc,char** argv){
    CHECK(argc==2);snprintf(directory,sizeof directory,"%s",argv[1]);
#ifdef _WIN32
    CHECK(!_putenv_s("WIILLR_USER_DIR",""));
#else
    CHECK(!setenv("WIILLR_USER_DIR","",1));
#endif
    char image[4096],selected[4096];CHECK(snprintf(image,sizeof image,"%s/menu-launcher-synthetic.app",directory)<(int)sizeof image);
    for(unsigned n=0;n<sizeof installed;n++)installed[n]=(unsigned char)(n*13+5);file(image,installed,sizeof installed);
    CHECK(wii_launcher_menu_start(image,"synthetic NAND",0,1,false,true,selected,sizeof selected,4)==0);
    CHECK(!ui_calls && !pref_calls && !settings_calls && !init_calls && !strcmp(selected,image));
    CHECK(wii_launcher_menu_verify(installed,sizeof installed) && !wii_launcher_verify(installed,sizeof installed));
    unsigned char changed[256];memcpy(changed,installed,sizeof changed);changed[1]^=1;CHECK(!wii_launcher_menu_verify(changed,sizeof changed));
    CHECK(wii_launcher_menu_start(image,"synthetic NAND",0,1,false,false,selected,sizeof selected,4)==0);
    CHECK(ui_calls==1 && !strcmp(pref_name,"Wii Menu"));
    skip=true;ui_calls=0;CHECK(wii_launcher_menu_start(image,"synthetic NAND",0,1,false,false,selected,sizeof selected,4)==0 && !ui_calls);
    peripheral_missing=true;
    CHECK(wii_launcher_menu_start(image,"synthetic NAND",0,1,false,false,selected,sizeof selected,4)==0 && ui_calls==1);
    peripheral_missing=false;ui_calls=0;
    CHECK(wii_launcher_menu_start(image,"synthetic NAND",0,1,true,false,selected,sizeof selected,4)==0 && ui_calls==1);
    ui_action=RECOMP_LAUNCHER_RESULT_QUIT;CHECK(wii_launcher_menu_start(image,"synthetic NAND",0,1,true,false,selected,sizeof selected,4)==1);
    ui_action=RECOMP_LAUNCHER_RESULT_UNAVAILABLE;CHECK(wii_launcher_menu_start(image,"synthetic NAND",0,1,true,false,selected,sizeof selected,4)==2);
    ui_action=RECOMP_LAUNCHER_RESULT_LAUNCH;
    ios=0x100000050ull;CHECK(wii_launcher_menu_start(image,"synthetic NAND",0,1,false,true,selected,sizeof selected,80)==0);
    CHECK(wii_launcher_menu_start(image,"synthetic NAND",0,1,false,true,selected,sizeof selected,4)==2);
    ios=0x100000025ull;CHECK(wii_launcher_menu_start(image,"synthetic NAND",0,1,false,true,selected,sizeof selected,0)==2);
    ios=0x100000004ull;title=0x1000157523945ull;CHECK(wii_launcher_menu_start(image,"synthetic NAND",0,1,false,true,selected,sizeof selected,4)==2);
    title=0x100000002ull;launch_status=-1022;CHECK(wii_launcher_menu_start(image,"synthetic NAND",0,1,false,true,selected,sizeof selected,4)==2);
    launch_status=0;views=0;CHECK(wii_launcher_menu_start(image,"synthetic NAND",0,1,false,true,selected,sizeof selected,4)==2);views=1;
    CHECK(wii_launcher_menu_start(image,"synthetic NAND",1,1,false,true,selected,sizeof selected,4)==2);
    file(image,changed,sizeof changed);CHECK(wii_launcher_menu_start(image,"synthetic NAND",0,1,false,true,selected,sizeof selected,4)==2);
    CHECK(!wii_launcher_menu_verify(installed,sizeof installed));file(image,installed,sizeof installed);
    /* Direct CLI attachment initializes controls/settings but never calls the
     * startup UI; WIILLR_WINDOW=1 remains safe for unattended raw sessions. */
    ui_calls=0;CHECK(wii_launcher_menu_session(image) && !ui_calls);
    CHECK(!remove(image));puts("Menu launcher: installed identity/content, actual UI-call contract, Quit/Skip/check/raw-session passed");return 0;
}
