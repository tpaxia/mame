// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
#include "emu.h"
#include "memory.h"
DEFINE_DEVICE_TYPE(P6066_RAM, p6066_ram_device, "p6066_ram", "Olivetti P6066 RAM board")
DEFINE_DEVICE_TYPE(P6066_ROMCA, p6066_romca_device, "p6066_romca", "Olivetti P6066 CAROM board")
DEFINE_DEVICE_TYPE(P6066_ME006, p6066_me006_device, "p6066_me006", "Olivetti P6066 ME006 microprogram RAM")
static INPUT_PORTS_START(memory_address)
	PORT_START("BASE")
	PORT_CONFNAME(0x1fc00, 0x10000, "Memory board base (word address)")
	PORT_CONFSETTING(0x10000, "Default configuration")
	PORT_CONFSETTING(0x0000, "0000")
	PORT_CONFSETTING(0x2000, "2000")
	PORT_CONFSETTING(0x4000, "4000")
	PORT_CONFSETTING(0x6000, "6000")
	PORT_CONFSETTING(0x8000, "8000")
	PORT_CONFSETTING(0x8400, "8400")
	PORT_CONFSETTING(0x8800, "8800")
	PORT_CONFSETTING(0xa000, "A000")
	PORT_CONFSETTING(0xc000, "C000")
	PORT_CONFSETTING(0xe000, "E000")
INPUT_PORTS_END
p6066_ram_device::p6066_ram_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: device_t(mconfig,P6066_RAM,tag,owner,clock), device_p6066_card_interface(mconfig,*this), m_address(*this,"BASE") { }
void p6066_ram_device::device_start()
{
	if (!m_words || m_words>m_ram.size()) fatalerror("Invalid P6066 RAM board capacity");
	save_item(NAME(m_ram));
}
ioport_constructor p6066_ram_device::device_input_ports() const { return INPUT_PORTS_NAME(memory_address); }
u16 p6066_ram_device::base() const { u32 v=m_address->read(); return BIT(v,16) ? slot_base() : v; }
bool p6066_ram_device::memory_claims(u16 address) const { return address>=base() && unsigned(address)-base()<m_words; }
u16 p6066_ram_device::memory_r(u16 address, u16 mask) { return m_ram[address-base()]; }
void p6066_ram_device::memory_w(u16 address,u16 data,u16 mask) { u16 &word=m_ram[address-base()]; word=(word&~mask)|(data&mask); }
p6066_romca_device::p6066_romca_device(const machine_config &mconfig,const char *tag,device_t *owner,u32 clock)
	: device_t(mconfig,P6066_ROMCA,tag,owner,clock), device_p6066_card_interface(mconfig,*this), m_address(*this,"BASE"), m_rom(*this,":carom") { }
ioport_constructor p6066_romca_device::device_input_ports() const { return INPUT_PORTS_NAME(memory_address); }
u16 p6066_romca_device::base() const { u32 v=m_address->read(); return BIT(v,16) ? slot_base() : v; }
bool p6066_romca_device::memory_claims(u16 address) const { return address>=base() && unsigned(address)-base()<m_rom.length(); }
u16 p6066_romca_device::memory_r(u16 address,u16 mask) { return m_rom[address-base()]; }
void p6066_memory_cards(device_slot_interface &device)
{
	device.option_add("ram16",P6066_RAM);
	device.option_add("ram8",P6066_RAM).machine_config([](device_t *card) { downcast<p6066_ram_device &>(*card).set_words(0x1000); });
	device.option_add("romca",P6066_ROMCA);
}

static INPUT_PORTS_START(me006_address)
	PORT_START("DECODE")
	PORT_CONFNAME(0xc000, 0x8000, "ME006 window (word addresses)")
	PORT_CONFSETTING(0x0000, "0000-3FFF")
	PORT_CONFSETTING(0x4000, "4000-7FFF")
	PORT_CONFSETTING(0x8000, "8000-BFFF")
	PORT_CONFSETTING(0xc000, "C000-FFFF")
	PORT_CONFNAME(0x0003, 0x0001, "ME006 excluded initial banks")
	PORT_CONFSETTING(0x0000, "None")
	PORT_CONFSETTING(0x0001, "First 2 Kwords")
	PORT_CONFSETTING(0x0002, "First 4 Kwords")
INPUT_PORTS_END
p6066_me006_device::p6066_me006_device(const machine_config &mconfig,const char *tag,device_t *owner,u32 clock)
	: device_t(mconfig,P6066_ME006,tag,owner,clock), device_p6066_card_interface(mconfig,*this), m_decode(*this,"DECODE") { }
void p6066_me006_device::device_start() { save_item(NAME(m_ram)); }
ioport_constructor p6066_me006_device::device_input_ports() const { return INPUT_PORTS_NAME(me006_address); }
bool p6066_me006_device::memory_claims(u16 address) const
{
	const u16 decode=m_decode->read();
	return (address&0xc000)==(decode&0xc000) && (address&0x3fff)>=(decode&3)*0x800;
}
u16 p6066_me006_device::memory_r(u16 address,u16 mask) { return m_ram[address&0x3fff]; }
void p6066_me006_device::memory_w(u16 address,u16 data,u16 mask)
{
	u16 &word=m_ram[address&0x3fff]; word=(word&~mask)|(data&mask);
}
void p6066_microprogram_cards(device_slot_interface &device) { device.option_add("me006",P6066_ME006); }
