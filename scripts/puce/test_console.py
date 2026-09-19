#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders: Salvatore Paxia
"""ROM-free tests of the live GOINO/CONDY transaction state."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TEST = r'''
#include "bus/p6066/goino_state.h"
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
    assert(c.data(0x0100, 4)); // explicitly requested printer no-op
    assert(c.lamps == 0xffff && c.lamp_strobes == 512);
    // Two preparation bytes never appear as visible columns.
    assert(c.data(0x20ff, 4)); assert(c.data(0x20ff, 4));
    for (unsigned i = 0; i != 221; ++i) assert(c.data(0x2000 | (i & 127), 4));
    assert(!c.display_ready && c.display_position == 223);
    assert(c.data(0x2081, 4));
    assert(c.display_ready && c.display_position == 0);
    assert(c.display[2] == 0 && c.display[222] == 92 && c.display[223] == 0x81);
    assert(c.data(0x2000, 4)); assert(!c.display_ready && c.display_position == 1);
    // Every upper nibble selects the same command from ECD8-ECDB.
    // Seed real latch state so these checks cannot pass with no-op handlers.
    for (unsigned upper = 0; upper != 16; ++upper)
    {
        p6066_goino_state q;
        q.select(0);
        assert(!q.requests_pending());
        q.matrix_request = q.column_request = q.button_request = true;
        q.pippo_request = q.keyboard_request = q.timer_request = q.double_key_request = true;
        q.pippo_enabled = q.timer_enabled = true;
        assert(q.requests_pending());
        const auto prefix = upper << 12;
        assert(q.data(prefix | 0x0401, 4));
        assert(!q.matrix_request && q.keyboard_request && q.timer_request);
        assert(q.commands_seen == 0x0010);
        // A REMAN command must not shift either CONDY data buffer.
        assert(q.lamp_strobes == 0);
        assert(q.display_position == 0);
        assert(q.data(prefix | 0x0500, 4) && !q.column_request && !q.button_request);
        assert(q.data(prefix | 0x0600, 4) && !q.pippo_request && q.pippo_enabled);
        assert(q.data(prefix | 0x0700, 4) && !q.keyboard_request);
        assert(q.data(prefix | 0x0800, 4) && !q.timer_request && q.timer_enabled);
        assert(q.data(prefix | 0x0900, 4) && !q.double_key_request);
        assert(q.data(prefix | 0x0b00, 4) && !q.pippo_enabled);
        assert(q.data(prefix | 0x0d00, 4) && !q.timer_enabled && q.interrupts_blocked);
        assert(q.data(prefix | 0x0e00, 4) && !q.interrupts_blocked);
        assert(q.commands_seen == 0x6bf0 && !q.requests_pending());
        q.matrix_request = true;
        q.select(1); assert(q.data(prefix | 0x0400, 4) && q.matrix_request);
        q.select(0); assert(q.data(prefix | 0x0400, 3) && q.matrix_request);
        // Timer enable exists; PIPPO and printer motion still have no producers.
        assert(!q.data(prefix | 0x0a00, 4));
        assert(q.data(prefix | 0x0c00, 4) && q.timer_enabled);
        assert(q.data(prefix | 0x0100, 4));
    }
    std::cout << "PASS: selection gating, serial lamp order/framing, CAROM lamp stream, display framing, four-bit command decode, reset latches and unsupported event producers\n";
}
'''
with tempfile.TemporaryDirectory(prefix='p6066-console-') as temp:
    source = Path(temp) / 'test.cpp'
    binary = Path(temp) / 'test'
    source.write_text(TEST)
    subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-O2',
                    '-I', str(ROOT / 'src/devices'), str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
