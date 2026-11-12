// SPDX-License-Identifier: MIT
#include <wiillr/mm9_compositor.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {
void need(bool b,const char* why){if(!b)throw std::runtime_error(why);}
struct Reader {
    WiiMm9Memory memory;
    const uint8_t* bytes(uint32_t a,uint32_t n)const{auto* p=memory.read(memory.user,a,n);need(p!=nullptr,"Unreadable MM9 compositor input");return p;}
    uint8_t u8(uint32_t a)const{return *bytes(a,1);}
    uint16_t u16(uint32_t a)const{auto* p=bytes(a,2);return uint16_t(p[0])<<8|p[1];}
    uint32_t u32(uint32_t a)const{auto* p=bytes(a,4);return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
};
constexpr uint32_t backgrounds=0x8039b190,world=0x80301280,palettes=0x803bb320,resources=0x8043ca80,sprite_pool=0x8037c428;
int signed10(uint32_t n){return int(n&1023)-int((n&512)<<1);}
uint16_t rgb565(uint16_t rgb555){return uint16_t((rgb555&0x7c00)<<1|(rgb555&0x03e0)<<1|(rgb555&0x0200)>>4|(rgb555&31));}
uint16_t color(const Reader& r,unsigned group,unsigned palette,unsigned index) {
    need(group<2 && r.u32(palettes+0x30+group*4)==0,"MM9 palette family needs qualification");
    need(palette<r.u32(palettes+0x20+group*4) && index<16,"Invalid MM9 palette selection");
    return rgb565(r.u16(r.u32(palettes+0x10+group*4)+(palette*16+index)*2)>>1);
}
unsigned nibble(const Reader& r,uint32_t base,unsigned width,unsigned x,unsigned y) {
    const unsigned at=((y/8)*(width/8)+x/8)*32+(y%8)*4+(x%8)/2;
    const auto b=r.u8(base+at);return (x&1)?(b&15):(b>>4);
}
void pixel(WiiMm9Frame& out,unsigned x,unsigned y,uint16_t value) {
    const unsigned at=((y/4)*64+x/4)*32+(y%4)*8+(x%4)*2;
    out.texture[at]=uint8_t(value>>8);out.texture[at+1]=uint8_t(value);
}
struct Layer {
    unsigned width{},height{},priority{},slot{},mode{};
    uint32_t map{},tiles{},scroll{};unsigned u{},v{};
    uint32_t world_grid{},world_definitions{};
    unsigned world_stride{},world_width{},world_height{},world_count{};
    int world_x{},world_y{};
};
void world_source(const Reader& r,unsigned id,Layer& layer) {
    // WR9E's 8009f1a8 / 800c9704 loader expands mutable 16x16 world cells
    // into a rolling 512-column cache. Read those cells directly for margins;
    // cache contents outside the original viewport need not be current.
    constexpr uint32_t state=0x8031bf70,grid=state+0x8a4;
    for(unsigned n=0;n<4;++n) {
        const auto object=state+0x20+n*0x94;
        if(r.u32(object+0x5c)!=id || r.u32(object+4)!=0x8009f1a8 ||
           r.u16(object+0xc) || layer.mode)continue;
        const auto layout=r.u32(object+0x28),definitions=r.u32(object+0x20);
        const auto attributes=r.u32(state+0x270);
        need(layout && definitions && definitions==r.u32(state+0x274) &&
             attributes>definitions && (attributes-definitions)%8==0,
             "Unbound MM9 world definitions");
        const unsigned columns=r.u8(layout+2),rows=r.u8(layout+3),stride=r.u16(grid);
        need(columns && columns<=127 && rows && rows<=127 && stride==columns*16,
             "Invalid MM9 world dimensions");
        // These are the loader's published coordinates, including incremental
        // scroll updates. Its target at +34/+38 can still name the prior cell.
        layer.world_x=int32_t(r.u32(object+0x14));layer.world_y=int32_t(r.u32(object+0x18));
        need((unsigned(layer.world_x)&511)==(layer.u&511) &&
             (unsigned(layer.world_y)&511)==(layer.v&511),"MM9 world/cache origin mismatch");
        layer.world_grid=grid+4;layer.world_definitions=definitions;
        layer.world_stride=stride;layer.world_width=columns*256;layer.world_height=rows*224;
        layer.world_count=(attributes-definitions)/8;
        r.bytes(layer.world_grid,stride*rows*14*2);
        r.bytes(definitions,attributes-definitions);
        return;
    }
}
struct Sprite {uint32_t tiles{},position{};unsigned width{},height{},palette{},priority{},kind{};int x{},y{};bool flip_x{},flip_y{},limited{};};
template<class Put> WiiMm9Frame compose(const Reader& r,unsigned width,Put put) {
    WiiMm9Frame out{};std::array<Layer,4> layers{};
    const int left=int(width-256)/2;
    const auto config=r.u32(world+0x40),flags=r.u32(world),enabled=r.u32(config)>>8;
    const auto graphics=r.u32(resources+0x258);
    for(unsigned order=0;order<4;++order) {
        const auto id=r.u32(0x80281e48+order*4);need(id<=63,"Unbound MM9 background id");
        auto& layer=layers[order];layer.slot=id>>4;
        if(!(enabled&(id&15)))continue;
        const auto attributes=r.u16(config+8+layer.slot*2);layer.width=256u<<((attributes>>14)&1);layer.height=256u<<((attributes>>15)&1);layer.priority=attributes&3;
        layer.map=graphics+((flags>>11)&0x70000)+((attributes<<3)&0xf800);
        layer.tiles=graphics+((flags>>8)&0x70000)+((attributes<<12)&0x3c000);
        layer.u=r.u16(config+0x10+layer.slot*4);layer.v=r.u16(config+0x12+layer.slot*4);
        layer.mode=r.u8(backgrounds+0x90+order);layer.scroll=r.u32(backgrounds+0x94+order*4);
        need(layer.mode<=3 && (!layer.mode || layer.scroll),"Unbound MM9 per-line scroll mode");
        r.bytes(layer.map,(layer.width/8)*(layer.height/8)*2);r.bytes(layer.tiles,256*256/2);
        if(width>256)world_source(r,id,layer);
    }
    const auto begin=r.u32(sprite_pool+0x80c),end=r.u32(sprite_pool+0x808);
    need(begin>=sprite_pool && end>=begin && end<=sprite_pool+0x800 && (end-begin)%8==0,"Invalid MM9 sprite pool span");
    std::vector<Sprite> sprites;const auto sprite_tiles=r.u32(resources+0x25c);
    for(auto address=begin;address<end;address+=8) {
        const auto descriptor=r.u32(address),position=r.u32(address+4);
        Sprite sprite{};sprite.width=1u<<(((descriptor>>2)&7)+3);sprite.height=1u<<(((descriptor>>5)&7)+3);
        need(sprite.width<=1024 && sprite.height<=1024,"Unbounded MM9 sprite size");
        sprite.tiles=sprite_tiles+(descriptor>>16)*32;sprite.palette=(descriptor>>8)&255;sprite.priority=descriptor&3;
        sprite.kind=(position>>6)&7;need(sprite.kind<=1,"Unbound MM9 sprite draw function");
        sprite.x=signed10(position>>12);sprite.y=signed10(position>>22);sprite.flip_x=(position>>10)&1;sprite.flip_y=(position>>11)&1;sprite.limited=(position>>9)&1;
        if(!sprite.kind)r.bytes(sprite.tiles,sprite.width*sprite.height/2);
        sprites.push_back(sprite);
    }
    // The logical active screen occupies rows 8..231 of the 256x240 copy.
    // Border/clear and exact copy sampling still require the live gate.
    const auto clear=r.u16(0x8038b2f8+0x26bc);const auto backdrop=rgb565(clear&0x7fff);const auto limit=r.u32(0x803b930c);
    for(unsigned y=0;y<224;++y) {
        for(unsigned x=0;x<width;++x)put(out,x,y+8,backdrop);
        unsigned count=0;
        for(int priority=3;priority>=0;--priority) {
            // The original layer order is 3,2,1,0 within each priority.
            for(int order=3;order>=0;--order) {
                const auto& layer=layers[unsigned(order)];if(!layer.width || layer.priority!=unsigned(priority))continue;
                auto u=layer.u,v=layer.v;
                if(layer.mode==1)u=r.u16(layer.scroll+y*2);else if(layer.mode==2)v=r.u16(layer.scroll+y*2);
                else if(layer.mode==3){const auto scroll=r.u32(layer.scroll+y*4);u=scroll&65535;v=scroll>>16;}
                v=(v+y)&(layer.height-1);
                for(unsigned x=0;x<width;++x) {
                    unsigned source_x=(u+x-unsigned(left))&(layer.width-1),source_y=v;
                    const unsigned page_x=source_x/256,page_y=v/256;
                    const unsigned tile=(page_y*(layer.width/256)+page_x)*1024+((v%256)/8)*32+(source_x%256)/8;
                    uint16_t item;
                    if(layer.world_grid && (x<unsigned(left) || x>=unsigned(left+256))) {
                        const int wx=layer.world_x+int(x)-left,wy=layer.world_y+int(y);
                        if(wx<0 || wy<0 || unsigned(wx)>=layer.world_width || unsigned(wy)>=layer.world_height)continue;
                        const auto cell=r.u16(layer.world_grid+((unsigned(wy)/16)*layer.world_stride+unsigned(wx)/16)*2);
                        need(cell<layer.world_count,"Invalid MM9 world metatile");
                        item=r.u16(layer.world_definitions+cell*8+((unsigned(wy)/8)&1)*4+((unsigned(wx)/8)&1)*2);
                        source_x=unsigned(wx);source_y=unsigned(wy);
                    } else item=r.u16(layer.map+tile*2);
                    const unsigned tx=(item>>6)&31,ty=item>>11;
                    const auto px=(item&32)?7-(source_x&7):(source_x&7),py=(item&16)?7-(source_y&7):(source_y&7);
                    const auto index=nibble(r,layer.tiles,256,tx*8+px,ty*8+py);if(!index)continue;
                    put(out,x,y+8,color(r,0,item&15,index));if(x>=unsigned(left) && x<unsigned(left+256))++out.background_pixels;
                }
            }
            for(const auto& sprite:sprites) {
                if(sprite.priority!=unsigned(priority) || int(y)<sprite.y || int(y)>=sprite.y+int(sprite.height))continue;
                if(limit && sprite.limited){if(count>=limit){++out.limited_sprite_rows;continue;}++count;}
                if(sprite.kind)continue;
                const auto sy=unsigned(int(y)-sprite.y);const auto py=sprite.flip_y?sprite.height-1-sy:sy;
                for(unsigned sx=0;sx<sprite.width;++sx) {
                    const int x=sprite.x+int(sx)+left;if(x<0 || x>=int(width))continue;
                    const auto px=sprite.flip_x?sprite.width-1-sx:sx;const auto index=nibble(r,sprite.tiles,sprite.width,px,py);if(!index)continue;
                    put(out,unsigned(x),y+8,color(r,1,sprite.palette,index));if(x>=left && x<left+256)++out.sprite_pixels;
                }
            }
        }
        out.final_sprite_count=count;
    }
    return out;
}
}
extern "C" int wii_mm9_composite_prepare(WiiMm9Memory memory,WiiMm9Frame* output,char* error,uint32_t size) {
    if(!memory.read || !output){if(error && size)std::snprintf(error,size,"Missing MM9 compositor input");return 0;}
    try{const auto frame=compose({memory},256,pixel);*output=frame;if(error && size)*error=0;return 1;}
    catch(const std::exception& e){if(error && size)std::snprintf(error,size,"%s",e.what());return 0;}
}
extern "C" int wii_mm9_composite_present_prepare(WiiMm9Memory memory,uint32_t width,WiiMm9Frame* stock,
    uint8_t* rgba,uint32_t capacity,char* error,uint32_t size) {
    if(!memory.read || !rgba || width<256 || width>WII_MM9_VIEW_MAX_WIDTH ||
       (width&1) || uint64_t(width)*240*4>capacity) {
        if(error && size)std::snprintf(error,size,"Invalid MM9 host view");return 0;
    }
    try {
        std::vector<uint8_t> prepared(size_t(width)*240*4);
        for(size_t i=3;i<prepared.size();i+=4)prepared[i]=255;
        const unsigned left=(width-256)/2;
        const auto frame=compose({memory},width,[&](WiiMm9Frame& out,unsigned x,unsigned y,uint16_t value) {
            if(x>=left && x<left+256)pixel(out,x-left,y,value);
            auto* p=prepared.data()+(size_t(y)*width+x)*4;
            const unsigned red=value>>11,green=(value>>5)&63,blue=value&31;
            p[0]=uint8_t((red<<3)|(red>>2));p[1]=uint8_t((green<<2)|(green>>4));
            p[2]=uint8_t((blue<<3)|(blue>>2));p[3]=255;
        });
        if(stock)*stock=frame;
        std::memcpy(rgba,prepared.data(),prepared.size());if(error && size)*error=0;return 1;
    }catch(const std::exception& e){if(error && size)std::snprintf(error,size,"%s",e.what());return 0;}
}
extern "C" int wii_mm9_composite_view_prepare(WiiMm9Memory memory,uint32_t width,
    uint8_t* rgba,uint32_t capacity,char* error,uint32_t size) {
    return wii_mm9_composite_present_prepare(memory,width,nullptr,rgba,capacity,error,size);
}
