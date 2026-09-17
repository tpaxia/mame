// license:BSD-3-Clause
// copyright-holders:Salvatore Paxia

#include "emu.h"
#include "uc.h"

#include "go252.h"

namespace {

constexpr offs_t EAROM_BASE = 0xe000;
constexpr offs_t EAROM_END = 0xe0ff;

} // anonymous namespace

olivetti_l1_uc042_device::olivetti_l1_uc042_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock)
	: device_t(mconfig, OLIVETTI_L1_UC042, tag, owner, clock)
	, device_olivetti_l1_cpu_card_interface(mconfig, *this)
	, m_cpu(*this, "maincpu")
	, m_mmu(*this, "mmu")
	, m_pit(*this, "pit")
	, m_acia(*this, "acia")
	, m_earom_nvram(*this, "earom")
	, m_rom(*this, "maincpu")
	, m_isl(*this, "ISL")
{
}

static INPUT_PORTS_START(uc042)
	PORT_START("ISL")
	PORT_CONFNAME(0x02, 0x02, "Console IPL Switch")
	PORT_CONFSETTING(0x02, "ISL1 - Hard Disk")
	PORT_CONFSETTING(0x00, "ISL2 - Floppy Disk")
INPUT_PORTS_END

ioport_constructor olivetti_l1_uc042_device::device_input_ports() const
{
	return INPUT_PORTS_NAME(uc042);
}

void olivetti_l1_uc042_device::device_add_mconfig(machine_config &config)
{
	Z8001(config, m_cpu, 32_MHz_XTAL / 8);
	m_cpu->set_addrmap(AS_PROGRAM, &olivetti_l1_uc042_device::mem_map);
	m_cpu->set_addrmap(AS_DATA, &olivetti_l1_uc042_device::mem_map);
	m_cpu->set_addrmap(z8001_device::AS_STACK, &olivetti_l1_uc042_device::mem_map);
	m_cpu->set_addrmap(AS_IO, &olivetti_l1_uc042_device::io_map);
	m_cpu->set_addrmap(z8001_device::AS_SIO, &olivetti_l1_uc042_device::sio_map);
	m_cpu->viack().set(FUNC(olivetti_l1_uc042_device::vi_ack_r));
	m_cpu->nviack().set(FUNC(olivetti_l1_uc042_device::nviack_r));
	m_cpu->segtack().set(FUNC(olivetti_l1_uc042_device::segtack_r));

	Z8010(config, m_mmu, 32_MHz_XTAL / 8);
	m_mmu->out_segt_cb().set_inputline(m_cpu, z8001_device::SEGT_LINE);

	PIT8253(config, m_pit);
	m_pit->set_clk<0>(32_MHz_XTAL / 16);
	m_pit->out_handler<0>().set(m_pit, FUNC(pit8253_device::write_clk1));
	m_pit->out_handler<1>().set(FUNC(olivetti_l1_uc042_device::pit_out1_w));
	m_pit->out_handler<2>().set(FUNC(olivetti_l1_uc042_device::pit_out2_w));
	m_pit->set_clk<2>(32_MHz_XTAL / 16);

	ACIA6850(config, m_acia, 0);
	m_acia->txd_handler().set(m_acia, FUNC(acia6850_device::write_rxd));
	m_acia->irq_handler().set(FUNC(olivetti_l1_uc042_device::acia_irq_w));

	NVRAM(config, m_earom_nvram, nvram_device::DEFAULT_ALL_0);
}

void olivetti_l1_uc042_device::device_start()
{
	m_arb_timer = timer_alloc(FUNC(olivetti_l1_uc042_device::arb_done), this);
	m_earom_nvram->set_base(m_earom, sizeof(m_earom));

	for (int const spacenum : { int(AS_PROGRAM), int(AS_DATA), int(z8001_device::AS_STACK) })
	{
		address_space &space = m_cpu->space(spacenum);
		space.install_readwrite_handler(0x000000, 0x7fffff,
			read16_delegate(*this, FUNC(olivetti_l1_uc042_device::mem_r)),
			write16_delegate(*this, FUNC(olivetti_l1_uc042_device::mem_w)));
	}

	save_item(NAME(m_nmi_status));
	save_item(NAME(m_kdc_status));
	save_item(NAME(m_lamp));
	save_item(NAME(m_acia_irq));
	save_item(NAME(m_suppress_enabled));
	save_item(NAME(m_timer_vector));
	save_item(NAME(m_acia_vector));
	save_item(NAME(m_timer_out1));
	save_item(NAME(m_timer_pending));
	save_item(NAME(m_arb_req));
	save_item(NAME(m_arb_grant));
	save_item(NAME(m_arb_rel));
	save_item(NAME(m_arb_vieno));
	save_item(NAME(m_masto));
	save_item(NAME(m_earom));
}

