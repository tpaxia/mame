// license: BSD-3-Clause
// copyright-holders: Dirk Best
/***************************************************************************

    mc-68000-Computer System Bus Cards

***************************************************************************/

#include "emu.h"
#include "cards.h"
#include "floppy.h"

void mcz_sysbus_cards(device_slot_interface &device)
{
	device.option_add("floppy", MCZ_FLOPPY);
}
