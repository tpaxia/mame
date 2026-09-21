// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
#ifndef MAME_BUS_P6066_DMA_H
#define MAME_BUS_P6066_DMA_H
#pragma once
#include <array>
#include <cstdint>

// RODMA 805.30.1 section 1. Addresses and boundaries are WORD addresses.
// This is transport state, independent of any peripheral's command/count/IRQ.
struct p6066_dma_arbiter
{
	enum : unsigned { DISABLED, LOW, HIGH, MIDDLE };
	unsigned mode = DISABLED;
	std::uint32_t boundary = 0;
	std::array<bool, 16> requests{};
	bool cpu_pending = false;
	bool eligible = false;
	int owner = -1; // -1 idle; 16 CPU; otherwise DMA chain position

	bool shared(std::uint16_t address) const
	{
		switch (mode)
		{
		case LOW: return address < boundary;
		case HIGH: return address >= boundary;
		case MIDDLE: return address >= boundary && address < 0x8000;
		default: return false;
		}
	}
	bool requested() const { for (bool r : requests) if (r) return true; return false; }
	// Initial synchronization and end-of-cycle queue sampling are distinct
	// events in the RODMA; neither grants a currently busy memory bus.
	void synchronize() { eligible = requested(); }
	int choose()
	{
		if (owner >= 0) return -1;
		if (eligible)
			for (unsigned n = 0; n < requests.size(); ++n)
				if (requests[n]) { eligible = false; return owner = n; }
		eligible = false;
		if (cpu_pending) { cpu_pending = false; return owner = 16; }
		return -1;
	}
	void complete()
	{
		const bool peripheral = owner >= 0 && owner < 16;
		owner = -1;
		if (peripheral) synchronize(); // PREMN queue path, R fig.1.15
	}
	void reset()
	{
		requests.fill(false); cpu_pending = eligible = false; owner = -1;
	}
};

struct p6066_dma_cycle
{
	std::uint16_t address = 0, data = 0, mask = 0xffff;
	bool write = false;
};
#endif
