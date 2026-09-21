// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
#ifndef MAME_BUS_P6066_RODMA_H
#define MAME_BUS_P6066_RODMA_H
#pragma once
#include "p6066.h"

class p6066_rodma_device : public device_t, public device_p6066_card_interface
{
public:
	p6066_rodma_device(const machine_config &, const char *, device_t *, u32 clock = 20'000'000);
	void set_partition(unsigned mode, u32 boundary) { m_dma.mode = mode; m_dma.boundary = boundary; }
	void set_dmaro(bool both_phases) { m_both_phases = both_phases; }
	bool shared(u16 address) const { return m_dma.shared(address); }
	void phase(unsigned beta);
	void request(unsigned position, bool state);
	void cpu_begin(offs_t address, u16 data, u16 mask);
	bool cpu_ready() const { return m_cpu_ready; }
	u16 cpu_data() const { return m_cpu_result; }
	void reset_transport();
protected:
	virtual void device_start() override;
	virtual void device_reset() override;
private:
	p6066_dma_arbiter m_dma;
	p6066_dma_cycle m_cpu_cycle, m_cycle;
	emu_timer *m_timer = nullptr;
	bool m_both_phases = false;
	bool m_cpu_ready = false;
	bool m_invalid = false;
	u16 m_result = 0, m_cpu_result = 0;
	u8 m_stage = 0;
	void dispatch();
	TIMER_CALLBACK_MEMBER(tick);
};
DECLARE_DEVICE_TYPE(P6066_RODMA, p6066_rodma_device)
#endif
