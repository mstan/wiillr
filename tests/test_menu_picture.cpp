// SPDX-License-Identifier: MIT
#include <wiillr/menu_picture.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <iostream>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>
namespace {
void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
std::vector<uint8_t> ram(0x4000);
constexpr uint32_t pane=0x80000100,material=0x80000400,info=0x80000600,mem=0x80000800,coords=0x80000900;
uint64_t uploads{},draws{},destroyed{},next_handle=1;bool fail_draw{};GcWiiNativeDraw last{};
const uint8_t* read(void*,uint32_t address,uint32_t size){address&=0x1fffffffu;if(address==0x1695618)address=0xa00;return address<=ram.size() && size<=ram.size()-address?ram.data()+address:nullptr;}
void p16(uint32_t a,uint16_t v){a&=0x1fffffff;ram.at(a)=v>>8;ram.at(a+1)=v&255;}
void p32(uint32_t a,uint32_t v){p16(a,v>>16);p16(a+2,v&65535);}
void pf(uint32_t a,float f){p32(a,std::bit_cast<uint32_t>(f));}
void identity(uint32_t a){for(unsigned i=0;i<12;++i)pf(a+i*4,i==0 || i==5 || i==10?1.f:0.f);}
WiiMenuView camera(){WiiMenuView out{};out.projection[0]=out.projection[5]=out.projection[15]=1;out.projection[10]=-1;out.width=out.clip_width=640;out.height=out.clip_height=480;out.far_depth=1;return out;}
void fixture() {
    std::fill(ram.begin(),ram.end(),0);p32(pane,0x816717f4);p32(pane+0x28,material);p32(material,0x81671b48);
    for(unsigned i=0;i<4;++i)p16(material+0x2c+i*2,255);
    identity(pane+0x84);pf(pane+0x4c,10);pf(pane+0x50,20);ram[(pane&0x1fffffff)+0xcc]=4;ram[(pane&0x1fffffff)+0xce]=128;
    constexpr uint32_t colors[]{0xff0000ff,0x00ff007f,0x0000ff40,0xffffffff};for(unsigned i=0;i<4;++i)p32(pane+0xd4+i*4,colors[i]);
    identity(info+4);pf(info+0x38,0);pf(info+0x40,480);
}
uint64_t create(const GcWiiNativeMesh* mesh){require(mesh && mesh->vertex_count==4 && mesh->index_count==6,"Native quad topology");require(mesh->indices[0]==0 && mesh->indices[5]==3,"Native quad winding");++uploads;return next_handle++;}
int destroy(uint64_t handle){require(handle>0,"Destroying invalid native mesh");++destroyed;return 1;}
int draw(const GcWiiNativeDraw* job){last=*job;if(fail_draw)return 0;++draws;return 1;}
void status(GcWiiNativeRenderStatus*){}
void units() {
    fixture();const WiiMenuMemory memory{nullptr,read};auto view=camera();WiiMenuPicture out{};char error[192]{};
    require(wii_menu_picture_prepare(memory,pane,info,&view,&out,error,sizeof(error)),error);
    require(out.vertices[0].position[0]==-5 && out.vertices[0].position[1]==-10 && out.vertices[2].position[0]==5 && out.vertices[2].position[1]==10,"Centered pane geometry");
    require(std::abs(out.vertices[0].color[3]-128.f/255)<1e-7f && std::abs(out.vertices[1].color[3]-63.f/255)<1e-7f,"Original integer global alpha");
    require(out.vertices[2].color[0]==1 && out.vertices[3].color[2]==1,"Pane corner order");
    ram[(info&0x1fffffff)+0x50]=0x80;pf(info+4+3*4,20);pf(info+0x40,-480);
    require(wii_menu_picture_prepare(memory,pane,info,&view,&out,error,sizeof(error)),error);
    require(out.draw.model_view[3]==20 && out.draw.model_view[5]==-1,"View multiplication and Y-axis column inversion");
    p32(material+0x4c,0x11100000);p32(material+0x50,0x11100000);p32(material+0x58,mem);
    p32(mem,(4u<<5)|(1u<<4)|1u);p32(mem+8,(1u<<20)|(3u<<10)|7);p32(mem+12,0x10000100u>>5);
    // TexSRT = translate.xy, rotation, scale.xy; one generator follows it.
    pf(mem+32,.25f);pf(mem+36,.5f);pf(mem+40,0);pf(mem+44,2);pf(mem+48,.5f);
    p32(mem+52,0x01041eff);ram[(pane&0x1fffffff)+0xe5]=1;p32(pane+0xe8,coords);
    constexpr float uv[]{0,0,1,0,0,1,1,1};for(unsigned n=0;n<8;++n)pf(coords+n*4,uv[n]);
    require(wii_menu_picture_prepare(memory,pane,info,&view,&out,error,sizeof(error)),error);
    require(out.draw.material.texture.address==0x10000100 && out.draw.material.texture.size==32 && out.draw.material.texture.width==8,"MEM2 texture identity/layout");
    require(out.vertices[0].uv[0]==-.25f && out.vertices[2].uv[0]==1.75f && out.vertices[0].uv[1]==.75f && out.vertices[2].uv[1]==1.25f,"Native texture scale/translation");
    const GcWiiNativeRenderApi api{1,create,destroy,draw,status};auto* renderer=wii_menu_native_create(memory,&api);require(renderer,"Native provider creation");
    require(wii_menu_native_draw_picture(renderer,pane,info,&view)==1,"Initial native picture draw");
    pf(pane+0x84+3*4,12);require(wii_menu_native_draw_picture(renderer,pane,info,&view)==1,"Animated pose draw");
    require(uploads==1 && draws==2 && last.model_view[3]==32,"Pose animation must reuse geometry");
    ram[(pane&0x1fffffff)+0xce]=255;require(wii_menu_native_draw_picture(renderer,pane,info,&view)==1,"Animated alpha draw");
    require(uploads==2 && destroyed==1,"Changed vertex alpha must replace immutable mesh");
    p32(material+0x50,0x21100000);const auto previous=out;
    require(!wii_menu_picture_prepare(memory,pane,info,&view,&out,error,sizeof(error)) && std::memcmp(&previous,&out,sizeof(out))==0,"Unsupported material must not publish partial output");
    require(wii_menu_native_draw_picture(renderer,pane,info,&view)==0 && draws==3,"Unsupported material must not submit");
    p32(material+0x50,0x11100000);fail_draw=true;require(wii_menu_native_draw_picture(renderer,pane,info,&view)==-1,"Backend failure must be distinct from unsupported family");fail_draw=false;
    wii_menu_native_forget_picture(renderer,pane);WiiMenuNativeStatus result{};wii_menu_native_status(renderer,&result);
    require(result.live_meshes==0 && result.draws==3 && result.rejected==2,"Native provider accounting");
    p32(pane+0x28,0);require(wii_menu_native_draw_picture(renderer,pane,info,&view)==1,"Original null-material no-op");
    wii_menu_native_destroy(renderer);
    p32(0xa00,0x80001000);p32(0x1528,1);pf(0x152c,1);pf(0x1534,1);pf(0x153c,-1);pf(0x1540,0);
    pf(0x154c,640);pf(0x1550,480);pf(0x1558,1);p32(0x1148,(342u<<12)|342u);p32(0x114c,(981u<<12)|821u);p32(0x1220,24);
    WiiMenuView sdk{};require(wii_menu_picture_sdk_view(memory,&sdk,error,sizeof(error)),error);
    require(sdk.width==640 && sdk.height==480 && sdk.clip_width==640 && sdk.clip_height==480 && sdk.projection[15]==1,"Original SDK view layout");
    std::cout<<"Native Menu pane geometry, alpha, pose, textures, cache and explicit failures passed\n";
}
struct Fixture {std::vector<uint8_t> mem1,mem2;};
const uint8_t* fixture_read(void* user,uint32_t address,uint32_t size) {
    const auto& f=*static_cast<const Fixture*>(user);address&=0x1fffffff;
    const auto* bank=&f.mem1;if(address>=0x10000000){address-=0x10000000;bank=&f.mem2;}
    return address<=bank->size() && size<=bank->size()-address?bank->data()+address:nullptr;
}
std::vector<uint8_t> file(const char* path){std::ifstream input(path,std::ios::binary);require(bool(input),"Missing private memory fixture");return {std::istreambuf_iterator<char>(input),{}};}
void snapshot(const char* mem1,const char* mem2) {
    Fixture f{file(mem1),file(mem2)};WiiMenuMemory memory{&f,fixture_read};char error[192];WiiMenuView view{};
    require(wii_menu_picture_sdk_view(memory,&view,error,sizeof(error)),error);
    uint32_t info=0;
    for(const auto* bank:{&f.mem1,&f.mem2})for(size_t at=0;at+0x54<=bank->size();at+=4)if((*bank)[at]==0x81 && (*bank)[at+1]==0x67 && (*bank)[at+2]==0x1b && (*bank)[at+3]==0x70){info=uint32_t(at)+(bank==&f.mem1?0x80000000u:0x90000000u);break;}
    require(info!=0,"No original DrawInfo object in fixture");
    size_t accepted=0,rejected=0;
    for(size_t at=0;at+0xec<=f.mem2.size();at+=4)if(f.mem2[at]==0x81 && f.mem2[at+1]==0x67 && f.mem2[at+2]==0x17 && f.mem2[at+3]==0xf4) {
        WiiMenuPicture picture{};const auto address=uint32_t(at)+0x90000000u;const auto ok=wii_menu_picture_prepare(memory,address,info,&view,&picture,error,sizeof(error));
        const auto* name=reinterpret_cast<const char*>(f.mem2.data()+at+0xb4);
        std::cout<<(ok?"accepted ":"rejected ")<<std::hex<<address<<std::dec<<' '<<std::string(name,std::find(name,name+16,'\0'))<<' '<<(ok?"":error)<<'\n';
        if(ok)++accepted;else ++rejected;
    }
    std::cout<<"live_pictures_accepted="<<accepted<<" live_pictures_rejected="<<rejected<<" original_drawinfo="<<std::hex<<info<<std::dec<<'\n';
    require(accepted>0,"No real original Picture object reached native preparation");
}
}
int main(int argc,char** argv){try{units();if(argc==3)snapshot(argv[1],argv[2]);return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
