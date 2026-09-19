// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
#ifndef MAME_BUS_P6066_FLODI_SCAN_H
#define MAME_BUS_P6066_FLODI_SCAN_H
#pragma once
#include <cstdint>

// FLODI pp.40-42, table 6; FLOD2 K05 comparator, MOD/MAM and SCOK.
// This is the byte-level equivalent of the MSB-first serial comparator.
struct p6066_flodi_scan
{
	std::uint8_t mod = 0;
	bool different = false, found = false;

	void begin(std::uint8_t mas) { mod = mas; different = found = false; }
	void compare(std::uint8_t disk, std::uint8_t mask, std::uint8_t command,
		std::uint8_t mas, std::uint8_t &num)
	{
		if (found) return; // SCOK inhibits MOD; NUM and DIVE retain the result.
		if (!different && mask != 0xff && disk != mask)
		{
			different = true;
			found = disk > mask ? (command & 2) != 0 : (command & 1) != 0;
		}
		if (found) return;
		if (++mod == 0)
		{
			// Equality is accepted by all three documented scan commands.
			if (!different) found = true;
			else { ++num; mod = mas; different = false; }
		}
	}
};
#endif
