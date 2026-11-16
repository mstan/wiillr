/* SPDX-License-Identifier: MIT */
#include "wiillr/fs.h"
#include "wiillr/menu_profile.h"
#include "menu_profile_fixture.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#define CHECK(x) do {if(!(x)){std::cerr<<"Menu profile failure "<<__LINE__<<": "<<#x<<'\n';return 1;}}while(0)
using Bytes=std::vector<uint8_t>;
static void write(const std::filesystem::path& p,const void* data,size_t size){std::filesystem::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary);f.write(static_cast<const char*>(data),size);if(!f)throw std::runtime_error("fixture write");}
static Bytes read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);return Bytes(std::istreambuf_iterator<char>(f),{});}
static void be32(uint8_t* p,uint32_t v){for(int i=3;i>=0;i--){p[i]=uint8_t(v);v>>=8;}}
static uint32_t word(const uint8_t* p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
static Bytes blank(){
    Bytes b(20*1024*1024);memcpy(b.data(),"VFF \xfe\xff\1\0",8);be32(b.data()+8,uint32_t(b.size()));b[13]=32;
    for(size_t at:{size_t(32),size_t(32+81920)}){b[at]=0xf8;b[at+1]=b[at+2]=b[at+3]=0xff;}
    return b;
}
static bool envelope(const Bytes& b,uint32_t high,uint32_t low){
    const char magic[]="CDBFILE";const auto p=std::search(b.begin(),b.end(),magic,magic+7);
    if(p==b.end() || size_t(b.end()-p)<0x548)return false;const auto* h=&*p;
    return h[7]==2 && word(h+8)==high && word(h+12)==low && !word(h+0x7c) &&
        !memcmp(h+0x400,"RI_5",4) && !word(h+0x410) && !word(h+0x414);
}
int main(int argc,char** argv){
    CHECK(argc==2);namespace fs=std::filesystem;const auto parent=fs::canonical(argv[1]);
    const auto root=parent/("menu-profile-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    CHECK(fs::create_directory(root));
    const char marker[]="wiillr-copied-nand-v1\n";write(root/".wiillr-nand-profile",marker,sizeof marker-1);
    const uint8_t uid[]={0,0,0,1,0,0,0,2,0,0,0,0};write(root/"sys/uid.sys",uid,sizeof uid);
    std::array<uint8_t,0x208> tmd{};tmd[1]=1;tmd[3]=1;memcpy(tmd.data()+0x18c,uid,8);tmd[0x199]=1;
    write(root/"title/00000001/00000002/content/title.tmd",tmd.data(),tmd.size());
    auto config=menu_profile_fixture();config[32]=char(0xfe); // persisted CB = -2, not reset by delivery
    write(root/"shared2/sys/SYSCONF",config.data(),config.size());
    uint32_t local=99;CHECK(wii_menu_profile_local_seconds(reinterpret_cast<const uint8_t*>(config.data()),config.size(),2,&local) && local==0);
    CHECK(wii_menu_profile_local_seconds(reinterpret_cast<const uint8_t*>(config.data()),config.size(),1,&local) && local==0xffffffffu);
    const uint8_t mac[]={2,0x19,0x79,0,0,1};write(root/".wiillr-network-mac",mac,sizeof mac);
    const auto cdb=root/"title/00000001/00000002/data/cdb.vff";const auto original=blank();write(cdb,original.data(),original.size());
    const auto path=root.string();auto* nand=wii_fs_mount(path.c_str(),0,1);CHECK(nand);
    bool enabled=false;CHECK(wii_fs_menu_welcome_enabled(nand,&enabled) && enabled);
    CHECK(wii_fs_menu_welcome_set_enabled(nand,false));wii_fs_unmount(nand);
    nand=wii_fs_mount(path.c_str(),0,1);CHECK(nand && wii_fs_menu_welcome_enabled(nand,&enabled) && !enabled);
    CHECK(wii_fs_menu_welcome(nand,2)==WII_MENU_WELCOME_DISABLED && read(cdb)==original);
    CHECK(wii_fs_menu_welcome_set_enabled(nand,true));
    CHECK(wii_fs_menu_welcome(nand,2)==WII_MENU_WELCOME_CREATED);
    auto delivered=read(cdb);
    /* Independent Python hashlib SHA1 of synthetic 02:19:79:00:00:01 + uyy. */
    CHECK(envelope(delivered,0x87634ec8,0xbf697588));
    CHECK(read(root/"shared2/sys/SYSCONF")==Bytes(config.begin(),config.end()));
    CHECK(wii_fs_menu_welcome(nand,1234567)==WII_MENU_WELCOME_PRESENT && read(cdb)==delivered);
    const std::array<uint8_t,40> identity={0x10,0x20,0x30,0x40,0x50,0x60,0x70,0x80};
    const auto identity_path=root/"title/00000001/00000002/data/nocopy/cdbwiiid.dat";
    write(identity_path,identity.data(),identity.size());write(cdb,original.data(),original.size());
    CHECK(wii_fs_menu_welcome(nand,2)==WII_MENU_WELCOME_CREATED && envelope(read(cdb),0x10203040,0x50607080));
    CHECK(read(identity_path)==Bytes(identity.begin(),identity.end()));
    const std::string unknown="unrecognized mod state\n";write(root/".wiillr-menu-mods",unknown.data(),unknown.size());
    delivered=read(cdb);CHECK(!wii_fs_menu_welcome_enabled(nand,&enabled) && !wii_fs_menu_welcome_set_enabled(nand,false));
    CHECK(wii_fs_menu_welcome(nand,2)==WII_MENU_WELCOME_INVALID && read(cdb)==delivered);
    CHECK(read(root/".wiillr-menu-mods")==Bytes(unknown.begin(),unknown.end()));
    write(root/".wiillr-menu-mods",nullptr,0);
    CHECK(!wii_fs_menu_welcome_enabled(nand,&enabled) && !wii_fs_menu_welcome_set_enabled(nand,false) &&
        fs::file_size(root/".wiillr-menu-mods")==0 && read(cdb)==delivered);
    CHECK(fs::remove(root/".wiillr-menu-mods"));CHECK(fs::remove(cdb));
    CHECK(wii_fs_menu_welcome(nand,2)==WII_MENU_WELCOME_NOT_READY && !fs::exists(cdb));
    wii_fs_unmount(nand);
    /* Audited cleanup of this unique synthetic tree, no links or repositories. */
    CHECK(fs::canonical(root).parent_path()==parent);
    for(const auto& item:fs::recursive_directory_iterator(root))CHECK(!item.is_symlink() && item.path().filename()!=".git");
    fs::remove_all(root);
    std::cout<<"Menu profile: persistent hidden switch, RTC+bias, identity and real mailbox transaction passed\n";
}
