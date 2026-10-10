// license:BSD-3-Clause
// copyright-holders:Salvatore Paxia
#include "emu.h"
#include "unixv7.h"

#include "cpu/z8000/z8000.h"

namespace {
class z8002unix_state : public unixv7_state
{
public:
	z8002unix_state(machine_config const &c, device_type t, char const *tag) :
		unixv7_state(c,t,tag), m_variant(*this,"maincpu") { }
	void z8002unix(machine_config &config) ATTR_COLD;
protected:
	virtual u32 logical(u32 address, bool program) const override;
	virtual u32 first_address(u32 address) const override;
	virtual bool first_fetch() const override { return m_variant->is_ifetch1(); }
	virtual int fault_line() const override { return z8002_device::NMI_LINE; }
	virtual u32 address_mask() const override { return 0xffff; }
	virtual u32 epu_base() const override { return 0xf0000; }
	virtual void reset_contexts() override;
	virtual void machine_start() override ATTR_COLD;
	virtual u16 extra_io_r(u16 port) override;
	virtual void extra_io_w(u16 port, u16 data) override;
private:
	required_device<z8002_device> m_variant;
	u8 m_user=0, m_system=0, m_data=1;
	u16 m_window=0xffff;
};

// External system/normal and I/D signals select the current MMU context.
u32 z8002unix_state::logical(u32 address, bool program) const
{
	unsigned const off=address&65535;
	if (m_normal) return (u32(m_user)<<16)|off;
	if (program) return (u32(m_system)<<16)|off;
	if (m_window!=0xffff && off>=0xe000 && off<0xe800)
		return (u32(m_window>>5)<<16)|((m_window&31)<<11)|(off&2047);
	return (u32(m_data)<<16)|off;
}
u32 z8002unix_state::first_address(u32 address) const
{
	return (u32(m_normal?m_user:1)<<16)|(address&65535);
}
void z8002unix_state::reset_contexts()
{
	m_user=m_system=0; m_data=1; m_window=0xffff;
}
void z8002unix_state::machine_start()
{
	unixv7_state::machine_start();
	save_item(NAME(m_user)); save_item(NAME(m_system));
	save_item(NAME(m_data)); save_item(NAME(m_window));
}
u16 z8002unix_state::extra_io_r(u16 port)
{
	switch (port) {
	case 0xbe: return m_pages[m_select]|m_attr[m_select];
	case 0xd6: return m_window;
	default: return 0xdead;
	}
}
void z8002unix_state::extra_io_w(u16 port, u16 data)
{
	switch (port) {
	case 0xd4: m_user=data&127; break;
	case 0xd6: m_window=data; break;
	case 0xd8: m_system=data&127; break;
	case 0xda: m_data=data&127; break;
	}
}
void z8002unix_state::z8002unix(machine_config &config)
{
	Z8002(config,m_variant,4'000'000);
	m_variant->set_addrmap(AS_PROGRAM,&z8002unix_state::program_map);
	m_variant->set_addrmap(AS_OPCODES,&z8002unix_state::opcode_map);
	m_variant->set_addrmap(AS_DATA,&z8002unix_state::data_map);
	m_variant->set_addrmap(z8002_device::AS_STACK,&z8002unix_state::data_map);
	m_variant->set_addrmap(AS_IO,&z8002unix_state::io_map);
	m_variant->ns().set([this](int state){m_normal=bool(state);});
	m_variant->nviack().set(FUNC(z8002unix_state::nvi_ack));
	m_variant->viack().set(FUNC(z8002unix_state::vi_ack));
	m_variant->nmiack().set(FUNC(z8002unix_state::fault_ack));
	board(config,"1M","320K,322K,384K,512K,768K");
}
ROM_START(z8002unix)
	ROM_REGION(0x800,"boot",0)
	ROM_LOAD("unix.rom",0,0x800,NO_DUMP)
ROM_END
} // anonymous namespace
COMP(2026,z8002unix,0,0,z8002unix,0,z8002unix_state,empty_init,"Homebrew","Z8002-unix",MACHINE_NO_SOUND_HW)
