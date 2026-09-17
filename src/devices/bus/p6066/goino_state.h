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
	// Asynchronous request latches and controls reset by the documented commands.
	// Event producers and synchronized IRQ delivery are not implemented yet.
	bool matrix_request = false, column_request = false, button_request = false;
	bool pippo_request = false, keyboard_request = false, timer_request = false;
	bool double_key_request = false;
	bool pippo_enabled = false, timer_enabled = false, interrupts_blocked = true;
	std::uint16_t commands_seen = 0; // diagnostic, not a hardware register

	bool command(unsigned code)
	{
		// GOINO description, printed pp.5,9,11,14-16: ECD8-ECDB alone
		// select the command. Upper bits belong to separate data/input logic.
		switch (code)
		{
		case 0x0: break; // NOPPO
		case 0x4: matrix_request = false; break; // REMAN: FIT20
		case 0x5: column_request = button_request = false; break; // RECON
		case 0x6: pippo_request = false; break; // REPIN
		case 0x7: keyboard_request = false; break; // RECAN / UTCAN
		case 0x8: timer_request = false; break; // RETIN: FIC30
		case 0x9: double_key_request = false; break; // REDBN / RESIN
		case 0xb: pippo_enabled = false; break; // FPIPN: DIRTO
		case 0xd: timer_enabled = false; break; // FTIMN: VTIMO
		case 0xe: interrupts_blocked = false; break; // SASPN: ASPEO
		default: return false; // printer motion and event generation still absent
		}
		commands_seen |= 1U << code;
		return true;
	}
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
		if (!command((value >> 8) & 0x0f)) return false;
		// Printed p.13: ECDD and ECDE are independent display/lamp
		// strobes. Do not make their decoding select or suppress a command.
		// This remains transaction-level: gate/pulse timing is not modelled.
		if (value & 0x4000)
		{
			lamp_shift = (lamp_shift << 1) | (value & 1);
			++lamp_strobes;
			lamp_bits = (lamp_bits + 1) & 15;
			if (!lamp_bits) lamps = lamp_shift;
		}
		if (value & 0x2000)
		{
			if (display_position == 0) display_ready = false;
			display[display_position++] = value;
			if (display_position == display.size())
			{
				display_position = 0;
				display_ready = true;
			}
		}
		return true;
	}
};
#endif
