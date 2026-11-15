// SPDX-License-Identifier: MIT
#include <wiillr/menu_picture.h>
#include <array>
#include <bit>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
std::vector<uint8_t> ram(0x8000);
constexpr uint32_t pane=0x80000100,mat=0x80000400,info=0x80000600,mem=0x80000800,uv=0x80001000;
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
const uint8_t* read(void*,uint32_t a,uint32_t n){a&=0x1fffffff;return a<=ram.size() && n<=ram.size()-a?ram.data()+a:nullptr;}
void p16(uint32_t a,uint16_t v){a&=0x1fffffff;ram.at(a)=v>>8;ram.at(a+1)=v;}
void p32(uint32_t a,uint32_t v){p16(a,v>>16);p16(a+2,v);}
void pf(uint32_t a,float v){p32(a,std::bit_cast<uint32_t>(v));}
void identity(uint32_t a){for(unsigned i=0;i<12;++i)pf(a+i*4,i==0 || i==5 || i==10?1.f:0.f);}
uint32_t material(uint32_t a,uint32_t storage,unsigned textures,unsigned generators,unsigned stages) {
    p32(a,0x81671b48);const uint32_t count=(textures<<28)|(generators<<20)|(stages<<9);p32(a+0x4c,count);p32(a+0x50,count);p32(a+0x58,storage);
    for(unsigned c=0;c<4;++c)p16(a+0x2c+c*2,255);
    for(unsigned i=0;i<16;++i)ram[(a&0x1fffffff)+0x3c+i]=128;
    for(unsigned i=0;i<textures;++i){p32(storage+i*32+8,3|(3<<10)|(6<<20));p32(storage+i*32+12,(0x200000+i*0x10000)>>5);}
    for(unsigned i=0;i<generators;++i)p32(storage+textures*32+i*4,(1u<<24)|((4+i)<<16)|(60<<8));
    return storage+textures*32+generators*4;
}
void stage(uint32_t a,unsigned texture,std::array<uint8_t,4> color,std::array<uint8_t,4> alpha) {
    p32(a,((texture<8?texture:255)<<24)|(4u<<16)|((texture<8?texture:255)<<8));
    p32(a+4,((color[0]|(color[1]<<4))<<24)|((color[2]|(color[3]<<4))<<16)|0x61);
    p32(a+8,((alpha[0]|(alpha[1]<<4))<<24)|((alpha[2]|(alpha[3]<<4))<<16)|0xe1);p32(a+12,0);
}
void fixture(bool window,unsigned textures,unsigned generators,unsigned stages) {
    std::fill(ram.begin(),ram.end(),0);p32(pane,window?0x81671a20:0x816717f4);p32(pane+0x28,mat);
    pf(pane+0x4c,80);pf(pane+0x50,60);identity(pane+0x84);identity(info+4);pf(info+0x40,480);
    ram[(pane&0x1fffffff)+0xce]=255;
    for(unsigned i=0;i<4;++i)p32(pane+(window?0xe4:0xd4)+i*4,0xffffffff);
    ram[(pane&0x1fffffff)+(window?0xf5:0xe5)]=generators;p32(pane+(window?0xf8:0xe8),uv);
    for(unsigned c=0;c<generators;++c)for(unsigned v=0;v<4;++v){pf(uv+c*32+v*8,float(c*10+v%2));pf(uv+c*32+v*8+4,float(c*20+v/2));}
    material(mat,mem,textures,generators,stages);
}
WiiMenuView camera(){WiiMenuView v{};v.projection[0]=v.projection[5]=v.projection[15]=1;v.projection[10]=-1;v.width=v.clip_width=640;v.height=v.clip_height=480;v.far_depth=1;return v;}
unsigned uploads,draws;std::vector<float> copied_uv;GcWiiNativeGuiProgramDraw submitted;
uint64_t create(const GcWiiNativeMesh*){return ++uploads;}
uint64_t create_program(const GcWiiNativeGuiMesh* mesh){copied_uv.assign(mesh->extra_uv,mesh->extra_uv+mesh->extra_uv_count*8);return create(&mesh->mesh);}
int destroy(uint64_t){return 1;}int draw(const GcWiiNativeDraw*){++draws;return 1;}
int program_draw(const GcWiiNativeGuiProgramDraw* input){submitted=*input;return draw(&input->draw);}
void status(GcWiiNativeRenderStatus*){}
}
int main(){try {
    const WiiMenuMemory memory{nullptr,read};const auto view=camera();char error[192];WiiMenuWindow window{};
    fixture(true,0,0,1);stage(mem,255,{15,15,15,10},{7,7,7,5});ram[(pane&0x1fffffff)+0xce]=128;
    check(wii_menu_window_prepare(memory,pane,info,&view,&window,error,sizeof error),error);
    check(window.count==1 && window.quads[0].needs_program && window.quads[0].program.stage_count==1,"Untextured authored Window program");
    check(window.quads[0].vertices[0].color[3]==128.f/255.f && window.quads[0].program.stages[0].color.inputs[3]==10,"Window raster alpha and color remain original inputs");
#ifndef WIILLR_PROGRAM_BASELINE
    fixture(false,3,3,3);auto stages=mem+3*32+3*4;
    stage(stages,0,{15,15,15,8},{7,7,7,4});stage(stages+16,1,{15,0,8,15},{7,0,4,7});stage(stages+32,2,{15,15,15,0},{7,7,7,0});
    WiiMenuPicture picture{};check(wii_menu_picture_prepare(memory,pane,info,&view,&picture,error,sizeof error),error);
    check(picture.needs_program && picture.program.stage_count==3 && picture.program_extra_count==2,"Complete three-image equation accepted without named byte pattern");
    check(picture.program.stages[1].color.inputs[1]==0 && picture.program.stages[1].texture==1,"Second-stage dependency and texture preserved");
    const GcWiiNativeRenderApi render{GC_WII_NATIVE_RENDER_ABI,create,destroy,draw,status};
    auto* owner=wii_menu_native_create(memory,&render);check(owner!=nullptr,"Native owner created");
    check(!wii_menu_native_draw_picture(owner,pane,info,&view) && uploads==0 && draws==0,"Missing program backend rejects before resource/draw effects");
    const GcWiiNativeGuiProgramApi program{GC_WII_NATIVE_GUI_PROGRAM_ABI,create_program,program_draw};
    check(wii_menu_native_gui_program(owner,&program) && wii_menu_native_draw_picture(owner,pane,info,&view)==1,"Typed shader submission");
    check(uploads==1 && draws==1 && copied_uv.size()==16 && copied_uv[4]==11 && copied_uv[5]==20 && copied_uv[6]==21 && copied_uv[7]==40,"Extra UVs copied densely per vertex");
    check(wii_menu_native_draw_picture(owner,pane,info,&view)==1 && uploads==1 && draws==2,"Unchanged retained mesh reused");
    check(submitted.texture_count==3 && submitted.coordinate_source[2]==2,"Typed texture/UV ownership");wii_menu_native_destroy(owner);
    fixture(false,2,2,0);check(wii_menu_picture_prepare(memory,pane,info,&view,&picture,error,sizeof error),error);
    check(picture.needs_program && picture.program.stage_count==2 && picture.program.stages[1].color_constant==31,"Automatic two-image interpolation material");
    const auto previous=picture;ram[(mem&0x1fffffff)+64+1]=15;
    check(!wii_menu_picture_prepare(memory,pane,info,&view,&picture,error,sizeof error) && !std::memcmp(&picture,&previous,sizeof picture),"Unbound UV source rejects atomically");
    fixture(false,3,3,3);stages=mem+108;stage(stages,0,{15,15,15,8},{7,7,7,4});stage(stages+16,1,{15,0,8,15},{7,0,4,7});stage(stages+32,2,{15,15,15,0},{7,7,7,0});
    ram[(stages&0x1fffffff)+8]=0x78;
    check(!wii_menu_picture_prepare(memory,pane,info,&view,&picture,error,sizeof error),"Invalid alpha selector is not generalized");
#endif
    std::cout<<"native complete GUI material contract PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
