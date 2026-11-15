// SPDX-License-Identifier: MIT
#include <wiillr/package.h>
#include <wiillr/menu_profile.h>
#include <wiillr/network_profile.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace fs=std::filesystem;
struct WiiPackage {
    std::string boot,nand,rom,coef,graphics,data,cache,log,uid,gid,ios;
    bool standalone{},menu{};
#ifdef _WIN32
    HANDLE menu_lock=INVALID_HANDLE_VALUE;
    ~WiiPackage(){if(menu_lock!=INVALID_HANDLE_VALUE)CloseHandle(menu_lock);}
#else
    int menu_lock=-1;
    ~WiiPackage(){if(menu_lock>=0)close(menu_lock);}
#endif
    std::array<std::string,18> values;
    std::array<char*,19> argv{};
};
#ifndef WIILLR_PACKAGE_DSP_HLE
#define WIILLR_PACKAGE_DSP_HLE 0
#endif
namespace {
void need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool linked(const fs::path& p) {
#ifdef _WIN32
    const auto attributes=GetFileAttributesW(p.c_str());
    return attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_REPARSE_POINT);
#else
    return fs::is_symlink(fs::symlink_status(p));
#endif
}
bool same_component(const fs::path& a,const fs::path& b) {
#ifdef _WIN32
    return CompareStringOrdinal(a.c_str(),int(a.native().size()),b.c_str(),int(b.native().size()),TRUE)==CSTR_EQUAL;
#else
    return a==b;
#endif
}
bool within(const fs::path& child,const fs::path& parent) {
    auto a=child.begin();
    for(const auto& b:parent)if(a==child.end() || !same_component(*a++,b))return false;
    return true;
}
std::string trim(std::string s) {
    const auto first=s.find_first_not_of(" \t\r"),last=s.find_last_not_of(" \t\r");
    return first==std::string::npos?std::string{}:s.substr(first,last-first+1);
}
std::map<std::string,std::string> read_config(const fs::path& path) {
    need(fs::is_regular_file(path) && !linked(path),"Missing game.ini beside the executable");
    need(fs::file_size(path)<=65536,"Package launch description is too large");
    std::ifstream file(path);need(bool(file),"Cannot read the package launch description");
    std::map<std::string,std::string> result;std::string line;bool game=false;
    while(std::getline(file,line)) {
        need(line.find('\0')==std::string::npos,"Invalid package launch description");
        line=trim(line);if(line.empty() || line[0]=='#' || line[0]==';')continue;
        if(line=="[Game]"){need(!game,"Duplicate package section");game=true;continue;}
        const auto at=line.find('=');need(game && at!=std::string::npos,"Invalid package launch description");
        const auto key=trim(line.substr(0,at)),value=trim(line.substr(at+1));
        need(!value.empty() && result.emplace(key,value).second,"Empty or duplicate package value");
    }
    need(!file.bad(),"Could not finish reading the package launch description");
    for(const char* key:{"Boot","Graphics","DspRom","DspCoef","Uid","Gid","Ios"})
        need(result.contains(key),"Incomplete package launch description");
    need(result.contains("Nand")!=result.contains("Import"),"Select installed data or WAD import in game.ini");
    if(result.contains("Import"))need(result.at("Import")=="wad","Unsupported game import format");
    if(result.contains("Name"))need(result.at("Name")=="Wii Menu" || result.at("Name")=="Mega Man 9","Unsupported package name");
    need(result.size()==8+(result.contains("Name")?1:0),"Unknown package launch setting");return result;
}
fs::path relative(const std::string& text) {
    fs::path path(text);need(!path.empty() && !path.has_root_path(),"Package paths must be relative");
    for(const auto& part:path)need(part!=".." && part!=".","Invalid package path component");
    return path;
}
fs::path resource(const fs::path& base,const std::string& text) {
    fs::path path=base;
    for(const auto& part:relative(text)) {path/=part;need(!linked(path),"Linked package resources are unsupported");}
    need(fs::exists(path),"Required game files are missing from the package");return path;
}
unsigned number(const std::string& text,unsigned maximum) {
    unsigned value{};const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    need(parsed.ec==std::errc{} && parsed.ptr==text.data()+text.size() && value<=maximum,"Invalid package title identity");
    return value;
}
bool marked(const fs::path& root) {
    std::ifstream file(root/".wiillr-nand-profile");std::string line;std::getline(file,line);
    return file && trim(line)=="wiillr-copied-nand-v1";
}
void lock_menu_profile(WiiPackage& package,const fs::path& user) {
    const auto path=user/".wiillr-menu-profile.lock";
    need(!linked(path),"Linked Wii Menu profile lease files are unsupported");
#ifdef _WIN32
    package.menu_lock=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,
        OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    need(package.menu_lock!=INVALID_HANDLE_VALUE,"The Wii Menu profile is already in use or cannot be locked");
    BY_HANDLE_FILE_INFORMATION info{};
    need(GetFileInformationByHandle(package.menu_lock,&info) &&
        !(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)) &&
        !info.nFileSizeHigh && !info.nFileSizeLow,"Unsupported Wii Menu profile lease file");
