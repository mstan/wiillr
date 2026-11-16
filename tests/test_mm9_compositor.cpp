// SPDX-License-Identifier: MIT
#include <wiillr/mm9_compositor.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
std::vector<uint8_t> ram(24*1024*1024);
uint64_t memory_reads;
constexpr uint32_t config=0x80001000,graphics=0x80600000,sprite_graphics=0x80700000,pool=0x8037c428,palettes=0x803bb320;
void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
const uint8_t* read(void*,uint32_t address,uint32_t size){++memory_reads;address&=0x1fffffff;return address<=ram.size() && size<=ram.size()-address?ram.data()+address:nullptr;}
void p16(uint32_t a,uint16_t v){a&=0x1fffffff;ram.at(a)=v>>8;ram.at(a+1)=v&255;}
void p32(uint32_t a,uint32_t v){p16(a,v>>16);p16(a+2,v&65535);}
void fill(uint32_t a,unsigned n,uint8_t value){a&=0x1fffffff;std::fill_n(ram.begin()+a,n,value);}
uint16_t pixel(const WiiMm9Frame& f,unsigned x,unsigned y){const auto at=((y/4)*64+x/4)*32+(y%4)*8+(x%4)*2;return uint16_t(f.texture[at])<<8|f.texture[at+1];}
void sprite(unsigned id,unsigned priority,unsigned x,unsigned y,unsigned tile,bool limited=false,unsigned kind=0,unsigned flip=0) {
    p32(pool+id*8,(tile<<16)|priority);p32(pool+id*8+4,(y<<22)|(x<<12)|(flip<<10)|(unsigned(limited)<<9)|(kind<<6));
    p32(pool+0x808,pool+(id+1)*8);
}
void fixture() {
    std::fill(ram.begin(),ram.end(),0);p32(0x80301280+0x40,config);p32(0x80301280,1u<<24);p32(config,0x100);
    p16(0x803140c0,0x000a); // Original gameplay scene, with scrolling margins.
    p32(0x8043ca80+0x258,graphics);p32(0x8043ca80+0x25c,sprite_graphics);
    constexpr unsigned ids[]{1,0x12,0x24,0x38};for(unsigned n=0;n<4;++n)p32(0x80281e48+n*4,ids[n]);
    p16(config+8,1);fill(graphics+0x10000,32,0x11);
    for(unsigned group=0;group<2;++group){p32(palettes+0x10+group*4,0x80500000+group*0x1000);p32(palettes+0x20+group*4,16);}
    p16(0x80500002,0xf800);p16(0x80500006,0x003e); // red and blue in RGB5551
    p16(0x80501004,0x07c0);p16(0x80501006,0x003e); // green and blue sprite palette
    p32(pool+0x80c,pool);p32(pool+0x808,pool);
}
void sprite_emitter_contract() {
    fixture();const WiiMm9Memory memory{nullptr,read};char error[192];
    constexpr uint32_t object=0x80506000,position=0x80506100,camera=0x80506200,table=0x80506300;
    p32(object+8,table);p32(object+0xc,position);p32(object+0x10,0x00050302);p32(object+0x14,512);
    p32(position,250*256);p32(position+4,10*256);
    p16(table,16);ram[(table+2)&0x1fffffff]=2;
    p16(table+16,2);p16(table+18,0x0100);ram[(table+20)&0x1fffffff]=8;
    p16(table+22,1);p16(table+24,0x0800);
    p32(pool+4,0x2b);WiiMm9SpriteBatch batch{};
    require(wii_mm9_sprites_prepare(memory,object,camera,&batch,error,sizeof error),error);
    require(batch.admitted && batch.size==8 && batch.end==pool+8 && batch.margin_count==1,
            "Native emitter preserves original crop and exposes uncropped components");
    const uint8_t expected[]{0,6,3,6,2,0x8f,0xa2,0x2b};
    require(!std::memcmp(batch.packed,expected,sizeof expected),"Original packed dimensions, palette, priority, coordinates and reserved bits");
    const auto& margin=batch.margins[0];
    require(margin.before==pool+8 && margin.x==258 && margin.y==10 && margin.width==8 && margin.height==16 &&
            margin.tile==7 && margin.palette==3 && margin.priority==2 && margin.flags==4,"Margin packet has full coordinates and original insertion order");
    require(ram[pool&0x1fffffff]==0 && read(nullptr,pool+4,4)[3]==0x2b,"Sprite preparation does not write guest RAM");
    p32(object+0x14,0xe00);
    require(wii_mm9_sprites_prepare(memory,object,camera,&batch,error,sizeof error),error);
    require(batch.size==16 && !batch.margin_count && batch.packed[4]==0 && batch.packed[5]==0x8e &&
            batch.packed[6]==0xae && batch.packed[7]==0x2b,"Object flips reflect component bounds and preserve packed flags");
    p32(pool+0x808,pool+0x7f8);
    require(wii_mm9_sprites_prepare(memory,object,camera,&batch,error,sizeof error),error);
    require(!batch.admitted && !batch.size && !batch.margin_count,"Original whole-animation queue capacity check retained");
    const auto unchanged=batch;p32(pool+0x808,pool+1);
    require(!wii_mm9_sprites_prepare(memory,object,camera,&batch,error,sizeof error) &&
            !std::memcmp(&batch,&unchanged,sizeof batch),"Invalid sprite cursor publishes no partial batch");

    fixture();fill(sprite_graphics,32,0x22);fill(sprite_graphics+32,32,0x33);
    fill(sprite_graphics+64,32,0x11);p16(0x80501002,0xf800);
    sprite(0,0,254,3,0);sprite(1,0,254,3,1);
    WiiMm9Frame stock{},combined{};std::vector<uint8_t> wide(344*240*4);
    require(wii_mm9_composite_prepare(memory,&stock,error,sizeof error),error);
    WiiMm9MarginSprite extra{pool+8,2,8,8,0,0,0,258,3};
    auto render=[&](){require(wii_mm9_composite_present_sprites_prepare(memory,344,&extra,1,&combined,wide.data(),unsigned(wide.size()),error,sizeof error),error);
        require(!std::memcmp(&stock,&combined,sizeof stock),"Margin packets cannot alter original stock pixels or counters");};
    render();const auto pixel_at=[&](){const auto* p=wide.data()+((3+8)*344+44+258)*4;return uint16_t((p[0]>>3)<<11|(p[1]>>2)<<5|(p[2]>>3));};
    require(pixel_at()==0x001f,"Margin sprite retains order before the following guest sprite");
    extra.before=pool+16;render();require(pixel_at()==0xf800,"Margin sprite retains order after preceding guest sprites");
}
}
void camera_room_contract() {
    fixture();const WiiMm9Memory memory{nullptr,read};char error[192];
    constexpr uint32_t object=0x8031bf90,grid=0x8031c814,definitions=0x80504000,
        layout=0x80505000,parent=0x80508000,mask=0x80509000;
    p16(0x803140c0,0x000a);p16(config+8,0x4001);p16(config+0x12,224);
    p32(object+4,0x8009f1a8);p32(object+0x5c,1);p32(object+0x18,224);
    p32(object+0x20,definitions);p32(object+0x28,layout);
    p32(0x8031c1e4,definitions);p32(0x8031c1e0,definitions+16);
    p32(layout,0x04000202);p16(grid,32);
    for(unsigned n=0;n<32*28;++n)p16(grid+4+n*2,1);
    p32(0x8031c1f8,parent);p32(parent+0x10,layout);p32(parent+0xf4,mask);p32(mask,0x02030202);
    fill(sprite_graphics,32,0x22);sprite(0,0,16,10,0);sprite(1,0,260,10,0);
    const WiiMm9SpriteAnchor anchor{pool,pool+8,-1};
    const WiiMm9ViewLayout view_layout{nullptr,0,&anchor,1,32};
    unsigned width=344;std::vector<uint8_t> view(width*240*4);WiiMm9Frame stock{},before{};
    auto prepare=[&](){return wii_mm9_composite_layout_prepare(memory,width,&view_layout,&stock,view.data(),unsigned(view.size()),error,sizeof error);};
    auto color=[&](unsigned x,unsigned y){const auto* p=view.data()+(y*width+x)*4;return uint32_t(p[0])<<16|uint32_t(p[1])<<8|p[2];};
    require(prepare(),error);before=stock;
    require(color(308,18)==0x00ff00 && color(330,8)==0xff0000,"Connected camera region exposes real neighboring tiles and actors");
    // Rows are 224 pixels high and physically padded to four screen entries.
    // The first row remains valid, so either wrong row/stride leaks filler.
    ram[(mask+4+4+1)&0x1fffffff]=15;
    require(prepare(),error);
    require(!std::memcmp(&stock,&before,sizeof stock),"Room clipping preserves the complete original center and counters");
    require(color(308,18)==0 && color(330,8)==0,"Unavailable camera screens expose neither filler tiles nor world sprites");
    require(view[(8*width+330)*4+3]==0 && view[330*4+3]==0 &&
            view[(18*width+16)*4+3]==255 && view[(8*width+44)*4+3]==255,
            "Room coverage excludes full-height pillarboxes while retaining the original center and anchored HUD");
    require(color(16,18)==0x00ff00,"Anchored HUD remains visible over a finite room's margins");
    ram[(mask+4+4+1)&0x1fffffff]=1;require(prepare(),error);
    require(color(308,18)==0x00ff00,"Original adjacent camera classes remain connected");
    ram[(mask+4+4+1)&0x1fffffff]=2;require(prepare(),error);
    require(color(308,18)==0,"Nonadjacent camera classes retain the original room boundary");
    for(unsigned offset=0;offset<=44;++offset) {
        p32(object+0x14,offset);p16(config+0x10,offset);require(prepare(),error);
        for(unsigned x=0;x<44;++x)
            require(color(x,8)==(x+offset>=44?0xff0000u:0u),
                    "Finite room edges follow each published camera pixel without repeating filler");
    }
    p32(object+0x14,0);p16(config+0x10,0);
    p32(object+0x18,0);p16(config+0x12,0);require(prepare(),error);
    require(color(330,8)==0xff0000,"Entering a connected camera row restores its real margin content");
    p32(object+0x18,224);p16(config+0x12,224);require(prepare(),error);
    require(color(330,8)==0,"Entering a finite camera row restores black margins immediately");
    width=864;view.resize(width*240*4);p32(layout,0x04000402);p16(grid,64);
    for(unsigned n=0;n<64*28;++n)p16(grid+4+n*2,1);
    p32(mask,0x02030402);ram[(mask+4+4+1)&0x1fffffff]=15;
    require(prepare(),error);
    require(color(824,8)==0,"A wide view cannot reveal a disconnected region beyond an unavailable screen");
    ram[(mask+2)&0x1fffffff]=3;ram[(mask+4+4+1)&0x1fffffff]=0;
    require(prepare() && color(824,8)==0xff0000,"Camera and art maps may have different valid dimensions");
    ram[(mask+3)&0x1fffffff]=1;
    require(prepare() && color(824,8)==0,"An unused art row cannot read beyond the camera map");
    const auto valid=view;before=stock;ram[mask&0x1fffffff]=0;
    require(!prepare() && view==valid && !std::memcmp(&stock,&before,sizeof stock),"Invalid camera metadata publishes no partial frame");
    ram[mask&0x1fffffff]=2;p32(parent+0x10,layout+4);
    require(!prepare() && view==valid && !std::memcmp(&stock,&before,sizeof stock),"A parked camera cannot bind another world's layout");
    require(wii_mm9_composite_prepare(memory,&stock,error,sizeof error),"Original layout does not require enhancement camera metadata");
}
void activation_room_contract() {
    fixture();const WiiMm9Memory memory{nullptr,read};char error[192];
    constexpr uint32_t parent=0x80508000,layout=0x80505000,mask=0x80509000;
    p32(0x8031c1f8,parent);p32(parent+0x10,layout);p32(parent+0xf4,mask);
    p32(layout,0x0000120a);p32(mask,0x0500120a);fill(mask+4,32*10,15);
    // Special Stage's actual failure geometry: city Cactus3032, camera3200,
    // region2 at screen12, disconnected region0 at screen11 and screen13.
    const uint32_t row=mask+4+3*32;
    ram[(row+11)&0x1fffffff]=0;ram[(row+12)&0x1fffffff]=2;ram[(row+13)&0x1fffffff]=0;
    WiiMm9ActivationBounds bounds{};
    auto prepare=[&](int32_t x,int32_t y,unsigned width){
        return wii_mm9_activation_bounds(memory,x,y,width,&bounds,error,sizeof error);};
    for(unsigned width:{256u,342u,448u,864u}) {
        require(prepare(3200,784,width),error);
        require(bounds.minimum==3056 && bounds.maximum==3344,
                "Disconnected boss-room margins cannot admit the prior city's Cactus");
    }
    ram[(row+11)&0x1fffffff]=1;ram[(row+13)&0x1fffffff]=3;
    require(prepare(3200,784,342) && bounds.minimum==3013 && bounds.maximum==3387,
            "Connected room margins retain full enhanced actor activation");
    ram[(row+11)&0x1fffffff]=0;
    require(prepare(3200,784,342) && bounds.minimum==3056 && bounds.maximum==3387,
            "Finite room clipping is independent on the two camera sides");
    require(prepare(3072,784,342) && bounds.minimum==2928 && bounds.maximum==3259,
            "Room-aware enhancement never removes an original admission");
    p32(0x8031c1f8,0);memory_reads=0;
    require(prepare(3200,784,256) && bounds.minimum==3056 && bounds.maximum==3344 && !memory_reads,
            "Original activation does not read enhancement room metadata");
    const auto before=bounds;
    require(!prepare(3200,784,342) && !std::memcmp(&bounds,&before,sizeof bounds),
            "Missing room metadata cannot publish partial enhanced bounds");
    require(!prepare(3200,784,343),"Odd activation widths are rejected");
    require(!prepare(INT32_MAX,784,256),"Activation coordinate overflow is rejected");
}
void finite_canvas_contract() {
  for(unsigned scene:{0x0000u,0x0202u}) {
    fixture();const WiiMm9Memory memory{nullptr,read};char error[192];
    p16(0x803140c0,uint16_t(scene));fill(graphics+0x10000+32,32,0x33);
    for(unsigned row=0;row<32;++row)p16(graphics+(row*32+31)*2,64);
    fill(sprite_graphics,32,0x22);sprite(0,0,16,10,0);sprite(1,0,260,10,0);
    WiiMm9Frame original{},stock{};require(wii_mm9_composite_prepare(memory,&original,error,sizeof error),error);
    const WiiMm9MarginSprite extra{pool+16,0,8,8,0,0,0,-8,30};
    const WiiMm9ViewLayout layout{&extra,1,nullptr,0,32};
    for(unsigned width:{342u,344u,448u,864u}) {
        std::vector<uint8_t> view(width*240*4);const unsigned left=(width-256)/2;
        require(wii_mm9_composite_layout_prepare(memory,width,&layout,&stock,view.data(),unsigned(view.size()),error,sizeof error),error);
        require(!std::memcmp(&stock,&original,sizeof stock),"Finite scenes retain every original center pixel and counter");
        for(unsigned y=8;y<232;++y)for(unsigned x=0;x<width;++x) {
            if(x>=left && x<left+256)continue;
            const auto* p=view.data()+(y*width+x)*4;
            const uint32_t color=uint32_t(p[0])<<16|uint32_t(p[1])<<8|p[2];
            require(color==(x<left?0xff0000u:0x0000ffu),
                    "Finite intro and menu backgrounds extend edge columns without repeating maps or sprites");
        }
    }
  }
}
void hud_layout_contract() {
    fixture();const WiiMm9Memory memory{nullptr,read};char error[192];
    fill(sprite_graphics,32,0x22);
    sprite(0,0,16,10,0);sprite(1,0,224,10,0);sprite(2,0,100,10,0);
    std::array<uint8_t,24> queue{};std::copy_n(ram.data()+(pool&0x1fffffff),24,queue.data());
    WiiMm9Frame original{},stock{};require(wii_mm9_composite_prepare(memory,&original,error,sizeof error),error);
    const WiiMm9SpriteAnchor anchors[]={{pool,pool+8,-1},{pool+8,pool+16,1}};
    WiiMm9ViewLayout layout{nullptr,0,anchors,2,32};
    std::vector<uint8_t> view(344*240*4);
    auto prepare=[&](unsigned width){return wii_mm9_composite_layout_prepare(memory,width,&layout,&stock,view.data(),unsigned(view.size()),error,sizeof error);};
    require(prepare(256),error);
    require(!std::memcmp(&original,&stock,sizeof stock),"Original layout retains every pixel and counter with tagged HUD sprites");
    require(prepare(344),error);
    auto color=[&](unsigned x,unsigned y){const auto* p=view.data()+(y*344+x)*4;return uint32_t(p[0])<<16|uint32_t(p[1])<<8|p[2];};
    require(color(16,18)==0x00ff00 && color(312,18)==0x00ff00,"HUD bars retain their original left and right edge insets");
    require(color(60,18)==0xff0000 && color(268,18)==0xff0000 && color(144,18)==0x00ff00,"Anchoring removes old HUD images and preserves world sprite placement");
    require(pixel(stock,16,18)==0xf800 && pixel(stock,100,18)==0x07e0,"Physical center is the enhanced layout crop, without duplicate HUD");
    require(!std::memcmp(queue.data(),ram.data()+(pool&0x1fffffff),24),"Host anchoring never rewrites the original packed sprite queue");
    const auto untinted=stock;layout.color_level=16;
    require(prepare(344),error);
    require(color(16,18)==0x008200 && color(60,18)==0x840000,"Wide HUD and world use the original screen tint including RGB565 green precision");
    require(!std::memcmp(&stock,&untinted,sizeof stock),"Screen tint is not applied twice to the physical stock texture");
    layout.color_level=0;require(prepare(344),error);require(color(16,18)==0 && color(60,18)==0,"Full screen fade also hides anchored HUD and margins");
    const auto before=view;WiiMm9SpriteAnchor invalid{pool+4,pool+8,-1};layout.anchors=&invalid;layout.anchor_count=1;
    require(!prepare(344) && view==before && !std::memcmp(&stock,&untinted,sizeof stock),"Invalid anchor spans publish neither a partial view nor stock texture");
}
void background_span_contract() {
    fixture();const WiiMm9Memory memory{nullptr,read};char error[192];
    constexpr uint32_t scroll=0x80506000;
    p16(config+8,0x4001); // 512-column map, alternating tile flips.
    fill(0x8039b220,1,1);p32(0x8039b224,scroll);
    constexpr unsigned offsets[]{7,12,255,510};
    for(unsigned y=0;y<224;++y)p16(scroll+y*2,offsets[y%4]);
    for(unsigned row=0;row<32;++row)for(unsigned column=0;column<64;++column)
        p16(graphics+((column/32)*1024+row*32+column%32)*2,
            ((column&1)?32:0)|((column&2)?16:0));
    for(unsigned y=0;y<8;++y)for(unsigned pair=0;pair<4;++pair) {
        const unsigned a=(y&1)?8-pair*2:1+pair*2,b=(y&1)?7-pair*2:2+pair*2;
        fill(graphics+0x10000+y*4+pair,1,uint8_t(a*16+b));
    }
    for(unsigned n=1;n<=8;++n)p16(0x80500000+n*2,n*2);
    WiiMm9Frame stock{};std::vector<uint8_t> wide(344*240*4);
    const auto prepare=[&] {return wii_mm9_composite_present_prepare(memory,344,&stock,
        wide.data(),unsigned(wide.size()),error,sizeof error);};
    require(prepare(),error);
    for(unsigned y=0;y<4;++y)for(unsigned x=0;x<344;++x) {
        const unsigned source=(offsets[y]+x-44)&511,tile=source/8;
        const unsigned px=(tile&1)?7-(source&7):(source&7),py=(tile&2)?7-y:y;
        const unsigned expected=(py&1)?8-px:1+px;
        const auto* color=wide.data()+((y+8)*344+x)*4;
        require(!color[0] && !color[1] && (color[2]>>3)==expected,
            "Per-line scroll preserves unaligned tile/page boundaries and both flips across wide margins");
        if(x>=44 && x<300)require(pixel(stock,x-44,y+8)==expected,
            "Batched background rows preserve the original tiled center");
    }
    p32(palettes+0x20,0);fill(graphics+0x10000,32,0);
    require(prepare() && stock.background_pixels==0,
        "Transparent tile rows do not require an unused palette");
    const auto before=wide,original=std::vector<uint8_t>(stock.texture,stock.texture+sizeof stock.texture);
    fill(graphics+0x10000,1,0x10);
    require(!prepare() && wide==before && !std::memcmp(stock.texture,original.data(),original.size()),
        "Invalid visible palettes cannot publish a partial batched row");
}
int main() {
    try {
        fixture();const WiiMm9Memory memory{nullptr,read};WiiMm9Frame frame{};char error[192];
        memory_reads=0;
        require(wii_mm9_composite_prepare(memory,&frame,error,sizeof(error)),error);
        require(memory_reads<200,"Pixel rasterization must use prepared native resources, not per-pixel guest address resolution");
        require(pixel(frame,0,8)==0xf800 && pixel(frame,255,231)==0xf800 && pixel(frame,0,0)==0,"Logical background and RGB565 tiled output");
        require(frame.background_pixels==256*224 && frame.sprite_pixels==0,"Logical background coverage");
        p16(0x80500002,0x07c0);
        require(wii_mm9_composite_prepare(memory,&frame,error,sizeof error),error);
        require(pixel(frame,0,8)==0x07e0,"Native palette preparation observes next-frame color changes");
        p16(0x80500002,0xf800);
        fill(sprite_graphics,32,0x22);sprite(0,0,4,3,0);
        require(wii_mm9_composite_prepare(memory,&frame,error,sizeof(error)),error);
        require(pixel(frame,4,11)==0x07e0 && pixel(frame,3,11)==0xf800 && pixel(frame,12,11)==0xf800,"Sprite placement, bounds and RGB555 green expansion");
        fill(sprite_graphics+32,32,0x33);sprite(1,0,4,3,1);
        require(wii_mm9_composite_prepare(memory,&frame,error,sizeof(error)),error);
        require(pixel(frame,4,11)==0x001f,"Later sprite order within one priority");
        ram[(sprite_graphics+32)&0x1fffffff]=0x03;
        require(wii_mm9_composite_prepare(memory,&frame,error,sizeof(error)),error);
        require(pixel(frame,4,11)==0x07e0 && pixel(frame,5,11)==0x001f,"Transparent palette zero preserves prior sprite");
        sprite(1,2,4,3,1);require(wii_mm9_composite_prepare(memory,&frame,error,sizeof(error)),error);
        require(pixel(frame,5,11)==0x07e0,"Priority crosses background and sprite families");
        p32(pool+0x808,pool+8);fill(sprite_graphics,32,0x33);ram[sprite_graphics&0x1fffffff]=0x23;
        sprite(0,0,4,3,0,false,0,1);require(wii_mm9_composite_prepare(memory,&frame,error,sizeof(error)),error);
        require(pixel(frame,4,11)==0x001f && pixel(frame,11,11)==0x07e0,"Horizontal sprite mirror uses logical texture axes");
        sprite(0,0,4,3,0,false,0,2);require(wii_mm9_composite_prepare(memory,&frame,error,sizeof(error)),error);
        require(pixel(frame,4,11)==0x001f && pixel(frame,4,18)==0x07e0,"Vertical sprite mirror uses logical texture axes");
        fill(sprite_graphics,32,0x22);
        p32(0x803b930c,1);sprite(0,0,4,223,0,true);sprite(1,0,4,223,1,true);
        require(wii_mm9_composite_prepare(memory,&frame,error,sizeof(error)),error);
        require(pixel(frame,5,231)==0x07e0 && frame.limited_sprite_rows==1 && frame.final_sprite_count==1,"Per-line sprite limit and published final counter");
        // No-op entries still consume the original optional line budget.
        sprite(0,0,4,223,0,true,1);require(wii_mm9_composite_prepare(memory,&frame,error,sizeof(error)),error);
        require(pixel(frame,5,231)==0xf800 && frame.final_sprite_count==1,"No-op sprite still participates in line limit");
        p32(pool+0x808,pool);p32(0x803b930c,0);
        p16(graphics+2,0x0040);fill(graphics+0x10000+32,32,0x33);
        ram[0x39b220]=1;p32(0x8039b190+0x94,0x80502000);for(unsigned y=0;y<224;++y)p16(0x80502000+y*2,8);
        require(wii_mm9_composite_prepare(memory,&frame,error,sizeof(error)),error);
        require(pixel(frame,0,8)==0x001f && pixel(frame,8,8)==0xf800,"Live per-line horizontal scroll and logical tile selection");
        const auto original=frame;
        // Actual neighboring map pages and sprites outside the old crop.
        fixture();p16(config+8,0x4001);
        p16(graphics+2048,0x0040);fill(graphics+0x10000+32,32,0x33);
        fill(sprite_graphics,32,0x22);sprite(0,0,260,10,0);
        WiiMm9Frame stock{};require(wii_mm9_composite_prepare(memory,&stock,error,sizeof error),error);
        std::vector<uint8_t> wide(344*240*4);
        require(wii_mm9_composite_view_prepare(memory,344,wide.data(),unsigned(wide.size()),error,sizeof error),error);
        WiiMm9Frame combined{};
        require(wii_mm9_composite_present_prepare(memory,344,&combined,wide.data(),unsigned(wide.size()),error,sizeof error),error);
        require(!std::memcmp(&stock,&combined,sizeof stock),"Combined traversal must preserve the complete original output and counters");
        const auto view_pixel=[&](unsigned x,unsigned y){const auto* p=wide.data()+(y*344+x)*4;return uint16_t((p[0]>>3)<<11|(p[1]>>2)<<5|(p[2]>>3));};
        for(unsigned y=0;y<240;++y)for(unsigned x=0;x<256;++x)
            require(view_pixel(x+44,y)==pixel(stock,x,y),"Wide view must preserve every original center pixel");
        require(view_pixel(44+256,8)==0x001f,"Right margin must read the next map page");
        require(view_pixel(44+260,18)==0x07e0,"Offscreen sprite must be visible in the new margin");
        // The standard world loader's visible cache can be valid while its
        // other half still contains old columns. Margins use mutable world
        // metatiles, with level bounds, without changing the original center.
        constexpr uint32_t scene=0x8031bf90,grid=0x8031c814,definitions=0x80504000,layout=0x80505000;
        p32(scene+4,0x8009f1a8);p32(scene+0x5c,1);
        p32(scene+0x20,definitions);p32(scene+0x28,layout);
        p32(scene+0x14,16); // Published scroll can differ from the target at +34.
        p32(0x8031c1e4,definitions);p32(0x8031c1e0,definitions+16);
        p32(layout,0x02000201);p16(grid,32);
        p32(0x8031c1f8,0x80508000);p32(0x80508010,layout);p32(0x805080f4,0x80509000);p32(0x80509000,0x02030201);
        for(unsigned n=0;n<32*14;++n)p16(grid+4+n*2,1);
        p16(definitions+8,0);p16(definitions+10,0x0040);
        p16(definitions+12,0x0080);p16(definitions+14,0x00c0);
        p16(0x80500004,0x07c0);fill(graphics+0x10000+64,32,0x22);
        p16(config+0x10,16);
        require(wii_mm9_composite_prepare(memory,&stock,error,sizeof error),error);
        require(wii_mm9_composite_present_prepare(memory,344,&combined,wide.data(),unsigned(wide.size()),error,sizeof error),error);
        require(!std::memcmp(&stock,&combined,sizeof stock),"Direct world margins keep stock pixels/counters exact");
        require(view_pixel(44-8,8)==0x001f && view_pixel(44+280,8)==0x001f,"Uncached margins come from real world cells");
        const auto gameplay_view=wide;
        p16(config+0x10,0);p32(scene+4,0);
        require(wii_mm9_composite_present_prepare(memory,344,&combined,wide.data(),unsigned(wide.size()),error,sizeof error),error);
        const auto modal_view=wide;const auto modal_stock=combined;
        p32(scene+4,0x8009f1a8);
        require(wii_mm9_composite_present_prepare(memory,344,&combined,wide.data(),unsigned(wide.size()),error,sizeof error),error);
        require(wide==modal_view && !std::memcmp(&combined,&modal_stock,sizeof combined),"Modal layer reuse cannot bind a parked world's camera or cells");
        p16(0x803140c0,0x0101);
        require(wii_mm9_composite_present_prepare(memory,344,&combined,wide.data(),unsigned(wide.size()),error,sizeof error),error);
        require(!std::memcmp(&combined,&modal_stock,sizeof combined),"Weapon panel preserves its original stock layout and counters");
        for(unsigned y=0;y<240;++y)for(unsigned x=0;x<344;++x)
            if(x<44 || x>=300)require(view_pixel(x,y)==0,"Weapon panel cannot repeat maps or sprites into host margins");
        p16(0x803140c0,0x000a);
        p16(config+0x10,16);
        require(wii_mm9_composite_present_prepare(memory,344,&combined,wide.data(),unsigned(wide.size()),error,sizeof error) && wide==gameplay_view,"World source rebinds when gameplay restores its published origin");
        require(view_pixel(44-16,8)==0xf800 && view_pixel(44-16,16)==0x07e0 &&
                view_pixel(44-8,16)==0,"World metatile quadrants preserve order and transparency");
        require(view_pixel(0,8)==0,"Negative world coordinates cannot wrap to the opposite side");
        p16(grid+4+(296/16)*2,0);
        require(wii_mm9_composite_present_prepare(memory,344,&combined,wide.data(),unsigned(wide.size()),error,sizeof error),error);
        require(view_pixel(44+280,8)==0xf800,"Mutable world cells update without refreshing the rolling cache");
        p32(scene+0x14,256);p16(config+0x10,256);
        require(wii_mm9_composite_present_prepare(memory,344,&combined,wide.data(),unsigned(wide.size()),error,sizeof error),error);
        require(view_pixel(44+280,8)==0,"World right edge cannot repeat cached foreground");
        const auto valid_world=wide;p16(grid,31);
        require(!wii_mm9_composite_present_prepare(memory,344,&combined,wide.data(),unsigned(wide.size()),error,sizeof error) && wide==valid_world,"Invalid world dimensions publish no partial view");
        p32(scene+4,0);
        const auto before=wide;
        require(!wii_mm9_composite_view_prepare(memory,345,wide.data(),unsigned(wide.size()),error,sizeof error) && wide==before,"Invalid view must not partially publish");
        frame=original;p32(pool+0x808,pool+8);sprite(0,0,4,3,0,false,2);
        require(!wii_mm9_composite_prepare(memory,&frame,error,sizeof(error)) && std::memcmp(&frame,&original,sizeof(frame))==0,"Unsupported sprite family must publish no partial texture");
        sprite_emitter_contract();
        camera_room_contract();
        activation_room_contract();
        finite_canvas_contract();
        hud_layout_contract();
        background_span_contract();
        std::cout<<"MM9 logical tile/sprite compositor contracts passed; live copy/queue qualification pending\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
