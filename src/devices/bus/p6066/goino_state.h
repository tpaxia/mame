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
	bool printer_attached = false;
	// Discard-output printer: handshake only, no paper/mechanical model.
	bool printer_running = false, printer_feeding = false;
	unsigned printer_columns_left = 0;
	std::uint32_t printer_columns_discarded = 0, printer_feed_events = 0;
	void printer_tick()
	{
		if (!printer_attached) return;
		// Allow each request to be acknowledged and cleared before another.
		if (column_request || matrix_request || synchronized2 || (synchronized3 & 1) || owned2 || owned3) return;
		if (printer_feeding) { matrix_request = true; ++printer_feed_events; }
		else if (printer_running)
		{
			if (printer_columns_left) column_request = true;
			else matrix_request = true;
		}
	}
	// Asynchronous request latches and controls reset by the documented commands.
	// ECM samples source latches; acknowledgement does not clear them.
	bool matrix_request = false, column_request = false, button_request = false;
	bool pippo_request = false, keyboard_request = false, timer_request = false;
	bool double_key_request = false;
	bool pippo_enabled = false, timer_enabled = false, interrupts_blocked = true;
	// Fig.1.2: encoder priority 1 is highest; mode-to-input wiring is qualified.
	// Stored bits 0..5: printer, MODE0 (character ready), timer, buttons,
	// PIPPO, keyboard error. ARDIO routes MODE0 to either encoder input.
	std::uint8_t synchronized3 = 0;
	bool synchronized2 = false, owned2 = false, owned3 = false;
	bool basic_mode = true; // ARDIO is set by RAUNN (printed p.19)
	std::uint16_t keyboard_code = 0;
	std::uint8_t buttons = 0, input_select = 0, printer_column = 0;

	bool enabled(unsigned level) const
	{
		return (level == 4 && selected) || (level == 3 && owned3);
	}
	void synchronize(unsigned mask)
	{
		if (mask & 4) synchronized2 = column_request;
		if (mask & 8)
			synchronized3 = (matrix_request ? 1 : 0)
				| (keyboard_request ? 2 : 0)
				| (timer_request ? 4 : 0) | (button_request ? 8 : 0)
				| (pippo_request ? 16 : 0) | (double_key_request ? 32 : 0);
	}
	unsigned irq_requests() const
	{
		return (synchronized2 ? 2 : 0)
			| (synchronized3 && !interrupts_blocked ? 4 : 0);
	}
	bool acknowledge(unsigned source)
	{
		if (source == 1 && synchronized2 && !owned2) { owned2 = true; return true; }
		if (source == 2 && synchronized3 && !interrupts_blocked && !owned3) { owned3 = true; return true; }
		return false;
	}
	void end(unsigned level)
	{
		if (level == 2) owned2 = false;
		if (level == 3) owned3 = false;
	}
	unsigned type() const
	{
		// Priority encoder inputs 0..6 map to logical EPT4..6 values 0..6.
		// Fig.1.2 bit-order ambiguity is cross-checked with all firmware dispatches.
		constexpr unsigned logical[] = {0x00,0x10,0x20,0x30,0x40,0x50,0x60};
		const unsigned inputs = (synchronized3 & ~2U)
			| ((synchronized3 & 2) ? (basic_mode ? 2 : 64) : 0);
		for (int i = 6; i >= 0; --i) if (inputs & (1U << i)) return logical[i];
		return 0;
	}
	void timer_tick() { if (timer_enabled) timer_request = true; }
	void buttons_w(std::uint8_t pressed)
	{
		// FIC40 is clocked when the encoder changes from no key to a key.
		if (!buttons && pressed) button_request = true;
		buttons = pressed;
	}
	unsigned button_code() const
	{
		// Fig.1.6: TASB, calculator, break, continue, trace, step,
		// print all, no print. Fig.1.6 gives active-low CON0N..2N;
		// convert their electrical code to logical CPU input polarity.
		for (unsigned i = 0; i < 8; ++i) if (buttons & (1U << i)) return 7-i;
		return 0;
	}
	unsigned key_data() const
	{
		// TAS1..8 are active-low; complementing both compared inputs
		// preserves equality.
		return (keyboard_code & 255)
			^ ((basic_mode && ((keyboard_code >> 6) & 1) == ((keyboard_code >> 8) & 1)) ? 0x20 : 0);
	}
	std::uint16_t commands_seen = 0; // diagnostic, not a hardware register

	bool requests_pending() const
	{
		return matrix_request || column_request || button_request || pippo_request
			|| keyboard_request || timer_request || double_key_request;
	}

	bool command(unsigned code)
	{
		// GOINO description, printed pp.5,9,11,14-16: ECD8-ECDB alone
		// select the command. Upper bits belong to separate data/input logic.
		switch (code)
		{
		case 0x0: break; // NOPPO
		case 0xf: // Functional ESE print-transfer start; see discard-printer.md.
			if (printer_attached) { printer_running = true; printer_columns_left = 3; }
			break;
		case 0x2: if (printer_attached) printer_feeding = true; break; // FAINN
		case 0x3: if (printer_attached) printer_feeding = false; break; // FINTN
		case 0x1: if (printer_attached) printer_running = false; break; // ESE ends column transfer
		case 0x4: // REMAN acknowledges matrix or line-feed event.
			matrix_request = false;
			if (printer_attached && printer_running && !printer_feeding) printer_columns_left = 7;
			break;
		case 0x5: column_request = button_request = false; break; // RECON
		case 0x6: pippo_request = false; break; // REPIN
		case 0x7: keyboard_request = false; break; // RECAN / UTCAN
		case 0x8: timer_request = false; break; // RETIN: FIC30
		case 0x9: double_key_request = false; break; // REDBN / RESIN
		case 0xc: timer_enabled = true; break; // TIMEN: VTIMO
		case 0xb: pippo_enabled = false; break; // FPIPN: DIRTO
		case 0xd: timer_enabled = false; break; // FTIMN: VTIMO
		case 0xe: interrupts_blocked = false; break; // SASPN: ASPEO
		default: return false; // PIPPO start still absent
		}
		commands_seen |= 1U << code;
		return true;
	}
	std::uint16_t lamp_shift = 0, lamps = 0;
	std::uint16_t lamp_known_shift = 0xffff, lamps_known = 0xffff;
	std::uint8_t lamp_bits = 0;
	std::uint32_t lamp_strobes = 0;
	bool running_lamp(bool blink_on) const { return (lamps & 0x80) || blink_on; }
	std::uint32_t display_strobes = 0; // diagnostic, not a hardware register
	std::array<std::uint8_t, 224> display{};
	std::array<std::uint8_t, 224> display_known{};
	std::uint16_t display_position = 0;
	bool display_ready = false;

	void select(std::uint8_t name) { selected = name == 0; }
	// ESE selection is separate from ECOT data: it must never clock a lamp.
	bool data(std::uint16_t value, unsigned level, std::uint16_t mask = 0xffff)
	{
		// Printed p.16: PASSO qualifies the level-2 column buffer and ECOT
		// clears FICIO. It does not decode a level-3/4 command.
		if (level == 2 && owned2)
		{
			printer_column = value & 0x7f;
			if (printer_attached)
			{
				if (printer_columns_left) --printer_columns_left;
				++printer_columns_discarded;
			}
			column_request = false;
			return true;
		}
		// SEL4 is suppressed by higher levels; SEL3 follows this board's grant.
		if (!enabled(level)) return true;
		input_select = (value >> 12) & 3;
		if (!command((value >> 8) & 0x0f)) return false;
		// Fig.1.3 specifies 20xx -> DISPN/NOPPO and 40xx -> PULSN/NOPPO.
		// A set ECDD/ECDE bit alone does not establish a data transaction:
		// e.g. F4xx is REMAN, not a display/lamp transfer. Model the
		// documented destinations; undocumented strobe aliases are unverified.
		const unsigned destination = value >> 8;
		if (destination == 0x40)
		{
			lamp_shift = (lamp_shift << 1) | (value & 1);
			lamp_known_shift = (lamp_known_shift << 1) | (mask & 1);
			++lamp_strobes;
			lamp_bits = (lamp_bits + 1) & 15;
			if (!lamp_bits) { lamps = lamp_shift; lamps_known = lamp_known_shift; }
		}
		if (destination == 0x20)
		{
			++display_strobes;
			if (display_position == 0) display_ready = false;
			display_known[display_position] = mask;
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
