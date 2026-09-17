// license:BSD-3-Clause
// copyright-holders:Salvatore Paxia
#ifndef MAME_CPU_PUCE_PUCEDASM_H
#define MAME_CPU_PUCE_PUCEDASM_H

#pragma once

#include "disasmintf.h"

// CPU19 (PUCE1/PUCE2): addresses and instruction lengths are in 16-bit words.
class puce_disassembler : public util::disasm_interface
{
public:
	virtual u32 opcode_alignment() const override { return 1; }
	virtual offs_t disassemble(std::ostream &stream, offs_t pc, const data_buffer &opcodes, const data_buffer &params) override;
};

#endif // MAME_CPU_PUCE_PUCEDASM_H
