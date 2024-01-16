// license: BSD-3-Clause
// copyright-holders: Salvatore Paxia
/***************************************************************************

    
***************************************************************************/

#ifndef MAME_BUS_MCZ_SYSBUS_H
#define MAME_BUS_MCZ_SYSBUS_H

#pragma once

#include "cpu/z80/z80.h"
#include "machine/z80daisy.h"

// forward declaration
class device_mcz_sysbus_card_interface;


//**************************************************************************
//  BUS DEVICE
//**************************************************************************

class mcz_sysbus_device : public device_t
{
public:
	// construction/destruction
	mcz_sysbus_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);
	
	virtual ~mcz_sysbus_device();

	//void add_card(int slot, device_mcz_sysbus_card_interface *card);
	auto disksector_callback() { return m_sector.bind(); }
 	DECLARE_WRITE_LINE_MEMBER( disksector_w ) { m_sector(state); }
 
		
	uint8_t bus_r(offs_t offset);
	void bus_w(offs_t offset, uint8_t data);
	void add_card(device_mcz_sysbus_card_interface &card);
	void set_bus_clock(u32 clock);
	void set_bus_clock(const XTAL &xtal) { set_bus_clock(xtal.value()); }
	void assign_installer(int index, address_space_installer *installer);
	address_space_installer *installer(int index) const;
	void assing_cpu(z80_device *cpu) { m_maincpu=cpu;}
	z80_device *get_maincpu() { return m_maincpu;}
	void add_to_daisy_chain(std::string tag) { m_daisy.push_back(tag); }
	const z80_daisy_config* get_daisy_chain();

protected:
	// device_t implementation
	mcz_sysbus_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock);
	
	virtual void device_start() override;
	virtual void device_reset() override;
	
private:
	 
	address_space_installer *m_installer[4];
	std::vector<std::string> m_daisy;
	devcb_write_line m_sector;
 	
	using card_vector = std::vector<std::reference_wrapper<device_mcz_sysbus_card_interface> >;

	z80_device *m_maincpu;
	
	char **m_daisy_chain;
	card_vector m_device_list;
	 
};




//**************************************************************************
//  SLOT DEVICE
//**************************************************************************

class mcz_sysbus_slot_device : public device_t, public device_slot_interface
{
public:
	mcz_sysbus_slot_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock);

	// construction/destruction
	template <typename T, typename U>
	mcz_sysbus_slot_device(machine_config const &mconfig, char const *tag, device_t *owner, T &&bus_tag, U &&slot_options, char const *default_option, bool fixed = false)
		: mcz_sysbus_slot_device(mconfig, tag, owner, DERIVED_CLOCK(1,1))
	{
		m_bus.set_tag(std::forward<T>(bus_tag));
		option_reset();
		slot_options(*this);
		set_default_option(default_option);
		set_fixed(fixed);
	}
	

protected:
	mcz_sysbus_slot_device(machine_config const &mconfig, device_type type, char const *tag, device_t *owner, u32 clock);

	// device_t implementation
	virtual void device_start() override;
	virtual void device_resolve_objects() override;

	required_device<mcz_sysbus_device> m_bus;
};




//**************************************************************************
//  CARD INTERFACE
//**************************************************************************

 

class device_mcz_sysbus_card_interface : public device_interface
{
	friend class mcz_sysbus_slot_device;
public:
	virtual uint8_t card_r(offs_t offset) { return 0xff; };
	virtual void card_w(offs_t offset, uint8_t data) {};

protected:
	// construction/destruction
	device_mcz_sysbus_card_interface(const machine_config &mconfig, device_t &device);

	void set_bus_device(mcz_sysbus_device *bus_device);

	mcz_sysbus_device  *m_bus;
	

};


// device type definition
DECLARE_DEVICE_TYPE(MCZ_SYSBUS, mcz_sysbus_device)
DECLARE_DEVICE_TYPE(MCZ_SYSBUS_SLOT, mcz_sysbus_slot_device)


#endif // MAME_BUS_MCZ_SYSBUS_H
