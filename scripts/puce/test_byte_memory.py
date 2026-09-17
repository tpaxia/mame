#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders: Salvatore Paxia
"""Check byte-transfer families, alias ordering, lane masks and direct addressing."""
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
    struct spec { unsigned hi; bool store, bank; int delta; };
    const spec ops[] = {{0xa8,1,0,0},{0x82,1,0,-1},{0x88,1,0,1},
        {0x89,1,1,0},{0x8a,1,1,-1},{0x8c,1,1,1},{0x91,0,0,0},
        {0x92,0,0,-1},{0x98,0,0,1},{0x99,0,1,0},{0x9a,0,1,-1},{0x9c,0,1,1}};
    unsigned count = 0;
    for (const auto &op : ops) for (unsigned x=0;x<16;++x) for(unsigned y=0;y<16;++y)
    for (unsigned initial : {0U,1U,0xffU,0x12ffU,0xffffU})
    {
        puce_state c; c.l.fill(0xabcd); c.l[x]=initial; c.di=0xa5;
        auto expected=c.l;
        const unsigned address = x<12 ? initial : initial%256;
        const unsigned updated = x<12 ? (initial+op.delta)&65535
            : (initial&0xff00)|((initial+op.delta)&255);
        expected[x]=updated;
        const unsigned outgoing = op.bank ? expected[y]/256 : expected[y]%256;
        if (!op.store) expected[y] = op.bank ? (expected[y]&255)|0x5a00 : (expected[y]&0xff00)|0x5a;
        unsigned calls=0;
        assert(c.execute_byte((op.hi<<8)|(x<<4)|y,
            [&](unsigned a) { assert(!op.store && a==address && c.l[x]==updated); ++calls; return 0x5a; },
            [&](unsigned a,unsigned v) { assert(op.store && a==address && v==outgoing && c.l[x]==updated); ++calls; }));
        assert(calls==1 && c.l==expected && c.di==0xa5); ++count;
    }
    for(unsigned address=0;address<256;++address)
    {
        puce_state c; c.l[3]=0xabcd; c.di=0xad; unsigned byte=0;
        assert(c.execute_byte(0x2300|address,[](unsigned){assert(false);return 0;},
            [&](unsigned a,unsigned v){assert(a==address);byte=v;}));
        assert(byte==0xcd && c.l[3]==0xabcd && c.di==0xad);
        assert(c.execute_byte(0x3300|address,[&](unsigned a){assert(a==address);return 0x56;},
            [](unsigned,unsigned){assert(false);}));
        assert(c.l[3]==0xab56 && c.di==0xad);
        const unsigned mask=puce_state::byte_mask(address), shift=puce_state::byte_shift(address);
        const unsigned word=(0x1234&~mask)|(0xab<<shift);
        assert(word==(address%2 ? 0x12ab : 0xab34));
    }
    puce_state c; c.l[2]=0xabcd; c.di=0x55;
    for(unsigned op : {0xab2f,0xbb2f,0xcb2f,0xdb2f})
    {
        c.l[2]=0xabcd; assert(c.execute_register(op));
        const unsigned expected=op==0xab2f?0xabc0:op==0xbb2f?0xab0d:op==0xcb2f?0xa0cd:0x0bcd;
        assert(c.l[2]==expected && c.di==0x55);
    }
    std::cout<<"PASS: "<<count<<" indirect byte cases, 256 direct addresses, lane preservation and nibble clears\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
    source=Path(tmp)/'test.cpp'; binary=Path(tmp)/'test'; source.write_text(TEST)
    subprocess.run(['c++','-std=c++17','-O2','-I',str(ROOT/'src/devices'),str(source),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
