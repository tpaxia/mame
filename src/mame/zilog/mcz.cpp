// license:BSD-3-Clause
// copyright-holders:Salvatore Paxia

// MAME driver for the Zilog MCZ series 

#include "emu.h"
#include "bus/mcz/sysbus.h"
#include "bus/mcz/modules.h"

namespace {

// State class - derives from driver_device
class mcz_state : public driver_device
{
public:
	mcz_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_bus(*this, "bus")
	{ }

	// This function sets up the machine configuration
	void mcz(machine_config &config);

protected:
	// address maps for program memory and io memory
	//void mcz_mem(address_map &map);
	//void mcz_io(address_map &map);
	
	 
private:
	required_device<mcz_sysbus_device> m_bus;
};

ROM_START(mcz)
ROM_END


// Trivial memory map for program memory
//void mcz_state::mcz_mem(address_map &map)/
//{
//	//T map(0x0000, 0x0bff).rom();
//	//T map(0x0c00, 0xffff).ram();
//}

void mcz_state::mcz(machine_config &config)
{
	/* basic machine hardware */
	MCZ_SYSBUS(config, m_bus, 0);
	MCZ_SYSBUS_SLOT(config, "bus:1", m_bus, mcz_bus_modules, "mcz_floppy").set_fixed(true);
	MCZ_SYSBUS_SLOT(config, "bus:2", m_bus, mcz_bus_modules, "z80_mcb").set_fixed(true);
	MCZ_SYSBUS_SLOT(config, "bus:3", m_bus, mcz_bus_modules, "z8000_mcb").set_fixed(true);
}

#ifdef SPAM

// ROM mapping is trivial, this binary was created from the HEX file on Grant's website
ROM_START(mcz)
	ROM_REGION(0x2000, "maincpu",0)
	ROM_LOAD("mcz.bin",   0x0000, 0x0c00, CRC(b4fe82ec) SHA1(777a3be2e7517f9857fd4d9552459f10f6689bba))
ROM_END
#endif

} // anonymous namespace

// This ties everything together
//    YEAR  NAME            PARENT    COMPAT    MACHINE        INPUT          CLASS             INIT           COMPANY           FULLNAME                FLAGS
COMP( 2007, mcz,          0,        0,        mcz,         0,             mcz_state,      empty_init,    "Zilog",   "MCZ",  MACHINE_NO_SOUND_HW )
