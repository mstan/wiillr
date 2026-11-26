// SPDX-License-Identifier: GPL-3.0-or-later
#include "wiillr/diagnostics.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
namespace fs=std::filesystem;
static std::string read(const fs::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static std::string path(const fs::path& p){auto s=p.generic_u8string();return {s.begin(),s.end()};}
int main(int argc,char** argv) {
 assert(argc==2);
 const auto parent=fs::canonical(argv[1]);
 const auto root=parent/("wii-diagnostics-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 assert(fs::create_directory(root));
 assert(wii_diagnostics_configure(path(root).c_str()) && wii_diagnostics_enabled());
 wii_diagnostics_begin("catalog-fixture");
 WiiBluetoothInput input{};input.buttons=0x800;input.present=true;input.nunchuk=wii_nunchuk_neutral();
 wii_diagnostics_input(123,2,&input);wii_diagnostics_input(124,2,&input);
 input.nunchuk.buttons=1;wii_diagnostics_input(125,2,&input);wii_diagnostics_input(126,3,&input);
 wii_diagnostics_end();
 unsigned logs=0;std::string captured;
 for(const auto& file:fs::directory_iterator(root/"diagnostics"))if(file.path().extension()==".log"){++logs;captured=read(file.path());}
 assert(logs==1 && captured.find("CATALOG catalog-fixture")!=std::string::npos);
 assert(captured.find("I 123 2 ")!=std::string::npos && captured.find("I 124 ")==std::string::npos);
 assert(captured.find("I 125 2 ")!=std::string::npos && captured.find("I 126 3 ")!=std::string::npos);
 assert(wii_diagnostics_set_enabled(false) && read(root/"diagnostics.cfg")=="0\n");
 wii_diagnostics_begin("disabled");wii_diagnostics_input(200,0,&input);wii_diagnostics_end();
 assert(fs::create_directory(root/"other"));
 assert(wii_diagnostics_configure(path(root/"other").c_str()) && wii_diagnostics_enabled());
 assert(wii_diagnostics_configure(path(root).c_str()) && !wii_diagnostics_enabled());
 assert(wii_diagnostics_set_enabled(true) && read(root/"diagnostics.cfg")=="1\n");
 wii_diagnostics_end();
 for(const auto& file:fs::recursive_directory_iterator(root))assert(!file.is_symlink() && file.path().filename()!=".git");
 assert(fs::canonical(root).parent_path()==parent);fs::remove_all(root);
 puts("Wii input recording, four-Remote identity, Nunchuk data and saved default-on choice passed");
}