void olivetti_l1_uc042_device::device_reset()
{
	m_acia->write_dcd(0);
	m_acia->write_cts(0);
	m_acia_irq = false;
	m_lamp = 0;
	m_nmi_status = 0x01;
	m_kdc_status = 0;
	m_suppress_enabled = true;
	m_timer_pending = false;
	m_timer_out1 = false;
	m_arb_req = 0;
	m_arb_grant = 0;
	m_arb_rel = 0;
	m_arb_vieno = false;
	m_masto = true;
}

olivetti_l1_go252_device *olivetti_l1_uc042_device::video_card() const
{
	for (u8 select = 0; select < 16; select++)
		if (auto *const card = dynamic_cast<olivetti_l1_go252_device *>(bus().get_card(select)))
			return card;
	return nullptr;
}

void olivetti_l1_uc042_device::ready_fault()
{
	// No READY raises NMI.  The RAM-sizing handler expects bit 6 clear for an
	// unpopulated-memory fault and uses bit 7 as the latched NMI cause.
	m_nmi_status = (m_nmi_status & ~0x40) | 0x80;
	m_cpu->set_input_line(z8001_device::NMI_LINE, ASSERT_LINE);
}

bool olivetti_l1_uc042_device::memory_claims(offs_t address) const
{
	address &= 0xffffff;
	return address < 0x4000 || (address >= EAROM_BASE && address <= EAROM_END);
}

u8 olivetti_l1_uc042_device::memory_r(offs_t address)
{
	address &= 0xffffff;
	if (address >= EAROM_BASE && address <= EAROM_END)
		return BIT(address, 0) ? (m_earom[(address - EAROM_BASE) >> 1] & 0x0f) : 0x00;
	return BIT(address, 0) ? (m_rom[address >> 1] & 0xff) : (m_rom[address >> 1] >> 8);
}

void olivetti_l1_uc042_device::memory_w(offs_t address, u8 data)
{
	address &= 0xffffff;
	if (address >= EAROM_BASE && address <= EAROM_END && BIT(address, 0))
		m_earom[(address - EAROM_BASE) >> 1] = data & 0x0f;
}

u16 olivetti_l1_uc042_device::physical_word_r(offs_t address, u16 mem_mask)
{
	address &= 0xffffff;
	u8 high;
	u8 low;
	if (!bus().memory_r(address, high) || !bus().memory_r(address + 1, low))
	{
		ready_fault();
		return 0xffff;
	}
	return (high << 8) | low;
}

void olivetti_l1_uc042_device::physical_word_w(offs_t address, u16 data, u16 mem_mask)
{
	address &= 0xffffff;
	bool responded = true;
	if (ACCESSING_BITS_8_15)
	{
		responded &= bus().memory_w(address, data >> 8);
	}
	if (ACCESSING_BITS_0_7)
	{
		responded &= bus().memory_w(address + 1, data);
	}
	if (!responded)
		ready_fault();
}

z8010_device::memory_result olivetti_l1_uc042_device::xlate(int spacenum, bool write, offs_t address)
{
	int status;
	if (spacenum == AS_PROGRAM)
	{
		status = m_cpu->is_ifetch1() ? z8002_device::ST_IFETCH_1 : z8002_device::ST_IFETCH_N;
		if (status == z8002_device::ST_IFETCH_1)
		{
			// The Z8010 sees all CPU bus cycles, including those it does not
			// translate.  Keep its instruction-address latch in step with IFETCH1.
			m_mmu->ifetch1_observed(address);
		}
	}
	else if (spacenum == z8001_device::AS_STACK)
		status = z8002_device::ST_REQ_STACK;
	else
		status = z8002_device::ST_REQ_DATA;

	// Preserve SN6.  The Z8001 presents a 23-bit logical address (segments
	// 0x00-0x7f); the Z8010 uses SN6 together with URS to decide whether it
	// qualifies for the cycle.  Masking to 22 bits aliases upper segments onto
	// 0x00-0x3f and can turn a harmless probe of an absent upper-range MMU into
	// a violation against an unrelated lower-range descriptor.
	address &= 0x7fffff;
	bool const system = BIT(m_cpu->state_int(Z8000_FCW), 14);
	return m_mmu->translate(address, write, system, false, status);
}

