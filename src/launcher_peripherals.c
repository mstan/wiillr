/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "wiillr/launcher_peripherals.h"
#include "wiillr/peripherals.h"
#include <recomp_persistence/atomic_file.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static RecompLauncherCPeripheral slots[3];
static WiiIosNative* bound_ios;
static char settings_path[32768];
static const RecompLauncherCPeripheralKind usb_kinds[]={
    {"Disconnected",0},{"Keyboard",0},{"Mass storage image",1}};
static const RecompLauncherCPeripheralKind sd_kinds[]={
    {"No card",0},{"SD card image",1}};

static void config_from_slots(WiiPeripheralsConfig* out,int changed,
                              const RecompLauncherCPeripheralConfig* replacement) {
    memset(out,0,sizeof *out);
    for(int i=0;i<3;++i) {
        const RecompLauncherCPeripheralConfig* c=i==changed?replacement:&slots[i].config;
        if(i<2) out->usb[i]=(WiiUsbPortConfig){(WiiUsbKind)c->kind,c->image_path,c->read_only!=0};
        else out->sd=(WiiSdConfig){c->kind?c->image_path:"",c->read_only!=0};
    }
}

static void status(RecompLauncherCPeripheral* p,const RecompLauncherCPeripheralConfig* c) {
    snprintf(p->status,sizeof p->status,"%s",c->kind==0?"Disconnected":
        p->kinds[c->kind].needs_image?(bound_ios?(c->read_only?"Mounted read only":"Mounted with write access"):
        (c->read_only?"Read only · connects at launch":"Write access · connects at launch")):
        (bound_ios?"Host keyboard attached":"Keyboard · connects at launch"));
}

static bool persist(int changed,const RecompLauncherCPeripheralConfig* replacement,
                    char* error,size_t capacity) {
    char data[4096];size_t used=0;
    const char* names[]={"USB1","USB2","SD"};
    for(int i=0;i<3;++i) {
        const RecompLauncherCPeripheralConfig* c=i==changed?replacement:&slots[i].config;
        if(strchr(c->image_path,'\r') || strchr(c->image_path,'\n')) {
            snprintf(error,capacity,"The image path contains a line break.");return false;
        }
        const int n=snprintf(data+used,sizeof data-used,"%s %d %d %s\n",names[i],c->kind,
                             c->read_only!=0,c->image_path);
        if(n<0 || (size_t)n>=sizeof data-used) {
            snprintf(error,capacity,"The peripheral settings are too long.");return false;
        }
        used+=(size_t)n;
    }
    if(!recomp_atomic_file_store(settings_path,data,used)) {
        snprintf(error,capacity,"Could not save peripheral settings.");return false;
    }
    return true;
}

typedef struct {
    int slot;
    const RecompLauncherCPeripheralConfig* config;
} PendingSelection;
static bool commit_selection(void* context,char* error,size_t capacity) {
    const PendingSelection* pending=context;
    return persist(pending->slot,pending->config,error,capacity);
}

static int apply(void* context,int slot,const RecompLauncherCPeripheralConfig* replacement,
                 char* error,size_t capacity) {
    (void)context;
    if(slot<0 || slot>=3)return 0;
    WiiPeripheralsConfig next;config_from_slots(&next,slot,replacement);
    if(next.usb[0].kind==WII_USB_KEYBOARD && next.usb[1].kind==WII_USB_KEYBOARD) {
        snprintf(error,capacity,"The host keyboard is already assigned to the other USB port.");return 0;
    }
    WiiIosNative temporary={0};
    WiiIosNative* ios=bound_ios;
    WiiPeripheralsConfig available=next;
    if(!ios) {
        /* A saved missing image must be repairable independently of another
         * unavailable slot. No live devices exist in the startup launcher. */
        for(int i=0;i<3;++i)if(i!=slot && !slots[i].ready) {
            if(i<2)available.usb[i]=(WiiUsbPortConfig){0};else available.sd=(WiiSdConfig){0};
        }
        wii_ios_native_init(&temporary);ios=&temporary;
    }
    PendingSelection pending={slot,replacement};
    /* Save only after attachments have been prepared, before publishing any
     * replacement. A failed save retains old guest handles and pending I/O. */
    const bool ok=wii_ios_native_configure_peripherals_commit(ios,bound_ios?&next:&available,
        commit_selection,&pending,error,capacity);
    if(!bound_ios)wii_ios_native_destroy(&temporary);
    if(ok) {
        status(&slots[slot],replacement);
        if(bound_ios)for(int i=0;i<3;++i) {slots[i].ready=1;slots[i].error[0]=0;}
        else if(slot==0 && slots[1].config.kind==WII_USB_KEYBOARD && replacement->kind!=WII_USB_KEYBOARD &&
                !strcmp(slots[1].error,"The host keyboard is already assigned to USB 1.")) {
            slots[1].ready=1;slots[1].error[0]=0;
        }
    }
    return ok;
}

