// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
#ifndef MAME_BUS_P6066_DIFO_H
#define MAME_BUS_P6066_DIFO_H
#pragma once
#include "p6066.h"
#include "difo_state.h"
#include "hdu.h"

class p6066_difo_device : public device_t, public device_p6066_card_interface
{
public:
	p6066_difo_device(const machine_config &,const char *,device_t *,u32 clock=0);
	virtual void select(u8 name) override { m_state.select(name); }
	virtual bool direct_selected() const override { return m_state.selected; }
	virtual u16 name_type(unsigned level) override { return m_state.type(m_state.servicing); }
	virtual u8 input_data(unsigned level) override { return m_state.input(drive().ready()); }
	virtual void controller_reset(bool asserted) override;
	virtual void command_word(unsigned level,u16 data,u16 mask) override;
	virtual void output_data_masked(unsigned level,u16 data,u16 mask) override;
	virtual void strobe(unsigned level) override { m_state.strobe(); }
	virtual void interrupt_sync(u8 mask) override { if(mask&8) m_state.synchronize(); }
	virtual u8 irq_requests() const override { return m_state.request ? 4 : 0; }
	virtual void irq_ack(unsigned source) override;
	virtual void irq_end(unsigned level) override;
	virtual p6066_dma_cycle dma_grant() override;
	virtual void dma_done(u16 data,bool invalid) override;
protected:
	virtual void device_start() override;
	virtual void device_reset() override;
	virtual void device_add_mconfig(machine_config &config) override;
private:
	p6066_difo_state m_state;
	required_device_array<p6066_hdu_device,2> m_drives;
	p6066_hdu_device &drive() const { return *m_drives[m_state.unit]; }
	p6066_dma_cycle m_cycle;
	bool m_dma_pending=false, m_dma_granted=false, m_finishing=false, m_dummy=false, m_reset_dma=false;
	emu_timer *m_timer=nullptr, *m_watchdog=nullptr;
	std::array<u8,256> m_buffer{};
	u16 m_byte=0, m_dma_byte=0;
	u8 m_dma_count=0, m_slot=0, m_revolutions=0, m_phase=0, m_active_unit=0;
	bool m_data_crc=true;
	std::array<u8,20> m_format{};
	u8 m_preamble=0, m_key_count=0;
	int m_compare=0;
	bool m_matched=false;
	void begin_media();
	void compare_byte(u8 value,unsigned index);
	TIMER_CALLBACK_MEMBER(timeout);
	void request_data(bool dummy=false);
	void next_sector();
	void finish(u8 errors=0);
	TIMER_CALLBACK_MEMBER(tick);
	void execute();
};
DECLARE_DEVICE_TYPE(P6066_DIFO,p6066_difo_device)
#endif
