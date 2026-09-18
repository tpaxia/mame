// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia

// CPU19/CPU19M canonical instruction disassembler.
// Source: Olivetti CPU19 Tabella Microistruzioni, publication 801.30.1,
// V2 PDF pp.1-14 and CPU19M supplement PDF pp.83,85 (3.09-3.10).
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

	// CPU19M additions are selected explicitly; they are not CPU19 opcodes.
	if (m_cpu19m)
	{
		if ((op >> 8) == 0xce)
		{
			util::stream_format(stream, "ADLL L%u,L%u", x, y);
			return 1 | SUPPORTED;
		}
		if ((op >> 8) == 0xa4)
		{
			util::stream_format(stream, "INC2%u D%u,L%u", x & 1, x >> 1, y);
			return 1 | SUPPORTED;
		}
		if (op == 0xbd20 || op == 0xbd21)
		{
			stream << (op == 0xbd20 ? "INTON" : "INTOF");
			return 1 | SUPPORTED;
		}
		struct entry { unsigned code; const char *name; char reg; };
		static constexpr entry extensions[] = {
			{0xfe0f,"AZL",'L'}, {0xb50f,"COINA",'A'}, {0xe30f,"COINB",'B'},
			{0x800f,"COMPA",'A'}, {0x810f,"COMPB",'B'}, {0x840f,"COMPL",'L'},
			{0xcd08,"DEL",'L'}, {0xf408,"EDAT",'A'}, {0xf208,"EDBT",'B'},
			{0xaa08,"EDTL",'L'}, {0xf301,"SLDL",'L'}, {0xdc01,"SLSL",'L'},
			{0xac0f,"ZMB",'M'}, {0xc10f,"ZMW",'M'}
		};
		for (const auto &e : extensions)
			if ((op & 0xff0f) == e.code)
			{
				util::stream_format(stream, "%s %c%u", e.name, e.reg == 'M' && x >= 12 ? 'A' : e.reg, x);
				return 1 | SUPPORTED;
			}
	}
	if ((op >> 12) == 4)
	{
		util::stream_format(stream, "SADE C%02X", op & 255);
		return 1 | SUPPORTED;
	}

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
	const char *word_transfer = nullptr;
	switch (op >> 8)
	{
	case 0xd1: word_transfer = "MLI"; break;
	case 0xdd: word_transfer = "MLIM"; break;
	case 0xde: word_transfer = "MLIP"; break;
	case 0xe1: word_transfer = "LMI"; break;
	case 0xed: word_transfer = "LMIM"; break;
	case 0xee: word_transfer = "LMIP"; break;
	case 0xe2: word_transfer = "LPMIP"; break;
	}
	if (word_transfer)
	{
		util::stream_format(stream, "%s %c%u,L%u", word_transfer, x < 12 ? 'M' : 'A', x, y);
		return 1 | SUPPORTED;
	}
	const char *input = nullptr;
	char input_reg = 'A';
	switch ((op & 0xff00) == 0xb200 ? 0xb20f : (op & 0xff0f))
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

	if ((op >> 12) == 2 || (op >> 12) == 3)
	{
		util::stream_format(stream, "%s A%u,C%02X", (op >> 12) == 2 ? "AMD" : "MAD", (op >> 8) & 15, op & 255);
		return 1 | SUPPORTED;
	}
	const char *byte_transfer = nullptr;
	char byte_bank = 'A';
	switch (op >> 8)
	{
	case 0xa8: byte_transfer = "AMI"; break;
	case 0x82: byte_transfer = "AMIM"; break;
	case 0x88: byte_transfer = "AMIP"; break;
	case 0x89: byte_transfer = "BMI"; byte_bank = 'B'; break;
	case 0x8a: byte_transfer = "BMIM"; byte_bank = 'B'; break;
	case 0x8c: byte_transfer = "BMIP"; byte_bank = 'B'; break;
	case 0x91: byte_transfer = "MAI"; break;
	case 0x92: byte_transfer = "MAIM"; break;
	case 0x98: byte_transfer = "MAIP"; break;
	case 0x99: byte_transfer = "MBI"; byte_bank = 'B'; break;
	case 0x9a: byte_transfer = "MBIM"; byte_bank = 'B'; break;
	case 0x9c: byte_transfer = "MBIP"; byte_bank = 'B'; break;
	}
	if (byte_transfer)
	{
		util::stream_format(stream, "%s %c%u,%c%u", byte_transfer, x < 12 ? 'M' : 'A', x, byte_bank, y);
		return 1 | SUPPORTED;
	}
	if ((op >> 8) == 0xbc)
	{
		util::stream_format(stream, "SLL L%u,L%u", x, y);
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
	case 0xab0f: mnemonic = "AZAM"; break;
	case 0xbb0f: mnemonic = "AZAP"; break;
	case 0xcb0f: mnemonic = "AZBM"; reg = 'B'; break;
	case 0xdb0f: mnemonic = "AZBP"; reg = 'B'; break;
	case 0x850f: mnemonic = "ICA"; break;
	case 0x950f: mnemonic = "ICB"; reg = 'B'; break;
	case 0xbe0f: mnemonic = "DCB"; reg = 'B'; break;
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

	// Remaining CPU19 canonical encodings, including service-console I/O.
	struct entry { unsigned code; const char *name; char reg; };
	static constexpr entry singles[] = {
		{0xfc02,"CAE",'L'}, {0xfb08,"DEA",'L'}, {0xad0f,"EDC",'L'},
		{0xae0f,"DCA",'A'}, {0xe50f,"DCL",'L'}, {0xa50f,"ICL",'L'},
		{0xa30f,"SDIA",'A'}, {0xb30f,"SDIB",'B'},
		{0xc401,"SLSA",'A'}, {0xd401,"SLSB",'B'},
		{0x830f,"TADI",'A'}, {0x930f,"TBDI",'B'},
		{0xca00,"TCCA",'A'}, {0xc50f,"TDIA",'A'}, {0xd50f,"TDIB",'B'},
		{0xda01,"TDMA",'A'}, {0xea02,"TDPA",'A'},
		{0x8e0f,"VRA",'A'}, {0x9e0f,"VRB",'B'}, {0xf50f,"VRL",'L'},
		{0xb402,"ECO",'M'}, {0x8d08,"EMI",'M'}, {0xa108,"EMIM",'M'},
		{0xa208,"EMIP",'M'}, {0xe008,"ESI",'M'}, {0xec08,"ESIM",'M'},
		{0xeb08,"ESIP",'M'}, {0x9000,"MEI",'M'}, {0x9d00,"MEIM",'M'},
		{0x9400,"MEIP",'M'}
	};
	for (const auto &e : singles)
		if ((op & 0xff0f) == e.code)
		{
			util::stream_format(stream, "%s %c%u", e.name, e.reg == 'M' && x >= 12 ? 'A' : e.reg, x);
			return 1 | SUPPORTED;
		}
	switch (op >> 8)
	{
	case 0xd8: mnemonic = "TAB"; break;
	case 0xfa: mnemonic = "TABC"; break;
	case 0xe8: mnemonic = "TABM"; break;
	case 0xd9: mnemonic = "TABP"; break;
	case 0xe9: mnemonic = "TBA"; break;
	case 0xf9: mnemonic = "TBAM"; break;
	case 0xf8: mnemonic = "TBAP"; break;
	}
	if (mnemonic)
	{
		util::stream_format(stream, "%s A%u,B%u", mnemonic, x, y);
		return 1 | SUPPORTED;
	}

	util::stream_format(stream, "DW %04X", op);
	return 1 | SUPPORTED;
}
