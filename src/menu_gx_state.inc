/* SPDX-License-Identifier: GPL-3.0-or-later */
/* CPU-visible GX state is maintained by the original compiled SDK. The native
 * Menu owns the hardware cache, so only identified cache/state setup is retired.
 * Draws, copies, tokens, TLUT uploads and unknown stores keep the strict floor. */
static const u64 sdk_hashes[16]={
    UINT64_C(0x0b7477b24bec0c02),UINT64_C(0x94f5194d8a2493ba),
    UINT64_C(0x6ffd680bfe88d4fc),UINT64_C(0x98179cc3993ff93d),
    UINT64_C(0x32ab9ca16a533117),UINT64_C(0xb8b7c5b5dfa9f2aa),
    UINT64_C(0xb54f76ee5a3e2195),UINT64_C(0x46d2bc75a4888175),
    UINT64_C(0x4c2a72241129e9e8),UINT64_C(0x1afa359268ad3f5e),
    UINT64_C(0x40e93a6a87832f85),UINT64_C(0x182198c9004fb51e),
    UINT64_C(0x8306957b23537655),UINT64_C(0x285ca3988b2bab21),
    UINT64_C(0xe74a071a712fbb84),UINT64_C(0x6bc2f3c963f783a8)};
static bool sdk_identity(CPUState* cpu) {
    if(state.sdk_verified)return true;
    for(u32 n=0;n<16;n++)if(!identity(cpu,0x81540000u+n*4096u,4096,sdk_hashes[n]))return false;
    for(u32 n=0;n<16;n++)gcn_native_code_watch(0x81540000u+n*4096u,4096);
    state.sdk_verified=true;return true;
}
static bool sdk_bp_state(u32 value) {
    const u32 reg=value>>24;
    if(reg==0x66)return value==0x66001000u || value==0x66001100u;
    if(reg==0x58)return value==0x5800000fu; /* verified SDK revision-compatibility setup */
    /* Defined cache/state registers only. In particular 0x63 starts a texture
     * preload, and 0x65 uploads a TLUT; neither is a harmless state write. */
    return reg<=0x04 || (reg>=0x06 && reg<=0x0f) || (reg>=0x10 && reg<=0x44) ||
        reg==0x46 || (reg>=0x49 && reg<=0x4b) || (reg>=0x4d && reg<=0x51) ||
        (reg>=0x53 && reg<=0x56) || reg==0x59 || (reg>=0x60 && reg<=0x62) ||
        reg==0x64 || reg==0x67 || reg==0x68 || reg==0x69 ||
        (reg>=0x80 && reg<=0x9b) || (reg>=0xa0 && reg<=0xbb) ||
        (reg>=0xc0 && reg<=0xfd) || reg==0xfe;
}
static bool sdk_setup_byte(u8 value,u32* word,u32* remaining,u8* command,u8* header) {
    if(!*command) {
        if(!value || value==0x48)return true; /* padding / vertex-cache invalidate */
        if(value!=0x08 && value!=0x10 && value!=0x61)return false;
        *command=value;*header=value==0x08?5:4;*word=0;return true;
    }
    if(*header) {
        if(*command==0x08 && *header==5) {
            if(value!=0x20 && value!=0x30 && value!=0x40 && value!=0x50 && value!=0x60 &&
               !(value>=0x70 && value<=0xbf))return false;
        }
        *word=(*word<<8)|value;
        if(--*header)return true;
        if(*command==0x10) {
            const u32 count=(*word>>16)+1,address=*word&0xffffu;
            if(count>16 || !((address<0x800 && count<=0x800-address) ||
               (address>=0x1000 && address<=0x1057 && count<=0x1058-address)))return false;
            *remaining=count*4;return true;
        }
        if(*command==0x61) {
            if(!sdk_bp_state(*word))return false;
        }
        *command=0;return true;
    }
    if(!*remaining)return false;
    if(!--*remaining)*command=0;
    return true;
}
static bool sdk_texture_store(CPUState* cpu,u64 value,unsigned width) {
    u32 base;
    switch(cpu->pc) {
    case 0x815460bc:case 0x815460d4:case 0x815460e8:case 0x815460f4:
    case 0x815460fc:case 0x81546104:case 0x81546150:return width==1 && value==0x61;
    case 0x815460c8:base=0x80;break;case 0x815460dc:base=0x84;break;
    case 0x815460f0:base=0x88;break;case 0x815460f8:base=0x8c;break;
    case 0x81546100:base=0x90;break;case 0x81546120:base=0x94;break;
    case 0x81546158:base=0x98;break;default:return false;
    }
    const u32 slot=cpu->gpr[31];
    return width==4 && slot<8 && (u32)(value>>24)==base+slot+(slot<4?0:28);
}
bool wii_menu_frame_gather(WiiMachine* machine,CPUState* cpu,u32 address,u64 value,unsigned width) {
    if(!state.owner || state.owner->cpu!=cpu || state.owner->machine!=machine ||
       state.owner->title!=UINT64_C(0x0000000100000002) || !machine->gx_native_owner ||
       strcmp(machine->gx_native_owner,"ipl43u-frame") ||
       (width!=1 && width!=2 && width!=4 && width!=8) || !state.sdk_verified)return false;
    if(!state.sdk_initializing) {
        if((address&0x1fffffffu)!=0x0c008000u || !sdk_texture_store(cpu,value,width))return false;
        ++state.sdk_texture_writes;return true;
    }
    if(word(cpu,0x800000e4)!=state.sdk_thread || state.bad)return false;
    /* Only stores in the verified SDK, during its original initializer. The
     * bounded state-only command grammar excludes every rendering/event effect. */
    if(cpu->pc<0x81540000u || cpu->pc>=0x81550000u)return false;
    const u32 instruction=word(cpu,cpu->pc),op=instruction>>26,port=address&0x1fffffffu;
    const bool scalar=port==0x0c008000u && ((op==38 && width==1) || (op==44 && width==2) ||
        ((op==36 || op==52) && width==4) || (op==54 && width==8));
    /* The real paired-single helper emits two canonical 32-bit bus stores,
     * preserving its CIA on the +4 half. Ordinary scalar stores cannot use
     * that second port, and quantized/narrow PSQ formats keep the raw floor. */
    const bool paired=op==60 && !(cpu->gqr[(instruction>>12)&7]&7) &&
        ((width==4 && (port==0x0c008000u || (!(instruction&0x8000) && port==0x0c008004u))) ||
         (width==8 && port==0x0c008000u && !(instruction&0x8000)));
    if(state.bad || !(scalar || paired))return false;
    u32 word=state.sdk_setup.word,remaining=state.sdk_setup.remaining;
    u8 command=state.sdk_setup.command,header=state.sdk_setup.header;
    for(unsigned n=0;n<width;n++)if(!sdk_setup_byte((u8)(value>>((width-n-1)*8)),&word,&remaining,&command,&header))return false;
    state.sdk_setup.word=word;state.sdk_setup.remaining=remaining;
    state.sdk_setup.command=command;state.sdk_setup.header=header;
    ++state.sdk_init_writes;return true;
}
