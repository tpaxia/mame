// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
#include "emu.h"
#include "rodma.h"

DEFINE_DEVICE_TYPE(P6066_RODMA, p6066_rodma_device, "p6066_rodma", "Olivetti P6066 RODMA memory bridge")
p6066_rodma_device::p6066_rodma_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: device_t(mconfig, P6066_RODMA, tag, owner, clock), device_p6066_card_interface(mconfig, *this) { }
void p6066_rodma_device::device_start()
{
	m_bus->set_dma_bridge(*this);
	m_timer = timer_alloc(FUNC(p6066_rodma_device::tick), this);
	save_item(NAME(m_dma.mode)); save_item(NAME(m_dma.boundary)); save_item(NAME(m_dma.requests));
	save_item(NAME(m_dma.cpu_pending)); save_item(NAME(m_dma.eligible)); save_item(NAME(m_dma.owner));
	save_item(NAME(m_cpu_cycle.address)); save_item(NAME(m_cpu_cycle.data));
	save_item(NAME(m_cpu_cycle.mask)); save_item(NAME(m_cpu_cycle.write));
	save_item(NAME(m_cycle.address)); save_item(NAME(m_cycle.data));
	save_item(NAME(m_cycle.mask)); save_item(NAME(m_cycle.write));
	save_item(NAME(m_cpu_ready)); save_item(NAME(m_invalid)); save_item(NAME(m_result)); save_item(NAME(m_cpu_result)); save_item(NAME(m_stage));
}
void p6066_rodma_device::device_reset() { reset_transport(); }
void p6066_rodma_device::reset_transport()
{
	m_dma.reset(); m_cpu_ready = false; m_invalid = false; m_stage = 0;
	m_timer->adjust(attotime::never);
}
void p6066_rodma_device::phase(unsigned beta)
{
	if (!beta || m_both_phases) { m_dma.synchronize(); dispatch(); }
}
void p6066_rodma_device::request(unsigned position, bool state)
{
	if (position >= m_dma.requests.size()) fatalerror("RODMA invalid DMA chain position");
	m_dma.requests[position] = state;
	// Asynchronous requests are NOT granted until a phase/queue sampling edge.
}
void p6066_rodma_device::cpu_begin(offs_t address, u16 data, u16 mask)
{
	if (m_dma.cpu_pending || m_dma.owner == 16) fatalerror("RODMA duplicate CPU memory request");
	m_cpu_cycle = { u16(address), data, mask, bool(BIT(address, 16)) };
	m_cpu_ready = false; m_dma.cpu_pending = true;
	dispatch();
}
void p6066_rodma_device::dispatch()
{
	const int owner = m_dma.choose();
	if (owner < 0) return;
	m_invalid = false;
	if (owner == 16) m_cycle = m_cpu_cycle;
	else m_cycle = m_bus->dma_grant(owner);
	m_stage = 1;
	// RODMA fig.1.12: grant-to-STARO budget for nearest GOP is 150 ns.
	// CPU path starts with CK000. RAM timing below is the documented example,
	// pending per-memory-board timing; it is not an HDU-sector shortcut.
	m_timer->adjust(owner == 16 ? attotime::zero : attotime::from_nsec(150));
}
TIMER_CALLBACK_MEMBER(p6066_rodma_device::tick)
{
	if (m_stage == 1)
	{
		m_stage = 2;
		m_timer->adjust(attotime::from_nsec(385));
		return;
	}
	if (m_stage == 2)
	{
		m_result = m_bus->dma_memory_cycle(m_cycle, m_invalid);
		m_stage = 3;
		if (m_dma.owner != 16) m_bus->dma_done(m_dma.owner, m_result, m_invalid);
		// CPU's MT->ME path has an additional 75 ns settling delay.
		m_timer->adjust(attotime::from_nsec(m_dma.owner == 16 ? 75 : 200));
		return;
	}
	if (m_dma.owner == 16)
	{
		m_cpu_result = m_result;
		m_cpu_ready = true;
		if (m_invalid) m_bus->dma_cpu_invalid();
	}
	m_stage = 0;
	m_dma.complete();
	dispatch();
}
