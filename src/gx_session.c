/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "wiillr/gx_session.h"
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
static void release(void* library) {
#ifdef _WIN32
    FreeLibrary((HMODULE)library);
#else
    dlclose(library);
#endif
}
int wii_gx_handoff_begin(WiiGx* gx,const char* library,WiiGxHandoff* out) {
    if(!gx || !out || out->library)return -1;
    if(!gx->library)return 0;
#ifdef _WIN32
    GcWiiNativeSessionGetApi get=(GcWiiNativeSessionGetApi)GetProcAddress((HMODULE)gx->library,"gc_wii_native_session_get_api");
#else
    GcWiiNativeSessionGetApi get=(GcWiiNativeSessionGetApi)dlsym(gx->library,"gc_wii_native_session_get_api");
#endif
    const GcWiiNativeSessionApi* api=get?get(GC_WII_NATIVE_SESSION_ABI):NULL;
    if(!api)return 0;
    if(api->abi!=GC_WII_NATIVE_SESSION_ABI || !api->retain_next_stop ||
       !api->cancel_retention || !api->window_identity || !library)return -1;
    const u64 window=api->window_identity();if(!window)return -1;
#ifdef _WIN32
    void* retained=(void*)LoadLibraryExA(library,NULL,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
#else
    void* retained=dlopen(library,RTLD_NOW|RTLD_LOCAL);
#endif
    if(!retained)return -1;
    if(retained!=gx->library || !api->retain_next_stop()) {release(retained);return -1;}
    *out=(WiiGxHandoff){retained,api,window};return 1;
}
bool wii_gx_handoff_end(WiiGxHandoff* handoff,bool success) {
    if(!handoff)return false;
    if(!handoff->library)return true;
    const bool ok=!success || handoff->api->window_identity()==handoff->window_identity;
    if(!success || !ok)handoff->api->cancel_retention();
    void* library=handoff->library;memset(handoff,0,sizeof *handoff);
    release(library);return ok;
}
