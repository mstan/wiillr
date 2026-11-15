// SPDX-License-Identifier: MIT
// Complete non-indirect NW4R materials become typed integer shader inputs.
// Guest command streams and persistent GX state are not reconstructed here.
void program_material(const Reader& r,uint32_t pane,const QuadInput* quad,const Counts& cap,const Counts& num,
                      uint32_t mem,uint32_t srt,uint32_t gen,uint32_t stages,uint32_t swaps,WiiMenuPicture& out) {
    need(!num.ind_stages && num.textures<=8 && num.generators<=8 && num.stages<=16,
         "Unsupported indirect or oversized native GUI program");
    auto& p=out.program;p.texture_count=num.textures;p.coordinate_count=num.generators;
    for(unsigned c=0;c<4;++c) {
        for(unsigned i=0;i<3;++i){const int32_t v=static_cast<int16_t>(r.u16(out.material+0x24+i*8+c*2));need(v>=-1024 && v<=1023,"Invalid signed GUI shader color");p.colors[i+1][c]=v;}
        for(unsigned i=0;i<4;++i)p.constants[i][c]=r.u8(out.material+0x3c+i*4+c);
    }
    uint8_t tables[4][4]={{0,1,2,3},{0,0,0,3},{1,1,1,3},{2,2,2,3}};
    if(cap.swaps)for(unsigned i=0;i<4;++i)for(unsigned c=0;c<4;++c)tables[i][c]=(r.u8(swaps+i)>>(c*2))&3;
    auto add=[&](unsigned texture,unsigned raster,std::array<uint8_t,4> color,std::array<uint8_t,4> alpha) -> GcWiiNativeGuiStage& {
        need(p.stage_count<16,"Oversized generated GUI shader");auto& z=p.stages[p.stage_count++];
        std::copy(color.begin(),color.end(),z.color.inputs);std::copy(alpha.begin(),alpha.end(),z.alpha.inputs);
        z.color.clamp=z.alpha.clamp=1;z.texture_enabled=texture<8;z.texture=z.coordinate=texture<8?uint8_t(texture):0;
        z.raster=uint8_t(raster);z.color_constant=12;z.alpha_constant=28;
        std::copy_n(tables[0],4,z.texture_swap);std::copy_n(tables[0],4,z.raster_swap);return z;
    };
    if(num.stages) {
        bool previous_color=false,previous_alpha=false;
        for(unsigned i=0;i<num.stages;++i) {
            const auto address=stages+i*16;need(r.u32(address+12)==0,"Indirect GUI shader stage needs qualification");
            const auto coord=r.u8(address),map=r.u8(address+2),flags=r.u8(address+3),raster=r.u8(address+1);
            need(map<8 || map==255,"Invalid GUI shader texture slot");
            const bool enabled=map<8 && !(flags&1);
            need(!enabled || (map<num.textures && coord<num.generators),"Unbound GUI shader texture or coordinate");
            need(raster==0 || raster==2 || raster==4 || raster==6 || raster==255,"Additional GUI raster channel needs qualification");
            auto& z=add(255,raster<=5?raster&1:7,{},{ });z.texture_enabled=enabled;
            z.texture=enabled?map:0;z.coordinate=enabled?coord:0;
            for(unsigned c=0;c<4;++c){z.raster_swap[c]=tables[(flags>>1)&3][c];z.texture_swap[c]=tables[(flags>>3)&3][c];}
            for(unsigned channel=0;channel<2;++channel) {
                auto& op=channel?z.alpha:z.color;const auto base=address+4+channel*4;
                op.inputs[0]=r.u8(base)&15;op.inputs[1]=r.u8(base)>>4;
                op.inputs[2]=r.u8(base+1)&15;op.inputs[3]=r.u8(base+1)>>4;
                const auto operation=r.u8(base+2)&15;
                need(operation<=1 || operation>=8,"Invalid GUI shader arithmetic operation");
                if(channel)need(operation<=1 || operation>=14,"Unsupported GUI alpha comparison");
                op.operation=operation&1;op.bias=operation>=8?3:((r.u8(base+2)>>4)&3);
                op.scale=operation>=8?((operation>>1)&3):(r.u8(base+2)>>6);
                op.clamp=r.u8(base+3)&1;op.output=(r.u8(base+3)>>1)&3;
                const auto k=r.u8(base+3)>>3;need(k<=7 || k>=(channel?16:12),"Reserved GUI shader constant selector");
                if(channel)z.alpha_constant=k;else z.color_constant=k;
                for(auto input:op.inputs) {
                    need(input<=(channel?7:15),"Invalid GUI shader input selector");
                    need(!(channel?input==0:input==1) || previous_alpha,"GUI shader reads an unbound previous alpha");
                    if(!channel)need(input!=0 || previous_color,"GUI shader reads an unbound previous color");
                }
            }
            if(!z.color.output)previous_color=true;if(!z.alpha.output)previous_alpha=true;
        }
    }else {
        // Automatic setup is a bounded material expression: texture mixing,
        // optional endpoint remap, then the already-resolved pane/raster tint.
        if(!num.textures)add(255,0,{15,4,10,15},{7,2,5,7});
        else {
            if(num.textures==1)add(0,7,{2,4,8,15},{1,2,4,7});
            else if(num.textures==2) {
                add(0,7,{15,15,15,8},{7,7,7,4});
                auto& z=add(1,7,{8,0,14,15},{4,0,6,7});z.color_constant=z.alpha_constant=31;
            }else {
                constexpr uint8_t weights[]{31,27,23,19,30,26,22,18};
                for(unsigned i=0;i<num.textures;++i){auto& z=add(i,7,{15,8,14,uint8_t(i?0:15)},{7,4,6,uint8_t(i?0:7)});z.color_constant=z.alpha_constant=weights[i];}
            }
            bool remap=false,tint=false;
            for(unsigned c=0;c<4;++c){remap|=p.colors[1][c]!=0 || p.colors[2][c]!=255;for(const auto& v:out.vertices)tint|=v.color[c]!=1.f;}
            if(num.textures>1 && remap)add(255,7,{2,4,0,15},{1,2,0,7});
            if(tint)add(255,0,{15,0,10,15},{7,0,5,7});
        }
    }
    for(unsigned t=0;t<num.textures;++t)p.textures[t]=texture(r,mem+t*32);
    const auto count=quad?quad->coordinate_count:r.u8(pane+0xe5);
    need(count<=8 && (!num.generators || count),"Missing GUI program coordinates");
    const auto coords=quad?0:r.u32(pane+0xe8);need(quad || !count || coords,"Missing GUI coordinate storage");
    constexpr unsigned order[]{0,1,3,2};out.program_extra_count=count?uint8_t(count-1):0;
    for(unsigned c=0;c<count;++c)for(unsigned v=0;v<4;++v)for(unsigned a=0;a<2;++a) {
        const auto value=quad?quad->coordinates[c][v][a]:r.f32(coords+c*32+order[v]*8+a*4);
        if(c)out.program_extra_uv[v][c-1][a]=value;else out.vertices[v].uv[a]=value;
    }
    for(unsigned t=0;t<num.generators;++t) {
        const auto source=r.u8(gen+t*4+1);need(r.u8(gen+t*4)==1 && source>=4 && source-4<count,"Unsupported GUI coordinate generator");
        p.coordinate_source[t]=source-4;auto& m=p.uv[t];m[0][0]=m[1][1]=1;
        const auto matrix=r.u8(gen+t*4+2);if(matrix==60)continue;
        need(matrix>=30 && matrix<=57 && (matrix-30)%3==0,"Invalid GUI texture matrix");const auto slot=(matrix-30)/3;
        need(slot<num.srts,"Missing GUI texture transform");const auto transform=srt+slot*20;
        const auto angle=std::remainder(r.f32(transform+8),360.f);float cs=1,sn=0;
        if(angle==90)cs=0,sn=1;else if(angle==-90)cs=0,sn=-1;else if(angle==180 || angle==-180)cs=-1;
        else need(angle==0,"GUI non-quarter-turn rotation needs qualification");
        const auto x=r.f32(transform+12),y=r.f32(transform+16);m[0][0]=cs*x;m[0][1]=-sn*y;m[1][0]=sn*x;m[1][1]=cs*y;
        m[0][2]=r.f32(transform)+.5f-.5f*(m[0][0]+m[0][1]);m[1][2]=r.f32(transform+4)+.5f-.5f*(m[1][0]+m[1][1]);
    }
    out.needs_program=1;out.needs_gui=0;out.draw.material.texture=num.textures?p.textures[0]:GcWiiNativeTexture{};p.draw=out.draw;
}
