// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
#include "emu.h"
#include "bus/p6066/flodi_rotation.h"
#include <cassert>
#include <cstdio>
int main()
{
	const auto rev=attotime::from_hz(6);
	// Independently specified positions relative to index, including future epoch.
	assert(p6066_fm::rotational_cell(attotime::from_usec(1000),83333)==500);
	assert(p6066_fm::rotational_cell(attotime::zero-attotime::from_usec(1000),83333)==82833);
	assert(p6066_fm::rotational_cell(attotime::zero-rev,83333)==0);
	assert(p6066_fm::rotational_cell(rev,83333)==0);
	for (unsigned us=0;us<166666;us+=7)
	{
		const auto phase=attotime::from_usec(us);
		const unsigned expected=us/2;
		assert(p6066_fm::rotational_cell(phase,83333)==expected);
		assert(p6066_fm::rotational_cell(phase-rev,83333)==expected);
		assert(p6066_fm::rotational_cell(phase+rev+rev,83333)==expected);
	}
	// Demonstrate that the old conversion does not give the physical position.
	const s64 broken=(attotime::zero-attotime::from_usec(1000)).as_ticks(500000);
	assert(((broken%83333)+83333)%83333!=82833);
	std::puts("FLODI rotation: signed epoch and revolution-invariance checks passed");
}
