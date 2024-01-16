// license: BSD-3-Clause
// copyright-holders: Salvatore Paxia
/***************************************************************************

    MCZ BUS

***************************************************************************/

#include "emu.h"
#include "sysbus.h"

//**************************************************************************
//  BUS DEVICE
//**************************************************************************




mcz_sysbus_device::mcz_sysbus_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, type, tag, owner, clock)
	, m_installer{}
	, m_sector(*this)
	, m_daisy_chain{}
{
}



mcz_sysbus_device::mcz_sysbus_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: mcz_sysbus_device(mconfig, MCZ_SYSBUS, tag, owner, clock)
{
}

 

mcz_sysbus_device::~mcz_sysbus_device()
{

	for(size_t i = 0; i < m_daisy.size(); i++)
		delete [] m_daisy_chain[i];
	delete [] m_daisy_chain;
}

void mcz_sysbus_device::device_reset()
{
	if (m_installer[AS_IO])
		installer(AS_IO)->unmap_readwrite(0, (1 << installer(AS_IO)->space_config().addr_width()) - 1);
}


void mcz_sysbus_device::device_start()
{
	// resolve callbacks
	printf("BUS Device Start tag: '%s'\n",tag());
	m_sector.resolve_safe();
	printf("BUS Device Start DONE\n");
}

 

void mcz_sysbus_device::set_bus_clock(u32 clock)
{
	set_clock(clock);
	notify_clock_changed();
}


void mcz_sysbus_device::add_card(device_mcz_sysbus_card_interface &card)
{
	m_device_list.emplace_back(card);
}


 
uint8_t mcz_sysbus_device::bus_r(offs_t offset)
{
	uint8_t data = 0xff;

	for (device_mcz_sysbus_card_interface &entry : m_device_list)
		data &= entry.card_r(offset);

	return data;
}


void mcz_sysbus_device::bus_w(offs_t offset, uint8_t data)
{

	for (device_mcz_sysbus_card_interface &entry : m_device_list)
		entry.card_w(offset, data);
}


void mcz_sysbus_device::assign_installer(int index, address_space_installer *installer)
{
	if (m_installer[index] != nullptr )
		throw emu_fatalerror("Address installer already set on MCZ bus !!!");
	m_installer[index]  = installer;
}

address_space_installer *mcz_sysbus_device::installer(int index) const
{
	assert(index >= 0 && index < 4);
	if (m_installer[index] == nullptr )
		throw emu_fatalerror("Address installer not set on MCZ bus !!! Add CPU module.");
	return m_installer[index];
}

const z80_daisy_config* mcz_sysbus_device::get_daisy_chain()
{
	m_daisy_chain = new char*[m_daisy.size() + 1];
	for(size_t i = 0; i < m_daisy.size(); i++)
	{
		m_daisy_chain[i] = new char[m_daisy[i].size() + 1];
		strcpy(m_daisy_chain[i], m_daisy[i].c_str());
	}
	m_daisy_chain[m_daisy.size()] = nullptr;
	return (const z80_daisy_config*)m_daisy_chain;
}

//**************************************************************************
//  SLOT DEVICE
//**************************************************************************


mcz_sysbus_slot_device::mcz_sysbus_slot_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, type, tag, owner, clock)
	, device_slot_interface(mconfig, *this)
	, m_bus(*this, finder_base::DUMMY_TAG)
{
}

mcz_sysbus_slot_device::mcz_sysbus_slot_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: mcz_sysbus_slot_device(mconfig, MCZ_SYSBUS_SLOT, tag, owner, clock)
{
}

void mcz_sysbus_slot_device::device_start()
{
}

void mcz_sysbus_slot_device::device_resolve_objects()
{
	device_mcz_sysbus_card_interface *const card(dynamic_cast<device_mcz_sysbus_card_interface *>(get_card_device()));

	if (card) {
		printf("Adding card to bus\n");
		card->set_bus_device(m_bus);
		m_bus->add_card(*card);
	
	}
}


//**************************************************************************
//  CARD INTERFACE
//**************************************************************************


device_mcz_sysbus_card_interface::device_mcz_sysbus_card_interface(const machine_config &mconfig, device_t &device)
	: device_interface(device, "mczbus")
	, m_bus(nullptr)
{
}

void device_mcz_sysbus_card_interface::set_bus_device(mcz_sysbus_device *bus_device)
{
	m_bus = bus_device;
}

//**************************************************************************
//  DEVICE DEFINITIONS
//**************************************************************************

DEFINE_DEVICE_TYPE(MCZ_SYSBUS, mcz_sysbus_device, "mcz_sysbus", "MCZ System Bus")
DEFINE_DEVICE_TYPE(MCZ_SYSBUS_SLOT, mcz_sysbus_slot_device, "mcz_sysbus_slot", "mcz System Bus Slot")

 