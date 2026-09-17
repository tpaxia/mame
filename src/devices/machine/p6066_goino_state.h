// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
#ifndef MAME_MACHINE_P6066_GOINO_STATE_H
#define MAME_MACHINE_P6066_GOINO_STATE_H
#pragma once
#include <array>
#include <cstdint>

// Transaction-level GOINO/CONDY subset. See docs/p6066/console.md for limits.
struct p6066_goino_state
{
	bool selected = false;
	std::uint16_t lamp_shift = 0, lamps = 0;
	std::uint8_t lamp_bits = 0;
	std::uint32_t lamp_strobes = 0;
	std::array<std::uint8_t, 224> display{};
	std::uint16_t display_position = 0;
	bool display_ready = false;

	void select(std::uint8_t name) { selected = name == 0; }
	// ESE selection is separate from ECOT data: it must never clock a lamp.
	bool data(std::uint16_t value, unsigned level)
	{
		// Only the documented direct level-4 path is implemented here.
		// Interrupt-owned selection must later be supplied by the bus arbiter.
		if (!selected || level != 4) return true;
		switch (value >> 8)
		{
		case 0x00: return true; // NOPPO
		case 0x40:
			lamp_shift = (lamp_shift << 1) | (value & 1);
			++lamp_strobes;
			lamp_bits = (lamp_bits + 1) & 15;
			if (!lamp_bits) lamps = lamp_shift;
			return true;
		case 0x20:
			if (display_position == 0) display_ready = false;
			display[display_position++] = value;
			if (display_position == display.size())
			{
				display_position = 0;
				display_ready = true;
			}
			return true;
		}
		return false;
	}
};
#endif
