// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia

#ifndef MAME_BUS_OLIVETTI_L1_UC_H
#define MAME_BUS_OLIVETTI_L1_UC_H

#pragma once

#include "l1.h"

#include "cpu/z8000/z8000.h"
#include "machine/6850acia.h"
#include "machine/pit8253.h"
#include "machine/z8010.h"

#include <cstdio>

class olivetti_l1_go252_device;
class olivetti_l1_go280_device;

class olivetti_l1_uc042_device : public device_t, public device_olivetti_l1_cpu_card_interface
{
public:
	olivetti_l1_uc042_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock = 0);

	void crtc_trace_w(offs_t offset, u8 data);
	void floppy_trace_w(offs_t event, u32 data);

	virtual u8 io_r(offs_t offset) override { return 0xff; }

protected:
	virtual void device_add_mconfig(machine_config &config) override ATTR_COLD;
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

private:
	virtual bool memory_claims(offs_t address) const override { return (address & 0xffffff) < 0x4000; }
	virtual u8 memory_r(offs_t address) override;
	virtual void memory_w(offs_t address, u8 data) override { }
	virtual void ready_fault_w(int state) override { if (state) ready_fault(); }
	virtual void bus_vi_w(int state) override { update_vi(); }
	virtual void bus_request_w(int state) override { m_cpu->set_input_line(INPUT_LINE_HALT, state ? ASSERT_LINE : CLEAR_LINE); }
	virtual bool local_vi_pending(olivetti_l1_bus_device::interrupt_level level) const override;
	virtual u16 local_viack_r(olivetti_l1_bus_device::interrupt_level level) override;

	olivetti_l1_go252_device *video_card() const;
	olivetti_l1_go280_device *floppy_card() const;

	void mem_map(address_map &map) ATTR_COLD { }
	void io_map(address_map &map) ATTR_COLD;
	void sio_map(address_map &map) ATTR_COLD;
	u16 l1_io_r(offs_t offset, u16 mem_mask = ~0) { return bus().io_r(offset, mem_mask); }
	void l1_io_w(offs_t offset, u16 data, u16 mem_mask = ~0) { bus().io_w(offset, data, mem_mask); }

	u16 mem_r(address_space &space, offs_t offset, u16 mem_mask);
	void mem_w(address_space &space, offs_t offset, u16 data, u16 mem_mask);
	bool xlate(int spacenum, bool write, offs_t &address);
	u16 physical_word_r(offs_t address, u16 mem_mask);
	void physical_word_w(offs_t address, u16 data, u16 mem_mask);
	void ready_fault();

	u8 mmu_r(offs_t offset);
	void mmu_w(offs_t offset, u8 data);
	u16 segtack_r();
	u16 nmiack_r();

	void console_w(u8 data);
	u8 nmi_status_r();
	void nmi_ack_w(u8 data);
	u8 config_r();
	u8 suppression_disable_r();
	u8 keyboard_status_r();
	void keyboard_status_w(u8 data);
	u8 keyboard_data_r();
	void keyboard_data_w(u8 data);
	u8 pit_r(offs_t offset) { return m_pit->read((offset >> 1) & 3); }
	void pit_w(offs_t offset, u8 data) { m_pit->write((offset >> 1) & 3, data); }

	void acia_irq_w(int state) { m_acia_irq = bool(state); update_vi(); }
	void pit_out1_w(int state);
	void pit_out2_w(int state) { m_acia->write_txc(state); m_acia->write_rxc(state); }
	void update_vi();
	u16 vi_ack_r();
	u16 nviack_r();

	u8 arb_r(offs_t offset);
	void arb_w(offs_t offset, u8 data);
	void arb_update();
	TIMER_CALLBACK_MEMBER(arb_done);

	void masto_clear_w(u8 data) { m_masto = false; }
	void masto_set_w(u8 data) { m_masto = true; }
	u8 masto_r() { return m_masto ? 0x40 : 0x00; }
	u8 diagnostic_lamps_r(offs_t offset) { return 0xc0 | m_lamp | (m_lamp << 3); }
	void diagnostic_lamps_w(offs_t offset, u8 data);
	void timer_vector_w(u8 data) { m_timer_vector = data; }
	void acia_vector_w(u8 data) { m_acia_vector = data; }

	void debug_vram_w(offs_t address, u8 data, u16 mem_mask);
	void debug_crtc_w(u8 reg, u8 data);
	void debug_fdu(char const *event, u8 reg, u8 data);
	void debug_diag_w(offs_t logical, offs_t physical, u16 data, u16 mem_mask);
	void debug_pc_ctx(char const *event);

	required_device<z8001_device> m_cpu;
	required_device<z8010_device> m_mmu;
	required_device<pit8253_device> m_pit;
	required_device<acia6850_device> m_acia;
	required_region_ptr<u16> m_rom;
	u8 m_nmi_status = 0;
	u8 m_mmu_mode = 0;
	u8 m_kdc_status = 0;
	u8 m_lamp = 0;
	bool m_acia_irq = false;
	bool m_suppress_enabled = true;
	u8 m_timer_vector = 0;
	u8 m_acia_vector = 0;
	u32 m_viol_pc = 0xffffffff;
	bool m_timer_out1 = false;
	bool m_timer_pending = false;

	emu_timer *m_arb_timer = nullptr;
	u8 m_arb_req = 0;
	u8 m_arb_grant = 0;
	u8 m_arb_rel = 0;
	bool m_arb_vieno = false;
	bool m_masto = true;

	std::FILE *m_vram_trace = nullptr;
	std::FILE *m_fdu_trace = nullptr;
};

DECLARE_DEVICE_TYPE(OLIVETTI_L1_UC042, olivetti_l1_uc042_device)

#endif // MAME_BUS_OLIVETTI_L1_UC_H
