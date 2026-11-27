// SPDX-License-Identifier: GPL-3.0-or-later
#include "wiillr/diagnostics.h"
#include <replay_writer.h>
#include <recomp_persistence/atomic_file.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
namespace {
using recomp::diagnostics::ReplayWriter;
std::filesystem::path directory;
bool enabled=true,active=false,stopped=false;
std::string identity,error;
std::array<WiiBluetoothInput,4> previous;
std::array<bool,4> have_previous{};
std::unique_ptr<ReplayWriter> writer;
size_t accepted_bytes=0;
std::string utf8(const std::filesystem::path& path){auto text=path.generic_u8string();return {text.begin(),text.end()};}
void stop(const char* why) {
 error=why;stopped=true;
 std::fprintf(stderr,"Wii input recording: %s\n",why);
 // Leave accepted work to its existing worker; a full queue must not make
 // the input producer wait for disk. Normal shutdown drains it later.
}
void open() {
 if(!active || !enabled || directory.empty() || stopped || writer)return;
 try {
  const auto folder=directory/"diagnostics";std::filesystem::create_directories(folder);
  const auto name="input-"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count())+".log";
  writer=ReplayWriter::create();
  if(!writer || !writer->begin(utf8(folder/name),"WII-INPUT-EVENTS 1\nCATALOG "+identity+"\n",{},{})) {
   stop("Cannot start the input recording");return;
  }
  have_previous={};accepted_bytes=0;
 }catch(const std::exception&){stop("Cannot create the input recording");}
}
}
extern "C" bool wii_diagnostics_configure(const char* path) {
 try {
  const auto next=std::filesystem::u8path(path?path:"");
  if(next.empty())return false;
  if(next==directory)return true;
  wii_diagnostics_end();directory=next;enabled=true;error.clear();
  std::ifstream f(directory/"diagnostics.cfg",std::ios::binary);std::string value;
  if(f && std::getline(f,value) && value=="0")enabled=false;
  return true;
 }catch(const std::exception&){error="Cannot read diagnostics settings";return false;}
}
extern "C" bool wii_diagnostics_enabled(void){return enabled;}
extern "C" const char* wii_diagnostics_error(void){return error.c_str();}
extern "C" bool wii_diagnostics_set_enabled(bool on) {
 const char* text=on?"1\n":"0\n";
 if(directory.empty() || !recomp_atomic_file_store(utf8(directory/"diagnostics.cfg").c_str(),text,2)) {
  error="Cannot save diagnostics settings";return false;
 }
 enabled=on;error.clear();
 if(!on && writer){writer->end();writer->shutdown();writer.reset();}
 if(on){stopped=false;open();}
 return true;
}
extern "C" void wii_diagnostics_begin(const char* catalog) {
 wii_diagnostics_end();active=true;identity=catalog?catalog:"development";stopped=false;error.clear();open();
}
extern "C" void wii_diagnostics_input(u64 cycles,u32 remote,const WiiBluetoothInput* in) {
 try {
 if(!in || remote>=previous.size() || !enabled || stopped || !active)return;
 open();if(!writer)return;
 const auto& before=previous[remote];
 if(have_previous[remote] && before.buttons==in->buttons && before.pointer_x==in->pointer_x &&
    before.pointer_y==in->pointer_y && before.pointing==in->pointing && before.present==in->present &&
    before.pairing==in->pairing && std::equal(before.accel,before.accel+3,in->accel) &&
    wii_nunchuk_equal(&before.nunchuk,&in->nunchuk))return;
 std::ostringstream value;
 value<<in->buttons<<' '<<in->pointer_x<<' '<<in->pointer_y<<' '<<in->pointing<<' '<<in->present<<' '<<in->pairing;
 for(auto a:in->accel)value<<' '<<a;
 value<<' '<<in->nunchuk.attached<<' '<<unsigned(in->nunchuk.stick_x)<<' '<<unsigned(in->nunchuk.stick_y)<<' '<<unsigned(in->nunchuk.buttons);
 for(auto a:in->nunchuk.accel)value<<' '<<a;
 const auto body=value.str();
 const auto line="I "+std::to_string(cycles)+" "+std::to_string(remote)+" "+body+"\n";
 // This diagnostic event log is bounded; it is not a save state or a promise
 // of replay across builds. Queue exhaustion never blocks guest input.
 if(accepted_bytes+line.size()>16u*1024u*1024u){stop("The 16 MiB recording limit was reached; restart recording for a new segment");return;}
 if(!writer->line(line)){stop("The input recording queue or output failed");return;}
 accepted_bytes+=line.size();previous[remote]=*in;have_previous[remote]=true;
 }catch(const std::exception&){try{stop("Cannot record controller activity");}catch(...){stopped=true;}}
}
extern "C" void wii_diagnostics_end(void) {
 if(writer){writer->end();if(const char* why=writer->shutdown())std::fprintf(stderr,"Wii input recording: %s\n",why);writer.reset();}
 active=false;
}
