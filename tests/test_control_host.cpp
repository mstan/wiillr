// SPDX-License-Identifier: GPL-3.0-or-later
// A paused guest must still dispatch native window events, failure and close.
#define WIILLR_SCHED_DEADLINE 0
#include "../src/control.cpp"
#include <cassert>
static unsigned services,events;
static bool failure,closed,service_failure;
static int service_host(){++services;return !service_failure;}
static void poll_host(GcnllrNativeGxStatus* out){++events;*out={};out->failed=failure;out->quit=closed;}
int main() {
  static CPUState cpu;WiiGx gx{};WiiControl control{};
  GcnllrNativeGxApi api{};api.service=service_host;api.status=poll_host;
  gx.api=&api;control.cpu=&cpu;control.gx=&gx;control.paused=true;
  cpu.cycles=12345;cpu.pc=0x80004000;
  paused_host_events(&control);
  assert(services==1 && events==1 && control.paused && !control.quit && !cpu.halted);
  assert(cpu.cycles==12345 && cpu.pc==0x80004000);
  closed=true;paused_host_events(&control);
  assert(control.quit && !control.paused && !cpu.halted && cpu.cycles==12345);
  closed=false;control.quit=false;control.paused=true;failure=true;
  paused_host_events(&control);assert(cpu.halted && control.paused && !control.quit);
  cpu.halted=false;control.paused=true;api.status=nullptr;service_failure=true;
  paused_host_events(&control);assert(cpu.halted && control.paused);
  failure=service_failure=false;cpu.halted=false;control.paused=true;
  paused_host_events(&control);assert(control.paused && !cpu.halted);
  gx.api=nullptr;cpu.halted=false;control.paused=true;
  paused_host_events(&control);assert(control.paused && !cpu.halted && cpu.cycles==12345);
  puts("Paused host event dispatch, close/failure and guest-time isolation passed");
}
