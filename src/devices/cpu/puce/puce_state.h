// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
#ifndef MAME_CPU_PUCE_PUCE_STATE_H
#define MAME_CPU_PUCE_PUCE_STATE_H

#pragma once

#include <array>
#include <cstdint>

// Live architectural state, shared by the MAME CPU and ROM-free tests.
// No memory/peripheral implementation or clock assumptions live here.
struct puce_state
{
	std::array<std::uint16_t, 16> l{};
	std::uint8_t di = 0;
	std::uint8_t level = 3;
	// CPU19M 801.30.1 (03), p.3.14: internal level-3 requests
	// drive EX01; COM1 (ICOON) supplies name 02, INV supplies 03.
	std::uint8_t internal_name = 0;
	bool com1_pending = false; // software request; accepted at the next eligible ALFA
	bool ecorn = false; // active-low external controller reset
	// CPU19M, 801.30.1 (03) p.3.12: UC020 L03 has separate reset
	// and level-1/2 page selectors. These are configuration, not registers.
	std::uint16_t reset_base = 0x8000;
	std::uint16_t interrupt_base = 0x8000;
	std::uint8_t active = 0x18; // base + reset's level 3 context
	bool cpu19m = false; // CPU19M supplement 801.30.1 (03), pp.3.09-3.10
	bool inhibit_level3 = false;

	std::uint8_t a(unsigned r) const { return l[r] & 0xff; }
	std::uint8_t b(unsigned r) const { return l[r] >> 8; }
	void set_a(unsigned r, std::uint8_t v) { l[r] = (l[r] & 0xff00) | v; }
	void set_b(unsigned r, std::uint8_t v) { l[r] = (l[r] & 0x00ff) | (std::uint16_t(v) << 8); }
	unsigned pc_register() const { return level == 4 ? 0 : level == 3 ? 1 : level == 2 ? 13 : 12; }
	std::uint16_t pc() const { return level >= 3 ? l[pc_register()] : interrupt_base | (std::uint16_t(level) << 8) | a(pc_register()); }
	void set_pc(std::uint16_t v)
	{
		if (level >= 3) l[pc_register()] = v;
		else set_a(pc_register(), v);
	}
	void advance() { set_pc(pc() + 1); }
	std::uint16_t indirect(unsigned r) const { return r < 12 ? l[r] : a(r); }
	// Bus arbiter returns requests strictly above this level. CPU19M INTOF
	// masks external 3A/3B, not level 1/2 or internal COM1/INV.
	unsigned external_irq_poll_level() const { return inhibit_level3 && level == 4 ? 3 : level; }

	// RESE establishes the level-3 entry. Other scratchpad/DI reset values
	// remain unverified: preserve them on reset rather than fabricate clearing.
	void reset() { l[1] = reset_base; level = 3; active = 0x18; ecorn = false; internal_name = 0; com1_pending = false; }
	bool enter_level(unsigned next)
	{
		if (next < 1 || next >= level) return false;
		active |= 1U << next;
		level = next;
		return true;
	}
	void leave_level()
	{
		if (level == 4) return;
		if (level == 3) internal_name = 0;
		active &= ~(1U << level);
		do { ++level; } while (!(active & (1U << level)));
	}
	void zero(bool value) { di = (di & ~2U) | (value ? 2 : 0); }

	// CPU byte addresses: even = high byte, odd = low byte (CPU19 p.9).
	static unsigned byte_shift(std::uint16_t address) { return (address & 1) ? 0 : 8; }
	static std::uint16_t byte_mask(std::uint16_t address) { return 0xffU << byte_shift(address); }

