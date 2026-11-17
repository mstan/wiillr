/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "wiillr/launcher_mods.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(int argc,char** argv) {
    CHECK(argc==2);char root[32768],file[32768],invalid[32768];
    CHECK(snprintf(root,sizeof root,"%s/mm9-mod-settings-%llu",argv[1],(unsigned long long)SDL_GetTicksNS())<(int)sizeof root);
    CHECK(SDL_CreateDirectory(root));RecompLauncherCGameInfo game={0};
    CHECK(snprintf(invalid,sizeof invalid,"%s/not-a-nand",root)<(int)sizeof invalid);
    CHECK(SDL_SaveFile(invalid,"test",4));
    CHECK(wii_launcher_mods_prepare(&game,root,invalid));
    const RecompLauncherCModProvider* p=game.mods;CHECK(p && p->feature_count(p->ctx)==2);
    CHECK(p->feature_option_get && p->feature_set_option); /* complete shared feature surface */
    RecompLauncherCModFeature f;CHECK(p->feature_get(p->ctx,0,&f) && !f.enabled);
    CHECK(!p->feature_enable(p->ctx,"mm9.gameplay","character-switch",1));
    CHECK(*p->last_error(p->ctx));
    wii_launcher_mods_dlc_installed();
    CHECK(p->feature_enable(p->ctx,"mm9.gameplay","character-switch",1));
    CHECK(p->feature_enable(p->ctx,"mm9.gameplay","weapon-cycle",1));
    CHECK(p->commit(p->ctx,"unchanged-test-image"));wii_launcher_mods_start();
    wii_launcher_mods_in_session();
    CHECK(p->feature_enable(p->ctx,"mm9.gameplay","weapon-cycle",0));
    CHECK(p->feature_get(p->ctx,1,&f) && strstr(f.status,"Restart"));
    CHECK(p->commit(p->ctx,"unchanged-test-image"));
    CHECK(wii_launcher_mods_prepare(&game,root,invalid));
    CHECK(p->feature_get(p->ctx,0,&f) && !f.enabled); /* missing DLC never bypassed by saved choice */
    CHECK(p->feature_get(p->ctx,1,&f) && !f.enabled); /* independent persisted choice */
    CHECK(snprintf(file,sizeof file,"%s/gameplay-mods.cfg",root)<(int)sizeof file);
    const char windows[]="character-switch=1\r\nweapon-cycle=1\r\n";
    CHECK(SDL_SaveFile(file,windows,sizeof windows-1));
    CHECK(wii_launcher_mods_prepare(&game,root,invalid));
    CHECK(p->feature_get(p->ctx,0,&f) && !f.enabled);
    CHECK(p->feature_get(p->ctx,1,&f) && f.enabled); /* Windows text and persisted ON */
    CHECK(SDL_RemovePath(file));CHECK(SDL_RemovePath(invalid));CHECK(SDL_RemovePath(root));
    puts("MM9 mod selection, DLC gate, restart notice and persistence PASS");return 0;
}
