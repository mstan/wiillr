/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "wiillr/launcher_mods.h"
#include "wiillr/mm9_mods.h"
#include "wiillr/fs.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* package="mm9.gameplay";
static const char* ids[]={"character-switch","weapon-cycle"};
static const char* names[]={"Mega Man / Proto Man switch","Quick weapon switching"};
static const char* descriptions[]={
    "Tap Select to teleport between characters with separate health and weapon energy. Hold Select for two seconds for the leaderboard. Requires the installed Proto Man DLC.",
    "L/R cycles acquired weapons during gameplay. Bind Previous/Next weapon under Controller. Default shoulders: L1/R1; Home: L2."};
static struct {int staged[2],running[2];bool dlc,in_session;char path[32768],error[256];} state;
static int feature_index(const char* pkg,const char* id) {
    if(!pkg || !id || strcmp(pkg,package))return -1;
    for(int n=0;n<2;n++)if(!strcmp(id,ids[n]))return n;
    return -1;
}
static int count(void* context){(void)context;return 2;}
static int package_count(void* context){(void)context;return 1;}
static int package_get(void* context,int n,RecompLauncherCModPackage* out) {
    (void)context;if(n || !out)return 0;memset(out,0,sizeof *out);
    snprintf(out->id,sizeof out->id,"%s",package);strcpy(out->version,"1.0");
    strcpy(out->name,"Mega Man 9 gameplay");strcpy(out->license,"GPL-3.0-or-later");
    strcpy(out->description,"Optional character exchange and shoulder weapon selection.");
    out->enabled=state.staged[0] || state.staged[1];return 1;
}
static int get(void* context,int n,RecompLauncherCModFeature* out) {
    (void)context;if(n<0 || n>1 || !out)return 0;memset(out,0,sizeof *out);
    snprintf(out->id,sizeof out->id,"%s",ids[n]);snprintf(out->package_id,sizeof out->package_id,"%s",package);
    strcpy(out->package_version,"1.0");strcpy(out->package_name,"Mega Man 9 gameplay");
    snprintf(out->name,sizeof out->name,"%s",names[n]);snprintf(out->description,sizeof out->description,"%s",descriptions[n]);
    strcpy(out->group,"Gameplay");out->enabled=state.staged[n];
    if(!n && !state.dlc)strcpy(out->status,"Install the Proto Man DLC to enable.");
    else if(state.in_session && state.staged[n]!=state.running[n])strcpy(out->status,"Restart the game to apply.");
    else strcpy(out->status,state.staged[n]?"Enabled":"Disabled");
    return 1;
}
static int enable(void* context,const char* pkg,const char* id,int on) {
    (void)context;int n=feature_index(pkg,id);state.error[0]=0;
    if(n<0)return 0;
    if(!n && on && !state.dlc){strcpy(state.error,"Install the Proto Man DLC first.");return 0;}
    state.staged[n]=on!=0;return 1;
}
static int commit(void* context,const char* image) {
    (void)context;(void)image;state.error[0]=0;
    if(state.staged[0] && !state.dlc){strcpy(state.error,"The Proto Man DLC is unavailable.");return 0;}
    char data[96],temporary[32864];int size=snprintf(data,sizeof data,"character-switch=%d\nweapon-cycle=%d\n",state.staged[0],state.staged[1]);
    snprintf(temporary,sizeof temporary,"%s.%llu.tmp",state.path,(unsigned long long)SDL_GetTicksNS());
    if(!SDL_SaveFile(temporary,data,(size_t)size) || !SDL_RenamePath(temporary,state.path)) {
        SDL_RemovePath(temporary);snprintf(state.error,sizeof state.error,"Cannot save mod settings: %.180s",SDL_GetError());return 0;
    }
    return 1;
}
static const char* last_error(void* context){(void)context;return state.error;}
static int option_get(void* context,const char* pkg,const char* id,int n,RecompLauncherCModOption* out) {
    (void)context;(void)pkg;(void)id;(void)n;(void)out;return 0;
}
static int choice_get(void* context,const char* pkg,const char* id,const char* option,int n,RecompLauncherCModChoice* out) {
    (void)context;(void)pkg;(void)id;(void)option;(void)n;(void)out;return 0;
}
static int set_option(void* context,const char* pkg,const char* id,const char* option,const char* value) {
    (void)context;(void)pkg;(void)id;(void)option;(void)value;
    strcpy(state.error,"This feature has no additional options.");return 0;
}
static const RecompLauncherCModProvider provider={.package_count=package_count,.package_get=package_get,
    .feature_count=count,.feature_get=get,.feature_enable=enable,.feature_option_get=option_get,
    .feature_choice_get=choice_get,.feature_set_option=set_option,.commit=commit,.last_error=last_error};

bool wii_launcher_mods_prepare(RecompLauncherCGameInfo* game,const char* directory,const char* nand) {
    memset(&state,0,sizeof state);
    if(snprintf(state.path,sizeof state.path,"%s/gameplay-mods.cfg",directory)>=(int)sizeof state.path)return false;
    size_t size=0;char* saved=SDL_LoadFile(state.path,&size);
    if(saved && size<4096 && !memchr(saved,0,size)) {
        char* line=saved;
        while(*line) {
            char* end=strchr(line,'\n');if(end)*end=0;
            size_t line_size=strlen(line);if(line_size && line[line_size-1]=='\r')line[line_size-1]=0;
            for(int n=0;n<2;n++){size_t length=strlen(ids[n]);if(!strncmp(line,ids[n],length) && line[length]=='=' &&
                (line[length+1]=='0' || line[length+1]=='1') && !line[length+2])state.staged[n]=line[length+1]-'0';}
            if(!end)break;line=end+1;
        }
    }
    SDL_free(saved);
    WiiFs* fs=wii_fs_mount(nand,0,0);
    if(fs) {
        unsigned views=0;
        state.dlc=!wii_es_ticket_views(fs,0x0001000577523945ull,NULL,0,&views) && views &&
            wii_es_verify_content(fs,0x0001000577523945ull,1);
        wii_fs_unmount(fs);
    }
    /* A missing pack never turns a saved preference into an unlocked mode. */
    if(!state.dlc)state.staged[0]=0;
    game->mods=&provider;return true;
}
void wii_launcher_mods_start(void) {
    memcpy(state.running,state.staged,sizeof state.running);
    wii_mm9_mods_configure(state.running[0]!=0,state.running[1]!=0);
}
void wii_launcher_mods_in_session(void){state.in_session=true;}
void wii_launcher_mods_dlc_installed(void){state.dlc=true;}
