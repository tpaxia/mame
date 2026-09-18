#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders: Salvatore Paxia
"""Test live PUCE state and optionally the reference CAROM (not distributed)."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TEST = r'''
#include "cpu/puce/puce_state.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <vector>

int main(int argc, char **argv)
{
    // CPU19M p.3.14: software service is CPU name 02, not GOINO 00.
    // A higher-priority external service temporarily owns the name bus.
    {
        puce_state q; q.execute_register(0xbd00); q.di=0xc0;
        q.execute_register(0xbd10);
        assert(q.level==4 && q.com1_pending && q.internal_name==0 && q.di==0xc0);
        // Handler emits a request; the execution loop arbitrates at ALFA.
        assert(q.enter_level(3)); q.com1_pending=false; q.internal_name=2;
        assert(q.execute_input(0xaab0,0,0) && q.l[11]==2);
        q.execute_register(0xbd10); assert(q.internal_name==2);
        assert(q.enter_level(2));
        assert(q.execute_input(0xaab0,0x0860,0) && q.l[11]==0x0860);
        q.execute_register(0xbd00);
        assert(q.execute_input(0xb9b0,0,0) && q.a(11)==2);
        q.execute_register(0xbd00); assert(q.internal_name==0);
        assert(q.enter_level(3));
        assert(q.execute_input(0xaab0,0x0160,0) && q.l[11]==0x0160);
        q.internal_name=3; // INV's ICMEN row of the same priority table
        assert(q.execute_input(0xaab0,0,0) && q.l[11]==3);
        q.com1_pending=true; q.reset(); assert(q.internal_name==0 && !q.com1_pending);
    }
    puce_state c;
    c.l[2] = 0x1234; c.set_a(2, 0xab); assert(c.l[2] == 0x12ab);
    c.set_b(2, 0xcd); assert(c.l[2] == 0xcdab);
    c.di = 0x55; c.reset();
    assert(c.pc() == 0x8000 && c.l[2] == 0xcdab && c.di == 0x55);
    c.l[0] = 0x8089; c.advance(); assert(c.execute_register(0xbd00));
    assert(c.pc() == 0x8089 && c.l[1] == 0x8001 && c.level == 4);
    assert(c.execute_register(0xbd10)); assert(c.pc() == 0x8089 && c.com1_pending);
    assert(c.enter_level(3)); c.com1_pending=false; c.internal_name=2;
    assert(c.pc() == 0x8001);
    c.l[13] = 0x56ff; assert(c.enter_level(2)); c.advance();
    assert(c.pc() == 0x8200 && c.l[13] == 0x5600);
    c.l[12] = 0x78ff; assert(c.enter_level(1)); c.advance();
    assert(c.pc() == 0x8100 && c.l[12] == 0x7800);
    c.leave_level(); assert(c.level == 2); c.leave_level(); assert(c.level == 3);
    // UC020 L03: independent reset and interrupt-page selectors, not B12/B13.
    for (unsigned reset : {0x8000U, 0xc000U})
        for (unsigned base : {0x8000U, 0xc000U}) {
            puce_state q; q.reset_base=reset; q.interrupt_base=base; q.reset();
            assert(q.pc()==reset); q.l[0]=0x4567;
            assert(q.execute_register(0xbd00) && q.pc()==0x4567);
            q.l[13]=0x55ff; assert(q.enter_level(2));
            assert(q.pc()==(base|0x2ff)); q.advance();
            assert(q.pc()==(base|0x200) && q.l[13]==0x5500);
            q.l[12]=0xaa0c; assert(q.enter_level(1));
            assert(q.pc()==(base|0x10c)); q.advance();
            assert(q.execute_register(0x0110));
            assert(q.pc()==(base|0x110) && q.l[12]==0xaa10);
            q.di=0; assert(q.execute_register(0x62fe));
            assert(q.pc()==(base|0x1fe)); q.advance(); q.advance();
            assert(q.pc()==(base|0x100) && q.l[12]==0xaa00);
            assert(q.indirect(12)==0); // ordinary short data addresses stay low
            q.leave_level(); assert(q.pc()==(base|0x200));
            q.leave_level(); assert(q.pc()==0x4567);
        }

    c.l[11] = 0xabcd; c.l[12] = 0xabcd;
    assert(c.indirect(11) == 0xabcd && c.indirect(12) == 0xcd);
    c.set_pc(0x9fff); c.advance(); assert(c.execute_register(0x0123));
    assert(c.pc() == 0xa123); // advanced counter supplies the page
    c.set_pc(0x80ff); c.advance(); c.di = 0;
    assert(c.execute_register(0x6242)); assert(c.pc() == 0x8142);
    c.di = 2; assert(c.execute_register(0x6251)); assert(c.pc() == 0x8142);
    c.set_pc(0xffff); c.advance(); assert(c.pc() == 0);
    c.l[2] = 0xaaaa; c.di = 0xfd; assert(c.execute_register(0xd722));
    assert(c.l[2] == 0xaa00 && c.di == 0xff);
    assert(c.execute_register(0xba22)); assert(c.l[2] == 0x00aa);
    c.l[2] = 0x1281; c.di = 0xaa; assert(c.execute_register(0xc321));
    assert(c.l[2] == 0x1240 && c.di == 0xab);
    assert(c.execute_register(0xc421)); assert(c.l[2] == 0x1281 && c.di == 0xaa);
    c.l[4] = 0xffff; c.di = 0xff; assert(c.execute_register(0xa54f));
    assert(c.l[4] == 0 && c.di == 0xff);
    assert(c.execute_register(0xe54f)); assert(c.l[4] == 0xffff && c.di == 0xfd);
    c.l[4] = 1; assert(c.execute_register(0xe54f)); assert(c.di == 0xff);
    c.l[1] = 0x8001; c.di = 0; assert(c.execute_register(0xa001));
    assert(c.pc() == 0x8002); // conditional increment can skip through live PC
    assert(puce_state::byte_mask(0) == 0xff00 && puce_state::byte_mask(1) == 0x00ff);
    // COM3 asserts a persistent line without changing registers, DI or level.
    c.level = 3; c.active = 0x18;
    const auto registers = c.l;
    const auto flags = c.di;
    assert(c.execute_register(0xbd30));
    assert(!c.ecorn && c.level == 3 && c.l == registers && c.di == flags);
    assert(c.execute_register(0xc900) && !c.ecorn);
    assert(c.execute_register(0xbd00) && c.ecorn && c.level == 4);
    assert(c.execute_register(0xbd30) && !c.ecorn);
    assert(c.execute_register(0xbd00) && !c.ecorn && c.level == 4);
    // ETIB CROM/TROM select and write RB only; RO0..3 is unused.
    for (unsigned r=0; r<16; ++r)
        for (unsigned low=0; low<16; ++low)
        {
            puce_state alias;
            alias.l.fill(0x5aa5); alias.di=0xdb;
            auto expected=alias.l; expected[r]=0x39a5;
            assert(alias.execute_input(0xb200 | (r<<4) | low, 0x3972, 0xe6));
            assert(alias.l==expected && alias.di==0xdb);
        }
    // External input packing is tested with nonzero buses, independently of CAROM.
    c.di = 0xa5; c.l[9] = 0x1234;
    assert(c.execute_input(0xaa90, 0xabcd, 0x56) && c.l[9] == 0xabcd);
    assert(c.execute_input(0xb990, 0x1234, 0x56) && c.l[9] == 0xab34);
    assert(c.execute_input(0xb29f, 0x1234, 0x56) && c.l[9] == 0x1234);
    assert(c.execute_input(0xb898, 0xabcd, 0x56) && c.l[9] == 0x1256);
    assert(c.execute_input(0xa998, 0xabcd, 0x78) && c.l[9] == 0x7856);
    assert(c.di == 0xa5 && !c.execute_input(0xaa91, 0xabcd, 0x56));
    c.reset(); assert(!c.ecorn);
    c.advance();
    assert(c.execute_input(0xaa10, 0x9234, 0) && c.pc() == 0x9234);
    assert(c.execute_input(0xb910, 0xab56, 0) && c.pc() == 0x9256);
    const auto before = c;
    assert(!c.execute_register(0xbd20)); assert(!c.execute_register(0xb1f4));
    assert(c.l == before.l && c.di == before.di && c.level == before.level);
    std::cout << "PASS: registers, reset, nested counters, addressing, branches, flags and unsupported operations\n";
    // Independent arithmetic oracle: subtraction/borrow rather than the core's
    // complemented-add implementation. Exhaust all operands/carry/destinations,
    // both aliased and separate A/B halves, with varying preserved DI bits.
    unsigned arithmetic_cases = 0;
    for (unsigned sub = 0; sub != 2; ++sub)
    for (unsigned destination = 0; destination != 3; ++destination)
    for (unsigned alias = 0; alias != 2; ++alias)
    for (int a = 0; a != 256; ++a)
    for (int b = 0; b != 256; ++b)
    for (int carry = 0; carry != 2; ++carry)
    {
        puce_state t;
        t.l.fill(0x5aa5);
        const unsigned x = 3, y = alias ? 3 : 4;
        t.set_a(x, a); t.set_b(y, b);
        t.di = ((a ^ b) & 0xf8) | 6 | carry;
        const auto before = t;
        const int raw = sub ? a - b - (1 - carry) : a + b + carry;
        const unsigned result = raw & 255;
        const bool full_carry = sub ? raw >= 0 : raw >= 256;
        const bool half_carry = sub ? (a & 15) >= (b & 15) + (1 - carry)
                                   : (a & 15) + (b & 15) + carry >= 16;
        const unsigned expected_di = (before.di & 0xf8) | full_carry
                                   | (result == 0 ? 2 : 0) | (half_carry ? 4 : 0);
        const unsigned hi = (sub ? 0xb6 : 0x86) + destination * 0x10;
        assert(t.execute_register((hi << 8) | (x << 4) | y));
        auto expected = before.l;
        if (destination == 1) expected[x] = (expected[x] & 0xff00) | result;
        if (destination == 2) expected[y] = (expected[y] & 0xff) | (result << 8);
        assert(t.l == expected && t.di == expected_di);
        ++arithmetic_cases;
    }
    c.reset(); c.set_pc(0x80ff); c.set_b(2, 1); c.di = 0; c.advance();
    assert(c.execute_register(0x9612) && c.pc() == 0x8101);
    std::cout << "PASS: " << arithmetic_cases << " exhaustive ADD/SOT cases and post-fetch PC alias\n";
    if (argc == 1) return 0;
    std::ifstream input(argv[1], std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)), {});
    assert(bytes.size() == 4096);
    auto word = [&](unsigned pc) {
        assert(pc >= 0x8000 && pc < 0x8800);
        unsigned i = (pc - 0x8000) * 2;
        return (bytes[i] << 8) | bytes[i + 1];
    };
    c = puce_state{}; c.reset();
    unsigned count = 0;
    std::uint16_t ram0 = 0xffff;
    for (;;)
    {
        const auto pc = c.pc(); const auto op = word(pc); c.advance(); ++count;
        assert(count < 100);
        if (c.execute_register(op)) continue;
        if (op == 0xa8ff) // emulate the sole cold-path store in this state fixture
        {
            assert(c.indirect(15) == 0 && c.a(15) == 0);
            ram0 &= ~puce_state::byte_mask(0);
            continue;
        }
        assert(pc == 0x808b && op == 0xb1f4 && c.level == 4 && c.pc() == 0x808c);
        assert(ram0 == 0x00ff && count == 6);
        break;
    }
    std::cout << "PASS: reference CAROM cold path stops at ESE, word 808B, after 6 instructions\n";
    c = puce_state{}; c.reset(); c.set_pc(0x8003); count = 0;
    for (;;)
    {
        const auto pc = c.pc(); const auto op = word(pc); c.advance(); ++count;
        assert(count < 300000 && pc != 0x8051);
        if (c.execute_register(op)) continue;
        assert(pc == 0x802d && op == 0xaa90 && c.level == 3 && !c.ecorn);
        assert(c.l[4] == 0 && c.l[5] == 0 && c.l[6] == 0 && c.a(8) == 0x47);
        break;
    }
    std::cout << "PASS: isolated CAROM register self-test executes COM3 and reaches ENTL at 802D after "
              << count << " instructions (not a complete boot/self-test)\n";
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default='c++')
    parser.add_argument('--carom', type=Path)
    args = parser.parse_args()
    if args.carom and hashlib.sha1(args.carom.read_bytes()).hexdigest() != '63a68b4787f33bef156f05c6b50269357d0d3c2f':
        parser.error('CAROM is not the verified 4096-byte reference image')
    with tempfile.TemporaryDirectory(prefix='puce-cpu-test-') as temp:
        source = Path(temp) / 'test.cpp'
        binary = Path(temp) / 'test'
        source.write_text(TEST)
        subprocess.run([args.cxx, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-I', str(ROOT / 'src/devices'), str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)] + ([str(args.carom.resolve())] if args.carom else []), check=True)


if __name__ == '__main__':
    main()
