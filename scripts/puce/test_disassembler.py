#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders: Salvatore Paxia
"""Compile and test the actual PUCE disassembler without a full MAME build.

No ROM files are required. Expected strings are transcribed instruction
examples and boundary cases, not generated using the decoder's algorithm.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TEST = r'''
#include "cpu/puce/pucedasm.h"
#include <iostream>
#include <sstream>
#include <stdexcept>

class word_buffer : public util::disasm_interface::data_buffer
{
public:
    using u8 = util::disasm_interface::u8;
    using u16 = util::disasm_interface::u16;
    using u32 = util::disasm_interface::u32;
    using u64 = util::disasm_interface::u64;
    using offs_t = util::disasm_interface::offs_t;
    u16 word = 0;
    mutable unsigned reads = 0;
    virtual u8 r8(offs_t) const override { throw std::runtime_error("byte access"); }
    virtual u16 r16(offs_t pc) const override
    {
        if (pc != 0x8000) throw std::runtime_error("incorrect word address");
        ++reads;
        return word;
    }
    virtual u32 r32(offs_t) const override { throw std::runtime_error("32-bit access"); }
    virtual u64 r64(offs_t) const override { throw std::runtime_error("64-bit access"); }
};

int main()
{
    struct example { unsigned word; const char *text; };
    const example examples[] = {
        {0x27ff, "AMD A7,CFF"}, {0x3502, "MAD A5,C02"},
        {0xa823, "AMI M2,A3"}, {0x82cc, "AMIM A12,A12"}, {0x88ff, "AMIP A15,A15"},
        {0x8a12, "BMIM M1,B2"}, {0x8c22, "BMIP M2,B2"},
        {0x91c3, "MAI A12,A3"}, {0x9212, "MAIM M1,A2"}, {0x9811, "MAIP M1,A1"},
        {0x99f1, "MBI A15,B1"}, {0x9a22, "MBIM M2,B2"}, {0x9cc3, "MBIP A12,B3"},
        {0xab2f, "AZAM A2"}, {0xbb2f, "AZAP A2"}, {0xcb2f, "AZBM B2"}, {0xdb2f, "AZBP B2"},
        {0x89f4, "BMI A15,B4"}, {0x8922, "BMI M2,B2"},
        {0xbc36, "SLL L3,L6"}, {0xbc02, "SLL L0,L2"}, {0xbcff, "SLL L15,L15"},
        {0xd112, "MLI M1,L2"}, {0xddc3, "MLIM A12,L3"}, {0xde11, "MLIP M1,L1"},
        {0xe134, "LMI M3,L4"}, {0xed22, "LMIM M2,L2"}, {0xeef1, "LMIP A15,L1"},
        {0xe222, "LPMIP M2,L2"}, {0x85ff, "ICA A15"}, {0x954f, "ICB B4"},
        {0xbe4f, "DCB B4"}, {0x9540, "DW 9540"}, {0x8612, "ADD A1,B2"}, {0x9632, "ADDA A3,B2"}, {0xa623, "ADDB A2,B3"},
        {0xb6ff, "SOT A15,B15"}, {0xc60a, "SOTA A0,B10"}, {0xd6ca, "SOTB A12,B10"},
        {0xaa90, "ENTL L9"}, {0xb990, "ENUA A9"}, {0xb29f, "ETIB B9"},
        {0xb898, "EDA A9"}, {0xa998, "EDB B9"}, {0xaa91, "DW AA91"},
        {0xbd30, "COM3"}, {0xb1f4, "ESE A15"}, {0xb1b4, "ESE M11"}, {0xfca0, "DAE L10"},
        {0xb1f5, "DW B1F5"}, {0xfca1, "DW FCA1"},
        {0x5080, "CRTB B0,C80"}, {0x7089, "CRTA A0,C89"},
        {0x5fff, "CRTB B15,CFF"}, {0x7f03, "CRTA A15,C03"},
        {0xc900, "NOP"}, {0xc800, "REDI C00"},
        {0xc921, "SEDI C21"}, {0xc855, "REDI C55"},
        {0xa0a3, "INCD0 D5,L3"}, {0xa0b3, "INCD1 D5,L3"},
        {0xa0ff, "INCD1 D7,L15"}, {0xa000, "INCD0 D0,L0"},
        {0xbd00, "COM0"}, {0xbd10, "COM1"}, {0xbdf0, "COM15"},
        {0xbd20, "DW BD20"}, {0x6251, "SAD0 D1,C51"},
        {0xb722, "ANDB A2,B2"}, {0x1fff, "SAI 9FFF"},
        {0xbd01, "DW BD01"}, {0xd722, "OREA A2,B2"},
        {0xe61f, "OR A1,B15"}, {0xf622, "ORA A2,B2"},
        {0x8722, "ORB A2,B2"}, {0xc722, "ORE A2,B2"},
        {0xe7f1, "OREB A15,B1"}, {0xba12, "SAB A1,B2"},
        {0xc3f0, "SHDA A15"}, {0xd300, "SHDB B0"},
        {0xc430, "SHSA A3"}, {0xd440, "SHSB B4"},
        {0xc351, "SLDA A5"}, {0xd361, "SLDB B6"},
        {0xc312, "DW C312"}, {0x8b2f, "ROTA A2"}, {0x9b3f, "ROTB B3"},
        {0xf1b0, "SEI M11"}, {0xf1c0, "SEI A12"},
        {0xfdf0, "SEIM A15"}, {0xf700, "SEIP M0"},
        {0x9000, "DW 9000"}, // never invent RESE from its RO value
        {0xf000, "DW F000"}, // never invent ALFA as a memory opcode
        {0x0000, "SAI 8000"}, {0xffff, "DW FFFF"}
    };
    puce_disassembler dasm;
    word_buffer buffer;
    if (dasm.opcode_alignment() != 1) return 1;
    for (const auto &e : examples)
    {
        buffer.word = e.word;
        std::ostringstream out;
        const auto result = dasm.disassemble(out, 0x8000, buffer, buffer);
        if (out.str() != e.text || result != (1U | util::disasm_interface::SUPPORTED))
        {
            std::cerr << std::hex << e.word << ": expected " << e.text
                      << ", got " << out.str() << " flags=" << result << '\n';
            return 1;
        }
    }
    // Every input must consume exactly one word, even when still untranscribed.
    for (unsigned op = 0; op != 65536; ++op)
    {
        buffer.word = op;
        buffer.reads = 0;
        std::ostringstream out;
        const auto result = dasm.disassemble(out, 0x8000, buffer, buffer);
        if ((result & util::disasm_interface::LENGTHMASK) != 1 || buffer.reads != 1 || out.str().empty())
            return 1;
    }
    std::cout << "PASS: " << std::size(examples)
              << " reference cases; all 65536 words preserve fetch/address/length invariants\n";
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default='c++')
    parser.add_argument('--unidasm', type=Path, help='Also verify the built command-line tool')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='puce-test-') as temp:
        source = Path(temp) / 'test.cpp'
        binary = Path(temp) / 'test'
        source.write_text(TEST)
        command = [args.cxx, '-std=c++20', '-O2', '-Wall', '-Wextra',
                   '-Wno-unused-parameter', '-Werror']
        for include in ('src/devices', 'src/lib', 'src/lib/util', 'src/osd'):
            command += ['-I', str(ROOT / include)]
        command += [str(source), str(ROOT / 'src/devices/cpu/puce/pucedasm.cpp'),
                    str(ROOT / 'src/lib/util/disasmintf.cpp'),
                    str(ROOT / 'src/lib/util/strformat.cpp'), '-o', str(binary)]
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True)
        if args.unidasm:
            fixture = Path(temp) / 'words.bin'
            fixture.write_bytes(bytes.fromhex('5080 7089 bd00 a0b3 c921 f1c0 f000'))
            output = subprocess.check_output(
                [str(args.unidasm.resolve()), str(fixture), '-arch', 'puce',
                 '-basepc', '8000'], text=True)
            rows = [re.split(r'\s+', line.strip(), maxsplit=2)
                    for line in output.splitlines() if line.strip()]
            expected = [
                ['8000:', '5080', 'CRTB B0,C80'],
                ['8001:', '7089', 'CRTA A0,C89'],
                ['8002:', 'bd00', 'COM0'],
                ['8003:', 'a0b3', 'INCD1 D5,L3'],
                ['8004:', 'c921', 'SEDI C21'],
                ['8005:', 'f1c0', 'SEI A12'],
                ['8006:', 'f000', 'DW F000'],
            ]
            if rows != expected:
                raise RuntimeError(f'unidasm byte order/address integration failed:\n{output}')
            print('PASS: unidasm registration, big-endian input and word-addressed PCs')


if __name__ == '__main__':
    main()
