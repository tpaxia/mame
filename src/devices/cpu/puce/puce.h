// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
#ifndef MAME_CPU_PUCE_PUCE_H
#define MAME_CPU_PUCE_PUCE_H

#pragma once

#include "puce_state.h"

class puce_device : public cpu_device
{
public:
	puce_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock);

	auto select_cb() { return m_select_cb.bind(); }
	auto data_cb() { return m_data_cb.bind(); }
	auto stopped_cb() { return m_stopped_cb.bind(); }
	void set_hold_on_unsupported(bool hold) { m_hold_on_unsupported = hold; }

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void execute_run() override;
	virtual u32 execute_min_cycles() const noexcept override { return 2; }
	virtual u32 execute_max_cycles() const noexcept override { return 2; }
	virtual space_config_vector memory_space_config() const override;
	virtual std::unique_ptr<util::disasm_interface> create_disassembler() override;
	virtual void state_import(const device_state_entry &entry) override;
	virtual void state_export(const device_state_entry &entry) override;

private:
	const address_space_config m_program_config;
	memory_access<16, 1, -1, ENDIANNESS_BIG>::specific m_program;
	puce_state m_core;
	devcb_write8 m_select_cb;
	devcb_write16 m_data_cb;
	devcb_write_line m_stopped_cb;
	bool m_hold_on_unsupported = false;
	bool m_stopped = false;
	u16 m_ir = 0; // fetched memory word; NOT transformed hardware RO
	u16 m_fetch_pc = 0x8000;
	u16 m_debug_pc = 0x8000; // debugger-only import/export, never execution state
	u8 m_phase = 0;
	int m_icount = 0;
};

DECLARE_DEVICE_TYPE(PUCE, puce_device)

#endif // MAME_CPU_PUCE_PUCE_H