	// Word transfers use a latched, unscaled word address. Index adjustment
	// precedes the data phase, so aliased stores see the updated register and
	// aliased loads overwrite the updated index. LPMIP adds one on the output
	// path; it does not increment the source register a second time.
	template <typename Read, typename Write>
	bool execute_word(std::uint16_t op, Read &&read, Write &&write)
	{
		const unsigned hi = op >> 8, x = (op >> 4) & 15, y = op & 15;
		if (cpu19m && (op & 0xff0f) == 0xc10f)
		{
			write(indirect(x), std::uint16_t(0)); // ZMW, word address
			return true;
		}
		if (hi != 0xd1 && hi != 0xdd && hi != 0xde && hi != 0xe1 && hi != 0xed && hi != 0xee && hi != 0xe2)
			return false;
		const std::uint16_t address = indirect(x);
		const int adjustment = (hi == 0xdd || hi == 0xed) ? -1
			: (hi == 0xde || hi == 0xee || hi == 0xe2) ? 1 : 0;
		if (adjustment)
		{
			if (x < 12) l[x] += adjustment;
			else set_a(x, a(x) + adjustment);
		}
		if (hi == 0xd1 || hi == 0xdd || hi == 0xde)
			l[y] = read(address);
		else
			write(address, std::uint16_t(l[y] + (hi == 0xe2 ? 1 : 0)));
		return true;
	}

	// Byte callbacks take byte addresses. Like word transfers, updated indexes
	// are visible during the data phase; direct transfers address the low 256 bytes.
	template <typename Read, typename Write>
	bool execute_byte(std::uint16_t op, Read &&read, Write &&write)
	{
		const unsigned hi = op >> 8, x = (op >> 4) & 15, y = op & 15;
		if (cpu19m && (op & 0xff0f) == 0xac0f)
		{
			write(indirect(x), std::uint8_t(0)); // ZMB, byte address
			return true;
		}
		if ((op >> 12) == 2) { write(op & 255, a((op >> 8) & 15)); return true; }
		if ((op >> 12) == 3) { set_a((op >> 8) & 15, read(op & 255)); return true; }
		bool store, bank_b;
		int adjustment = 0;
		switch (hi)
		{
		case 0xa8: store = true; bank_b = false; break;
		case 0x82: store = true; bank_b = false; adjustment = -1; break;
		case 0x88: store = true; bank_b = false; adjustment = 1; break;
		case 0x89: store = true; bank_b = true; break;
		case 0x8a: store = true; bank_b = true; adjustment = -1; break;
		case 0x8c: store = true; bank_b = true; adjustment = 1; break;
		case 0x91: store = false; bank_b = false; break;
		case 0x92: store = false; bank_b = false; adjustment = -1; break;
		case 0x98: store = false; bank_b = false; adjustment = 1; break;
		case 0x99: store = false; bank_b = true; break;
		case 0x9a: store = false; bank_b = true; adjustment = -1; break;
		case 0x9c: store = false; bank_b = true; adjustment = 1; break;
		default: return false;
		}
		const std::uint16_t address = indirect(x);
		if (x < 12) l[x] += adjustment;
		else set_a(x, a(x) + adjustment);
		if (store) write(address, bank_b ? b(y) : a(y));
		else if (bank_b) set_b(y, read(address));
		else set_a(y, read(address));
		return true;
	}

	// Read-only external buses, in logical CPU bit polarity. No ECOT strobe.
	bool execute_input(std::uint16_t op, std::uint16_t name_type, std::uint8_t data)
	{
		const unsigned x = (op >> 4) & 15;
		if (level == 3 && internal_name)
			name_type = (name_type & 0xff00) | internal_name;
		// ETIB: CPU19 V2 p.6 gives CROM 7FDF, TROM 1C, VROM 06.
		// RB is selected by RO4..7; only RB is written. RO0..3 is unused
		// (US4032895 register selectors and TROM write enables). The manual
		// prints canonical B2xF, but B2xy executes the same transfer.
		if ((op & 0xff00) == 0xb200) { set_b(x, name_type >> 8); return true; }
		// V2 p.5: EDA BFFF:13 and EDB 7FFF:1C write only Rx.
		// RO3 selects EPD; RO0..2 affect no enabled path (US4032895
		// cols.13-14,20,31,33). Accept all eight data-input aliases.
		if ((op & 0xff08) == 0xb808) { set_a(x, data); return true; }
		if ((op & 0xff08) == 0xa908) { set_b(x, data); return true; }
		switch (op & 0xff0f)
		{
		case 0xaa00: l[x] = name_type; return true; // ENTL
		case 0xb900: set_a(x, name_type); return true; // ENUA
		}
		return false;
	}

