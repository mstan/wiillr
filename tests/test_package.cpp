// SPDX-License-Identifier: MIT
#include <wiillr/package.h>
#include <wiillr/menu_profile.h>
#include "menu_profile_fixture.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
namespace fs=std::filesystem;
static void require(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
static void write(const fs::path& p,const std::string& bytes) {fs::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary);f<<bytes;require(bool(f),"fixture write");}
static std::string read(const fs::path& p) {std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static bool reject;
static unsigned validations;
static bool validate(const char* image,const char* nand,unsigned uid,unsigned gid) {
    ++validations;
    if(reject) {write(fs::path(nand)/"fst.bin","temporary metadata");fs::create_directories(fs::path(nand)/"tmp");return false;}
    return uid==4107 && gid==12344 && read(image)=="supported boot";
}
static bool validate_menu(const char* image,const char*,unsigned uid,unsigned gid) {
    ++validations;return uid==0 && gid==1 && read(image)=="supported Menu boot";
}
static std::string config() {
    return "[Game]\nNand=seed/nand\nBoot=title/game/content/boot.app\nGraphics=gpu.bin\nDspRom=system/rom.bin\nDspCoef=system/coef.bin\nUid=4107\nGid=12344\nIos=37\n";
}
struct Root {
    fs::path path,parent;
    explicit Root(const char* base):parent(fs::canonical(base)) {
        path=parent/("package-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        require(fs::create_directory(path),"unique owned test directory");
    }
    ~Root() {
        // This unique tree was created by the test. Never follow a link or
        // delete an unexpected nested repository during normal cleanup.
        try {
            if(fs::canonical(path).parent_path()!=parent)return;
            for(const auto& e:fs::recursive_directory_iterator(path))
                if(e.is_symlink() || e.path().filename()==".git")return;
            fs::remove_all(path);
        }catch(...){}
    }
};
int main(int argc,char** argv) {
    try {
        require(argc==2,"test workspace argument");Root root(argv[1]);
        auto base=root.path/"original package with spaces",user=root.path/"player profile";
        const auto boot=fs::path("title/game/content/boot.app"),save=fs::path("title/game/data/save.dat");
        write(base/"game.ini",config());write(base/"seed/nand/.wiillr-nand-profile","wiillr-copied-nand-v1\n");
        write(base/"seed/nand"/boot,"supported boot");write(base/"seed/nand"/save,"initial progress");
        write(base/"seed/nand/empty.dat","");write(base/"gpu.bin","library");
        write(base/"system/rom.bin",std::string(8192,'R'));write(base/"system/coef.bin",std::string(4096,'C'));
        char error[512];
        auto open=[&](const fs::path& target) {return wii_package_open(base.string().c_str(),target.string().c_str(),validate,error,sizeof error);};
        WiiPackage* p=open(user);require(p,error);
        require(!fs::exists(user/".wiillr-menu-profile.lock"),"MM9 package behavior has no Menu lease");
        require(std::string(wii_package_name(nullptr))=="Mega Man 9" &&
            std::string(wii_package_name(""))=="Mega Man 9" &&
            std::string(wii_package_name(base.string().c_str()))=="Mega Man 9" && !wii_package_menu(p),
            "legacy eight-key packages and null base keep the MM9 default");
        require(validations==1 && read(user/"nand"/save)=="initial progress" && fs::file_size(user/"nand/empty.dat")==0,"first import copies complete writable progress including empty files");
        int count;char** values;wii_package_arguments(p,"Mega Man 9.exe","--launcher",&count,&values);
        require(count==17 && !values[count] && std::string(values[1])=="--launcher" && std::string(values[4]).empty() && std::string(values[16])=="37","normal launcher arguments omit developer memory dumps");
        require(fs::path(values[2])==user/"nand"/boot && fs::path(values[8])==user/"nand","boot and native persistence use the copied profile");
        wii_package_close(p);
        write(user/"nand"/save,"owner progress");write(user/"config.ini","explicit settings");write(user/"nand/ticket/additional.tik","imported license");
        write(base/"seed/nand"/save,"updated template progress");write(base/"seed/nand/new.dat","new template data");
        p=open(user);require(p,error);wii_package_close(p);
        require(validations==1 && read(user/"nand"/save)=="owner progress" && read(user/"config.ini")=="explicit settings" && read(user/"nand/ticket/additional.tik")=="imported license" && !fs::exists(user/"nand/new.dat"),"updates never merge a template over existing saves, settings or imported licenses");
        const auto relocated=root.path/"relocated package with spaces";fs::rename(base,relocated);base=relocated;
        p=open(user);require(p,error);wii_package_arguments(p,"Mega Man 9.exe","--launcher-settings",&count,&values);
        require(fs::path(values[12])==base/"gpu.bin" && fs::path(values[6])==base/"system/rom.bin" && std::string(values[1])=="--launcher-settings","resource paths follow a relocated package");wii_package_close(p);
        for(const auto& broken:{config()+"Uid=1\n",config()+"Unexpected=1\n",std::string("[Game]\n"),std::string(config()).replace(config().find("title/game/content/boot.app"),27,"../boot.app")}) {
            write(base/"game.ini",broken);p=open(root.path/"rejected profile");require(!p,"malformed manifest rejects before import");
            require(!fs::exists(root.path/"rejected profile/nand"),"invalid launch descriptions cannot publish a profile");
        }
        // On Windows a root-directory path without a drive (\path) is not
        // is_absolute(), but would still escape the package on the same drive.
        const auto outside=root.path/"outside.dll";write(outside,"outside resource");
        auto rooted=config();rooted.replace(rooted.find("Graphics=gpu.bin"),16,
            "Graphics="+(outside.root_directory()/outside.relative_path()).string());
        write(base/"game.ini",rooted);p=open(root.path/"rooted resource profile");
        require(!p && !fs::exists(root.path/"rooted resource profile/nand"),"rooted resources cannot escape the package");
        write(base/"game.ini",config());reject=true;p=open(root.path/"failed validation");require(!p,"failed title identity rejects");reject=false;
        require(!fs::exists(root.path/"failed validation/nand"),"failed validation cannot publish partial game data");
        for(const auto& e:fs::directory_iterator(root.path/"failed validation"))require(!e.path().filename().string().starts_with("nand-initializing-"),"failed import removes only its owned staging files");
        require(!open(base/"seed"),"packaged template cannot be used as its own writable profile");
        require(read(base/"seed/nand"/save)=="updated template progress" && read(user/"nand"/save)=="owner progress","source assets and existing native progress stay intact on failures");
        const auto standalone=root.path/"WAD-only package",fresh=root.path/"fresh WAD user";
        const std::string wad_config="[Game]\nImport=wad\nBoot=title/00010001/57523945/content/0000000a.app\nGraphics=gpu.bin\nDspRom=hle\nDspCoef=hle\nUid=4096\nGid=12344\nIos=37\n";
        write(standalone/"game.ini",wad_config);write(standalone/"gpu.bin","library");
        p=wii_package_open(standalone.string().c_str(),fresh.string().c_str(),validate,error,sizeof error);
#if WIILLR_PACKAGE_DSP_HLE
        require(p,error);require(wii_package_standalone(p),"WAD-only profile can return to the host launcher");
        wii_package_arguments(p,"Mega Man 9.exe","--launcher",&count,&values);
        require(std::string(values[6])=="@hle" && std::string(values[7])=="@hle" && std::string(values[9])=="4096","resource-free HLE starts explicitly and reserves the game UID before optional DLC");
        require(!fs::exists(fresh/"nand/title/00010001/57523945/content"),"fresh package contains no original game content");
        const auto generated=read(fresh/"nand/shared2/sys/SYSCONF");
        require(generated.size()==16384 && generated.substr(0,4)=="SCv0" && generated.substr(16380)=="SCed","fresh local settings have a complete SYSCONF container");
        const auto setting=[&](const std::string& name) {
            const auto word=[&](size_t at){return unsigned(uint8_t(generated.at(at)))<<8|uint8_t(generated.at(at+1));};
            for(unsigned n=0;n<word(4);++n) {
                auto at=word(6+2*n);const auto header=uint8_t(generated.at(at++));const auto length=(header&31)+1;
                if(generated.substr(at,length)!=name)continue;at+=length;
                unsigned size=1;if((header>>5)==1){size=word(at)+1;at+=2;}else if((header>>5)==2){size=uint8_t(generated.at(at++))+1;}else if((header>>5)==5)size=4;
                return generated.substr(at,size);
            }
            throw std::runtime_error("Missing fresh system setting: "+name);
        };
        const auto devices=setting("BT.DINF");
        require(devices.size()==1121 && uint8_t(devices[0])==5 && uint8_t(devices[3])==0x79 && uint8_t(devices[6])==0x11 && devices.substr(7,19)=="Nintendo RVL-CNT-01" && devices.substr(707,19)=="Nintendo RVL-CNT-01","fresh virtual-controller registrations match the native transport without copied link keys");
        require(setting("BT.CDIF")==std::string(517,'\0') && setting("BT.SENS")==std::string("\0\0\0\3",4) && setting("IPL.NIK").size()==22 && setting("IPL.NIK")[21]==6,"fresh pointer/controller/nickname defaults are present");
        wii_package_close(p);
        const auto fresh_save=fresh/"nand/title/00010001/57523945/data/rm9_sav0.dat";
        write(fresh_save,"native progress");write(fresh/"config.ini","explicit player settings");
        auto local_uid=read(fresh/"nand/sys/uid.sys");require(local_uid.size()==12,"reserved identity has one complete record");
        local_uid[11]=11;write(fresh/"nand/sys/uid.sys",local_uid);
        p=wii_package_open(standalone.string().c_str(),fresh.string().c_str(),validate,error,sizeof error);require(p,error);
        wii_package_arguments(p,"Mega Man 9.exe","--launcher",&count,&values);
        require(std::string(values[9])=="4107" && read(fresh_save)=="native progress" && read(fresh/"config.ini")=="explicit player settings" && read(fresh/"nand/shared2/sys/SYSCONF")==generated,"package updates reuse installed UID and preserve settings/progress without a seed");
        wii_package_close(p);
#else
        require(!p && !fs::exists(fresh/"nand"),"LLE package cannot silently select resource-free HLE");
#endif
        const auto menu=root.path/"Wii Menu package",menu_user=root.path/"Wii Menu player";
        const std::string menu_config="[Game]\nName=Wii Menu\nNand=seed/nand\nBoot=title/00000001/00000002/content/00000000.app\nGraphics=gpu.bin\nDspRom=system/rom.bin\nDspCoef=system/coef.bin\nUid=0\nGid=1\nIos=4\n";
        write(menu/"game.ini",menu_config);write(menu/"seed/nand/.wiillr-nand-profile","wiillr-copied-nand-v1\n");
        write(menu/"seed/nand/title/00000001/00000002/content/00000000.app","supported Menu boot");
        write(menu/"seed/nand/title/00000001/00000002/data/board", "synthetic Board data");
        const auto source_sc=menu_profile_fixture();write(menu/"seed/nand/shared2/sys/SYSCONF",source_sc);
        std::string source_network(7004,'\0');source_network[4]=1;source_network[8]=char(0x86);
        source_network[34]=5;source_network[35]=char(0xdc);source_network.replace(1996,6,"wiillr");source_network[2029]=6;
        write(menu/"seed/nand/shared2/sys/net/02/config.dat",source_network);
        write(menu/"gpu.bin","library");write(menu/"system/rom.bin",std::string(8192,'R'));write(menu/"system/coef.bin",std::string(4096,'C'));
        p=wii_package_open(menu.string().c_str(),menu_user.string().c_str(),validate_menu,error,sizeof error);require(p,error);
        require(wii_package_menu(p) && !wii_package_standalone(p) &&
            std::string(wii_package_name(menu.string().c_str()))=="Wii Menu","dedicated Menu package identity and prepared NAND mode");
        auto expected_sc=source_sc;expected_sc[21]=1;expected_sc.replace(29,4,4,'\0');
        require(read(menu_user/"nand/shared2/sys/SYSCONF")==expected_sc && read(menu/"seed/nand/shared2/sys/SYSCONF")==source_sc,
            "only new copied Menu AR/CB values change; unknown entries/padding/source stay exact");
        std::string expected_network(7004,'\0');expected_network[4]=2;expected_network[8]=char(0x87);expected_network[34]=5;expected_network[35]=char(0xdc);
        require(read(menu_user/"nand/shared2/sys/net/02/config.dat")==expected_network &&
            read(menu/"seed/nand/shared2/sys/net/02/config.dat")==source_network,"exact former generated Wi-Fi seed converts to wired on new copy only");
        for(const auto* action:{"--launcher","--launcher-settings","--launcher-check"}) {
            wii_package_arguments(p,"Wii Menu.exe",action,&count,&values);
            const std::string wanted=std::string("--menu")+std::string(action+1);
            require(count==17 && !values[count] && std::string(values[1])==wanted && std::string(values[16])=="4" &&
                std::string(values[9])=="0" && std::string(values[10])=="1","Menu startup/settings/check action uses Menu identity");
        }
        const unsigned before_blocked=validations;
        auto* blocked=wii_package_open(menu.string().c_str(),menu_user.string().c_str(),validate_menu,error,sizeof error);
        require(!blocked && validations==before_blocked &&
            read(menu_user/"nand/shared2/sys/SYSCONF")==expected_sc &&
            read(menu_user/"nand/title/00000001/00000002/data/board")=="synthetic Board data",
            "second Menu package open refuses before profile validation or writes");
        wii_package_close(p);
        p=wii_package_open(menu.string().c_str(),menu_user.string().c_str(),validate_menu,error,sizeof error);
        require(p && fs::file_size(menu_user/".wiillr-menu-profile.lock")==0,"closing first package releases its profile lease");wii_package_close(p);
        write(menu_user/"config.ini","explicit Menu settings");
        write(menu_user/"nand/shared2/sys/SYSCONF",source_sc);
        auto custom_network=source_network;custom_network[100]=9;write(menu_user/"nand/shared2/sys/net/02/config.dat",custom_network);
        write(menu_user/"nand/title/00000001/00000002/data/board","player Board data");
        auto menu80=menu_config;menu80.replace(menu80.find("Ios=4"),5,"Ios=80");write(menu/"game.ini",menu80);
        p=wii_package_open(menu.string().c_str(),menu_user.string().c_str(),validate_menu,error,sizeof error);require(p,error);
        wii_package_arguments(p,"Wii Menu.exe","--menu-launcher-check",&count,&values);
        require(std::string(values[1])=="--menu-launcher-check" && std::string(values[16])=="80" &&
            read(menu_user/"config.ini")=="explicit Menu settings" &&
            read(menu_user/"nand/shared2/sys/SYSCONF")==source_sc &&
            read(menu_user/"nand/shared2/sys/net/02/config.dat")==custom_network &&
            read(menu_user/"nand/title/00000001/00000002/data/board")=="player Board data",
            "IOS80 package retains existing Menu settings and Board data");wii_package_close(p);
        for(auto bad:{std::string(menu_config).replace(menu_config.find("Name=Wii Menu"),13,"Name=Unknown"),
                      std::string(menu_config).replace(menu_config.find("Ios=4"),5,"Ios=37"),
                      std::string(menu_config).replace(menu_config.find("Nand=seed/nand"),14,"Import=wad")}) {
            write(menu/"game.ini",bad);p=wii_package_open(menu.string().c_str(),(root.path/"invalid Menu profile").string().c_str(),validate_menu,error,sizeof error);
            require(!p && !fs::exists(root.path/"invalid Menu profile/nand"),"unknown names/unsupported Menu IOS/WAD import refuse before profile creation");
        }
        write(menu/"game.ini",menu_config);write(menu/"seed/nand/shared2/sys/net/02/config.dat",custom_network);
        const auto custom_user=root.path/"custom Menu seed";
        p=wii_package_open(menu.string().c_str(),custom_user.string().c_str(),validate_menu,error,sizeof error);require(p,error);wii_package_close(p);
        require(read(custom_user/"nand/shared2/sys/net/02/config.dat")==custom_network,"custom template connection is preserved on fresh copy");
        auto invalid_sc=source_sc;invalid_sc[22]=0x65;const auto before_sc=invalid_sc;
        require(!wii_menu_profile_seed(reinterpret_cast<uint8_t*>(invalid_sc.data()),invalid_sc.size()) && invalid_sc==before_sc,"wrong CB type refuses without mutation");
        write(menu/"seed/nand/shared2/sys/SYSCONF",invalid_sc);
        const auto unsupported_user=root.path/"unsupported Menu seed";
        p=wii_package_open(menu.string().c_str(),unsupported_user.string().c_str(),validate_menu,error,sizeof error);
        require(!p && !fs::exists(unsupported_user/"nand"),"unsupported settings never publish a partial Menu profile");
        const auto linked_user=root.path/"linked Menu lease";fs::create_directory(linked_user);
        write(menu/"seed/nand/shared2/sys/SYSCONF",source_sc);
        const auto lease_target=root.path/"unrelated lease content";write(lease_target,"");
        std::error_code link_error;fs::create_symlink(lease_target,linked_user/".wiillr-menu-profile.lock",link_error);
        if(!link_error) {
            p=wii_package_open(menu.string().c_str(),linked_user.string().c_str(),validate_menu,error,sizeof error);
            require(!p && !fs::exists(linked_user/"nand") && fs::file_size(lease_target)==0 &&
                std::string(error).find("Linked")!=std::string::npos,"linked lease refuses without following or changing it");
            fs::remove(linked_user/".wiillr-menu-profile.lock");
        }
        std::cout<<"Package import, relocation, validation, persistence and no-dump contracts passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
