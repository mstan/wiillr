/* SPDX-License-Identifier: MIT */
/* Exercise the production binder with an observing SDL boundary. No desktop
 * window is created; the startup fixture supplies title/UI validation stubs. */
#include "../src/launcher.c"
#define main startup_contract_main
#include "test_menu_launcher.c"
#undef main

static struct {SDL_WindowID id;const char* caption;int w,h;bool fullscreen;} fake={7,"Wii Menu",800,600,false};
static unsigned size_calls,fullscreen_calls;
static SDL_Window* fake_window(void){return (SDL_Window*)&fake;}
SDL_Window** SDL_GetWindows(int* count){SDL_Window** p=malloc(sizeof *p);CHECK(p);*p=fake_window();*count=1;return p;}
const char* SDL_GetWindowTitle(SDL_Window* window){CHECK(window==fake_window());return fake.caption;}
SDL_WindowID SDL_GetWindowID(SDL_Window* window){CHECK(window==fake_window());return fake.id;}
bool SDL_SetWindowSize(SDL_Window* window,int w,int h){CHECK(window==fake_window());++size_calls;fake.w=w;fake.h=h;return true;}
bool SDL_SetWindowFullscreen(SDL_Window* window,bool enabled){CHECK(window==fake_window());++fullscreen_calls;fake.fullscreen=enabled;return true;}
bool SDL_SetWindowFullscreenMode(SDL_Window* window,const SDL_DisplayMode* mode){CHECK(window==fake_window());(void)mode;return true;}
SDL_DisplayID SDL_GetDisplayForWindow(SDL_Window* window){CHECK(window==fake_window());return 1;}
bool SDL_GetClosestFullscreenDisplayMode(SDL_DisplayID display,int w,int h,float refresh,bool density,SDL_DisplayMode* mode){
    CHECK(display==1);(void)refresh;(void)density;memset(mode,0,sizeof *mode);mode->w=w;mode->h=h;return true;
}
bool SDL_AddEventWatch(SDL_EventFilter filter,void* user){CHECK(filter && user);return true;}
void SDL_RemoveEventWatch(SDL_EventFilter filter,void* user){CHECK(filter && user);}
bool SDL_CursorVisible(void){return true;}
bool SDL_ShowCursor(void){return true;}
bool SDL_HideCursor(void){return true;}
bool SDL_ShowWindow(SDL_Window* window){CHECK(window==fake_window());return true;}
bool SDL_HideWindow(SDL_Window* window){CHECK(window==fake_window());return true;}
bool SDL_IsMainThread(void){return true;}
SDL_Window* SDL_GetWindowFromID(SDL_WindowID id){return id==fake.id?fake_window():NULL;}
SDL_Window* SDL_GetKeyboardFocus(void){return NULL;}
SDL_Window* SDL_GetMouseFocus(void){return NULL;}
const bool* SDL_GetKeyboardState(int* count){*count=0;return NULL;}
SDL_MouseButtonFlags SDL_GetMouseState(float* x,float* y){(void)x;(void)y;return 0;}
SDL_Window* SDL_GL_GetCurrentWindow(void){return NULL;}
SDL_GLContext SDL_GL_GetCurrentContext(void){return NULL;}
bool SDL_GL_MakeCurrent(SDL_Window* window,SDL_GLContext context){(void)window;(void)context;return true;}
const char* SDL_GetError(void){return "synthetic SDL failure";}
void wii_launcher_input_stop(void* user){(void)user;}
unsigned wii_launcher_input_held_actions(void){return 0;}
void wii_launcher_mods_in_session(void){}
void wii_mm9_mods_host_actions(unsigned actions){(void)actions;}
bool gcn_host_audio_set_volume(u32 percent){return percent<=100;}
void wii_pacing_reset(WiiPacing* pacing){(void)pacing;}

int main(int argc,char** argv){
    CHECK(startup_contract_main(argc,argv)==0);
    WiiGx gx={0};gx.title=0x100000002ull;
    settings.window_scale=2;settings.fullscreen=0;settings.aspect_index=3;
    CHECK(wii_launcher_window(&gx) && size_calls==1 && fake.w==1280 && fake.h==720);
    /* A real user resize/fullscreen edit must survive title input rebinding. */
    fake.w=1136;fake.h=734;fake.fullscreen=true;
    gx.host_input=NULL;gx.title=0x1000157523945ull;fake.caption="Mega Man 9";
    CHECK(wii_launcher_window(&gx) && size_calls==1 && fullscreen_calls==1);
    CHECK(fake.w==1136 && fake.h==734 && fake.fullscreen);
    gx.host_input=NULL;gx.title=0x100000002ull;fake.caption="Wii Menu";
    CHECK(wii_launcher_window(&gx) && size_calls==1 && fullscreen_calls==1);
    CHECK(fake.w==1136 && fake.h==734 && fake.fullscreen);
    settings.volume=40;CHECK(wii_launcher_window(&gx) && size_calls==1);
    settings.window_scale=1;CHECK(wii_launcher_window(&gx) && size_calls==2 && fake.w==640 && fake.h==360 && !fake.fullscreen);
    settings.fullscreen=1;CHECK(wii_launcher_window(&gx) && size_calls==3 && fake.fullscreen);
    settings.aspect_index=0;CHECK(wii_launcher_window(&gx) && size_calls==4 && fake.h==480);
    ++fake.id;CHECK(wii_launcher_window(&gx) && size_calls==5);
    puts("Window binder: Menu/game/Menu preserves manual geometry; explicit settings and new windows apply");return 0;
}
