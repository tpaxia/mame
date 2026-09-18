#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders: Salvatore Paxia
"""CPU19M register/memory extension semantics, independent of boot firmware.

Source: 801.30.1 (03), detailed tables pp.3.09-3.10. This suite does not
claim coverage of the I/O extensions or interrupt masking.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TEST = r'''
#include "cpu/puce/puce_state.h"
#include <cassert>
#include <iostream>
int main()
{
    unsigned long count=0;
    // All unary values, all destinations and both incoming carries. Other
    // registers and every preserved flag must survive each instruction.
    for (unsigned r=0;r<16;++r)
    for (unsigned v=0;v<65536;++v)
    for (unsigned carry=0;carry<2;++carry)
    {
        const unsigned codes[]={0xfe0f,0xb50f,0xe30f,0x800f,0x810f,0x840f,0xf301,0xdc01};
        const unsigned values[]={0,(v&0xff00)|((256-(v&255))&255),
            (v&255)|(((256-(v>>8))&255)<<8),v^255,v^0xff00,v^0xffff,
            v/2+carry*32768,(v*2+carry)&65535};
        for (unsigned i=0;i<8;++i)
        {
            puce_state c; c.cpu19m=true; c.l.fill(0xa55a);c.l[r]=v;
            c.di=((v^(v>>8))&254)|carry;
            auto expected=c.l;expected[r]=values[i];
            unsigned flags=c.di;
            if (i==6) flags=(flags&254)|(v%2);
            if (i==7) flags=(flags&254)|(v/32768);
            assert(c.execute_register(codes[i]|(r<<4)));
            assert(c.l==expected && c.di==flags);++count;
        }
    }
    // ADLL distinct operands: conceptual 16-bit addition of Lx to the
    // byte-swapped Ly. Check final HIGH byte zero, not whole-word zero.
    for (unsigned x=0;x<16;++x)
    for (unsigned y=0;y<16;++y)
    for (unsigned sample=0;sample<4096;++sample)
    for (unsigned carry=0;carry<2;++carry)
    {
        puce_state c; c.cpu19m=true;c.l.fill(0x5aa5);
        c.l[x]=(sample*251+0x4b01)&65535;
        if(x!=y)c.l[y]=(sample*509+0x1cfe)&65535;
        c.di=(sample&0xf8)|6|carry;
        auto expected=c.l;
        unsigned first=c.l[x], other=c.l[y];
        unsigned result;
        if(x!=y) result=first+((other%256)*256+other/256)+carry;
        else {
            unsigned low=first%256+first/256+carry;
            // The second beta reads the just-written low byte.
            unsigned high=low%256+first/256+low/256;
            result=(low%256)+high*256;
        }
        expected[x]=result;
        const unsigned high_left=x==y?(result&255):(other&255);
        const unsigned low_carry=((first&255)+(other>>8)+carry)/256;
        const unsigned flags=(c.di&0xf8)|(result>65535?1:0)
            |((result&0xff00)==0?2:0)
            |((high_left%16+(first/256)%16+low_carry)>=16?4:0);
        assert(c.execute_register(0xce00|(x<<4)|y));
        assert(c.l==expected && c.di==flags);++count;
    }
    // Both conditional +2 forms: all selectors, DI patterns, destinations,
    // wrap boundaries, and untaken cases. Neither form changes DI.
    for(unsigned cond=0;cond<16;++cond)
    for(unsigned flags=0;flags<256;++flags)
    for(unsigned r=0;r<16;++r)
    for(unsigned v : {0U,1U,0xffU,0xfffeU,0xffffU})
    {
        puce_state c;c.cpu19m=true;c.l.fill(0x1234);c.l[r]=v;c.di=flags;
        auto expected=c.l;
        if(bool(flags&(1<<(cond/2)))==bool(cond%2))expected[r]=(v+2)&65535;
        assert(c.execute_register(0xa400|(cond<<4)|r));
        assert(c.l==expected && c.di==flags);++count;
    }
    for(unsigned r=0;r<16;++r)
    for(unsigned v : {0U,0xffU,0x100U,0x7fffU,0x8000U,0xffffU})
    {
        puce_state c;c.cpu19m=true;c.l.fill(0x4321);c.l[r]=v;c.di=0xdb;
        const auto before=c.l;
        unsigned reads=0,writes=0,address=0,data=1;
        auto read=[&](unsigned){++reads;return 0xbeef;};
        auto write=[&](unsigned a,unsigned d){++writes;address=a;data=d;};
        assert(c.execute_word(0xc10f|(r<<4),read,write));
        assert(writes==1 && reads==0 && address==(r<12?v:v%256) && data==0);
        assert(c.l==before && c.di==0xdb);
        writes=0;data=1;
        assert(c.execute_byte(0xac0f|(r<<4),read,write));
        assert(writes==1 && reads==0 && address==(r<12?v:v%256) && data==0);
        assert(c.l==before && c.di==0xdb);count+=2;
        c.cpu19m=false;writes=0;
        assert(!c.execute_word(0xc10f|(r<<4),read,write));
        assert(!c.execute_byte(0xac0f|(r<<4),read,write));
        assert(writes==0 && c.l==before && c.di==0xdb);
    }
    for(unsigned level=1;level<=4;++level)for(unsigned flags=0;flags<256;++flags)
    {
        puce_state c;c.cpu19m=true;c.level=level;c.di=flags;c.l.fill(0xa55a);
        auto before=c.l;auto active=c.active;
        assert(c.execute_register(0xbd21) && c.inhibit_level3);
        assert(c.external_irq_poll_level()==(level==4?3:level));
        assert(c.l==before && c.active==active && c.di==flags && c.level==level);
        assert(c.execute_register(0xbd20) && !c.inhibit_level3);
        assert(c.external_irq_poll_level()==level);
        assert(c.l==before && c.active==active && c.di==flags && c.level==level);
        count+=2;
    }
    // Explicit variant gate: CPU19 must not accept CPU19M extensions.
    for(unsigned op : {0xce12U,0xfe2fU,0xb52fU,0xe32fU,0x802fU,0x812fU,
        0x842fU,0xa4b2U,0xf321U,0xdc21U,0xbd20U,0xbd21U})
    { puce_state c;assert(!c.execute_register(op)); }
    std::cout<<"PASS: "<<count<<" CPU19M register/memory cases; I/O and interrupt audit remains separate\n";
}
'''

if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='puce-cpu19m-') as temp:
        source=Path(temp)/'test.cpp'; binary=Path(temp)/'test'
        source.write_text(TEST)
        subprocess.run(['c++','-std=c++20','-O2','-Wall','-Wextra','-Werror',
                        '-I',str(ROOT/'src/devices'),str(source),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
