// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
/***************************************************************************

    Olivetti M40 (L1 line)

    The machine is assembled on an L1 backplane.  The M40 configuration uses
    a UC042 central unit, RAM board, GO252 video/keyboard governo and GO280
    floppy governo, with an optional GO363 hard-disk governo.  The resident
    autodiagnostic ROM is release 6.0.

***************************************************************************/

#include "emu.h"

#include "bus/olivetti_l1/l1.h"
#include "bus/olivetti_l1/go252.h"
#include "bus/olivetti_l1/go280.h"
#include "bus/olivetti_l1/go363.h"
#include "bus/olivetti_l1/ram.h"
#include "bus/olivetti_l1/uc.h"

#include "machine/ram.h"

namespace {

void l1_ram_cards(device_slot_interface &device)
{
	device.option_add("me256k", OLIVETTI_L1_ME256K);
	device.option_add("me384k", OLIVETTI_L1_ME384K);
	device.option_add("me512k", OLIVETTI_L1_ME512K);
	device.option_add("ra57d", OLIVETTI_L1_RA57D);
	device.option_add("ra57e", OLIVETTI_L1_RA57E);
	device.option_add("ra57c", OLIVETTI_L1_RA57C);
	device.option_add("ra57b", OLIVETTI_L1_RA57B);
	device.option_add("ra57a", OLIVETTI_L1_RA57A);
}

void l1_first_ram_cards(device_slot_interface &device)
{
	device.option_add("auto", OLIVETTI_L1_RAM);
	l1_ram_cards(device);
}

void m40_l1_cards(device_slot_interface &device)
{
	device.option_add("go252", OLIVETTI_L1_GO252);
	device.option_add("go280", OLIVETTI_L1_GO280);
	device.option_add("go363", OLIVETTI_L1_GO363);
	l1_ram_cards(device);
}

void m40_l1_cpu_cards(device_slot_interface &device)
{
	device.option_add("uc042", OLIVETTI_L1_UC042);
}

class m40_state : public driver_device
{
public:
	m40_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_l1bus(*this, "l1bus")
		, m_uc042(*this, "cpu:uc042")
	{ }

