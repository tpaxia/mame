#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders:P6066 contributors
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
    puce_state c;
    c.l[2] = 0x1234; c.set_a(2, 0xab); assert(c.l[2] == 0x12ab);
    c.set_b(2, 0xcd); assert(c.l[2] == 0xcdab);
    c.di = 0x55; c.reset();
    assert(c.pc() == 0x8000 && c.l[2] == 0xcdab && c.di == 0x55);
    c.l[0] = 0x8089; c.advance(); assert(c.execute_register(0xbd00));
    assert(c.pc() == 0x8089 && c.l[1] == 0x8001 && c.level == 4);
    assert(c.execute_register(0xbd10)); assert(c.pc() == 0x8001);
    c.l[13] = 0x56ff; assert(c.enter_level(2)); c.advance();
    assert(c.pc() == 0 && c.l[13] == 0x5600);
    c.l[12] = 0x78ff; assert(c.enter_level(1)); c.advance();
    assert(c.pc() == 0 && c.l[12] == 0x7800);
    c.leave_level(); assert(c.level == 2); c.leave_level(); assert(c.level == 3);
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
    const auto before = c;
    assert(!c.execute_register(0xbd20)); assert(!c.execute_register(0xb1f4));
    assert(c.l == before.l && c.di == before.di && c.level == before.level);
    std::cout << "PASS: registers, reset, nested counters, addressing, branches, flags and unsupported operations\n";
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
        assert(pc == 0x802c && op == 0xbd30 && c.level == 3);
        assert(c.l[4] == 0 && c.l[5] == 0 && c.l[6] == 0 && c.a(8) == 0x47);
        break;
    }
    std::cout << "PASS: isolated CAROM register self-test reaches COM3 at 802C after "
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
