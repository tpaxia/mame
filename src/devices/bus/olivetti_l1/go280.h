// license:BSD-3-Clause
// copyright-holders:Salvatore Paxia

#ifndef MAME_BUS_OLIVETTI_L1_GO280_H
#define MAME_BUS_OLIVETTI_L1_GO280_H

#pragma once

#include "l1.h"

#include "imagedev/floppy.h"
#include "machine/am9517a.h"
#include "machine/pit8253.h"
#include "machine/upd765.h"

class olivetti_l1_go280_device : public device_t, public device_olivetti_l1_card_interface
{
public:
	olivetti_l1_go280_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock = 0);

	virtual u8 io_r(offs_t offset) override;
	virtual void io_w(offs_t offset, u8 data) override;
	virtual u16 viack_r() override;
	virtual olivetti_l1_bus_device::interrupt_level vi_level() const override { return olivetti_l1_bus_device::interrupt_level::l2; }
	virtual void bus_grant_w(int state) override;

protected:
	virtual void device_add_mconfig(machine_config &config) override ATTR_COLD;
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

private:
	void fdc_intrq_w(int state);
	void fdc_drq_w(int state);
	void fdu_timer_out(int state);
	void fdu_index_w(int state);
	void fdu_head_load_w(int state);
	void dma_hreq_w(int state);
	void dma_eop_w(int state);
	void dma_dack1_w(int state);
	void dma_dack2_w(int state);
	TIMER_CALLBACK_MEMBER(dma_channel1_request);
	TIMER_CALLBACK_MEMBER(dma_channel1_clear);
	u8 dma_fdc_r();
	void dma_fdc_w(u8 data);
	u32 dma_phys(u16 word_address, unsigned byte);
	void dma_memory_fault();
	u8 dma_memr(offs_t offset);
	void dma_memw(offs_t offset, u8 data);
	void update_vi();

	static void floppy_formats(format_registration &fr);

	required_device<upd765_family_device> m_fdc;
	required_device_array<floppy_connector, 4> m_floppy;
	required_device<pit8253_device> m_timer;
	required_device<am9517a_device> m_dmac;

	bool m_fdc_interrupt = false;
	bool m_timer_interrupt = false;
	bool m_fdc_latched = false;
	bool m_timer_latched = false;
	bool m_pending = false;
	bool m_interrupt_enable = false;
	u8 m_vector = 0;
	u8 m_control = 0;
	u8 m_dma_high = 0;
	bool m_fdc_drq = false;
	bool m_fdc_index = false;
	bool m_fdc_head_load = false;
	bool m_dma_fdc_cycle = false;
	bool m_dma_eop = false;
	s8 m_dma_channel = -1;
	std::array<u8, 4> m_dma_mode{};
	std::array<u8, 2> m_dma_buffer{};
	u8 m_dma_buffer_pos = 0;
	bool m_fumeo = false;
	bool m_perro = false;
};

DECLARE_DEVICE_TYPE(OLIVETTI_L1_GO280, olivetti_l1_go280_device)

#endif // MAME_BUS_OLIVETTI_L1_GO280_H
