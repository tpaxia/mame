// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
#ifndef MAME_BUS_P6066_GO011_STATE_H
#define MAME_BUS_P6066_GO011_STATE_H
#pragma once
#include <array>
#include <cstdint>

// Experimental, evidence-bounded GO011 storage transactions. This is NOT a
// complete display controller. See docs/p6066/go011.md for evidence and limits.
// "known" flags are emulator guards, not claims about physical validity bits.
struct p6066_go011_state
{
	// Software raster stride and lower-row tables, corroborated by the
	// graphics manual's 392-line page plus two service rows. Diagnostic
	// scanout only: control-dependent blanking/reversal is not recovered.
	static constexpr unsigned visible_width = 560;
	static constexpr unsigned visible_height = 410;
	static constexpr unsigned row_bytes = visible_width / 8;
	static constexpr unsigned page_bytes = 392 * row_bytes;
	enum class result { ok, lanes, address_range, address_unknown, unwritten, operation };
	std::array<std::uint8_t, 0x8000> ram{};
	std::array<std::uint8_t, 0x8000> written{};
	std::uint16_t address = 0;
	bool address_known = false;
	bool selected = false;
	bool reset_held = false;
	// Functional control model inferred from original command sequences.
	bool inverted = false;
	bool blanked = false;
	std::uint8_t access_mode = 0;
	std::uint32_t control_writes = 0;

	// Inferred coordinate latches: native firmware encoding corroborated by
	// M40 pointer movement bounds/lifecycle. Enable/blanking remains unknown.
	std::uint16_t pointer_x_counter = 0;
	std::uint16_t pointer_y_counter = 0;
	bool pointer_x_known = false;
	bool pointer_y_known = false;
	unsigned pointer_x() const { return 1023 - pointer_x_counter; }
	unsigned pointer_y() const { return (pointer_y_counter - 10) & 1023; }

	bool diagnostic_pixel(unsigned x, unsigned y) const
	{
		if (x >= visible_width || y >= visible_height) return false;
		const unsigned offset = y * row_bytes + x / 8;
		// Unknown storage is presented as black; it remains unknown to the
		// CPU. This is not an assertion of zero-filled power-on video RAM.
		return written[offset] && (ram[offset] & (0x80 >> (x & 7)));
	}

	bool text_cursor_pixel(unsigned x, unsigned y, bool blink_on) const
	{
		if (blanked || !blink_on || !pointer_x_known || !pointer_y_known)
			return false;
		// Text firmware emits 119A and (1026 - 7*column) modulo 1024.
		// Undo its three-pixel bias; graphics coordinates use another origin.
		const unsigned left = (pointer_x() + 3) & 1023;
		return pointer_y() == 400 && y == 400 && x < visible_width
			&& x >= left && x - left < 5;
	}

	void reset()
	{
		selected = false;
		address_known = false;
		pointer_x_known = pointer_y_known = false;
		inverted = blanked = false;
		access_mode = 0;
		control_writes = 0;
		// Do not invent cleared physical RAM or assume reset retention.
		written.fill(0);
	}
	void controller_reset(bool asserted)
	{
		if (asserted) reset();
		reset_held = asserted;
	}
	void select(std::uint8_t value)
	{
		selected = !reset_held && value == 0xff;
		// Persistence across selection changes is not established.
		address_known = false;
	}
	result command(std::uint16_t data, std::uint16_t mask)
	{
		if (mask != 0xffff) return result::lanes;
		if (data >= ram.size()) return result::address_range;
		address = data;
		address_known = true;
		return result::ok;
	}
	result output(std::uint16_t data, std::uint16_t mask)
	{
		const auto operation = data >> 8;
		if (operation == 0x04)
		{
			if (mask != 0xffff) return result::lanes;
			if (!address_known) return result::address_unknown;
			// CAE is a signed 15-bit displacement. Native scroll uses
			// 7D44 (-700): move the graphics page up ten 70-byte rows.
			// Keep the two service rows fixed. Atomic functional transfer;
			// the following ETIB therefore observes completion.
			const int displacement = (address & 0x4000) ? int(address) - 0x8000 : int(address);
			const auto old_ram = ram;
			const auto old_written = written;
			for (unsigned destination = 0; destination < page_bytes; ++destination)
			{
				const int source = int(destination) - displacement;
				if (source >= 0 && source < int(page_bytes))
				{
					ram[destination] = old_ram[source];
					written[destination] = old_written[source];
				}
				else
				{
					ram[destination] = 0;
					written[destination] = 1;
				}
			}
			access_mode = operation;
			++control_writes;
			address_known = false;
			return result::ok;
		}
		if (operation == 0x08 || operation == 0x38 || operation == 0x1c || operation == 0 || operation == 0x98)
		{
			if (mask != 0xffff) return result::lanes;
			++control_writes;
			access_mode = operation;
			// 08 establishes the normal control state after probing RAM and
			// before either pointer lifecycle operation. It preserves RAM and
			// coordinates; 38 is the common end-of-access control.
			if (operation == 0x08) blanked = false;
			if (operation == 0x1c) inverted = !inverted;
			if (operation == 0x00) blanked = true;
			if (operation == 0x98) blanked = false;
			return result::ok;
		}
		if ((data & 0xfc00) == 0x0c00 || (data & 0xfc00) == 0x1000)
		{
			if (mask != 0xffff) return result::lanes;
			if ((data & 0xfc00) == 0x0c00)
			{
				pointer_x_counter = data & 0x03ff;
				pointer_x_known = true;
			}
			else
			{
				pointer_y_counter = data & 0x03ff;
				pointer_y_known = true;
			}
			// No RAM address retention claim across these controls.
			address_known = false;
			return result::ok;
		}
		if (operation == 0x18)
		{
			// DEA drives only the upper ECD byte, then samples RAM. Low ECD
			// is not a parameter. Firmware follows this read with a 58 write.
			if (mask != 0xff00) return result::lanes;
			return address_known ? result::ok : result::address_unknown;
		}
		if (operation != 0x54 && operation != 0x58) return result::operation;
		if (mask != 0xffff) return result::lanes;
		if (!address_known) return result::address_unknown;
		ram[address] = data & 0xff;
		written[address] = 1;
		if (operation == 0x54)
		{
			// Firmware emits contiguous 70-byte raster rows and clears from
			// CAE 0000 using repeated 54 writes. No wrap rule is assumed.
			if (address == ram.size() - 1) address_known = false;
			else ++address;
		}
		else
		{
			// All identified 58 paths readdress before the next access.
			// Store the byte but refuse to guess the resulting address.
			address_known = false;
		}
		return result::ok;
	}
	result input(std::uint8_t &data) const
	{
		if (!address_known) return result::address_unknown;
		if (!written[address]) return result::unwritten;
		// Overlay3 reads with EDB before each 54 write, so the read itself
		// must leave the addressed byte available for that write.
		data = ram[address];
		return result::ok;
	}
};
#endif
