// SPDX-License-Identifier: MIT
#include <wiillr/package.h>
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
        std::cout<<"Package import, relocation, validation, persistence and no-dump contracts passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