u16 olivetti_l1_uc042_device::mem_r(address_space &space, offs_t offset, u16 mem_mask)
{
	offs_t const address = offset << 1;
	auto const result = xlate(space.spacenum(), false, address);
	if (result.suppress)
		return space.spacenum() == AS_PROGRAM ? 0x8d07 : 0xffff; // instruction violation becomes NOP

	// With no MMU driving the translated-address bus, the UC selects the
	// Z8001 logical-address path directly.
	return physical_word_r(result.address_driven ? result.address : address, mem_mask);
}

void olivetti_l1_uc042_device::mem_w(address_space &space, offs_t offset, u16 data, u16 mem_mask)
{
	offs_t const address = offset << 1;
	auto const result = xlate(space.spacenum(), true, address);
	if (!result.suppress || !m_suppress_enabled)
		physical_word_w(result.address_driven ? result.address : address, data, mem_mask);
}

u8 olivetti_l1_uc042_device::mmu_r(offs_t offset)
{
	if (!BIT(offset, 0))
		return m_mmu->read(offset >> 8);
	return 0xff;
}

void olivetti_l1_uc042_device::mmu_w(offs_t offset, u8 data)
{
	if (!BIT(offset, 0))
		m_mmu->write(offset >> 8, data);
}

u16 olivetti_l1_uc042_device::l1_io_r(offs_t offset, u16 mem_mask)
{
	return bus().io_r(offset, mem_mask);
}

void olivetti_l1_uc042_device::l1_io_w(offs_t offset, u16 data, u16 mem_mask)
{
	bus().io_w(offset, data, mem_mask);
}

u8 olivetti_l1_uc042_device::nmi_status_r()
{
	// bit 0 BBU-valid, bit 1 ISL, bit 4 timer OUT1, bit 6 READY and bit 7 NMI cause
	return (m_nmi_status & ~0x02) | m_isl->read() | (m_timer_out1 ? 0x10 : 0x00);
}

void olivetti_l1_uc042_device::nmi_ack_w(u8 data)
{
	m_nmi_status &= ~0x40;
	m_cpu->set_input_line(z8001_device::NMI_LINE, CLEAR_LINE);
}

u8 olivetti_l1_uc042_device::config_r()
{
	// Reading the configuration register re-enables MMU write suppression.
	if (!machine().side_effects_disabled())
		m_suppress_enabled = true;
	return 0xff;
}

u8 olivetti_l1_uc042_device::suppression_disable_r()
{
	// UC3003 reads 0xff00 to let a violating write reach memory; 0xffa0 restores
	// suppression through config_r().
	if (!machine().side_effects_disabled())
		m_suppress_enabled = false;
	return 0xff;
}

u8 olivetti_l1_uc042_device::keyboard_status_r()
{
	// Overlay the GO252 byte-ready indications on the real 6850 status.
	olivetti_l1_go252_device *const video = video_card();
	u8 const data = m_acia->status_r() | ((video && video->keyboard_data_available()) ? 0x05 : 0x00);
	return data;
}

void olivetti_l1_uc042_device::keyboard_status_w(u8 data)
{
	m_kdc_status = data;
	m_acia->control_w(data);
}

u8 olivetti_l1_uc042_device::keyboard_data_r()
{
	// Keyboard bytes take precedence; otherwise preserve the 6850 loopback used
	// by the UC3003 ACIA test.
	olivetti_l1_go252_device *const video = video_card();
	u8 const data = (video && video->keyboard_data_available()) ? video->keyboard_data_r() : m_acia->data_r();
	return data;
}

void olivetti_l1_uc042_device::keyboard_data_w(u8 data)
{
	// The resident handler uses this as acknowledge/echo while the 6850 still
	// needs the transmitted byte for its diagnostic loopback.
	if (olivetti_l1_go252_device *const video = video_card())
		video->keyboard_data_w(data);
	m_acia->data_w(data);
}

