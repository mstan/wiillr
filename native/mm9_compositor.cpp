// SPDX-License-Identifier: MIT
#include <wiillr/mm9_compositor.h>
#include <algorithm>
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
uint16_t half(const uint8_t* data,unsigned offset){return uint16_t(data[offset])<<8|data[offset+1];}
struct NativePalette {
    const Reader& reader;unsigned group;
    uint32_t count{};bool ready{};std::vector<uint16_t> colors;
    uint16_t get(unsigned palette,unsigned index) {
        if(!ready) {
            need(reader.u32(palettes+0x30+group*4)==0,"MM9 palette family needs qualification");
            count=reader.u32(palettes+0x20+group*4);
            // Maps select four-bit palettes; sprite records select eight-bit
            // palettes. Resolve the address once for this immutable frame.
            const unsigned entries=(count<256?count:256)*16;
            const auto* source=reader.bytes(reader.u32(palettes+0x10+group*4),entries*2);
            colors.resize(entries);
            for(unsigned n=0;n<entries;++n)colors[n]=rgb565(half(source,n*2)>>1);
            ready=true;
        }
        need(palette<count && palette<256 && index<16,"Invalid MM9 palette selection");
        return colors[palette*16+index];
    }
};
unsigned nibble(const uint8_t* data,unsigned width,unsigned x,unsigned y) {
    const unsigned at=((y/8)*(width/8)+x/8)*32+(y%8)*4+(x%8)/2;
    const auto b=data[at];return (x&1)?(b&15):(b>>4);
}
void pixel(WiiMm9Frame& out,unsigned x,unsigned y,uint16_t value) {
    const unsigned at=((y/4)*64+x/4)*32+(y%4)*8+(x%4)*2;
    out.texture[at]=uint8_t(value>>8);out.texture[at+1]=uint8_t(value);
}
struct Layer {
    unsigned width{},height{},priority{},slot{},mode{};
    uint32_t map{},tiles{},scroll{};unsigned u{},v{};
    uint32_t world_grid{},world_definitions{},world_layout{};
    unsigned world_stride{},world_width{},world_height{},world_count{};
    int world_x{},world_y{};
    const uint8_t* map_data{};const uint8_t* tile_data{};
    const uint8_t* world_grid_data{};const uint8_t* world_definition_data{};
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
        const int world_x=int32_t(r.u32(object+0x14)),world_y=int32_t(r.u32(object+0x18));
        // Modal scenes reuse the live layer maps/scroll while the gameplay
        // loader remains parked at its old camera. Those maps no longer
        // belong to that world origin. Rebind on a later matching frame;
        // never substitute the parked world's cells for the modal image.
        if((unsigned(world_x)&511)!=(layer.u&511) ||
           (unsigned(world_y)&511)!=(layer.v&511))continue;
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
        layer.world_x=world_x;layer.world_y=world_y;
        layer.world_grid=grid+4;layer.world_definitions=definitions;layer.world_layout=layout;
        layer.world_stride=stride;layer.world_width=columns*256;layer.world_height=rows*224;
        layer.world_count=(attributes-definitions)/8;
        layer.world_grid_data=r.bytes(layer.world_grid,stride*rows*14*2);
        layer.world_definition_data=r.bytes(definitions,attributes-definitions);
        return;
    }
}
struct RoomView {
    bool active{};int64_t origin{},minimum{},maximum{};
    bool visible(unsigned x,int left)const {
        if(!active || (x>=unsigned(left) && x<unsigned(left+256)))return true;
        const auto world_x=origin+int64_t(x)-left;
        return world_x>=minimum && world_x<maximum;
    }
};
RoomView room_view(const Reader& r,const std::array<Layer,4>& layers) {
    for(const auto& layer:layers) {
        if(!layer.world_grid)continue;
        // WR9E 8009e974 checks horizontal camera limits using the low nibble
        // of this padded screen map. F is unavailable; neighboring classes
        // within one are connected. Off-camera art screens can contain filler
        // even when the mutable world grid itself is completely up to date.
        const auto parent=r.u32(0x8031c1f8);need(parent,"Missing MM9 camera owner");
        need(r.u32(parent+0x10)==layer.world_layout,"MM9 camera and world owners differ");
        const auto mask=r.u32(parent+0xf4);need(mask,"Missing MM9 camera regions");
        const unsigned shift=r.u8(mask),columns=r.u8(mask+2),rows=r.u8(mask+3);
        need(shift<=7 && columns && rows && columns<=127 && rows<=127 &&
             (1u<<shift)>=columns,
             "Invalid MM9 camera regions");
        const auto* cells=r.bytes(mask+4,(1u<<shift)*rows);
        RoomView result{true,layer.world_x,layer.world_x,int64_t(layer.world_x)+256};
        const int64_t center_x=int64_t(layer.world_x)+128,center_y=int64_t(layer.world_y)+112;
        // Camera maps may omit unused art rows (Hornet does). Their domain
        // and the mutable art grid are independent, but share this owner.
        if(center_x<0 || center_y<0 || center_x>=layer.world_width || center_y>=layer.world_height ||
           center_x>=columns*256 || center_y>=rows*224)return result;
        const unsigned screen_columns=std::min(columns,layer.world_width/256);
        unsigned first=unsigned(center_x)/256,last=first;
        const auto* row=cells+((unsigned(center_y)/224)<<shift);
        const unsigned region=row[first]&15;
        if(region==15)return result;
        const auto connected=[&](unsigned x){const unsigned value=row[x]&15;
            return value!=15 && ((region+1-value)&255)<=2;};
        while(first && connected(first-1))--first;
        while(last+1<screen_columns && connected(last+1))++last;
        result.minimum=int64_t(first)*256;result.maximum=int64_t(last+1)*256;
        return result;
    }
    return {};
}
struct Sprite {uint32_t tiles{},position{};unsigned width{},height{},palette{},priority{},kind{};int x{},y{};bool flip_x{},flip_y{},limited{},margin{},anchored{};const uint8_t* tile_data{};};
template<class Put> WiiMm9Frame compose(const Reader& r,unsigned width,Put put,
    const WiiMm9MarginSprite* margins=nullptr,unsigned margin_count=0,
    const WiiMm9SpriteAnchor* anchors=nullptr,unsigned anchor_count=0) {
    WiiMm9Frame out{};std::array<Layer,4> layers{};
    NativePalette background_palette{r,0},sprite_palette{r,1};
    const int left=int(width-256)/2;
    // The original weapon screen is a fixed-size panel, not a scrolling world.
    // Keep its layout centered instead of repeating its maps into the margins.
    const auto scene=r.u16(0x803140c0);
    const bool centered_panel=scene==0x0101,stage_selection=scene==0x0202;
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
        layer.map_data=r.bytes(layer.map,(layer.width/8)*(layer.height/8)*2);
        layer.tile_data=r.bytes(layer.tiles,256*256/2);
        if(width>256 && !centered_panel && !stage_selection)world_source(r,id,layer);
    }
    const auto room=room_view(r,layers);
    std::array<bool,WII_MM9_VIEW_MAX_WIDTH> world_visible{};
    for(unsigned x=0;x<width;++x)world_visible[x]=room.visible(x,left);
    const auto begin=r.u32(sprite_pool+0x80c),end=r.u32(sprite_pool+0x808);
    need(begin>=sprite_pool && end>=begin && end<=sprite_pool+0x800 && (end-begin)%8==0,"Invalid MM9 sprite pool span");
    need(anchor_count<=128 && (!anchor_count || anchors),"Invalid MM9 sprite anchor list");
    for(unsigned n=0;n<anchor_count;++n) {
        const auto& a=anchors[n];
        need(a.begin>=sprite_pool && a.end<=sprite_pool+0x800 && a.begin<a.end &&
             !((a.begin-sprite_pool)&7) && !((a.end-sprite_pool)&7) &&
             (a.edge==-1 || a.edge==1) && (!n || a.begin>=anchors[n-1].end),"Invalid MM9 sprite anchor span");
    }
    unsigned anchor=0;
    std::vector<Sprite> sprites;const auto sprite_tiles=r.u32(resources+0x25c);
    need(margin_count<=4096 && (!margin_count || margins),"Invalid MM9 margin sprite list");
    unsigned next_margin=0;
    auto insert_margins=[&](uint32_t at) {
        while(next_margin<margin_count && margins[next_margin].before<=at) {
            const auto& input=margins[next_margin++];
            if(input.before<begin)continue;
            need(input.before==at && input.tile<65536 && input.palette<256 && input.priority<4 &&
                 !(input.flags&~7u) && input.width>=8 && input.width<=1024 && !(input.width&(input.width-1)) &&
                 input.height>=8 && input.height<=1024 && !(input.height&(input.height-1)) &&
                 input.x>=-32768 && input.x<=32767 && input.y>=-32768 && input.y<=32767,"Unbound MM9 margin sprite");
            if(input.x>=int(width)-left || input.x+int(input.width)<=-left ||
               input.y>=224 || input.y+int(input.height)<=0)continue;
            Sprite sprite{};sprite.tiles=sprite_tiles+input.tile*32;sprite.width=input.width;sprite.height=input.height;
            sprite.palette=input.palette;sprite.priority=input.priority;sprite.x=input.x;sprite.y=input.y;
            sprite.flip_x=input.flags&1;sprite.flip_y=input.flags&2;sprite.limited=input.flags&4;sprite.margin=true;
            sprite.tile_data=r.bytes(sprite.tiles,sprite.width*sprite.height/2);sprites.push_back(sprite);
        }
    };
    for(auto address=begin;address<end;address+=8) {
        insert_margins(address);
        const auto descriptor=r.u32(address),position=r.u32(address+4);
        Sprite sprite{};sprite.width=1u<<(((descriptor>>2)&7)+3);sprite.height=1u<<(((descriptor>>5)&7)+3);
        need(sprite.width<=1024 && sprite.height<=1024,"Unbounded MM9 sprite size");
        sprite.tiles=sprite_tiles+(descriptor>>16)*32;sprite.palette=(descriptor>>8)&255;sprite.priority=descriptor&3;
        sprite.kind=(position>>6)&7;need(sprite.kind<=1,"Unbound MM9 sprite draw function");
        sprite.x=signed10(position>>12);sprite.y=signed10(position>>22);sprite.flip_x=(position>>10)&1;sprite.flip_y=(position>>11)&1;sprite.limited=(position>>9)&1;
        while(anchor<anchor_count && anchors[anchor].end<=address)++anchor;
        if(!centered_panel && anchor<anchor_count && anchors[anchor].begin<=address && address<anchors[anchor].end) {
            sprite.x+=anchors[anchor].edge*left;sprite.anchored=true;
        }
        if(!sprite.kind)sprite.tile_data=r.bytes(sprite.tiles,sprite.width*sprite.height/2);
        sprites.push_back(sprite);
    }
    insert_margins(end);need(next_margin==margin_count,"MM9 margin sprite queue changed");
    // The logical active screen occupies rows 8..231 of the 256x240 copy.
    // Border/clear and exact copy sampling still require the live gate.
    const auto clear=r.u16(0x8038b2f8+0x26bc);const auto backdrop=rgb565(clear&0x7fff);const auto limit=r.u32(0x803b930c);
    for(unsigned y=0;y<224;++y) {
        for(unsigned x=0;x<width;++x)put(out,x,y+8,!world_visible[x] ||
            (centered_panel && (x<unsigned(left) || x>=unsigned(left+256)))?0:backdrop);
        unsigned count=0,margin_budget=0;
        for(int priority=3;priority>=0;--priority) {
            // The original layer order is 3,2,1,0 within each priority.
            for(int order=3;order>=0;--order) {
                const auto& layer=layers[unsigned(order)];if(!layer.width || layer.priority!=unsigned(priority))continue;
                auto u=layer.u,v=layer.v;
                if(layer.mode==1)u=r.u16(layer.scroll+y*2);else if(layer.mode==2)v=r.u16(layer.scroll+y*2);
                else if(layer.mode==3){const auto scroll=r.u32(layer.scroll+y*4);u=scroll&65535;v=scroll>>16;}
                v=(v+y)&(layer.height-1);
                for(unsigned x=centered_panel?unsigned(left):0;x<(centered_panel?unsigned(left+256):width);++x) {
                    if(!world_visible[x])continue;
                    // Stage selection is a finite canvas. Its quiet edge
                    // columns extend the background stripes, while wrapping
                    // the map would repeat portraits and menu labels.
                    const int column=stage_selection?std::clamp(int(x)-left,0,255):int(x)-left;
                    unsigned source_x=(u+unsigned(column))&(layer.width-1),source_y=v;
                    const unsigned page_x=source_x/256,page_y=v/256;
                    const unsigned tile=(page_y*(layer.width/256)+page_x)*1024+((v%256)/8)*32+(source_x%256)/8;
                    uint16_t item;
                    if(layer.world_grid && (x<unsigned(left) || x>=unsigned(left+256))) {
                        const int wx=layer.world_x+int(x)-left,wy=layer.world_y+int(y);
                        if(wx<0 || wy<0 || unsigned(wx)>=layer.world_width || unsigned(wy)>=layer.world_height)continue;
                        const auto cell=half(layer.world_grid_data,((unsigned(wy)/16)*layer.world_stride+unsigned(wx)/16)*2);
                        need(cell<layer.world_count,"Invalid MM9 world metatile");
                        item=half(layer.world_definition_data,cell*8+((unsigned(wy)/8)&1)*4+((unsigned(wx)/8)&1)*2);
                        source_x=unsigned(wx);source_y=unsigned(wy);
                    } else item=half(layer.map_data,tile*2);
                    const unsigned tx=(item>>6)&31,ty=item>>11;
                    const auto px=(item&32)?7-(source_x&7):(source_x&7),py=(item&16)?7-(source_y&7):(source_y&7);
                    const auto index=nibble(layer.tile_data,256,tx*8+px,ty*8+py);if(!index)continue;
                    put(out,x,y+8,background_palette.get(item&15,index));if(x>=unsigned(left) && x<unsigned(left+256))++out.background_pixels;
                }
            }
            for(const auto& sprite:sprites) {
                if(sprite.priority!=unsigned(priority) || int(y)<sprite.y || int(y)>=sprite.y+int(sprite.height))continue;
                if(limit && sprite.limited) {
                    if(sprite.margin){if(margin_budget>=limit)continue;++margin_budget;}
                    else {if(count>=limit){++out.limited_sprite_rows;continue;}++count;}
                }
                if(sprite.kind)continue;
                const auto sy=unsigned(int(y)-sprite.y);const auto py=sprite.flip_y?sprite.height-1-sy:sy;
                for(unsigned sx=0;sx<sprite.width;++sx) {
                    const int x=sprite.x+int(sx)+left;if(x<0 || x>=int(width))continue;
                    if((centered_panel || stage_selection) && (x<left || x>=left+256))continue;
                    if(!sprite.anchored && !world_visible[unsigned(x)])continue;
                    if(sprite.margin && x>=left && x<left+256)continue;
                    const auto px=sprite.flip_x?sprite.width-1-sx:sx;const auto index=nibble(sprite.tile_data,sprite.width,px,py);if(!index)continue;
                    put(out,unsigned(x),y+8,sprite_palette.get(sprite.palette,index));if(x>=left && x<left+256)++out.sprite_pixels;
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
    return wii_mm9_composite_present_sprites_prepare(memory,width,nullptr,0,stock,rgba,capacity,error,size);
}
extern "C" int wii_mm9_composite_present_sprites_prepare(WiiMm9Memory memory,uint32_t width,
    const WiiMm9MarginSprite* margins,uint32_t margin_count,WiiMm9Frame* stock,
    uint8_t* rgba,uint32_t capacity,char* error,uint32_t size) {
    const WiiMm9ViewLayout layout{margins,margin_count,nullptr,0,32};
    return wii_mm9_composite_layout_prepare(memory,width,&layout,stock,rgba,capacity,error,size);
}
extern "C" int wii_mm9_composite_layout_prepare(WiiMm9Memory memory,uint32_t width,
    const WiiMm9ViewLayout* layout,WiiMm9Frame* stock,
    uint8_t* rgba,uint32_t capacity,char* error,uint32_t size) {
    if(!memory.read || !layout || !rgba || width<256 || width>WII_MM9_VIEW_MAX_WIDTH ||
       (width&1) || uint64_t(width)*240*4>capacity) {
        if(error && size)std::snprintf(error,size,"Invalid MM9 host view");return 0;
    }
    try {
        std::vector<uint8_t> prepared(size_t(width)*240*4);
        for(size_t i=3;i<prepared.size();i+=4)prepared[i]=255;
        const unsigned left=(width-256)/2;
        const auto level=layout->color_level;
        const unsigned tint=level>=32?65535:((level&31)<<11)|((level&31)<<6)|(level&31);
        const unsigned tr=((tint>>11)<<3)|((tint>>11)>>2),tg=(((tint>>5)&63)<<2)|(((tint>>5)&63)>>4),tb=((tint&31)<<3)|((tint&31)>>2);
        const auto frame=compose({memory},width,[&](WiiMm9Frame& out,unsigned x,unsigned y,uint16_t value) {
            if(x>=left && x<left+256)pixel(out,x-left,y,value);
            auto* p=prepared.data()+(size_t(y)*width+x)*4;
            const unsigned red=value>>11,green=(value>>5)&63,blue=value&31;
            p[0]=uint8_t((red<<3)|(red>>2));p[1]=uint8_t((green<<2)|(green>>4));
            p[2]=uint8_t((blue<<3)|(blue>>2));p[3]=255;
            if(level<32) {
                p[0]=uint8_t((unsigned(p[0])*tr+127)/255);p[1]=uint8_t((unsigned(p[1])*tg+127)/255);p[2]=uint8_t((unsigned(p[2])*tb+127)/255);
            }
        },layout->margins,layout->margin_count,layout->anchors,layout->anchor_count);
        if(stock)*stock=frame;
        std::memcpy(rgba,prepared.data(),prepared.size());if(error && size)*error=0;return 1;
    }catch(const std::exception& e){if(error && size)std::snprintf(error,size,"%s",e.what());return 0;}
}
extern "C" int wii_mm9_sprites_prepare(WiiMm9Memory memory,uint32_t object,uint32_t camera,
    WiiMm9SpriteBatch* output,char* error,uint32_t size) {
    if(!memory.read || !output){if(error && size)std::snprintf(error,size,"Missing MM9 sprite input");return 0;}
    try {
        const Reader r{memory};WiiMm9SpriteBatch out{};
        const auto table=r.u32(object+8),entry=table+r.u8(object+0x18)*4;
        const unsigned count=r.u8(entry+2);const auto start=r.u32(sprite_pool+0x808);
        need(start>=sprite_pool && start<=sprite_pool+0x800 && (start-sprite_pool)%8==0,"Invalid MM9 sprite queue cursor");
        out.address=out.end=start;
        out.admitted=(sprite_pool+0x800-start)/8>=count;
        if((sprite_pool+0x800-start)/8>=count && count) {
            const auto parts=table+r.u16(entry),position=r.u32(object+0xc);
            const auto attributes=r.u32(object+0x10),flags=r.u32(object+0x14);
            const auto delta=[](uint32_t a,uint32_t b){const int64_t d=int32_t(a-b);return int(d>=0?d/256:-((-d+255)/256));};
            const int origin_x=delta(r.u32(position),r.u32(camera+8)),origin_y=delta(r.u32(position+4),r.u32(camera+0xc));
            const auto signed16=[](unsigned v){return int(v&65535)-int((v&32768)<<1);};
            const auto signed8=[](unsigned v){return int(v&255)-int((v&128)<<1);};
            r.bytes(parts,count*6);
            for(unsigned n=count;n;--n) {
                const auto part=parts+(n-1)*6;const unsigned shape=r.u16(part+2);
                const unsigned width=8u<<((shape>>11)&7),height=8u<<((shape>>8)&7);
                const int local_y=signed8(r.u8(part+5));
                const int y=signed16(unsigned(origin_y+((flags&0x800)?-local_y-int(height):local_y)));
                if(y< -64 || y>=224)continue;
                const int local_x=signed8(r.u8(part+4));
                const int x=signed16(unsigned(origin_x+((flags&0x400)?-local_x-int(width):local_x)));
                const unsigned tile=((attributes>>16)+r.u16(part))&65535;
                const unsigned fx=((flags>>10)^(shape>>15))&1,fy=((flags>>11)^(shape>>14))&1;
                if(x< -128 || x>=256) {
                    constexpr int margin=(WII_MM9_VIEW_MAX_WIDTH-256)/2;
                    if(x<256+margin && x+int(width)>-margin)
                        out.margins[out.margin_count++]={out.end,tile,width,height,(attributes>>8)&255,attributes&3,
                            fx|(fy<<1)|((flags>>7)&4),x,y};
                    continue;
                }
                const uint32_t descriptor=(tile<<16)|(attributes&0xff03)|((shape>>3)&0xe0)|((shape>>9)&0x1c);
                const uint32_t packed=(r.u32(out.end+4)&63)|((unsigned(y)&1023)<<22)|((unsigned(x)&1023)<<12)|
                    (fy<<11)|(fx<<10)|(flags&512);
                auto store=[&](uint32_t v){for(unsigned shift=32;shift;shift-=8)out.packed[out.size++]=uint8_t(v>>(shift-8));};
                store(descriptor);store(packed);out.end+=8;
            }
        }
        *output=out;if(error && size)*error=0;return 1;
    }catch(const std::exception& e){if(error && size)std::snprintf(error,size,"%s",e.what());return 0;}
}
extern "C" int wii_mm9_composite_view_prepare(WiiMm9Memory memory,uint32_t width,
    uint8_t* rgba,uint32_t capacity,char* error,uint32_t size) {
    return wii_mm9_composite_present_prepare(memory,width,nullptr,rgba,capacity,error,size);
}
