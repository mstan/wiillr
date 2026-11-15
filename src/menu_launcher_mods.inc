/* SPDX-License-Identifier: MIT */
/* Menu-only display provider. The UI owns a copy of settings while open;
 * stage mod choices here and apply them after that copy is returned. */
static struct {
    bool enabled;
    int selected;
    bool wide_enabled;
    int wide_selected;
    char error[128];
} menu_mods;
static const char menu_mod_package[]="wii-menu.display";
static const char menu_mod_feature[]="internal-resolution";
static const char menu_mod_wide_feature[]="mm9-widescreen";

static int menu_mod_identity(const char* package,const char* feature) {
    if(!package || !feature || strcmp(package,menu_mod_package))return -1;
    if(!strcmp(feature,menu_mod_feature))return 0;
    return !strcmp(feature,menu_mod_wide_feature)?1:-1;
}
static int menu_mod_count(void* context){(void)context;return 1;}
static int menu_mod_feature_count(void* context){(void)context;return 2;}
static int menu_mod_package_get(void* context,int index,RecompLauncherCModPackage* out) {
    (void)context;if(index || !out)return 0;memset(out,0,sizeof *out);
    strcpy(out->id,menu_mod_package);strcpy(out->version,"1.0");
    strcpy(out->name,"Wii Menu display");strcpy(out->license,"MIT");
    strcpy(out->description,"Display enhancements for the Wii Menu and launched channels.");
    out->enabled=menu_mods.enabled || menu_mods.wide_enabled;return 1;
}
static int menu_mod_get(void* context,int index,RecompLauncherCModFeature* out) {
    (void)context;if(index<0 || index>1 || !out)return 0;memset(out,0,sizeof *out);
    strcpy(out->id,index?menu_mod_wide_feature:menu_mod_feature);strcpy(out->package_id,menu_mod_package);
    strcpy(out->package_name,"Wii Menu display");strcpy(out->package_version,"1.0");
    strcpy(out->name,index?"Mega Man 9 widescreen":"Internal resolution");strcpy(out->group,"Display");
    strcpy(out->description,index?
        "Extend Mega Man 9's scene to a wider view. Fit to screen adapts to the window. The Wii Menu's layout follows its own Wii Screen setting.":
        "Increase the resolution of geometry and effects in the Wii Menu and Mega Man 9. Bitmap artwork retains its original detail. Higher settings need more graphics memory and processing power.");
    out->enabled=index?menu_mods.wide_enabled:menu_mods.enabled;out->option_count=1;
    strcpy(out->status,out->enabled?"Applies when you Launch or Resume.":index?"Original view (4:3).":"Original resolution (1x).");
    return 1;
}
static int menu_mod_enable(void* context,const char* package,const char* feature,int enabled) {
    (void)context;const int index=menu_mod_identity(package,feature);if(index<0)return 0;
    if(index)menu_mods.wide_enabled=enabled!=0;else menu_mods.enabled=enabled!=0;
    menu_mods.error[0]=0;return 1;
}
static int menu_mod_option(void* context,const char* package,const char* feature,int index,
    RecompLauncherCModOption* out) {
    (void)context;const int kind=menu_mod_identity(package,feature);if(index || !out || kind<0)return 0;
    memset(out,0,sizeof *out);strcpy(out->id,kind?"view":"resolution");strcpy(out->label,kind?"View":"Resolution");
    strcpy(out->group,"Display");strcpy(out->default_value,kind?"3":"1");
    strcpy(out->description,kind?"Choose Mega Man 9's expanded view.":"Choose the rendering scale. Original bitmap artwork is unchanged.");
    out->type=RECOMP_MOD_OPTION_CHOICE;out->choice_count=kind?4:6;
    const int value=kind?(menu_mods.wide_enabled?menu_mods.wide_selected:0):(menu_mods.enabled?menu_mods.selected:1);
    snprintf(out->value,sizeof out->value,"%d",value);
    return 1;
}
static int menu_mod_choice(void* context,const char* package,const char* feature,const char* option,
    int index,RecompLauncherCModChoice* out) {
    (void)context;const int kind=menu_mod_identity(package,feature);
    if(!out || kind<0 || index<0 || index>=(kind?4:6) || !option || strcmp(option,kind?"view":"resolution"))return 0;
    memset(out,0,sizeof *out);strcpy(out->label,kind?view_labels[index]:resolution_labels[index]);
    snprintf(out->value,sizeof out->value,"%d",kind?index:resolution_values[index]);return 1;
}
static int menu_mod_set_option(void* context,const char* package,const char* feature,
    const char* option,const char* value) {
    (void)context;const int kind=menu_mod_identity(package,feature);
    if(kind<0 || !option || strcmp(option,kind?"view":"resolution") || !value)return 0;
    for(int index=0;index<(kind?4:6);++index) {
        char accepted[16];snprintf(accepted,sizeof accepted,"%d",kind?index:resolution_values[index]);
        if(strcmp(value,accepted))continue;
        if(kind) {
            menu_mods.wide_enabled=index!=0;
            if(index)menu_mods.wide_selected=index;
        } else {
            menu_mods.enabled=index!=0;
            if(index)menu_mods.selected=resolution_values[index];
        }
        menu_mods.error[0]=0;return 1;
    }
    strcpy(menu_mods.error,"Choose one of the listed options.");return 0;
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
    menu_mods.enabled=false;menu_mods.error[0]=0;
    if(menu_mods.selected<=1)menu_mods.selected=resolution_values[1];
    for(int index=1;index<6;++index)if(settings.internal_resolution==resolution_values[index]) {
        menu_mods.enabled=true;menu_mods.selected=resolution_values[index];break;
    }
    menu_mods.wide_enabled=settings.aspect_index>0 && settings.aspect_index<4;
    if(menu_mods.wide_enabled)menu_mods.wide_selected=settings.aspect_index;
    else if(menu_mods.wide_selected<1 || menu_mods.wide_selected>3)menu_mods.wide_selected=3;
    game->mods=&menu_mod_provider;
    /* The same choice belongs in Mods rather than appearing twice. Keep the
     * vocabulary so a capability-enabled copy can persist the existing key. */
    game->has_supersampling=0;
    game->aspect_labels=NULL;game->num_aspect_labels=0;
}
static void menu_mod_finish(const RecompLauncherCGameInfo* game,
    const RecompLauncherCSettings* before) {
    if(game->mods!=&menu_mod_provider)return;
    RecompLauncherCSettings resolution_before=settings;
    resolution_before.internal_resolution=before->internal_resolution;
    resolution_before.aspect_index=before->aspect_index;
    settings.internal_resolution=menu_mods.enabled?menu_mods.selected:1;
    settings.aspect_index=menu_mods.wide_enabled?menu_mods.wide_selected:0;
    RecompLauncherCGameInfo persistent=*game;persistent.has_supersampling=1;
    persistent.aspect_labels=view_labels;persistent.num_aspect_labels=4;
    launcher_settings_save(&settings,&resolution_before,&persistent);
}