	// Called AFTER ALFA has advanced the selected scratchpad counter.
	// false means unimplemented; the caller must stop or handle memory/I/O.
	bool execute_register(std::uint16_t op)
	{
		const unsigned x = (op >> 4) & 15, y = op & 15, hi = op >> 8;
		if (cpu19m && execute_register_m(op)) return true;
		const std::uint8_t av = a(x), bv = b(y);
		if ((op & 0xe000) == 0)
		{
			set_pc((pc() & 0xe000) | (op & 0x1fff)); // SAI, 13-bit destination
			return true;
		}
		if ((op & 0xf000) == 0x6000)
		{
			if (((di >> ((hi & 15) >> 1)) & 1) == (hi & 1))
				set_pc((pc() & 0xff00) | (op & 255));
			return true;
		}
		if ((op & 0xf000) == 0x5000) { set_b(hi & 15, op); return true; }
		if ((op & 0xf000) == 0x7000) { set_a(hi & 15, op); return true; }
		if (hi == 0xc8) { di &= ~(op & 255); return true; }
		if (hi == 0xc9) { di |= op & 255; return true; }
		if (hi == 0xa0)
		{
			if (((di >> (x >> 1)) & 1) == (x & 1)) ++l[y];
			return true;
		}
		if (op == 0xbd00) { if (level != 4) { ecorn = true; leave_level(); } return true; }
		if (op == 0xbd30) { ecorn = false; return true; }
		if (op == 0xbd10) { if (level == 4) com1_pending = true; return true; }

		if (hi == 0x86 || hi == 0x96 || hi == 0xa6 || hi == 0xb6 || hi == 0xc6 || hi == 0xd6)
		{
			// SOT uses A + complement(B) + old DI0, i.e. A - B - !DI0.
			// DI2 is carry from the low nibble, not signed overflow.
			const unsigned rhs = hi >= 0xb6 ? (bv ^ 0xff) : bv;
			const unsigned carry = di & 1;
			const unsigned sum = av + rhs + carry;
			const std::uint8_t result = sum;
			di = (di & 0xf8) | (sum > 255 ? 1 : 0) | (result == 0 ? 2 : 0)
				| ((av & 15) + (rhs & 15) + carry > 15 ? 4 : 0);
			if (hi == 0x96 || hi == 0xc6) set_a(x, result);
			if (hi == 0xa6 || hi == 0xd6) set_b(y, result);
			return true;
		}

		std::uint8_t value;
		switch (hi)
		{
		case 0x97: case 0xa7: case 0xb7:
			value = av & bv; zero(value == 0);
			if (hi == 0xa7) set_a(x, value);
			if (hi == 0xb7) set_b(y, value);
			return true;
		case 0xe6: case 0xf6: case 0x87:
			value = av | bv; zero(value == 0);
			if (hi == 0xf6) set_a(x, value);
			if (hi == 0x87) set_b(y, value);
			return true;
		case 0xc7: case 0xd7: case 0xe7:
			value = av ^ bv; zero(value == 0);
			if (hi == 0xd7) set_a(x, value);
			if (hi == 0xe7) set_b(y, value);
			return true;
		case 0xba: set_a(x, bv); set_b(y, av); return true;
		case 0xbc:
			// SLL is two ordered cross-half exchanges, not a plain word swap.
			// With x == y the second exchange undoes the first (V2 p.12).
			set_a(x, bv); set_b(y, av);
			value = a(y); set_a(y, b(x)); set_b(x, value);
			return true;
		case 0xd8: set_b(y, av); return true;
		case 0xe9: set_a(x, bv); return true;
		case 0xe8: set_b(y, (bv & 0xf0) | (av & 15)); return true;
		case 0xd9: set_b(y, (bv & 15) | (av & 0xf0)); return true;
		case 0xf9: set_a(x, (av & 0xf0) | (bv & 15)); return true;
		case 0xf8: set_a(x, (av & 15) | (bv & 0xf0)); return true;
		}

		const auto old_di = di;
		switch (op & 0xff0f)
		{
		case 0xa30f: di = a(x); set_a(x, old_di); return true;
		case 0xb30f: di = b(x); set_b(x, old_di); return true;
		case 0x830f: di = a(x); return true;
		case 0x930f: di = b(x); return true;
		case 0xc50f: set_a(x, di); return true;
		case 0xd50f: set_b(x, di); return true;
		case 0xab0f: set_a(x, a(x) & 0xf0); return true;
		case 0xbb0f: set_a(x, a(x) & 0x0f); return true;
		case 0xcb0f: set_b(x, b(x) & 0xf0); return true;
		case 0xdb0f: set_b(x, b(x) & 0x0f); return true;
		case 0x850f: set_a(x, a(x) + 1); return true;
		case 0x950f: set_b(x, b(x) + 1); return true;
		case 0xbe0f: set_b(x, b(x) - 1); zero(b(x) == 0); return true;
		case 0xa50f: ++l[x]; return true; // ICL does not update DI
		case 0xe50f: --l[x]; zero(l[x] == 0); return true;
		case 0xae0f: set_a(x, a(x) - 1); zero(a(x) == 0); return true;
		case 0x8e0f: zero(a(x) == 0); return true;
		case 0x9e0f: zero(b(x) == 0); return true;
		case 0xf50f: zero(l[x] == 0); return true;
		case 0x8b0f: set_a(x, (a(x) << 4) | (a(x) >> 4)); return true;
		case 0x9b0f: set_b(x, (b(x) << 4) | (b(x) >> 4)); return true;
		}
		if ((hi == 0xc3 || hi == 0xd3 || hi == 0xc4 || hi == 0xd4) && y <= 1)
		{
			const bool bank_b = hi & 0x10;
			const bool left = (hi & 15) == 4;
			value = bank_b ? b(x) : a(x);
			const unsigned incoming = y ? (di & 1) : 0;
			di = (di & ~1U) | (left ? (value >> 7) : (value & 1));
			value = left ? (value << 1) | incoming : (value >> 1) | (incoming << 7);
			if (bank_b) set_b(x, value); else set_a(x, value);
			return true;
		}
		return false;
	}

