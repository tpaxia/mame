// license:BSD-3-Clause
// copyright-holders:Salvatore Paxia
#ifndef MAME_HOMEBREW_UNIXV7_H
#define MAME_HOMEBREW_UNIXV7_H
#pragma once

#include "imagedev/harddriv.h"
#include "machine/ram.h"
#include "machine/terminal.h"
#include "machine/timer.h"

#include <memory>

class unixv7_state : public driver_device
{
protected:
	unixv7_state(machine_config const &c, device_type t, char const *tag) :
		driver_device(c,t,tag), m_cpu(*this,"maincpu"), m_terminal(*this,"terminal"),
		m_disk(*this,"harddisk"), m_rom(*this,"boot"), m_ram(*this,RAM_TAG) { }

	void board(machine_config &config, char const *ram_default, char const *ram_options) ATTR_COLD;
	virtual void machine_start() override ATTR_COLD;
	virtual void machine_reset() override ATTR_COLD;
	void program_map(address_map &map) ATTR_COLD;
	void opcode_map(address_map &map) ATTR_COLD;
	void data_map(address_map &map) ATTR_COLD;
	void io_map(address_map &map) ATTR_COLD;
	virtual u32 logical(u32 address, bool program) const = 0;
	virtual u32 first_address(u32 address) const = 0;
	virtual bool first_fetch() const = 0;
	virtual int fault_line() const = 0;
	virtual u32 address_mask() const = 0;
	virtual u32 epu_base() const = 0;
	virtual void reset_contexts() = 0;
	virtual u16 extra_io_r(u16 port);
	virtual void extra_io_w(u16 port, u16 data);
	u16 nvi_ack() { m_cpu->set_input_line(NVI_LINE, CLEAR_LINE); return 0; }
	u16 vi_ack() { m_disk_irq=false; update_vi(); return 0; }
	u16 fault_ack() { m_cpu->set_input_line(fault_line(), CLEAR_LINE); return 0; }

	bool m_normal=false;
	u16 m_pages[4096]{}, m_attr[4096]{}, m_stack[128]{};
	u8 m_imap[128]{};
	u16 m_select=0;
	static constexpr u16 RO=0x8000, SYS=0x4000;

private:
	// Both Z8000 variants use these external interrupt line numbers.
	static constexpr int NVI_LINE=0, VI_LINE=1;
	static constexpr unsigned SWAP_BYTES=4*1024*1024;
	u16 memory_r(offs_t off, u16 mask, bool program, bool first);
	void memory_w(offs_t off, u16 data, u16 mask, bool program);
	u32 translate(u32 address, unsigned size, bool write, bool program);
	void fault(u32 address, unsigned size, u16 reason);
	u16 io_r(offs_t off, u16 mask = ~0);
	void io_w(offs_t off, u16 data, u16 mask = ~0);
	u8 byte_r(u16 port);
	void byte_w(u16 port, u8 value);
	void key(u8 data);
	void command(u8 value);
	void complete_write();
	void update_vi();
	void upage(u16 frame);
	TIMER_DEVICE_CALLBACK_MEMBER(tick);
	u8 *physical(u32 p) { return p>=epu_base() ? &m_epu[p-epu_base()] : &m_ram->pointer()[p]; }
	u16 physical_r(u32 p) { u8 *b=physical(p); return (b[0]<<8)|b[1]; }
	required_device<cpu_device> m_cpu;
	required_device<generic_terminal_device> m_terminal;
	required_device<harddisk_image_device> m_disk;
	required_region_ptr<u8> m_rom;
	required_device<ram_device> m_ram;
	std::unique_ptr<u8[]> m_epu, m_swap;
	u16 m_stacksel=0, m_fault=0, m_fseg=0, m_flow=0, m_fhigh=0;
	u32 m_first=0, m_fpc=0;
	bool m_disk_irq=false;
	u8 m_rx[256]{};
	u16 m_rxin=0, m_rxout=0;
	u8 m_sc=0,m_sn=0,m_cl=0,m_ch=0,m_dh=0,m_status=0x40,m_error=0;
	u8 m_sector[512]{};
	u16 m_index=0;
	bool m_reading=false,m_writing=false;
};

#endif // MAME_HOMEBREW_UNIXV7_H
