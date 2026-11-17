// SPDX-License-Identifier: GPL-3.0-or-later
#include "wiillr/pacing.h"
#if WIILLR_PACING_FIELD
#include <chrono>
#include <cmath>
#include <thread>
#include <cstdlib>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
namespace {
struct FieldTimer {
    // Older hosts can reject the high-resolution flag; retain portable sleep.
    HANDLE handle=CreateWaitableTimerExW(nullptr,nullptr,0x00000002,
                                       TIMER_MODIFY_STATE|SYNCHRONIZE);
    ~FieldTimer(){if(handle)CloseHandle(handle);}
};
}
#endif
#endif

void wii_pacing_reset(WiiPacing* pacing) {
    *pacing={};
}

void wii_pacing_on_field(WiiPacing* pacing,double period_seconds) {
#if WIILLR_PACING_FIELD
    using Clock=std::chrono::steady_clock;
    using namespace std::chrono;
    // Development headroom runs keep guest clocks/fields unchanged and remove
    // only the host sleep. Normal launches retain field pacing by default.
    static const bool unpaced=[](){const char* value=std::getenv("WIILLR_UNPACED");
        return value && value[0]=='1' && !value[1];}();
    if(unpaced)return;
    if(!std::isfinite(period_seconds) || period_seconds<=0) {
        wii_pacing_reset(pacing);
        return;
    }
    if(!pacing->armed) {
        pacing->origin_ns=duration_cast<nanoseconds>(Clock::now().time_since_epoch()).count();
        pacing->elapsed_seconds=0;
        pacing->armed=1;
        return;
    }
    /* Accumulate from one origin so an oversleep does not add long-term drift.
     * A slow emulation simply runs late; no guest time or frames are skipped. */
    pacing->elapsed_seconds+=period_seconds;
    const Clock::time_point origin{duration_cast<Clock::duration>(nanoseconds(pacing->origin_ns))};
    const auto target=origin+duration_cast<Clock::duration>(duration<double>(pacing->elapsed_seconds));
#ifdef _WIN32
    static thread_local FieldTimer timer;
    const auto remaining=target-Clock::now();
    if(remaining<=Clock::duration::zero())return;
    const auto ticks=duration_cast<duration<int64_t,std::ratio<1,10000000>>>(remaining).count();
    LARGE_INTEGER due;
    due.QuadPart=-ticks;
    if(timer.handle && ticks>0 && SetWaitableTimer(timer.handle,&due,0,nullptr,nullptr,FALSE) &&
       WaitForSingleObject(timer.handle,INFINITE)==WAIT_OBJECT_0)return;
#endif
    std::this_thread::sleep_until(target);
#else
    (void)pacing;
    (void)period_seconds;
#endif
}
