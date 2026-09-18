// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
#ifndef MAME_BUS_P6066_FLODI_IRQ_H
#define MAME_BUS_P6066_FLODI_IRQ_H
#pragma once
#include <cstdint>

// FLOD2 K02 A3/E3 (SELE/COMA), K06 A4/C4/C6, E4/G4/G6
// (FOGO/FUGO, FINE/RIFI), M6/P6/R6 (RILI/LIVI).
// Transaction-level strobes; this does not model propagation delays.
struct p6066_flodi_irq
{
	bool sele = false, coma = false, command_armed = false;
	bool fogo = false, fine = false, rili = false;
	bool fugo = false, rifi = false, livi = false;

	std::uint8_t requests() const
	{
		return (livi ? 1 : 0) | ((fugo || rifi) ? 4 : 0)
			| ((sele || coma) ? 8 : 0);
	}
	std::uint8_t type(bool response) const
	{
		return response ? (coma ? 3 : (sele ? 1 : 0))
			: ((fugo ? 8 : 0) | (rifi ? 4 : 0));
	}
	void synchronize(unsigned mask, bool response_owned)
	{
		if (mask & 2) livi = rili;
		if (mask & 8)
		{
			fugo = fogo;
			rifi = fine;
			// Unacknowledged selection/command responses survive ECM3.
			// Commands issued during RIMI arm COMA for this boundary.
			if (response_owned) { sele = false; coma = command_armed; }
			command_armed = false;
		}
	}
	void ecot(bool function_owned)
	{
		// K06 M2/M3 drives the reset of BOTH asynchronous SR latches.
		if (function_owned) { fogo = false; fine = false; }
	}
	void byte_ack() { rili = false; } // RICA resets RILI, not LIVI
	void stop_bytes() { rili = false; livi = false; }
};
#endif