	// CPU19M supplement, detailed tables pp.3.09-3.10. In particular,
	// ADLL is NOT an ordinary addition of Lx and Ly: the source halves
	// cross and beta 2 sees beta 1's result when the operands alias.
	bool execute_register_m(std::uint16_t op)
	{
		const unsigned x = (op >> 4) & 15, y = op & 15;
		if (op == 0xbd20 || op == 0xbd21)
		{
			inhibit_level3 = op & 1;
			return true;
		}
		if ((op >> 8) == 0xce)
		{
			const unsigned low = a(x) + b(y) + (di & 1);
			set_a(x, low);
			const unsigned high_a = a(y), high_b = b(x), carry = low >> 8;
			const unsigned high = high_a + high_b + carry;
			set_b(x, high);
			di = (di & 0xf8) | (high >> 8) | (b(x) == 0 ? 2 : 0)
				| ((high_a & 15) + (high_b & 15) + carry > 15 ? 4 : 0);
			return true;
		}
		if ((op >> 8) == 0xa4)
		{
			if (((di >> (x >> 1)) & 1) == (x & 1)) l[y] += 2;
			return true;
		}
		switch (op & 0xff0f)
		{
		case 0xfe0f: l[x] = 0; return true;
		case 0xb50f: set_a(x, -a(x)); return true;
		case 0xe30f: set_b(x, -b(x)); return true;
		case 0x800f: set_a(x, ~a(x)); return true;
		case 0x810f: set_b(x, ~b(x)); return true;
		case 0x840f: l[x] = ~l[x]; return true;
		case 0xf301:
		{
			const auto old = l[x];
			l[x] = (old >> 1) | (std::uint16_t(di & 1) << 15);
			di = (di & 0xfe) | (old & 1);
			return true;
		}
		case 0xdc01:
		{
			const auto old = l[x];
			l[x] = (old << 1) | (di & 1);
			di = (di & 0xfe) | (old >> 15);
			return true;
		}
		}
		return false;
	}

