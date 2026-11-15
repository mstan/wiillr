/* SPDX-License-Identifier: MIT */
#include "wiillr/gx_session.h"
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
static void* load(const char* path) {return (void*)LoadLibraryA(path);}
static void unload(void* module) {FreeLibrary((HMODULE)module);}
static void* symbol(void* module,const char* name) {return (void*)GetProcAddress((HMODULE)module,name);}
#else
#include <dlfcn.h>
static void* load(const char* path) {return dlopen(path,RTLD_NOW|RTLD_LOCAL);}
static void unload(void* module) {dlclose(module);}
static void* symbol(void* module,const char* name) {return dlsym(module,name);}
#endif
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(int argc,char** argv) {
    CHECK(argc==2);void* module=load(argv[1]);CHECK(module);
    GcnllrNativeGxGetApi get=(GcnllrNativeGxGetApi)symbol(module,"gcnllr_native_gx_get_api");
    GcWiiNativeSessionGetApi session_get=(GcWiiNativeSessionGetApi)symbol(module,"gc_wii_native_session_get_api");
    void (*state)(int)=(void(*)(int))symbol(module,"fixture_state");
    void (*change)(void)=(void(*)(void))symbol(module,"fixture_change_window");
    CHECK(get && session_get && state && change);
    const GcnllrNativeGxApi* api=get(GCNLLR_NATIVE_GX_ABI);
    const GcWiiNativeSessionApi* session=session_get(GC_WII_NATIVE_SESSION_ABI);
    WiiGx gx={.library=module,.api=api};WiiGxHandoff lease={0};
    /* No extension is the existing portable floor, with no armed retention. */
    state(1);CHECK(wii_gx_handoff_begin(&gx,argv[1],&lease)==0 && !lease.library);
    state(2);CHECK(wii_gx_handoff_begin(&gx,argv[1],&lease)==-1 && !lease.library);
    state(0);CHECK(wii_gx_handoff_begin(&gx,argv[1],&lease)==1);
    CHECK(lease.window_identity==17);api->stop();
    /* Release the old guest's real loader reference. Only the lease now owns
     * this module; its callbacks/state must remain valid until the new bind. */
    unload(module);CHECK(session->window_identity()==17);
    module=load(argv[1]);CHECK(module && api->start(NULL));
    CHECK(wii_gx_handoff_end(&lease,true) && !lease.library && session->window_identity()==17);
    gx.library=module;
    /* Failed boot after retirement closes the retained host before releasing. */
    state(0);CHECK(wii_gx_handoff_begin(&gx,argv[1],&lease)==1);api->stop();
    CHECK(wii_gx_handoff_end(&lease,false) && !session->window_identity());
    /* Identity replacement must reject a successful handoff claim. */
    state(0);CHECK(wii_gx_handoff_begin(&gx,argv[1],&lease)==1);api->stop();
    CHECK(api->start(NULL));change();CHECK(!wii_gx_handoff_end(&lease,true));
    api->stop();unload(module);
    puts("native session module lease, portable floor, cancellation and identity PASS");return 0;
}