#else
    package.menu_lock=open(path.c_str(),O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK,0600);
    need(package.menu_lock>=0,"The Wii Menu profile is already in use or cannot be locked");
    struct stat info{};
    need(fstat(package.menu_lock,&info)==0 && S_ISREG(info.st_mode) && info.st_size==0,
        "Unsupported Wii Menu profile lease file");
    need(flock(package.menu_lock,LOCK_EX|LOCK_NB)==0,"The Wii Menu profile is already in use");
#endif
}
struct Created {
    std::vector<fs::path> paths;bool committed{};
    ~Created() {
        if(committed)return;
        // Remove only paths created by this attempt, without recursion or
        // clearing protections. Unknown/concurrent files remain untouched.
        for(auto i=paths.rbegin();i!=paths.rend();++i) {std::error_code error;fs::remove(*i,error);}
    }
};
void fresh_profile(const fs::path& destination) {
    if(fs::exists(destination)) {
        need(fs::is_directory(destination) && !linked(destination) && marked(destination),"Existing game profile is invalid; no files were replaced");
        return;
    }
    Created created;
    const auto staging=destination.parent_path()/("nand-initializing-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    need(fs::create_directory(staging),"Cannot create game storage");created.paths.push_back(staging);
    const auto write=[&](const fs::path& relative,const std::string& data) {
        fs::path directory=staging;
        for(const auto& part:relative.parent_path()) {
            directory/=part;
            if(fs::create_directory(directory))created.paths.push_back(directory);
        }
        const auto path=staging/relative;created.paths.push_back(path);
        std::ofstream output(path,std::ios::binary);output.write(data.data(),data.size());output.close();
        need(!output.fail(),"Cannot initialize game storage");
    };
    write(".wiillr-nand-profile","wiillr-copied-nand-v1\n");
    // Reserve the game's local UID, including when DLC is imported first.
    // These are fresh emulator settings, never copied console identity/progress.
    const unsigned char uid[]={0,1,0,1,0x57,0x52,0x39,0x45,0,0,0x10,0};
    write("sys/uid.sys",std::string(reinterpret_cast<const char*>(uid),sizeof uid));
    const std::pair<const char*,unsigned char> entries[]={{"IPL.LNG",1},{"IPL.SND",1},{"IPL.AR",1},{"IPL.E60",1},{"IPL.PGS",0},{"BT.BAR",1},{"BT.MOT",1},{"BT.SPKV",88}};
    struct Setting {const char* name;unsigned type;std::string data;};
    std::vector<Setting> settings;
    for(const auto& [name,value]:entries)settings.push_back({name,3,std::string(1,char(value))});
    // Match the maintained emulated Bluetooth transport's public virtual
    // addresses. These local registrations contain no pairing/link keys.
    std::string devices(0x461,'\0');devices[0]=5;
    for(unsigned slot=0;slot<5;++slot)for(const unsigned start:{1u,701u}) {
        const auto at=start+70*slot;const unsigned char address[]={static_cast<unsigned char>(slot),0,0x79,0x19,2,0x11};
        std::copy(std::begin(address),std::end(address),devices.begin()+at);
        const char* name=slot==4?"Nintendo RVL-WBC-01":"Nintendo RVL-CNT-01";
        devices.replace(at+6,std::char_traits<char>::length(name),name);
    }
    settings.push_back({"BT.DINF",1,std::move(devices)});
    settings.push_back({"BT.CDIF",1,std::string(517,'\0')});
    settings.push_back({"BT.SENS",5,std::string("\0\0\0\3",4)});
    std::string nickname(22,'\0');const std::string player="Player";
    for(size_t n=0;n<player.size();++n)nickname[2*n+1]=player[n];nickname[21]=char(player.size());
    settings.push_back({"IPL.NIK",2,std::move(nickname)});
    std::string config(0x4000,'\0');config.replace(0,4,"SCv0");config.replace(0x3ffc,4,"SCed");
    const auto be16=[&](size_t at,unsigned value){config[at]=char(value>>8);config[at+1]=char(value);};
    be16(4,settings.size());unsigned offset=6+2*(settings.size()+1),index=0;
    for(const auto& setting:settings) {
        be16(6+2*index++,offset);const auto length=std::char_traits<char>::length(setting.name);
        config[offset++]=char((setting.type<<5)|(length-1));config.replace(offset,length,setting.name);offset+=length;
        if(setting.type==1){be16(offset,setting.data.size()-1);offset+=2;}
        else if(setting.type==2)config[offset++]=char(setting.data.size()-1);
        need(offset+setting.data.size()<0x3ffc,"Fresh settings exceed the SYSCONF container");
        config.replace(offset,setting.data.size(),setting.data);offset+=setting.data.size();
    }
    be16(6+2*index,offset);write("shared2/sys/SYSCONF",config);
    const std::string plain="AREA=USA\r\nMODEL=RVL-001(USA)\r\nDVD=0\r\nMPCH=0x7FFE\r\nCODE=LU\r\nSERNO=000000000\r\nVIDEO=NTSC\r\nGAME=US\r\n";
    std::string region(256,'\0');uint32_t key=0x73b5dbfa;
    for(size_t n=0;n<plain.size();++n){region[n]=char(uint8_t(plain[n])^uint8_t(key));key=(key<<1)|(key>>31);}
    write("title/00000001/00000002/data/setting.txt",region);
    std::error_code error;fs::rename(staging,destination,error);
    if(!error){created.committed=true;return;}
    need(fs::is_directory(destination) && !linked(destination) && marked(destination),"Cannot publish game storage");
}
unsigned profile_uid(const fs::path& root) {
    std::ifstream file(root/"sys/uid.sys",std::ios::binary);need(bool(file),"Missing local game identity");
    unsigned char record[12];unsigned uid=0,matches=0;
    const unsigned char title[]={0,1,0,1,0x57,0x52,0x39,0x45};
    while(file.read(reinterpret_cast<char*>(record),sizeof record))if(std::equal(std::begin(title),std::end(title),record)) {
        uid=unsigned(record[8])<<24|unsigned(record[9])<<16|unsigned(record[10])<<8|record[11];++matches;
    }
    need(file.eof() && file.gcount()==0 && matches==1,"Invalid local game identity");return uid;
}
void seed_profile(const fs::path& source,const fs::path& destination,const fs::path& boot,
                  unsigned uid,unsigned gid,WiiPackageValidate validate,bool menu) {
    need(fs::is_directory(source) && marked(source),"The package needs its prepared game data");
    if(fs::exists(destination)) {
        need(fs::is_directory(destination) && !linked(destination) && marked(destination),"Existing game profile is invalid; no files were replaced");
        return;
    }
    need(!within(fs::weakly_canonical(destination.parent_path()),fs::canonical(source)),"Game data cannot contain its writable profile");
    Created created;fs::path staging;
    for(unsigned n=0;n<16;++n) {
        staging=destination.parent_path()/("nand-initializing-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(n));
        if(fs::create_directory(staging)){created.paths.push_back(staging);break;}
    }
    need(!created.paths.empty(),"Cannot create the game profile");
    for(const auto& item:fs::recursive_directory_iterator(source)) {
        need(!linked(item.path()) && item.path().filename()!=".git","Linked or repository content cannot seed a game profile");
        const auto target=staging/item.path().lexically_relative(source);
        if(item.is_directory()) {
            need(fs::create_directory(target),"Cannot create a game-data directory");created.paths.push_back(target);
        } else {
            need(item.is_regular_file(),"Unsupported file in the game data");
            created.paths.push_back(target);
            // New files inherit the writable user's permissions, rather than
            // inheriting a read-only attribute from the packaged template.
            std::ifstream input(item.path(),std::ios::binary);
            std::ofstream output(target,std::ios::binary);
            need(bool(input) && bool(output),"Cannot copy the game data");
            std::array<char,65536> buffer;
            while(input) {input.read(buffer.data(),buffer.size());output.write(buffer.data(),input.gcount());}
            output.close();
            need(!input.bad() && !output.fail(),"Could not finish copying the game data");
        }
    }
    if(menu) {
        const auto path=staging/"shared2/sys/SYSCONF";
        need(fs::is_regular_file(path) && fs::file_size(path)==16384,"Prepared Wii Menu settings are missing or unsupported");
        std::array<uint8_t,16384> config{};
        {std::ifstream f(path,std::ios::binary);f.read(reinterpret_cast<char*>(config.data()),config.size());need(bool(f),"Cannot read prepared Wii Menu settings");}
        need(wii_menu_profile_seed(config.data(),config.size()),"Prepared Wii Menu calendar/display settings are unsupported");
        {std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(config.data()),config.size());f.close();need(!f.fail(),"Cannot seed new Wii Menu settings");}
        const auto network=staging/"shared2/sys/net/02/config.dat";
        if(fs::is_regular_file(network) && fs::file_size(network)==7004) {
            std::array<uint8_t,7004> bytes{};
            {std::ifstream f(network,std::ios::binary);f.read(reinterpret_cast<char*>(bytes.data()),bytes.size());need(bool(f),"Cannot read prepared network settings");}
            if(wii_network_seed_fresh_menu_config(bytes.data(),bytes.size())) {
                std::ofstream f(network,std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());f.close();need(!f.fail(),"Cannot seed new wired settings");
            }
        }
    }
    // The installed-title validator may initialize these IOS metadata paths.
    // They belong to this new staging tree, not an existing user profile.
    created.paths.push_back(staging/"tmp");created.paths.push_back(staging/"fst.bin");
    const auto image=(staging/boot).string(),profile=staging.string();
    need(validate && validate(image.c_str(),profile.c_str(),uid,gid),"Packaged game data did not pass the installed-title checks");
    std::error_code error;fs::rename(staging,destination,error);
    if(!error){created.committed=true;return;}
    // Another launch can finish the same initial import first. Never replace
    // its completed profile or merge a template over native progress.
    need(fs::is_directory(destination) && !linked(destination) && marked(destination),"Cannot publish the game profile");
}
}
extern "C" WiiPackage* wii_package_open(const char* base_text,const char* user_text,WiiPackageValidate validate,char* error,size_t capacity) {
    try {
        need(base_text && *base_text && user_text && *user_text,"Missing package or user directory");
        const auto base=fs::canonical(base_text);const auto config=read_config(base/"game.ini");
        auto package=std::make_unique<WiiPackage>();
        package->menu=config.contains("Name") && config.at("Name")=="Wii Menu";
        const auto uid=number(config.at("Uid"),0xffffffffu),gid=number(config.at("Gid"),0xffffu);
        package->uid=std::to_string(uid);package->gid=std::to_string(gid);
        const auto ios=number(config.at("Ios"),80);
        need(package->menu?(ios==4 || ios==80):ios==37,"Unsupported package IOS profile");package->ios=std::to_string(ios);
        const bool importing=config.contains("Import");
        need(!package->menu || !importing,"The Wii Menu package requires its prepared console data");
        package->standalone=importing;
        const auto boot=relative(config.at("Boot"));
        const auto source=importing?fs::path{}:resource(base,config.at("Nand"));
        if(!importing)need(fs::is_directory(source) && marked(source) && fs::is_regular_file(resource(source,config.at("Boot"))),"The package needs its prepared game data");
        const bool hle=config.at("DspRom")=="hle" && config.at("DspCoef")=="hle";
        need(!hle || WIILLR_PACKAGE_DSP_HLE,"This build requires original DSP files");
        const auto rom=hle?fs::path{}:resource(base,config.at("DspRom")),coef=hle?fs::path{}:resource(base,config.at("DspCoef"));
        const auto graphics=resource(base,config.at("Graphics"));
        need(fs::is_regular_file(graphics) && fs::file_size(graphics)>0 &&
             (hle || (fs::is_regular_file(rom) && fs::file_size(rom)==8192 &&
             fs::is_regular_file(coef) && fs::file_size(coef)==4096)),"Incomplete graphics or audio files in the package");
        const auto user=fs::absolute(user_text).lexically_normal();
        const auto profile=user/"nand";
        if(!importing) {
            need(!within(fs::weakly_canonical(user),fs::canonical(source)),"User storage cannot be inside the packaged game data");
            const auto target=fs::weakly_canonical(profile),origin=fs::canonical(source);
            need(!within(target,origin) && !within(origin,target),"Packaged game data cannot be the writable profile");
        }
        fs::create_directories(user);
        // Before any destination validation or metadata write, hold this
        // Menu-package profile through UI, execution and title handoffs.
        // The OS releases it on close/crash; a harmless empty file may remain.
        if(package->menu)lock_menu_profile(*package,user);
        if(importing){fresh_profile(profile);package->uid=std::to_string(profile_uid(profile));}
        else seed_profile(source,profile,boot,uid,gid,validate,package->menu);
        const auto image=profile/boot;
        if(fs::exists(image))need(fs::is_regular_file(resource(profile,config.at("Boot"))),"The installed game boot file is invalid");
        else need(importing,"The installed game boot file is missing");
        fs::create_directories(user/"data");fs::create_directories(user/"cache");
        package->boot=image.string();package->nand=profile.string();package->rom=hle?"@hle":rom.string();package->coef=hle?"@hle":coef.string();
        package->graphics=graphics.string();package->data=(user/"data").string();package->cache=(user/"cache").string();package->log=(user/"game.log").string();
        if(error && capacity)*error=0;return package.release();
    } catch(const std::exception& e) {if(error && capacity)std::snprintf(error,capacity,"%s",e.what());return nullptr;}
}
extern "C" void wii_package_close(WiiPackage* package){delete package;}
extern "C" void wii_package_arguments(WiiPackage* p,const char* executable,const char* action,int* argc,char*** argv) {
    std::string selected_action=action;
    if(p->menu) {
        if(selected_action=="--launcher")selected_action="--menu-launcher";
        else if(selected_action=="--launcher-settings")selected_action="--menu-launcher-settings";
        else if(selected_action=="--launcher-check")selected_action="--menu-launcher-check";
    }
    p->values={executable,selected_action,p->boot,"62985600000000","","0",p->rom,p->coef,p->nand,p->uid,p->gid,
        "--gx",p->graphics,p->data,p->cache,"--ios-profile",p->ios,""};
    for(unsigned n=0;n<17;++n)p->argv[n]=p->values[n].data();p->argv[17]=nullptr;
    *argc=17;*argv=p->argv.data();
}
extern "C" const char* wii_package_log(WiiPackage* p){return p->log.c_str();}
extern "C" bool wii_package_standalone(WiiPackage* p){return p && p->standalone;}
extern "C" bool wii_package_menu(WiiPackage* p){return p && p->menu;}
extern "C" const char* wii_package_name(const char* base) {
    if(!base || !*base)return "Mega Man 9";
    try {
        const auto config=read_config(fs::path(base)/"game.ini");
        return config.contains("Name") && config.at("Name")=="Wii Menu"?"Wii Menu":"Mega Man 9";
    }catch(const std::exception&){return "Mega Man 9";}
}
