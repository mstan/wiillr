/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Menu-only display provider. The UI owns a copy of settings while open;
 * stage mod choices here and apply them after that copy is returned. */
static struct {
    bool enabled;
    int selected;
    char error[128];
} menu_mods;
static const char menu_mod_package[]="wii-menu.display";
static const char menu_mod_feature[]="internal-resolution";
static const char menu_grid_feature[]="adaptive-channel-grid";
static bool menu_grid_choice,menu_library_choice;
static unsigned menu_library_multiplier=2;
static const char menu_library_feature[]="expanded-channels";
static bool menu_library_identity(const char* package,const char* feature) {
    return package && feature && !strcmp(package,menu_mod_package) && !strcmp(feature,menu_library_feature);
}
static int menu_mod_feature_count(void* context){(void)context;return wii_menu_grid_supported()?3:1;}
static bool menu_grid_identity(const char* package,const char* feature) {
    return package && feature && !strcmp(package,menu_mod_package) && !strcmp(feature,menu_grid_feature);
}

static bool menu_mod_identity(const char* package,const char* feature) {
    return package && feature && !strcmp(package,menu_mod_package) &&
        !strcmp(feature,menu_mod_feature);
}
static int menu_mod_count(void* context){(void)context;return 1;}
static int menu_mod_package_get(void* context,int index,RecompLauncherCModPackage* out) {
    (void)context;if(index || !out)return 0;memset(out,0,sizeof *out);
    strcpy(out->id,menu_mod_package);strcpy(out->version,"1.0");
    strcpy(out->name,"Wii Menu enhancements");strcpy(out->license,"GPL-3.0-or-later");
    strcpy(out->description,"Display and channel-library enhancements for the Wii Menu.");
    out->enabled=menu_mods.enabled || menu_grid_choice || menu_library_choice;return 1;
}
static int menu_mod_get(void* context,int index,RecompLauncherCModFeature* out) {
    (void)context;if(index<0 || index>2 || !out)return 0;memset(out,0,sizeof *out);
    if(index==2) {
        if(!wii_menu_grid_supported())return 0;
        strcpy(out->id,menu_library_feature);strcpy(out->package_id,menu_mod_package);
        strcpy(out->package_name,"Wii Menu enhancements");strcpy(out->package_version,"1.0");
        strcpy(out->name,"Expanded channel library (experimental)");strcpy(out->group,"Channels");
        strcpy(out->description,"Add channel positions in multiples of the stock 48. Installed overflow channels appear automatically. Optional shortcuts live in channels.cfg in your copied NAND. Native saves and stock channel order stay separate. Restart Wii Menu to apply capacity changes.");
        out->enabled=menu_library_choice;out->option_count=1;
        const char* error=wii_menu_grid_library_error();
        if(error && *error)snprintf(out->status,sizeof out->status,"%s",error);
        else snprintf(out->status,sizeof out->status,menu_library_choice?"%u channel positions. Applies after restarting Wii Menu.":"Stock 48 channel positions.",48*menu_library_multiplier);
        return 1;
    }
    if(index==1) {
        if(!wii_menu_grid_supported())return 0;
        strcpy(out->id,menu_grid_feature);strcpy(out->package_id,menu_mod_package);
        strcpy(out->package_name,"Wii Menu display");strcpy(out->package_version,"1.0");
        strcpy(out->name,"Adaptive channel grid (experimental)");strcpy(out->group,"Display");
        strcpy(out->description,"Keep three rows and fit the channel columns to the window. Use Fit to screen, then resize the game window. Channel rearrangement is unavailable during this experiment; switch this mod off to rearrange channels.");
        out->enabled=menu_grid_choice;
        strcpy(out->status,menu_grid_choice?"Responsive columns with Fit to screen.":"Original channel grid.");return 1;
    }
    strcpy(out->id,menu_mod_feature);strcpy(out->package_id,menu_mod_package);
    strcpy(out->package_name,"Wii Menu display");strcpy(out->package_version,"1.0");
    strcpy(out->name,"Internal resolution");strcpy(out->group,"Display");
    strcpy(out->description,"Increase the resolution of geometry and effects. Bitmap artwork retains its original detail. Higher settings need more graphics memory and processing power.");
    out->enabled=menu_mods.enabled;out->option_count=1;
    strcpy(out->status,menu_mods.enabled?"Applies when you Launch or Resume.":"Original resolution (1x).");
    return 1;
}
static int menu_mod_enable(void* context,const char* package,const char* feature,int enabled) {
    if(menu_library_identity(package,feature)) {
        if(!wii_menu_grid_supported())return 0;
        menu_library_choice=enabled!=0;menu_mods.error[0]=0;return 1;
    }
    if(menu_grid_identity(package,feature)){
        if(!wii_menu_grid_supported())return 0;
        menu_grid_choice=enabled!=0;return 1;
    }
    (void)context;if(!menu_mod_identity(package,feature))return 0;
    menu_mods.enabled=enabled!=0;menu_mods.error[0]=0;return 1;
}
static int menu_mod_option(void* context,const char* package,const char* feature,int index,
    RecompLauncherCModOption* out) {
    (void)context;
    if(!index && out && menu_library_identity(package,feature)) {
        memset(out,0,sizeof *out);strcpy(out->id,"capacity");strcpy(out->label,"Channel capacity");
        strcpy(out->group,"Channels");strcpy(out->default_value,"2");
        strcpy(out->description,"Multiples of the original 48 positions. Three rows and the original page-slide animation are preserved.");
        out->type=RECOMP_MOD_OPTION_CHOICE;out->choice_count=7;
        snprintf(out->value,sizeof out->value,"%u",menu_library_multiplier);return 1;
    }
    if(index || !out || !menu_mod_identity(package,feature))return 0;
    memset(out,0,sizeof *out);strcpy(out->id,"resolution");strcpy(out->label,"Resolution");
    strcpy(out->group,"Display");strcpy(out->default_value,"1");
    strcpy(out->description,"Choose the rendering scale. Original bitmap artwork is unchanged.");
    out->type=RECOMP_MOD_OPTION_CHOICE;out->choice_count=6;
    snprintf(out->value,sizeof out->value,"%d",menu_mods.enabled?menu_mods.selected:1);
    return 1;
}
static int menu_mod_choice(void* context,const char* package,const char* feature,const char* option,
    int index,RecompLauncherCModChoice* out) {
    (void)context;
    if(out && option && !strcmp(option,"capacity") && index>=0 && index<7 && menu_library_identity(package,feature)) {
        unsigned multiplier=2u<<index;memset(out,0,sizeof *out);
        snprintf(out->label,sizeof out->label,"%ux stock (%u positions)",multiplier,48*multiplier);
        snprintf(out->value,sizeof out->value,"%u",multiplier);return 1;
    }
    if(!out || index<0 || index>=6 || !option || strcmp(option,"resolution") ||
        !menu_mod_identity(package,feature))return 0;
    memset(out,0,sizeof *out);strcpy(out->label,resolution_labels[index]);
    snprintf(out->value,sizeof out->value,"%d",resolution_values[index]);return 1;
}
static int menu_mod_set_option(void* context,const char* package,const char* feature,
    const char* option,const char* value) {
    (void)context;
    if(option && value && !strcmp(option,"capacity") && menu_library_identity(package,feature)) {
        for(unsigned multiplier=2;multiplier<=128;multiplier*=2) {
            char accepted[16];snprintf(accepted,sizeof accepted,"%u",multiplier);
            if(!strcmp(value,accepted)){menu_library_multiplier=multiplier;menu_mods.error[0]=0;return 1;}
        }
        strcpy(menu_mods.error,"Choose one of the listed capacities.");return 0;
    }
    if(!option || strcmp(option,"resolution") || !value ||
        !menu_mod_identity(package,feature))return 0;
    for(int index=0;index<6;++index) {
        char accepted[16];snprintf(accepted,sizeof accepted,"%d",resolution_values[index]);
        if(strcmp(value,accepted))continue;
        menu_mods.enabled=index!=0;
        if(index)menu_mods.selected=resolution_values[index];
        menu_mods.error[0]=0;return 1;
    }
    strcpy(menu_mods.error,"Choose one of the listed resolutions.");return 0;
}
static int menu_mod_commit(void* context,const char* image) {
    (void)context;(void)image;return 1;
}
static const char* menu_mod_error(void* context){(void)context;return menu_mods.error;}
static const RecompLauncherCModProvider menu_mod_provider={
    .package_count=menu_mod_count,.package_get=menu_mod_package_get,
    .commit=menu_mod_commit,.last_error=menu_mod_error,
    .feature_count=menu_mod_feature_count,.feature_get=menu_mod_get,
    .feature_option_get=menu_mod_option,.feature_choice_get=menu_mod_choice,
    .feature_enable=menu_mod_enable,.feature_set_option=menu_mod_set_option,
    .hide_hidden_features=1
};
static void menu_mod_prepare(RecompLauncherCGameInfo* game) {
    if(!game->has_supersampling)return;
    char path[32768];
    if(snprintf(path,sizeof path,"%s/adaptive-grid.cfg",launcher_directory)<(int)sizeof path) {
        FILE* f=fopen(path,"rb");if(f){menu_grid_choice=fgetc(f)=='1';fclose(f);}
    }
    wii_menu_grid_configure(menu_grid_choice);
    wii_menu_grid_library_configure(menu_library_choice?menu_library_multiplier:1);
    if(snprintf(path,sizeof path,"%s/channel-library.cfg",launcher_directory)<(int)sizeof path) {
        FILE* f=fopen(path,"rb");unsigned enabled,multiplier;
        if(f){if(fscanf(f,"%u %u",&enabled,&multiplier)==2 && enabled<=1 && multiplier>=2 && multiplier<=128 && !(multiplier&(multiplier-1))) {
            menu_library_choice=enabled!=0;menu_library_multiplier=multiplier;
        }fclose(f);}
    }
    wii_menu_grid_library_configure(menu_library_choice?menu_library_multiplier:1);
    menu_mods.enabled=false;menu_mods.error[0]=0;
    if(menu_mods.selected<=1)menu_mods.selected=resolution_values[1];
    for(int index=1;index<6;++index)if(settings.internal_resolution==resolution_values[index]) {
        menu_mods.enabled=true;menu_mods.selected=resolution_values[index];break;
    }
    game->mods=&menu_mod_provider;
    /* The same choice belongs in Mods rather than appearing twice. Keep the
     * vocabulary so a capability-enabled copy can persist the existing key. */
    game->has_supersampling=0;
}
static void menu_mod_finish(const RecompLauncherCGameInfo* game,
    const RecompLauncherCSettings* before) {
    if(game->mods!=&menu_mod_provider)return;
    RecompLauncherCSettings resolution_before=settings;
    resolution_before.internal_resolution=before->internal_resolution;
    settings.internal_resolution=menu_mods.enabled?menu_mods.selected:1;
    char path[32768];
    if(snprintf(path,sizeof path,"%s/adaptive-grid.cfg",launcher_directory)<(int)sizeof path) {
        FILE* f=fopen(path,"wb");
        if(f){fprintf(f,"%d\n",menu_grid_choice?1:0);fclose(f);}
    }
    wii_menu_grid_configure(menu_grid_choice);
    wii_menu_grid_library_configure(menu_library_choice?menu_library_multiplier:1);
    if(snprintf(path,sizeof path,"%s/channel-library.cfg",launcher_directory)<(int)sizeof path) {
        FILE* f=fopen(path,"wb");if(f){fprintf(f,"%u %u\n",menu_library_choice?1:0,menu_library_multiplier);fclose(f);}
    }
    RecompLauncherCGameInfo persistent=*game;persistent.has_supersampling=1;
    launcher_settings_save(&settings,&resolution_before,&persistent);
}
