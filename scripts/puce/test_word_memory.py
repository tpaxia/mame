#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders: Salvatore Paxia
"""Test live PUCE word transfers against boundary and CAROM-order fixtures."""
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
    struct example {
        unsigned op, index, source, bus_value, address, adjusted, final_index, final_source;
        bool store;
    };
    const example cases[] = {
        {0xe123,0x8001,0x1234,0x1234,0x8001,0x8001,0x8001,0x1234,true},
        {0xd123,0x8001,0x1111,0x1234,0x8001,0x8001,0x8001,0x1234,false},
        {0xee22,0x0020,0,0x0021,0x0020,0x0021,0x0021,0x0021,true},
        {0xed22,0x0021,0,0x0020,0x0021,0x0020,0x0020,0x0020,true},
        {0xe222,0x0020,0,0x0022,0x0020,0x0021,0x0021,0x0021,true},
        {0xe223,0x0020,0xffff,0x0000,0x0020,0x0021,0x0021,0xffff,true},
        {0xde22,0x8000,0,0x0053,0x8000,0x8001,0x0053,0x0053,false},
        {0xdd22,0x8000,0,0x0053,0x8000,0x7fff,0x0053,0x0053,false},
        {0xeecc,0xabff,0,0xab00,0x00ff,0xab00,0xab00,0xab00,true},
        {0xdec3,0xabff,0x1111,0x1234,0x00ff,0xab00,0xab00,0x1234,false},
        {0xddc3,0xab00,0x1111,0x1234,0x0000,0xabff,0xabff,0x1234,false},
        {0xee23,0xffff,0xa55a,0xa55a,0xffff,0x0000,0x0000,0xa55a,true},
        {0xde23,0xffff,0x1111,0x1234,0xffff,0x0000,0x0000,0x1234,false},
        {0xdd23,0x0000,0x1111,0xabcd,0x0000,0xffff,0xffff,0xabcd,false},
        {0xe1c3,0xabff,0x4321,0x4321,0x00ff,0xabff,0xabff,0x4321,true},
    };
    for (const auto &e : cases)
    {
        puce_state c; c.l.fill(0x7654); c.di = 0xad;
        const unsigned x = (e.op >> 4) & 15, y = e.op & 15;
        c.l[y] = e.source; c.l[x] = e.index;
        auto expected = c.l; expected[x] = e.final_index; expected[y] = e.final_source;
        unsigned accesses = 0;
        auto check = [&](unsigned address) {
            assert(address == e.address && c.l[x] == e.adjusted && c.di == 0xad);
            ++accesses;
        };
        assert(c.execute_word(e.op,
            [&](unsigned address) { assert(!e.store); check(address); return e.bus_value; },
            [&](unsigned address, unsigned value) { assert(e.store && value == e.bus_value); check(address); }));
        assert(accesses == 1 && c.l == expected && c.di == 0xad);
    }
    puce_state c; c.reset(); c.set_pc(0x804e); c.advance();
    assert(c.execute_word(0xde11,
        [&](unsigned address) { assert(address == 0x804f && c.pc() == 0x8050); return 0x8053; },
        [](unsigned, unsigned) { assert(false); }));
    assert(c.pc() == 0x8053);
    // SLL returns through a byte-reversed saved counter, preserving flags.
    c.level = 4; c.l[0] = 0x81bf; c.l[2] = 0xac80; c.di = 0x97;
    c.advance(); assert(c.execute_register(0xbc02));
    assert(c.pc() == 0x80ac && c.l[2] == 0xc081 && c.di == 0x97);
    c.l[3] = 0x1234; c.l[6] = 0x5678;
    assert(c.execute_register(0xbc36));
    assert(c.l[3] == 0x7856 && c.l[6] == 0x3412 && c.di == 0x97);
    c.l[15] = 0x1234; assert(c.execute_register(0xbcff));
    assert(c.l[15] == 0x1234 && c.di == 0x97);
    const auto before = c;
    assert(!c.execute_word(0xffff,
        [](unsigned) { assert(false); return 0; }, [](unsigned, unsigned) { assert(false); }));
    assert(c.l == before.l && c.di == before.di);
    c.l[4] = 0xffaa; c.di = 0xad;
    assert(c.execute_register(0x954f) && c.l[4] == 0x00aa && c.di == 0xad);
    assert(c.execute_register(0xbe4f) && c.l[4] == 0xffaa && c.di == 0xad);
    c.l[4] = 0x01ff;
    assert(c.execute_register(0xbe4f) && c.l[4] == 0x00ff && c.di == 0xaf);
    assert(c.execute_register(0x854f) && c.l[4] == 0 && c.di == 0xaf);
    std::cout << "PASS: 15 word-transfer fixtures, access ordering, full/short wrap, PC alias and byte increment/decrement flags\n";
}
'''
with tempfile.TemporaryDirectory(prefix='puce-word-test-') as temp:
    source = Path(temp) / 'test.cpp'
    binary = Path(temp) / 'test'
    source.write_text(TEST)
    subprocess.run(['c++', '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I', str(ROOT / 'src/devices'), str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
