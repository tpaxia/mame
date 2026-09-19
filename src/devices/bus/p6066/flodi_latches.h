// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
#ifndef MAME_BUS_P6066_FLODI_LATCHES_H
#define MAME_BUS_P6066_FLODI_LATCHES_H
#pragma once
#include <cstdint>

// FLOD2 168664-K02 A7/C6-C8/E8/E9/P9; K05 INCO/MAS;
// K07 status mux. Bus bytes here use CPU logical polarity.
struct p6066_flodi_latches
{
	enum class effect { local, command, inco };
	std::uint8_t command = 0, mas = 0, num = 0;
	bool prico = false, cote = true;

	effect write(bool mema, std::uint8_t data, bool drive2 = true)
	{
		const bool second = prico;
		prico = true;
		// COLON excludes a local command from MACON during selection.
		// K02 M1 decodes ECD6/ECD7, not an all-eight-bits zero test.
		if (!mema && !(data & 0xc0)) return effect::local;
		if (mema && second)
		{
			mas = data;
			// CATE's active-low preset overrides the INCO clock on P9.
			if (command & 0x80) cote = false;
			return effect::inco;
		}
		// K02 G6/G7 gates CADI in MACON only; all eight MAS bits pass.
		command = data & (drive2 ? 0xff : 0xdf);
		if (!(command & 0x80)) cote = true;
		return effect::command;
	}
	void ecm3() { prico = false; }
	bool busy(bool changer_busy=false) const { return (command & 0x90) != 0 || changer_busy; } // K02 P6: CATE, VIRI, !CICO
	std::uint8_t status(bool end, bool index, bool track_zero, std::uint8_t result, bool scan_found=false, bool different=false, bool operator_intervention=false) const
	{
		if (prico) return num;
		// K07 E5/E6: TEVE = DIVE.RIFI + !CATE. DIVE/SCOK are
		// qualified by the synchronized completion response.
		return (index ? 1 : 0) | (cote ? 2 : 0) | ((!(command & 0x80) || (end && different)) ? 4 : 0) | (scan_found ? 8 : 0)
			| (operator_intervention ? 0x20 : 0) | (result & 0x80) | (end ? (result & 0x40) : (track_zero ? 0x40 : 0));
	}
};
#endif
