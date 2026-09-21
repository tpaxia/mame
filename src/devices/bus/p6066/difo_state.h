// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
#ifndef MAME_BUS_P6066_DIFO_STATE_H
#define MAME_BUS_P6066_DIFO_STATE_H
#pragma once
#include "dma.h"
#include <cstdint>

// DIFO 755.30.1 pp.17,20-23,61-65. Logical asserted bits, not pin voltages.
// Geometry is cylinder-wide ST, not a floppy head/sector register pair.
struct p6066_difo_state
{
	enum operation : std::uint8_t { POSITION=0, READ=4, VERIFY=8, WRITE=16, SCAN=32, FORMAT=128 };
	std::uint8_t name=10, unit=0, cylinder=0, sector=0, length=0, scan=0, key=0, execute=0;
	std::uint16_t word=0;
	bool odd=false, home=false, selected=false, busy=false;
	std::uint8_t status=0, keys=0xff, response=0;
	bool completion=false, request=false, servicing=false;

	void select(std::uint8_t value)
	{
		selected = ((value >> 3) & 15) == name;
		if (selected) unit = value >> 7;
	}
	// Returns true only for execute. The bus caller must preserve ECD8.
	bool latch(std::uint16_t value)
	{
		// PDF22 lists complemented ECDBN/ECDAN/ECD9N; the bus API
		// carries CPU register bits, so invert only these three selectors.
		switch (((value >> 9) & 7) ^ 7)
		{
		case 7: scan=value; break;
		case 6: cylinder=value; break;
		case 5: sector=value; break;
		case 4: length=value; break;
		case 3: word=(word&0xff80)|((value>>1)&0x7f); odd=value&1; break;
		case 2: word=(word&0x7f)|((value&0x1ff)<<7); break;
		case 1: key=value; home=value&0x100; break;
		case 0: execute=value; return true;
		}
		return false;
	}
	unsigned operation_bits() const { return execute & 0xbc; }
	bool valid_operation() const
	{
		const unsigned op=operation_bits();
		return !op || !(op&(op-1));
	}
	unsigned byte_address() const { return unsigned(word)*2+odd; }
	unsigned head() const { return sector/48; }
	std::uint8_t selection_status(bool ready) const { return (ready?0:0x20)|(busy?6:0); }
	std::uint16_t type(bool interrupt) const
	{
		// PDF20/26 selection flow: EPT7,5,4 + MOM2:1 = B2.
		// PDF65 summary incorrectly adds EPT2 to selection; completion is B6.
		// Generator064 startup independently accepts B0/B1/B2, not B4/B5/B6.
		return interrupt ? 0xb600 | (name<<3) : 0xb200;
	}
	void finish(std::uint8_t errors=0)
	{
		status |= errors; busy=false; completion=true;
	}
	void synchronize()
	{
		if (servicing && response==2)
		{
			completion=request=servicing=false; response=0;
		}
		else if (completion) request=true;
	}
	void acknowledge() { servicing=true; response=0; }
	std::uint8_t input(bool ready) const
	{
		if (!servicing) return selection_status(ready);
		return response==0 ? status : response==1 ? keys : sector;
	}
	void strobe() { if (servicing && response<2) ++response; }
	void reset()
	{
		const auto old_name=name; *this={}; name=old_name;
	}
};
#endif
