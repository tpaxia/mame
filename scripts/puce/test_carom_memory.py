#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders: Salvatore Paxia
"""Run original CAROM's level-4 memory probe and test with fault injection."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TEST = r'''
#include "cpu/puce/puce_state.h"
#include <array>
#include <cassert>
#include <fstream>
#include <iostream>

int main(int argc, char **argv)
{
    std::array<unsigned char, 4096> rom{};
    std::ifstream f(argv[1], std::ios::binary);
    f.read(reinterpret_cast<char *>(rom.data()), rom.size());
    assert(f.gcount() == rom.size());
    for (unsigned mode = 0; mode != 5; ++mode)
    {
        puce_state c; c.level = 4; c.active = 0x10; c.set_pc(0x8092); c.l[1] = 0x807d;
        std::array<std::uint16_t, 32768> ram{};
        unsigned stores = 0, verifies = 0, errors = 0, steps = 0;
        bool probe_branch = false, finished = false, invalid_pending = false;
        unsigned invalid_cycles = 0;
        auto read = [&](unsigned address) -> std::uint16_t {
            if (address >= 0x8000 && address < 0x8800)
                return (rom[(address - 0x8000) * 2] << 8) | rom[(address - 0x8000) * 2 + 1];
            if (address >= ram.size())
            {
                assert(mode == 4); invalid_pending = true; ++invalid_cycles; return 0;
            }
            return ram[address];
        };
        while (++steps < 2000000)
        {
            if (invalid_pending) { invalid_pending = false; assert(c.enter_level(3)); }
            const auto pc = c.pc();
            if (pc == 0x80a1 && c.l[4] == 0)
            {
                // Cross-half XOR after SAB compares the same byte positions
                // of the first and second reads. Writable RAM must DIFFER.
                assert(c.l[6] == (mode == 1 ? 0 : 0xff00));
                assert(c.l[9] == (mode == 1 ? 0 : 0xff00));
                assert((c.a(6) ^ c.b(9)) == (mode == 1 ? 0 : 0xff));
            }
            if (pc == 0x80b7) probe_branch = true;
            if (pc == 0x81b6) ++errors;
            if (pc == 0x80ba && mode < 3)
            {
                assert(mode == 1 && !probe_branch && c.l[2] == 0xa880);
                std::cout << "PASS: read-only memory takes the alternate probe path at 80BA\n";
                finished = true; break;
            }
            if (pc == 0x81be && mode < 3)
            {
                assert(mode != 1 && probe_branch && c.l[2] == 0xac80);
                assert(bool(c.di & 0x10) == (mode == 0));
                assert(errors == (mode == 2 ? 1U : 0U));
                if (mode == 0) assert(stores == 2048 && verifies == 2048);
                else assert(verifies == 1);
                std::cout << "PASS: " << (mode == 0 ? "RAM verifies both patterns" : "injected read fault reaches 81B6 and clears D4")
                    << "; stop 81BE, DI=" << std::hex << unsigned(c.di) << std::dec
                    << ", stores=" << stores << ", verifies=" << verifies << "\n";
                finished = true; break;
            }
            if (pc == 0x80aa && mode == 3)
            {
                assert(mode == 3 && c.l[4] == 0x8000 && (c.di & 0x10));
                assert(stores == 65536 && verifies == 65536 && errors == 0);
                std::cout << "PASS: all 32 RAM blocks verify both patterns; CAROM checksum path returns with D4 set; next BMI/80AA\n";
                finished = true; break;
            }
            if (pc == 0x8308)
            {
                assert(mode == 4 && invalid_cycles == 30 && errors == 0);
                assert(stores == 65536 && verifies == 65536);
                assert(c.l[1] == 0x8226 && c.a(14) == 1 && (ram[0] & 255) == 0xe0);
                assert((ram[7] >> 8) == 0x80); // byte 0E: first non-RAM block
                std::cout << "PASS: memory enumeration recovers 30 invalid cycles and reaches floppy selection E0 at 8308\n";
                finished = true; break;
            }
            const auto op = read(pc); c.advance();
            if (c.execute_register(op)) continue;
            bool done = c.execute_word(op,
                [&](unsigned address) -> std::uint16_t {
                    auto value = read(address);
                    if (pc == 0x81ab)
                    {
                        ++verifies;
                        if (mode == 2 && verifies == 1) value ^= 1;
                    }
                    return value;
                },
                [&](unsigned address, std::uint16_t value) {
                    if (address >= 0x8800) { assert(mode == 4); invalid_pending = true; ++invalid_cycles; return; }
                    if (address >= 0x8000) return; // ROM ignores writes
                    assert(address < ram.size());
                    if (mode != 1) ram[address] = value;
                    if (pc == 0x8197) ++stores;
                });
            if (!done) done = c.execute_byte(op,
                [&](unsigned address) { return (read(address / 2) >> puce_state::byte_shift(address)) & 255; },
                [&](unsigned address, unsigned value) {
                    const unsigned word=address/2, mask=puce_state::byte_mask(address);
                    ram[word]=(ram[word]&~mask)|(value<<puce_state::byte_shift(address));
                });
            if (!done) std::cerr << "Unsupported " << std::hex << op << " at " << pc << "\n";
            assert(done);
        }
        assert(finished);
    }
}
'''
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--carom', type=Path, required=True)
args = parser.parse_args()
assert hashlib.sha1(args.carom.read_bytes()).hexdigest() == '63a68b4787f33bef156f05c6b50269357d0d3c2f', 'Unexpected CAROM revision'
with tempfile.TemporaryDirectory() as tmp:
    source = Path(tmp) / 'test.cpp'
    binary = Path(tmp) / 'test'
    source.write_text(TEST)
    subprocess.run(['c++', '-std=c++17', '-O2', '-I', str(ROOT / 'src/devices'), str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary), str(args.carom.resolve())], check=True)
