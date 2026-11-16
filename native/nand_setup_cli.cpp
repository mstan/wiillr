/* SPDX-License-Identifier: MIT */
#include "wiillr/nand_setup.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
static int run(int argc,char** argv){
    if(argc<3 || argc>5){std::fprintf(stderr,"Usage: wiillr-nand-setup inspect SOURCE [KEYS] | import SOURCE NEW_DESTINATION [KEYS]\n");return 2;}
    WiiNandSetupInfo info{};char error[1024]{};WiiNandSetupStatus status;
    if(!std::strcmp(argv[1],"inspect") && argc<=4)
        status=wii_nand_setup_inspect(argv[2],argc==4?argv[3]:nullptr,&info,error,sizeof error);
    else if(!std::strcmp(argv[1],"import") && argc>=4)
        status=wii_nand_setup_import(argv[2],argc==5?argv[4]:nullptr,argv[3],&info,error,sizeof error);
    else return 2;
    std::printf("status=%d ready=%d menu=%s region=%s ios=%u files=%llu bytes=%llu\n%s\n",int(status),int(info.ready),info.menu_label,info.region_label,info.ios_profile,(unsigned long long)info.files,(unsigned long long)info.bytes,info.details);
    return status==WII_NAND_SETUP_READY?0:int(status)+10;
}
#ifdef _WIN32
int wmain(int argc,wchar_t** values){
    std::vector<std::string> text;std::vector<char*> args;
    for(int i=0;i<argc;++i){const int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,values[i],-1,nullptr,0,nullptr,nullptr);if(!n)return 2;std::string s(size_t(n),'\0');if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,values[i],-1,s.data(),n,nullptr,nullptr))return 2;s.pop_back();text.push_back(std::move(s));}
    for(auto& s:text)args.push_back(s.data());return run(argc,args.data());
}
#else
int main(int argc,char** argv){return run(argc,argv);}
#endif
