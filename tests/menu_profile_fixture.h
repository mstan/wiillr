/* SPDX-License-Identifier: MIT */
#pragma once
#include <string>
/* Synthetic SCv0 with independent expected offsets: AR@21, CB@29, plus an
 * unknown array and nonzero unused bytes to detect accidental replacement. */
static std::string menu_profile_fixture(){
    std::string s(16384,'Q');s.replace(0,4,"SCv0");s.replace(16380,4,"SCed");
    s[4]=0;s[5]=3;const unsigned offsets[]={14,22,33,49};
    for(unsigned i=0;i<4;i++){s[6+2*i]=char(offsets[i]>>8);s[7+2*i]=char(offsets[i]);}
    s[14]=0x65;s.replace(15,6,"IPL.AR");s[21]=0;
    s[22]=char(0xa5);s.replace(23,6,"IPL.CB");s.replace(29,4,4,char(0xff));
    s[33]=0x27;s.replace(34,8,"APP.TEST");s[42]=0;s[43]=4;s.replace(44,5,"array");return s;
}
