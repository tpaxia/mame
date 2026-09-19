// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
#ifndef MAME_BUS_P6066_FLODI_ROTATION_H
#define MAME_BUS_P6066_FLODI_ROTATION_H
#pragma once

#include "attotime.h"

namespace p6066_fm {
// FDU STAC 1L p.1.06: 360 RPM; FLODISC p.22: 2 us FM cells.
// epoch can be the next index, hence delta can be negative. as_ticks() uses
// unsigned seconds multiplication: normalize TIME before converting to cells.
// Use the full revolution period, not 83333 cells (which loses 2/3 us per turn).
inline unsigned rotational_cell(attotime delta, unsigned cells)
{
	const attotime revolution=attotime::from_hz(6);
	while (delta<attotime::zero) delta+=revolution;
	while (delta>=revolution) delta-=revolution;
	return unsigned(delta.as_ticks(500000)%cells);
}
}
#endif
