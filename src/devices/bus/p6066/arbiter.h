// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
#ifndef MAME_BUS_P6066_ARBITER_H
#define MAME_BUS_P6066_ARBITER_H
#pragma once
#include <array>
#include <cstdint>
struct p6066_irq_arbiter
{
	std::array<int,4> owners{{-1,-1,-1,-1}};
	std::array<std::uint8_t,16> requests{};
	static unsigned level(unsigned source) { return source<3 ? source : 3; }
	int candidate(unsigned source) const
	{
		if (!source || source>4) return -1;
		for (unsigned slot=0;slot<requests.size();++slot)
			if (requests[slot] & (1U<<(source-1))) return slot;
		return -1;
	}
	unsigned next(unsigned current_level) const
	{
		for (unsigned source=1;source<=4;++source)
			if (level(source)<current_level && owners[level(source)]<0 && candidate(source)>=0) return source;
		return 0;
	}
	int acknowledge(unsigned source)
	{
		if (!source || source>4 || owners[level(source)]>=0) return -1;
		const int slot=candidate(source);
		if (slot>=0) owners[level(source)]=slot;
		return slot;
	}
	int release(unsigned current_level)
	{
		if (!current_level || current_level>3) return -1;
		const int slot=owners[current_level]; owners[current_level]=-1; return slot;
	}
	void reset() { owners.fill(-1); requests.fill(0); }
};
#endif
