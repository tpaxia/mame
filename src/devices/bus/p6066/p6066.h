// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
#ifndef MAME_BUS_P6066_P6066_H
#define MAME_BUS_P6066_P6066_H
#pragma once
#include "dislot.h"
#include "arbiter.h"
#include <array>

class p6066_bus_device;
class device_p6066_card_interface : public device_interface
{
	friend class p6066_bus_device;
public:
	virtual bool memory_claims(u16 address) const { return false; }
	virtual u16 memory_r(u16 address, u16 mask) { return 0; }
	virtual void memory_w(u16 address, u16 data, u16 mask) { }
	virtual void select(u8 name) { }
	virtual bool direct_selected() const { return false; }
	virtual u16 name_type(unsigned level) { return 0; }
	virtual u8 input_data(unsigned level) { return 0; }
	virtual void output_data(unsigned level, u16 data) { }
	virtual void output_data_masked(unsigned level, u16 data, u16 mask)
	{
		if (mask != 0xffff) fatalerror("%s: partial ECD transaction %04X needs board wiring", device().tag(), mask);
		output_data(level, data);
	}
	virtual void command(unsigned level, u8 data) { fatalerror("%s: channel command not implemented", device().tag()); }
	virtual void command_word(unsigned level, u16 data, u16 mask)
	{
		if (mask != 0x00ff) fatalerror("%s: word command needs board wiring", device().tag());
		command(level, data);
	}
	virtual void control(unsigned level, u8 signal) { }
	virtual void strobe(unsigned level) { }
	virtual void controller_reset(bool asserted) { }
	// Request bits: 1, 2, 3A, 3B. Ownership survives request deassertion.
	virtual void interrupt_sync(u8 mask) { } // bits 1/2/3: ECM1/2/3
	virtual u8 irq_requests() const { return 0; }
	virtual void irq_ack(unsigned source) { }
	virtual void irq_end(unsigned level) { }
	u16 slot_base() const { return m_base; }
protected:
	device_p6066_card_interface(const machine_config &mconfig, device_t &device);
	virtual void interface_pre_start() override;
	p6066_bus_device *m_bus = nullptr;
private:
	u16 m_base = 0;
};

class p6066_bus_device : public device_t
{
public:
	p6066_bus_device(const machine_config &, const char *, device_t *, u32 clock = 0);
	auto invalid_cb() { return m_invalid_cb.bind(); }
	auto ecorn_output_cb() { return m_ecorn_output_cb.bind(); }
	void add_card(unsigned position, u16 base, device_p6066_card_interface &card);
	u16 memory_r(offs_t address, u16 mask = 0xffff);
	void memory_w(offs_t address, u16 data, u16 mask = 0xffff);
	void select_w(u8 name);
	u16 name_type_r(offs_t level);
	u8 input_data_r(offs_t level);
	void data_w(offs_t level, u16 data, u16 mask = 0xffff);
	void command_w(offs_t level, u16 data, u16 mask = 0x00ff);
	void strobe_w(u8 level);
	void control_w(offs_t level, u8 signal);
	void ecorn_w(int state);
	void interrupt_sync_w(u8 mask);
	u8 irq_r(offs_t level);
	void irq_ack_w(u8 source);
	void irq_end_w(u8 level);
protected:
	virtual void device_start() override;
	virtual void device_reset() override;
private:
	device_p6066_card_interface *memory_card(u16 address);
	device_p6066_card_interface *channel_card(unsigned level);
	std::array<device_p6066_card_interface *, 16> m_cards{};
	p6066_irq_arbiter m_irq;
	void refresh_requests();
	devcb_write_line m_invalid_cb, m_ecorn_output_cb;
	u32 m_floppy_selects = 0;
	output_finder<> m_floppy_select_output;
	void update_outputs();
};

class p6066_slot_device : public device_t, public device_single_card_slot_interface<device_p6066_card_interface>
{
public:
	template <typename T>
	p6066_slot_device(const machine_config &mconfig, const char *tag, device_t *owner, T &&options, const char *default_card)
		: p6066_slot_device(mconfig, tag, owner, 0) { set_options(std::forward<T>(options), default_card, false); }
	p6066_slot_device(const machine_config &, const char *, device_t *, u32 clock = 0);
	void set_position(unsigned position) { m_position = position; }
	void set_base(u16 base) { m_base = base; }
protected:
	virtual void device_start() override;
private:
	required_device<p6066_bus_device> m_bus;
	unsigned m_position = 0;
	u16 m_base = 0;
};
DECLARE_DEVICE_TYPE(P6066_BUS, p6066_bus_device)
DECLARE_DEVICE_TYPE(P6066_SLOT, p6066_slot_device)
#endif
