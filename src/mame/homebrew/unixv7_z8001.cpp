// license:BSD-3-Clause
// copyright-holders:Salvatore Paxia
#include "emu.h"
#include "unixv7.h"

#include "cpu/z8000/z8000.h"

namespace {
class z8001unix_state : public unixv7_state
{
public:
	z8001unix_state(machine_config const &c, device_type t, char const *tag) :
		unixv7_state(c,t,tag), m_variant(*this,"maincpu") { }
	void z8001unix(machine_config &config) ATTR_COLD;
protected:
	virtual u32 logical(u32 address, bool program) const override;
	virtual u32 first_address(u32 address) const override;
	virtual bool first_fetch() const override { return m_variant->is_ifetch1(); }
	virtual int fault_line() const override { return z8001_device::SEGT_LINE; }
	virtual u32 address_mask() const override { return 0x7fffff; }
	virtual u32 epu_base() const override { return 0x7f0000; }
	virtual void reset_contexts() override;
private:
	required_device<z8001_device> m_variant;
};

u32 z8001unix_state::logical(u32 address, bool program) const { return address; }
u32 z8001unix_state::first_address(u32 address) const { return address; }
void z8001unix_state::reset_contexts()
{
	for (unsigned p=0;p<32;p++) {
		unsigned const i=127*32+p;
		m_pages[i]=i; m_attr[i]=SYS;
	}
	// EPU stack aliases the current process's kernel stack.
	m_pages[4094]=m_pages[62]; m_pages[4095]=m_pages[63];
}
void z8001unix_state::z8001unix(machine_config &config)
{
	Z8001(config,m_variant,4'000'000);
	m_variant->set_addrmap(AS_PROGRAM,&z8001unix_state::program_map);
	m_variant->set_addrmap(AS_OPCODES,&z8001unix_state::opcode_map);
	m_variant->set_addrmap(AS_DATA,&z8001unix_state::data_map);
	m_variant->set_addrmap(z8001_device::AS_STACK,&z8001unix_state::data_map);
	m_variant->set_addrmap(AS_IO,&z8001unix_state::io_map);
	m_variant->ns().set([this](int state){m_normal=bool(state);});
	m_variant->nviack().set(FUNC(z8001unix_state::nvi_ack));
	m_variant->viack().set(FUNC(z8001unix_state::vi_ack));
	m_variant->segtack().set(FUNC(z8001unix_state::fault_ack));
	board(config,"8M","320K,322K,384K,1M,2M,4M");
}
ROM_START(z8001unix)
	ROM_REGION(0x800,"boot",0)
	ROM_LOAD("unix.rom",0,0x800,NO_DUMP)
ROM_END
} // anonymous namespace
COMP(2026,z8001unix,0,0,z8001unix,0,z8001unix_state,empty_init,"Homebrew","Z8001-unix",MACHINE_NO_SOUND_HW)
