#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders:Salvatore Paxia
"""ROM-free tests of the live GOINO/CONDY transaction state."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TEST = r'''
#include "machine/p6066_goino_state.h"
#include <cassert>
#include <iostream>
int main()
{
    p6066_goino_state c;
    assert(c.data(0x4001, 4) && c.lamp_strobes == 0);
    c.select(0); c.select(0);
    assert(c.selected && c.lamp_strobes == 0);
    assert(c.data(0x4001, 3) && c.lamp_strobes == 0);
    c.select(1); assert(c.data(0x4001, 4) && c.lamp_strobes == 0);
    c.select(0);
    // Single walking bits check serial direction and exact 16-strobe boundary.
    for (unsigned bit = 0; bit != 16; ++bit)
    {
        const auto previous = c.lamps;
        for (unsigned n = 0; n != 16; ++n)
        {
            assert(c.data(0x40fe | (n == 15 - bit), 4));
            if (n < 15) assert(c.lamps == previous);
        }
        assert(c.lamps == (1U << bit));
    }
    assert(c.lamp_strobes == 256);
    // CAROM's actual initialization sends 256 one-bits (16 complete words).
    for (unsigned i = 0; i != 256; ++i) assert(c.data(0x40ff, 4));
    assert(c.lamps == 0xffff && c.lamp_bits == 0 && c.lamp_strobes == 512);
    assert(c.data(0, 4));
    assert(!c.data(0x0100, 4)); // no invented printer success
    assert(c.lamps == 0xffff && c.lamp_strobes == 512);
    // Two preparation bytes never appear as visible columns.
    assert(c.data(0x20ff, 4)); assert(c.data(0x20ff, 4));
    for (unsigned i = 0; i != 221; ++i) assert(c.data(0x2000 | (i & 127), 4));
    assert(!c.display_ready && c.display_position == 223);
    assert(c.data(0x2081, 4));
    assert(c.display_ready && c.display_position == 0);
    assert(c.display[2] == 0 && c.display[222] == 92 && c.display[223] == 0x81);
    assert(c.data(0x2000, 4)); assert(!c.display_ready && c.display_position == 1);
    std::cout << "PASS: selection gating, serial lamp order/framing, CAROM lamp stream, display framing and unsupported commands\n";
}
'''
with tempfile.TemporaryDirectory(prefix='p6066-console-') as temp:
    source = Path(temp) / 'test.cpp'
    binary = Path(temp) / 'test'
    source.write_text(TEST)
    subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-O2',
                    '-I', str(ROOT / 'src/devices'), str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