void olivetti_l1_uc042_device::diagnostic_lamps_w(offs_t offset, u8 data)
{
	u8 const lamp = offset & 0x0f;
	if (lamp >= 8 && lamp <= 10)
		m_lamp |= 1 << (lamp - 8);
	else if (lamp <= 2)
		m_lamp &= ~(1 << lamp);
}

void olivetti_l1_uc042_device::io_map(address_map &map)
{
	map.unmap_value_high();
	// L1 I/O uses the high nibble for board select and the low byte for
	// registers; bits 11-8 are not decoded. BCOS uses F084/F08C for VIENO.
	map(0x0000, 0xffff).rw(FUNC(olivetti_l1_uc042_device::l1_io_r), FUNC(olivetti_l1_uc042_device::l1_io_w));
	map(0xf020, 0xf021).mirror(0x0f00).rw(FUNC(olivetti_l1_uc042_device::keyboard_status_r), FUNC(olivetti_l1_uc042_device::keyboard_status_w)).umask16(0xff00);
	map(0xf022, 0xf023).mirror(0x0f00).rw(FUNC(olivetti_l1_uc042_device::keyboard_data_r), FUNC(olivetti_l1_uc042_device::keyboard_data_w)).umask16(0xff00);
	map(0xf041, 0xf041).mirror(0x0f00).rw(FUNC(olivetti_l1_uc042_device::nmi_status_r), FUNC(olivetti_l1_uc042_device::nmi_ack_w));
	map(0xf011, 0xf011).mirror(0x0f00).w(FUNC(olivetti_l1_uc042_device::masto_clear_w));
	map(0xf019, 0xf019).mirror(0x0f00).w(FUNC(olivetti_l1_uc042_device::masto_set_w));
	map(0xf0b1, 0xf0b1).mirror(0x0f00).r(FUNC(olivetti_l1_uc042_device::masto_r));
	map(0xf060, 0xf06f).mirror(0x0f00).rw(FUNC(olivetti_l1_uc042_device::diagnostic_lamps_r), FUNC(olivetti_l1_uc042_device::diagnostic_lamps_w));
	map(0xf080, 0xf08f).mirror(0x0f00).rw(FUNC(olivetti_l1_uc042_device::arb_r), FUNC(olivetti_l1_uc042_device::arb_w));
	map(0xf000, 0xf000).mirror(0x0f00).r(FUNC(olivetti_l1_uc042_device::suppression_disable_r));
	map(0xf001, 0xf001).mirror(0x0f00).w(FUNC(olivetti_l1_uc042_device::timer_vector_w));
	map(0xf0a0, 0xf0a0).mirror(0x0f00).rw(FUNC(olivetti_l1_uc042_device::config_r), FUNC(olivetti_l1_uc042_device::acia_vector_w));
	map(0xf0c0, 0xf0c7).mirror(0x0f00).rw(FUNC(olivetti_l1_uc042_device::pit_r), FUNC(olivetti_l1_uc042_device::pit_w));
	map(0xf0e0, 0xf0e1).mirror(0x0f00).nopw().umask16(0xff00); // console output device not emulated
}

void olivetti_l1_uc042_device::sio_map(address_map &map)
{
	map.unmap_value_high();
	map(0x0000, 0x20ff).rw(FUNC(olivetti_l1_uc042_device::mmu_r), FUNC(olivetti_l1_uc042_device::mmu_w));
}

u16 olivetti_l1_uc042_device::segtack_r()
{
	return m_mmu->segtack_r();
}

u16 olivetti_l1_uc042_device::nmiack_r()
{
	// NMI acknowledge likewise terminates any live suppressed instruction.
	m_mmu->instruction_end();
	return 0;
}

void olivetti_l1_uc042_device::pit_out1_w(int state)
{
	// OUT1 is edge-latched as the UC timer VI source and gated by VIENO.
	if (state && !m_timer_out1)
		m_timer_pending = true;
	m_timer_out1 = bool(state);
	update_vi();
}

void olivetti_l1_uc042_device::update_vi()
{
	m_cpu->set_input_line(z8001_device::VI_LINE, bus().vi_pending() ? ASSERT_LINE : CLEAR_LINE);
}

