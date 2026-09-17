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
	bool ecorn = false; // active-low external controller reset
	std::uint8_t active = 0x18; // base + reset's level 3 context

	std::uint8_t a(unsigned r) const { return l[r] & 0xff; }
	std::uint8_t b(unsigned r) const { return l[r] >> 8; }
	void set_a(unsigned r, std::uint8_t v) { l[r] = (l[r] & 0xff00) | v; }
	void set_b(unsigned r, std::uint8_t v) { l[r] = (l[r] & 0x00ff) | (std::uint16_t(v) << 8); }
	unsigned pc_register() const { return level == 4 ? 0 : level == 3 ? 1 : level == 2 ? 13 : 12; }
	std::uint16_t pc() const { return level >= 3 ? l[pc_register()] : a(pc_register()); }
	void set_pc(std::uint16_t v)
	{
		if (level >= 3) l[pc_register()] = v;
		else set_a(pc_register(), v);
	}
	void advance() { set_pc(pc() + 1); }
	std::uint16_t indirect(unsigned r) const { return r < 12 ? l[r] : a(r); }

	// RESE establishes the level-3 entry. Other scratchpad/DI reset values
	// remain unverified: preserve them on reset rather than fabricate clearing.
	void reset() { l[1] = 0x8000; level = 3; active = 0x18; ecorn = false; }
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

	// Read-only external buses, in logical CPU bit polarity. No ECOT strobe.
	bool execute_input(std::uint16_t op, std::uint16_t name_type, std::uint8_t data)
	{
		const unsigned x = (op >> 4) & 15;
		switch (op & 0xff0f)
		{
		case 0xaa00: l[x] = name_type; return true; // ENTL
		case 0xb900: set_a(x, name_type); return true; // ENUA
		case 0xb20f: set_b(x, name_type >> 8); return true; // ETIB
		case 0xb808: set_a(x, data); return true; // EDA
		case 0xa908: set_b(x, data); return true; // EDB
		}
		return false;
	}

	// Called AFTER ALFA has advanced the selected scratchpad counter.
	// false means unimplemented; the caller must stop or handle memory/I/O.
	bool execute_register(std::uint16_t op)
	{
		const unsigned x = (op >> 4) & 15, y = op & 15, hi = op >> 8;
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
		if (op == 0xbd10) { if (level == 4) enter_level(3); return true; }

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
};

#endif // MAME_CPU_PUCE_PUCE_STATE_H
