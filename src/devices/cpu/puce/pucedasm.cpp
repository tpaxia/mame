// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia

// Initial, deliberately partial CPU19/PUCE disassembler.
// Source: Olivetti CPU19 Tabella Microistruzioni, publication 801.30.1,
// V2 PDF pp.1-6,9-12,14,20 (including arithmetic, input and command tables).
// Decode memory words, not the transformed RO register contents. ALFA and
// RESE are hardware-generated pseudo-instructions, not memory opcodes.
// Untranscribed encodings remain DW; this does not imply a hardware trap.

#include "pucedasm.h"
#include "util/strformat.h"

puce_disassembler::offs_t puce_disassembler::disassemble(std::ostream &stream, offs_t pc, const data_buffer &opcodes, const data_buffer &params)
{
	const u16 op = opcodes.r16(pc);
	const unsigned x = (op >> 4) & 15;
	const unsigned y = op & 15;

	if ((op & 0xe000) == 0)
	{
		// Full-width counter target; level 1/2 uses only the low byte.
		util::stream_format(stream, "SAI %04X", ((pc + 1) & 0xe000) | (op & 0x1fff));
		return 1 | SUPPORTED;
	}
	if ((op & 0xf000) == 0x6000)
	{
		const unsigned condition = (op >> 8) & 15;
		util::stream_format(stream, "SAD%u D%u,C%02X", condition & 1, condition >> 1, op & 255);
		return 1 | SUPPORTED;
	}
	if ((op & 0xf000) == 0x5000 || (op & 0xf000) == 0x7000)
	{
		const char reg = (op & 0x2000) ? 'A' : 'B';
		util::stream_format(stream, "CRT%c %c%u,C%02X", reg, reg, (op >> 8) & 15, op & 255);
		return 1 | SUPPORTED;
	}
	if ((op & 0xff00) == 0xa000)
	{
		util::stream_format(stream, "INCD%u D%u,L%u", x & 1, x >> 1, y);
		return 1 | SUPPORTED;
	}
	if ((op & 0xff00) == 0xc800 || (op & 0xff00) == 0xc900)
	{
		if (op == 0xc900)
			stream << "NOP";
		else
			util::stream_format(stream, "%s C%02X", (op & 0x0100) ? "SEDI" : "REDI", op & 255);
		return 1 | SUPPORTED;
	}
	const char *input = nullptr;
	char input_reg = 'A';
	switch (op & 0xff0f)
	{
	case 0xaa00: input = "ENTL"; input_reg = 'L'; break;
	case 0xb900: input = "ENUA"; break;
	case 0xb20f: input = "ETIB"; input_reg = 'B'; break;
	case 0xb808: input = "EDA"; break;
	case 0xa908: input = "EDB"; input_reg = 'B'; break;
	}
	if (input)
	{
		util::stream_format(stream, "%s %c%u", input, input_reg, x);
		return 1 | SUPPORTED;
	}
	if ((op & 0xff0f) == 0xb104)
	{
		util::stream_format(stream, "ESE %c%u", x < 12 ? 'M' : 'A', x);
		return 1 | SUPPORTED;
	}
	if ((op & 0xff0f) == 0xfc00)
	{
		util::stream_format(stream, "DAE L%u", x);
		return 1 | SUPPORTED;
	}
	if ((op & 0xff0f) == 0xbd00 && x != 2)
	{
		util::stream_format(stream, "COM%u", x);
		return 1 | SUPPORTED;
	}

	const char *mnemonic = nullptr;
	switch (op >> 8)
	{
	case 0x86: mnemonic = "ADD"; break;
	case 0x96: mnemonic = "ADDA"; break;
	case 0xa6: mnemonic = "ADDB"; break;
	case 0xb6: mnemonic = "SOT"; break;
	case 0xc6: mnemonic = "SOTA"; break;
	case 0xd6: mnemonic = "SOTB"; break;
	case 0x97: mnemonic = "AND"; break;
	case 0xa7: mnemonic = "ANDA"; break;
	case 0xb7: mnemonic = "ANDB"; break;
	case 0xe6: mnemonic = "OR";   break;
	case 0xf6: mnemonic = "ORA";  break;
	case 0x87: mnemonic = "ORB";  break;
	case 0xc7: mnemonic = "ORE";  break;
	case 0xd7: mnemonic = "OREA"; break;
	case 0xe7: mnemonic = "OREB"; break;
	case 0xba: mnemonic = "SAB";  break;
	}
	if (mnemonic)
	{
		util::stream_format(stream, "%s A%u,B%u", mnemonic, x, y);
		return 1 | SUPPORTED;
	}

	char reg = 'A';
	switch (op & 0xff0f)
	{
	case 0xc300: mnemonic = "SHDA"; break;
	case 0xd300: mnemonic = "SHDB"; reg = 'B'; break;
	case 0xc400: mnemonic = "SHSA"; break;
	case 0xd400: mnemonic = "SHSB"; reg = 'B'; break;
	case 0xc301: mnemonic = "SLDA"; break;
	case 0xd301: mnemonic = "SLDB"; reg = 'B'; break;
	case 0x8b0f: mnemonic = "ROTA"; break;
	case 0x9b0f: mnemonic = "ROTB"; reg = 'B'; break;
	}
	if (mnemonic)
	{
		util::stream_format(stream, "%s %c%u", mnemonic, reg, x);
		return 1 | SUPPORTED;
	}

	switch (op & 0xff0f)
	{
	case 0xf100: mnemonic = "SEI";  break;
	case 0xfd00: mnemonic = "SEIM"; break;
	case 0xf700: mnemonic = "SEIP"; break;
	}
	if (mnemonic)
	{
		// Short indirect selectors 12..15 address through A, not full L.
		util::stream_format(stream, "%s %c%u", mnemonic, x < 12 ? 'M' : 'A', x);
		return 1 | SUPPORTED;
	}

	util::stream_format(stream, "DW %04X", op);
	return 1 | SUPPORTED;
}
