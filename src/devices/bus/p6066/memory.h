// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
#ifndef MAME_BUS_P6066_MEMORY_H
#define MAME_BUS_P6066_MEMORY_H
#pragma once
#include "p6066.h"
class p6066_ram_device : public device_t, public device_p6066_card_interface
{
public:
	p6066_ram_device(const machine_config &, const char *, device_t *, u32 clock = 0);
	void set_words(unsigned words) { m_words=words; }
	virtual bool memory_claims(u16 address) const override;
	virtual u16 memory_r(u16 address, u16 mask) override;
	virtual void memory_w(u16 address, u16 data, u16 mask) override;
protected:
	virtual void device_start() override;
	virtual ioport_constructor device_input_ports() const override;
private:
	u16 base() const;
	required_ioport m_address;
	unsigned m_words=0x2000;
	std::array<u16,0x2000> m_ram{};
};
class p6066_romca_device : public device_t, public device_p6066_card_interface
{
public:
	p6066_romca_device(const machine_config &, const char *, device_t *, u32 clock = 0);
	virtual bool memory_claims(u16 address) const override;
	virtual u16 memory_r(u16 address, u16 mask) override;
protected:
	virtual void device_start() override { }
	virtual ioport_constructor device_input_ports() const override;
private:
	u16 base() const;
	required_ioport m_address;
	required_region_ptr<u16> m_rom;
};
// ME006 manual pp.2.01-2.02: 32 KB, four selectable word-address windows,
// with the first zero, one or two 2-Kword banks optionally excluded.
// Functional storage/decode only; look-ahead timing and parity remain pending.
class p6066_me006_device : public device_t, public device_p6066_card_interface
{
public:
	p6066_me006_device(const machine_config &, const char *, device_t *, u32 clock = 0);
	virtual bool memory_claims(u16 address) const override;
	virtual u16 memory_r(u16 address, u16 mask) override;
	virtual void memory_w(u16 address, u16 data, u16 mask) override;
protected:
	virtual void device_start() override;
	virtual ioport_constructor device_input_ports() const override;
private:
	required_ioport m_decode;
	std::array<u16,0x4000> m_ram{};
};
DECLARE_DEVICE_TYPE(P6066_RAM, p6066_ram_device)
DECLARE_DEVICE_TYPE(P6066_ROMCA, p6066_romca_device)
DECLARE_DEVICE_TYPE(P6066_ME006, p6066_me006_device)
void p6066_memory_cards(device_slot_interface &device);
void p6066_microprogram_cards(device_slot_interface &device);
#endif
