// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
/***************************************************************************

    Olivetti L1 backplane bus

***************************************************************************/

#ifndef MAME_BUS_OLIVETTI_L1_L1_H
#define MAME_BUS_OLIVETTI_L1_L1_H

#pragma once

#include "machine/ram.h"

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

class olivetti_l1_bus_device;
class device_olivetti_l1_card_interface;
class device_olivetti_l1_cpu_card_interface;

class olivetti_l1_slot_device : public device_t, public device_single_card_slot_interface<device_olivetti_l1_card_interface>
{
public:
	template <typename T, typename U>
	olivetti_l1_slot_device(
			machine_config const &mconfig,
			char const *tag,
			device_t *owner,
			T &&bus_tag,
			u8 position,
			u8 select,
			U &&options,
			char const *default_option,
			bool fixed = false)
		: olivetti_l1_slot_device(mconfig, tag, owner, 0)
	{
		m_bus.set_tag(std::forward<T>(bus_tag));
		m_position = position;
		m_select = select;
		option_reset();
		options(*this);
		set_default_option(default_option);
		set_fixed(fixed);
	}

	olivetti_l1_slot_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock = 0);

protected:
	virtual void device_resolve_objects() override ATTR_COLD;
	virtual void device_start() override ATTR_COLD;

private:
	required_device<olivetti_l1_bus_device> m_bus;
	u8 m_position = 0;
	u8 m_select = 0;
};

class olivetti_l1_bus_device : public device_t
{
public:
	enum class chassis : u8 { m30_m34, m40_m44 };
	enum class interrupt_level : u8 { l1a, l1b, l2 };

	olivetti_l1_bus_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock = 0);

	olivetti_l1_bus_device &set_chassis(chassis type) { m_chassis = type; return *this; }
	void add_slot(u8 position, u8 select, device_olivetti_l1_card_interface *card);
	device_olivetti_l1_card_interface *get_card(u8 select) const { return m_cards[select & 0x0f]; }

	u16 io_r(offs_t offset, u16 mem_mask = ~0);
	void io_w(offs_t offset, u16 data, u16 mem_mask = ~0);

	bool vi_pending() const;
	u16 viack_r();

	bool memory_r(offs_t address, u8 &data);
	bool memory_w(offs_t address, u8 data);
	u8 physical_r(offs_t address);
	void physical_w(offs_t address, u8 data);

	void vi_w(u8 select, int state);
	void busreq_w(u8 select, int state);

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

private:
	device_olivetti_l1_card_interface *card(u8 select) const { return m_cards[select]; }
	void validate_layout();
	void update_vi();
	void update_busreq();

	std::array<device_olivetti_l1_card_interface *, 16> m_cards{};
	std::array<device_olivetti_l1_card_interface *, 14> m_positions{};
	std::array<bool, 14> m_connectors{};
	std::vector<device_olivetti_l1_card_interface *> m_chain;
	required_device<ram_device> m_ram;
	device_olivetti_l1_cpu_card_interface *m_cpu = nullptr;
	chassis m_chassis = chassis::m40_m44;
	u16 m_vi_state = 0;
	u16 m_busreq_state = 0;
};

class device_olivetti_l1_card_interface : public device_interface
{
	friend class olivetti_l1_bus_device;
	friend class olivetti_l1_slot_device;

public:
	virtual ~device_olivetti_l1_card_interface();

	virtual u8 io_r(offs_t offset) { return 0xff; }
	virtual void io_w(offs_t offset, u8 data) { }
	virtual u16 viack_r() { return 0; }
	virtual olivetti_l1_bus_device::interrupt_level vi_level() const { return olivetti_l1_bus_device::interrupt_level::l2; }
	virtual void bus_grant_w(int state) { }
	virtual bool io_responds() const { return true; }
	virtual bool memory_claims(offs_t address) const { return false; }
	virtual u8 memory_r(offs_t address) { return 0xff; }
	virtual void memory_w(offs_t address, u8 data) { }
	virtual bool is_ram() const { return false; }
	virtual u32 ram_capacity() const { return 0; }
	virtual void configure_ram(u32 base, u32 backing_offset, u32 size) { }

protected:
	device_olivetti_l1_card_interface(machine_config const &mconfig, device_t &device);

	virtual void interface_pre_start() override;

	u8 select() const { return m_select; }
	u8 position() const { return m_position; }
	olivetti_l1_bus_device &bus() const { assert(m_bus); return *m_bus; }

	void vi_w(int state) { bus().vi_w(m_select, state); }
	void busreq_w(int state) { bus().busreq_w(m_select, state); }
	bool physical_try_r(offs_t address, u8 &data) { return bus().memory_r(address, data); }
	bool physical_try_w(offs_t address, u8 data) { return bus().memory_w(address, data); }
	u8 physical_r(offs_t address) { return bus().physical_r(address); }
	void physical_w(offs_t address, u8 data) { bus().physical_w(address, data); }

private:
	olivetti_l1_bus_device *m_bus = nullptr;
	u8 m_position = 0;
	u8 m_select = 0;
};

class device_olivetti_l1_cpu_card_interface : public device_olivetti_l1_card_interface
{
	friend class olivetti_l1_bus_device;

public:
	virtual ~device_olivetti_l1_cpu_card_interface();

protected:
	device_olivetti_l1_cpu_card_interface(machine_config const &mconfig, device_t &device);

	virtual void ready_fault_w(int state) = 0;
	virtual void bus_vi_w(int state) = 0;
	virtual void bus_request_w(int state) = 0;
	virtual bool local_vi_pending(olivetti_l1_bus_device::interrupt_level level) const = 0;
	virtual u16 local_viack_r(olivetti_l1_bus_device::interrupt_level level) = 0;
};

DECLARE_DEVICE_TYPE(OLIVETTI_L1_BUS, olivetti_l1_bus_device)
DECLARE_DEVICE_TYPE(OLIVETTI_L1_SLOT, olivetti_l1_slot_device)

#endif // MAME_BUS_OLIVETTI_L1_L1_H