	// Transaction-level channel instruction semantics. The adapter owns the
	// electrical bus and attached devices. output/command/select include their
	// documented ECOT; input-only transactions call strobe explicitly.
	// A mask describes the ECD lanes specified by the instruction table. It
	// makes no electrical claim about the unspecified half of the bus.
	template <typename Io>
	bool execute_channel(std::uint16_t op, Io &io)
	{
		const unsigned x = (op >> 4) & 15, code = op & 0xff0f;
		if ((op >> 12) == 4)
		{
			if (io.ecof()) set_pc((pc() & 0xff00) | (op & 255));
			return true;
		}
		if ((op >> 8) == 0xfa)
		{
			io.console_output((std::uint16_t(b(op & 15)) << 8) | a(x));
			return true;
		}
		if (code == 0xca00 || code == 0xda01 || code == 0xea02)
		{
			set_a(x, io.console_input(code == 0xca00 ? 0 : code == 0xda01 ? 1 : 2));
			return true;
		}
		if ((op & 0xff0f) == 0xbd00 && x >= 4)
		{
			if (x == 4) io.strobe();
			else if (x >= 11) io.console_control(x);
			else io.control(x);
			return true;
		}
		if (code == 0xad0f)
		{
			l[x] = (l[x] & 0xf000) | ((l[x] - 1) & 0xfff);
			if (!(l[x] & 0xfff)) io.control(0); // ECOF
			return true;
		}
		if (code == 0xfc00 || code == 0xfc02)
		{
			if (code == 0xfc02) io.command(l[x], 0xffff);
			else io.output(l[x], 0xffff);
			return true;
		}
		if (code == 0xfb08 || (cpu19m && code == 0xcd08))
		{
			// DEA/DEL output in beta 1, sample input in beta 2.
			io.output(std::uint16_t(b(x)) << 8, 0xff00);
			const std::uint8_t data = io.input();
			if (code == 0xcd08) l[x] = (io.name_type() & 0xff00) | data;
			else set_a(x, data);
			return true;
		}
		if (cpu19m && (code == 0xf408 || code == 0xf208 || code == 0xaa08))
		{
			const std::uint8_t data = io.input();
			if (code == 0xaa08) l[x] = (io.name_type() & 0xff00) | data;
			else
			{
				if (code == 0xf408) set_a(x, data); else set_b(x, data);
				io.strobe();
			}
			return true;
		}
		if ((op & 0xff00) == 0xb200 || code == 0xaa00 || code == 0xb900)
			return execute_input(op, io.name_type(), 0);
		if ((op & 0xff08) == 0xb808 || (op & 0xff08) == 0xa908)
			return execute_input(op, 0, io.input());
		if (code == 0xb402 || code == 0xb104)
		{
			const auto value = io.read_byte(indirect(x));
			if (code == 0xb402) io.command(value, 0x00ff); else io.select(value);
			return true;
		}
		bool input, word;
		int adjustment;
		switch (code)
		{
		case 0x8d08: input = true;  word = false; adjustment = 0; break;
		case 0xa108: input = true;  word = false; adjustment = -1; break;
		case 0xa208: input = true;  word = false; adjustment = 1; break;
		case 0xe008: input = true;  word = true;  adjustment = 0; break;
		case 0xec08: input = true;  word = true;  adjustment = -1; break;
		case 0xeb08: input = true;  word = true;  adjustment = 1; break;
		case 0x9000: input = false; word = false; adjustment = 0; break;
		case 0x9d00: input = false; word = false; adjustment = -1; break;
		case 0x9400: input = false; word = false; adjustment = 1; break;
		case 0xf100: input = false; word = true;  adjustment = 0; break;
		case 0xfd00: input = false; word = true;  adjustment = -1; break;
		case 0xf700: input = false; word = true;  adjustment = 1; break;
		default: return false;
		}
		const std::uint16_t address = indirect(x);
		if (x < 12) l[x] += adjustment; else set_a(x, a(x) + adjustment);
		if (input)
		{
			const std::uint8_t data = io.input();
			if (word) io.write_word(address, (io.name_type() & 0xff00) | data);
			else io.write_byte(address, data);
			io.strobe();
		}
		else if (word) io.output(io.read_word(address), 0xffff);
		else io.output(io.read_byte(address), 0x00ff);
		return true;
	}
};

#endif // MAME_CPU_PUCE_PUCE_STATE_H
