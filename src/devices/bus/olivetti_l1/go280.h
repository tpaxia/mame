// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia

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
	enum trace_event : unsigned
	{
		TRACE_IO_R,
		TRACE_IO_W,
		TRACE_FDC_INT,
		TRACE_TIMER,
		TRACE_VI_ACK,
		TRACE_INDEX,
		TRACE_DMA_W
	};

	olivetti_l1_go280_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock = 0);

	auto trace_callback() { return m_trace_cb.bind(); }

	bool pending() const { return m_pending; }
	bool interrupt_enabled() const { return m_interrupt_enable; }
	bool timer_latched() const { return m_timer_latched; }
	bool timer_interrupt() const { return m_timer_interrupt; }
	bool fdc_latched() const { return m_fdc_latched; }
	bool fdc_interrupt() const { return m_fdc_interrupt; }
	u8 vector() const { return m_vector; }
	u8 dma_high() const { return m_dma_high; }
	u16 dma_channel1() const { return m_dma_channel1; }
	u32 dma_byte() const { return m_dma_byte; }
	u32 last_dma_address() const { return m_last_dma_address; }

	virtual u8 io_r(offs_t offset) override;
	virtual void io_w(offs_t offset, u8 data) override;
	virtual u16 viack_r() override;
	virtual olivetti_l1_bus_device::interrupt_level vi_level() const override { return olivetti_l1_bus_device::interrupt_level::l2; }
	virtual void bus_grant_w(int state) override { m_dmac->hack_w(state); }

protected:
	virtual void device_add_mconfig(machine_config &config) override ATTR_COLD;
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

private:
	void fdc_intrq_w(int state);
	void fdc_drq_w(int state);
	void fdu_timer_out(int state);
	void fdu_index_w(int state);
	void dma_hreq_w(int state);
	void dma_eop_w(int state);
	u32 dma_phys();
	u8 dma_memr(offs_t offset);
	void dma_memw(offs_t offset, u8 data);
	void update_vi();
	void trace(trace_event event, u8 reg = 0, u8 data = 0) { m_trace_cb(event, u32(reg) << 8 | data); }

	static void floppy_formats(format_registration &fr);

	required_device<upd765_family_device> m_fdc;
	required_device_array<floppy_connector, 4> m_floppy;
	required_device<pit8253_device> m_timer;
	required_device<am9517a_device> m_dmac;
	devcb_write32 m_trace_cb;

	bool m_fdc_interrupt = false;
	bool m_timer_interrupt = false;
	bool m_fdc_latched = false;
	bool m_timer_latched = false;
	bool m_pending = false;
	bool m_interrupt_enable = false;
	u8 m_vector = 0;
	u8 m_dma_high = 0;
	u16 m_dma_channel1 = 0;
	bool m_dma_flipflop = false;
	u32 m_dma_byte = 0;
	u32 m_last_dma_address = 0;
};

DECLARE_DEVICE_TYPE(OLIVETTI_L1_GO280, olivetti_l1_go280_device)

#endif // MAME_BUS_OLIVETTI_L1_GO280_H
