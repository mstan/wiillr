/* SPDX-License-Identifier: MIT */
/* CPU-only module fixture: no SDL window/device or guest memory is created. */
#include "gcnllr/native_gx.h"
#include "gcnllr/native_session.h"
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif
static int active=1,retaining,retained,legacy,decline;
static uint64_t window=17;
static int start(const GcnllrNativeGxConfig* config) {
    (void)config;if(active || !retained)return 0;
    active=1;retained=0;return 1;
}
static void stop(void) {
    active=0;retained=retaining;retaining=0;if(!retained)window=0;
}
static int retain(void) {
    if(!active || retaining || decline)return 0;
    retaining=1;return 1;
}
static void cancel(void) {retaining=0;if(retained){retained=0;window=0;}}
static uint64_t identity(void) {return window;}
static const GcWiiNativeSessionApi session={GC_WII_NATIVE_SESSION_ABI,retain,cancel,identity};
static const GcnllrNativeGxApi api={.abi=GCNLLR_NATIVE_GX_ABI,.start=start,.stop=stop};
EXPORT const GcnllrNativeGxApi* gcnllr_native_gx_get_api(uint32_t abi) {
    return abi==GCNLLR_NATIVE_GX_ABI?&api:0;
}
EXPORT const GcWiiNativeSessionApi* gc_wii_native_session_get_api(uint32_t abi) {
    return !legacy && abi==GC_WII_NATIVE_SESSION_ABI?&session:0;
}
EXPORT void fixture_state(int mode) {
    active=1;retaining=retained=0;legacy=mode==1;decline=mode==2;window=17;
}
EXPORT void fixture_change_window(void) {window=23;}
