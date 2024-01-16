// license:BSD-3-Clause
// copyright-holders:Salvatore Paxia
/**********************************************************************

    MCZ Modules

**********************************************************************/

#include "emu.h"
#include "bus/mcz/sysbus.h"
#include "bus/mcz/z80_mcb.h"
#include "bus/mcz/z8000_mcb.h"
#include "bus/mcz/mdc.h"


void mcz_bus_modules(device_slot_interface &device)
{
	device.option_add("mcz_floppy", MCZ_FLOPPY);
    device.option_add("z80_mcb", MCZ_Z80MCB);
    device.option_add("z8000_mcb", MCZ_Z8000MCB);
}