bool wii_launcher_peripherals_prepare(RecompLauncherCGameInfo* game,const char* directory) {
    if(!game || !directory || snprintf(settings_path,sizeof settings_path,"%s/peripherals.cfg",directory)
        >=(int)sizeof settings_path)return false;
    bound_ios=NULL;memset(slots,0,sizeof slots);
    for(int i=0;i<3;++i) {
        slots[i].label=i==0?"USB 1":i==1?"USB 2":"SD Card";
        slots[i].kinds=i<2?usb_kinds:sd_kinds;
        slots[i].num_kinds=i<2?3:2;
        slots[i].config.read_only=1;
    }
    slots[0].config.kind=1;
    SDL_PathInfo info;
    if(SDL_GetPathInfo(settings_path,&info)) {
        size_t size=0;char* data=SDL_LoadFile(settings_path,&size);
        bool valid=data && size<4096 && !memchr(data,0,size);
        unsigned seen=0;
        char* line=data;
        while(valid && line && *line) {
            char* end=strchr(line,'\n');if(end)*end=0;
            const size_t n=strlen(line);if(n && line[n-1]=='\r')line[n-1]=0;
            char name[8];int kind=-1,ro=-1,offset=0;
            valid=sscanf(line,"%7s %d %d %n",name,&kind,&ro,&offset)==3;
            const int slot=valid?(!strcmp(name,"USB1")?0:!strcmp(name,"USB2")?1:!strcmp(name,"SD")?2:-1):-1;
            valid=valid && slot>=0 && !(seen&(1u<<slot)) && kind>=0 &&
                kind<slots[slot].num_kinds && (ro==0 || ro==1) &&
                strlen(line+offset)<sizeof slots[slot].config.image_path;
            if(valid) {
                RecompLauncherCPeripheralConfig* c=&slots[slot].config;
                c->kind=kind;c->read_only=ro;strcpy(c->image_path,line+offset);
                valid=(!slots[slot].kinds[kind].needs_image || *c->image_path) &&
                      (slots[slot].kinds[kind].needs_image || !*c->image_path);
                seen|=1u<<slot;
            }
            line=end?end+1:NULL;
        }
        SDL_free(data);
        if(!valid || seen!=7) {
            fprintf(stderr,"launcher: peripheral settings are invalid; preserve the file and correct its device paths\n");
            return false;
        }
    }
    for(int i=0;i<3;++i) {
        WiiPeripheralsConfig c={0};
        const RecompLauncherCPeripheralConfig* s=&slots[i].config;
        if(i<2)c.usb[i]=(WiiUsbPortConfig){(WiiUsbKind)s->kind,s->image_path,s->read_only!=0};
        else c.sd=(WiiSdConfig){s->kind?s->image_path:"",s->read_only!=0};
        WiiIosNative temporary={0};wii_ios_native_init(&temporary);
        slots[i].ready=wii_ios_native_configure_peripherals(&temporary,&c,slots[i].error,sizeof slots[i].error);
        wii_ios_native_destroy(&temporary);
        if(slots[i].ready)status(&slots[i],s);
        else snprintf(slots[i].status,sizeof slots[i].status,"Image unavailable. Change the image or disconnect this slot.");
    }
    if(slots[0].config.kind==WII_USB_KEYBOARD && slots[1].config.kind==WII_USB_KEYBOARD) {
        slots[1].ready=0;
        snprintf(slots[1].error,sizeof slots[1].error,"The host keyboard is already assigned to USB 1.");
    }
    /* Validate the combined selection too: canonical aliases of one disk
     * cannot occupy multiple ports. Individual failures stay independently
     * repairable in the startup UI. */
    WiiPeripheralsConfig combined;config_from_slots(&combined,-1,NULL);
    for(int i=0;i<3;++i)if(!slots[i].ready) {
        if(i<2)combined.usb[i]=(WiiUsbPortConfig){0};else combined.sd=(WiiSdConfig){0};
    }
    WiiIosNative temporary={0};char error[256];wii_ios_native_init(&temporary);
    const bool ready=wii_ios_native_configure_peripherals(&temporary,&combined,error,sizeof error);
    wii_ios_native_destroy(&temporary);
    if(!ready)for(int i=0;i<3;++i)if(slots[i].kinds[slots[i].config.kind].needs_image) {
        slots[i].ready=0;snprintf(slots[i].error,sizeof slots[i].error,"%s",error);
    }
    game->peripherals_title="Wii Peripherals";game->peripherals=slots;game->num_peripherals=3;
    game->peripheral_apply=apply;game->peripheral_ctx=NULL;
    return true;
}

bool wii_launcher_peripherals_bind(WiiIosNative* ios) {
    char error[256];WiiPeripheralsConfig config;config_from_slots(&config,-1,NULL);
    if(!wii_ios_native_configure_peripherals(ios,&config,error,sizeof error)) {
        fprintf(stderr,"launcher: %s\n",error);return false;
    }
    bound_ios=ios;
    for(int i=0;i<3;++i)status(&slots[i],&slots[i].config);
    return true;
}
void wii_launcher_peripherals_unbind(void) {bound_ios=NULL;}