bool olivetti_l1_uc042_device::local_vi_pending(olivetti_l1_bus_device::interrupt_level level) const
{
	switch (level)
	{
	case olivetti_l1_bus_device::interrupt_level::l1a: return m_acia_irq;
	case olivetti_l1_bus_device::interrupt_level::l1b: return false;
	case olivetti_l1_bus_device::interrupt_level::l2:  return m_timer_pending && m_arb_vieno;
	}
	return false;
}

u16 olivetti_l1_uc042_device::local_viack_r(olivetti_l1_bus_device::interrupt_level level)
{
	if (level == olivetti_l1_bus_device::interrupt_level::l2 && m_timer_pending && m_arb_vieno)
	{
		m_timer_pending = false;
		update_vi();
		return m_timer_vector;
	}
	if (level == olivetti_l1_bus_device::interrupt_level::l1a && m_acia_irq)
		return m_acia_vector;
	return 0;
}

u16 olivetti_l1_uc042_device::vi_ack_r()
{
	m_mmu->instruction_end();
	return bus().viack_r();
}

u16 olivetti_l1_uc042_device::arb_r(offs_t offset)
{
	if (offset == 0)
	{
		// Request readback is independent of the NV2-NV4 delivery masks.
		u8 const high = (BIT(m_arb_req, 0) ? 0x80 : 0) | (BIT(m_arb_req, 1) ? 0x40 : 0)
			| (BIT(m_arb_req, 2) ? 0x20 : 0) | (BIT(m_arb_req, 3) ? 0x10 : 0);
		return high | (m_arb_req ? 0 : 0x07) | ((m_arb_vieno || m_arb_grant) ? 0x08 : 0);
	}
	return 0;
}

void olivetti_l1_uc042_device::arb_update()
{
	u8 grant = 0;
	for (int channel = 0; channel < 4; channel++)
		if (BIT(m_arb_req, channel) && (!channel || BIT(m_arb_rel, channel - 1)))
			grant |= 1 << channel;
	m_arb_grant = grant;
	if (grant)
		m_arb_timer->adjust(attotime::from_usec(50));
	else
	{
		m_arb_timer->adjust(attotime::never);
		m_cpu->set_input_line(z8001_device::NVI_LINE, CLEAR_LINE);
	}
}

void olivetti_l1_uc042_device::arb_w(offs_t offset, u16 data, u16 mem_mask)
{
	// One address strobe per bus cycle, including word OUT at odd ports.
	// A pair of byte handlers would incorrectly strobe both adjacent registers.
	u8 const reg = (offset << 1) | ((mem_mask == 0xffff) ? BIT(m_cpu->io_address(), 0) : !ACCESSING_BITS_8_15);
	if (reg <= 0x3)
	{
		// Acknowledging a request drops that channel's request/grant only.  The
		// release mask is a programmed latch; BCOS relies on it remaining set
		// when it issues the next dispatcher request while the arbiter is idle.
		m_arb_req &= ~(1 << reg);
	}
	else if (reg >= 0x8 && reg <= 0xb)
	{
		m_arb_req |= 1 << (reg - 8);
	}
	else if (reg >= 0x5 && reg <= 0x7)
		m_arb_rel &= ~(1 << (reg - 5));
	else if (reg >= 0xd)
		m_arb_rel |= 1 << (reg - 0xd);

	if (reg >= 0xc)
		m_arb_vieno = true;
	else if (reg >= 0x4 && reg <= 0x7)
		m_arb_vieno = false;
	arb_update();
	update_vi();
}

TIMER_CALLBACK_MEMBER(olivetti_l1_uc042_device::arb_done)
{
	if (m_arb_grant)
		m_cpu->set_input_line(z8001_device::NVI_LINE, ASSERT_LINE);
}

u16 olivetti_l1_uc042_device::nviack_r()
{
	m_mmu->instruction_end();
	// The vector fetch clears NVI but leaves the grant readable until the
	// corresponding per-channel arbiter acknowledgement.
	m_cpu->set_input_line(z8001_device::NVI_LINE, CLEAR_LINE);
	return 0;
}

DEFINE_DEVICE_TYPE(OLIVETTI_L1_UC042, olivetti_l1_uc042_device, "olivetti_l1_uc042", "Olivetti UC042 central unit")