	void m40(machine_config &config);
	void m44(machine_config &config);

private:
	required_device<olivetti_l1_bus_device> m_l1bus;
	optional_device<olivetti_l1_uc042_device> m_uc042;
	void     l1_backplane(machine_config &config, olivetti_l1_bus_device::chassis chassis);
	void     configure_slot(olivetti_l1_slot_device &slot, bool governi);
	void     go252_crtc_trace_w(offs_t offset, uint8_t data);
	void     go280_trace_w(offs_t event, uint32_t data);
};

//**************************************************************************

void m40_state::go252_crtc_trace_w(offs_t offset, uint8_t data)
{
	if (m_uc042)
		m_uc042->crtc_trace_w(offset, data);
}

void m40_state::go280_trace_w(offs_t event, uint32_t data)
{
	if (m_uc042)
		m_uc042->floppy_trace_w(event, data);
}

void m40_state::configure_slot(olivetti_l1_slot_device &slot, bool governi)
{
	if (governi)
	{
		slot.set_option_machine_config("go252", [this](device_t *device)
		{
			olivetti_l1_go252_device &card = downcast<olivetti_l1_go252_device &>(*device);
			card.crtc_write_callback().set(*this, FUNC(m40_state::go252_crtc_trace_w));
		});
		slot.set_option_machine_config("go280", [this](device_t *device)
		{
			olivetti_l1_go280_device &card = downcast<olivetti_l1_go280_device &>(*device);
			card.trace_callback().set(*this, FUNC(m40_state::go280_trace_w));
		});
	}
}

void m40_state::l1_backplane(machine_config &config, olivetti_l1_bus_device::chassis chassis)
{
	OLIVETTI_L1_BUS(config, m_l1bus, 0).set_chassis(chassis);
	u8 const positions = (chassis == olivetti_l1_bus_device::chassis::m30_m34) ? 9 : 14;
	u8 const cpu_position = (chassis == olivetti_l1_bus_device::chassis::m30_m34) ? 1 : 0;

	static char const *const slot_tags[14] =
	{
		"slot1", "slot2", "slot3", "slot4", "slot5", "slot6", "slot7",
		"slot8", "slot9", "slot10", "slot11", "slot12", "slot13", "slot14"
	};

	// M30/M34 has nine positions with the CPU second; M40/M44 has fourteen with
	// the CPU first.  The CPU always has logical select F; the other positions
	// receive selects 0 upwards in physical order, skipping the CPU.
	u8 select = 0;
	for (u8 position = 0; position < positions; position++)
	{
		if (position == cpu_position)
		{
			OLIVETTI_L1_SLOT(config, "cpu", m_l1bus, position, 15, m40_l1_cpu_cards, "uc042", true);
			continue;
		}

		char const *const default_card = (select == 0) ? "auto" : (select == 1) ? "go252" : (select == 2) ? "go280" : nullptr;
		auto const options = (select == 0) ? l1_first_ram_cards : m40_l1_cards;
		olivetti_l1_slot_device &slot(OLIVETTI_L1_SLOT(config, slot_tags[position], m_l1bus, position, select, options, default_card));
		configure_slot(slot, select != 0);
		select++;
	}

	// MAME's root RAM device supplies the -ramsize option and backing allocation;
	// only the RAM card responds to its physical address range on the L1 bus.
	RAM(config, RAM_TAG).set_default_size("512K").set_default_value(0)
		.set_extra_options("128K,256K,384K,640K,768K,896K,1024K");
}

void m40_state::m40(machine_config &config)
{
	// M40 and M44 use the fourteen-position INO74 layout: CPU in position 1,
	// first RAM module in position 2.
	l1_backplane(config, olivetti_l1_bus_device::chassis::m40_m44);
}

void m40_state::m44(machine_config &config)
{
	// Provisional until the UC048 and the M44 card population are emulated.
	m40(config);
}

} // anonymous namespace


namespace {

//**************************************************************************
//  ROM
//**************************************************************************

ROM_START( m40 )
	ROM_REGION16_BE( 0x4000, "cpu:uc042:maincpu", 0 )
	ROM_LOAD( "m40rom-6.0.bin", 0x0000, 0x4000, CRC(8114ebec) SHA1(4e2c65b95718c77a87dbee0288f323bd1c8837a3) )
ROM_END

ROM_START( m44 )
	ROM_REGION16_BE( 0x14000, "cpu:uc042:maincpu", 0 ) // 14 MAR. 86 REL B.1
	ROM_LOAD16_BYTE( "pd30.128.c06", 0x0000, 0x4000, CRC(8155dc69) SHA1(ed65f842e2857ad10170c697d945745fd7d47f9c) )
	ROM_LOAD16_BYTE( "pd29.128.a06", 0x0001, 0x4000, CRC(74d7de4b) SHA1(dd3a69ff29a2f1292f3a7db73bd2447bd664e54b) )

	ROM_REGION( 0x114, "plds", 0 )
	ROM_LOAD( "pl46.j09", 0x000, 0x114, NO_DUMP ) // PLD, chip type unknown
ROM_END

} // anonymous namespace

//    YEAR  NAME  PARENT  COMPAT  MACHINE  INPUT  CLASS      INIT        COMPANY     FULLNAME           FLAGS
COMP( 1982, m40,  0,      0,      m40,     0,     m40_state, empty_init, "Olivetti", "M40 (L1)",        MACHINE_NOT_WORKING | MACHINE_NO_SOUND )
COMP( 1986, m44,  0,      0,      m44,     0,     m40_state, empty_init, "Olivetti", "Olivetti L1 M44", MACHINE_NOT_WORKING | MACHINE_NO_SOUND )
