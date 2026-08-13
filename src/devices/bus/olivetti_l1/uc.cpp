// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia

#include "emu.h"
#include "uc.h"

#include "go252.h"
#include "go280.h"

#include <cstdlib>

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
{
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

	if (char const *const path = std::getenv("M40_VRAM_TRACE"); path && path[0])
		m_vram_trace = std::fopen(path, "w");
	if (char const *const path = std::getenv("M40_FDU_TRACE"); path && path[0])
		m_fdu_trace = std::fopen(path, "w");

	for (int const spacenum : { int(AS_PROGRAM), int(AS_DATA), int(z8001_device::AS_STACK) })
	{
		address_space &space = m_cpu->space(spacenum);
		space.install_readwrite_handler(0x000000, 0x7fffff,
			read16_delegate(*this, FUNC(olivetti_l1_uc042_device::mem_r)),
			write16_delegate(*this, FUNC(olivetti_l1_uc042_device::mem_w)));
	}

	save_item(NAME(m_nmi_status));
	save_item(NAME(m_mmu_mode));
	save_item(NAME(m_kdc_status));
	save_item(NAME(m_lamp));
	save_item(NAME(m_acia_irq));
	save_item(NAME(m_suppress_enabled));
	save_item(NAME(m_timer_vector));
	save_item(NAME(m_acia_vector));
	save_item(NAME(m_viol_pc));
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
	m_mmu_mode = 0;
	m_kdc_status = 0;
	m_suppress_enabled = true;
	m_timer_pending = false;
	m_timer_out1 = false;
	m_viol_pc = 0xffffffff;
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

olivetti_l1_go280_device *olivetti_l1_uc042_device::floppy_card() const
{
	for (u8 select = 0; select < 16; select++)
		if (auto *const card = dynamic_cast<olivetti_l1_go280_device *>(bus().get_card(select)))
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
		if ((address & 0xff0000) == 0xff0000)
			debug_vram_w(address & 0xffff, data >> 8, mem_mask);
	}
	if (ACCESSING_BITS_0_7)
	{
		responded &= bus().memory_w(address + 1, data);
		if ((address & 0xff0000) == 0xff0000)
			debug_vram_w((address + 1) & 0xffff, data, mem_mask);
	}
	if (!responded)
		ready_fault();
}

bool olivetti_l1_uc042_device::xlate(int spacenum, bool write, offs_t &address)
{
	int status;
	if (spacenum == AS_PROGRAM)
	{
		status = m_cpu->is_ifetch1() ? z8002_device::ST_IFETCH_1 : z8002_device::ST_IFETCH_N;
		if (status == z8002_device::ST_IFETCH_1)
		{
			m_viol_pc = 0xffffffff;
			// The Z8010 sees all CPU bus cycles, including those it does not
			// translate.  Keep its instruction-address latch in step with IFETCH1.
			m_mmu->ifetch1_observed(address);
		}
	}
	else if (spacenum == z8001_device::AS_STACK)
		status = z8002_device::ST_REQ_STACK;
	else
		status = z8002_device::ST_REQ_DATA;

	if (!(m_mmu_mode & 0x80))
		return true;
	address &= 0x3fffff;
	bool const system = BIT(m_cpu->state_int(Z8000_FCW), 14);
	return m_mmu->translate(address, write, system, false, status);
}

u16 olivetti_l1_uc042_device::mem_r(address_space &space, offs_t offset, u16 mem_mask)
{
	offs_t address = offset << 1;
	// SUP suppresses the violating transfer and subsequent data accesses through
	// the end of the instruction.  A first-word fetch releases it in xlate().
	if (!xlate(space.spacenum(), false, address))
	{
		if (m_suppress_enabled)
			m_viol_pc = m_cpu->pc();
		return space.spacenum() == AS_PROGRAM ? 0x8d07 : 0xffff; // instruction violation becomes NOP
	}
	if (m_viol_pc == m_cpu->pc() && space.spacenum() != AS_PROGRAM)
		return 0xffff;

	if (space.spacenum() == AS_PROGRAM)
	{
		static u32 last_pc = 0xffffffff;
		u32 const pc = m_cpu->pc();
		if (pc != last_pc)
		{
			last_pc = pc;
			switch (pc)
			{
			case 0x00027fde: debug_pc_ctx("7fde-entry"); break;
			case 0x00027fe8: debug_pc_ctx("7fe8-fatal"); break;
			case 0x00028002: debug_pc_ctx("8002-return"); break;
			case 0x0002a800: debug_pc_ctx("a800-call"); break;
			case 0x0002a92a: debug_pc_ctx("a92a-call"); break;
			case 0x0002b668: debug_pc_ctx("b668-call"); break;
			case 0x00044586: debug_pc_ctx("4-4586-call"); break;
			case 0x0021018c: debug_pc_ctx("rv-setup"); break;
			case 0x002102b4: debug_pc_ctx("rv-fill"); break;
			case 0x002102ce: debug_pc_ctx("rv-march1"); break;
			case 0x00210366: debug_pc_ctx("rv-mismatch-a"); break;
			case 0x0021038a: debug_pc_ctx("rv-errprint"); break;
			case 0x00210304: debug_pc_ctx("uc-0304-gate"); break;
			case 0x00210330: debug_pc_ctx("uc-dispatch"); break;
			case 0x00210358: debug_pc_ctx("uc-t1-entry"); break;
			case 0x002106dc: debug_pc_ctx("uc-invalid-test"); break;
			case 0x00210720: debug_pc_ctx("uc-next-test"); break;
			case 0x00213576: debug_pc_ctx("uc-trap-body"); break;
			case 0x002134f6: debug_pc_ctx("uc-trap-handler"); break;
			case 0x00210ea2: debug_pc_ctx("uc-error"); break;
			case 0x00210180: debug_pc_ctx("uc-0180-back"); break;
			}
		}
	}
	return physical_word_r(address, mem_mask);
}

void olivetti_l1_uc042_device::mem_w(address_space &space, offs_t offset, u16 data, u16 mem_mask)
{
	offs_t address = offset << 1;
	offs_t const logical = address;
	bool valid = xlate(space.spacenum(), true, address);
	if (!valid && !m_suppress_enabled)
	{
		physical_word_w(address, data, mem_mask);
		return;
	}
	if (!valid)
		m_viol_pc = m_cpu->pc();
	else if (m_viol_pc == m_cpu->pc())
		valid = false;
	if (valid)
	{
		debug_diag_w(logical, address, data, mem_mask);
		physical_word_w(address, data, mem_mask);
	}
}

u8 olivetti_l1_uc042_device::mmu_r(offs_t offset)
{
	if (!BIT(offset, 0))
	{
		u8 const reg = offset >> 8;
		u8 const data = m_mmu->read(reg);
		if (m_fdu_trace && (reg == 0x0b || reg == 0x01 || reg == 0x20 || reg == 0x0f || reg == 0x05))
		{
			std::fprintf(m_fdu_trace, "MMU R pc=%08X off=%04X reg=%02X data=%02X\n", unsigned(m_cpu->pc()), unsigned(offset), reg, data);
			std::fflush(m_fdu_trace);
		}
		return data;
	}
	return 0xff;
}

void olivetti_l1_uc042_device::mmu_w(offs_t offset, u8 data)
{
	if (!BIT(offset, 0))
	{
		u8 const reg = offset >> 8;
		if (m_fdu_trace && (reg == 0x0b || reg == 0x01 || reg == 0x20 || reg == 0x0f || reg == 0x05))
		{
			std::fprintf(m_fdu_trace, "MMU W pc=%08X off=%04X reg=%02X data=%02X\n", unsigned(m_cpu->pc()), unsigned(offset), reg, data);
			std::fflush(m_fdu_trace);
		}
		m_mmu->write(reg, data);
		if (reg == 0x00)
		{
			// A mode change affects translation immediately.  Invalidating on every
			// descriptor byte would disrupt the firmware's SOTIRB block load.
			m_mmu_mode = data;
			m_cpu->space(AS_PROGRAM).invalidate_caches(read_or_write::READWRITE);
			m_cpu->space(AS_DATA).invalidate_caches(read_or_write::READWRITE);
			m_cpu->space(z8001_device::AS_STACK).invalidate_caches(read_or_write::READWRITE);
		}
	}
}

void olivetti_l1_uc042_device::console_w(u8 data)
{
	osd_printf_info("[M40 console] code = 0x%02X (%d)\n", data, data);
}

u8 olivetti_l1_uc042_device::nmi_status_r()
{
	// bit 0 BBU-valid, bit 1 ISL, bit 4 timer OUT1, bit 6 READY and bit 7 NMI cause
	return m_nmi_status | (m_timer_out1 ? 0x10 : 0x00);
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
	return m_acia->status_r() | ((video && video->keyboard_data_available()) ? 0x05 : 0x00);
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
	return (video && video->keyboard_data_available()) ? video->keyboard_data_r() : m_acia->data_r();
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
	map(0x0000, 0xffff).rw(FUNC(olivetti_l1_uc042_device::l1_io_r), FUNC(olivetti_l1_uc042_device::l1_io_w));
	map(0xff20, 0xff21).rw(FUNC(olivetti_l1_uc042_device::keyboard_status_r), FUNC(olivetti_l1_uc042_device::keyboard_status_w)).umask16(0xff00);
	map(0xff22, 0xff23).rw(FUNC(olivetti_l1_uc042_device::keyboard_data_r), FUNC(olivetti_l1_uc042_device::keyboard_data_w)).umask16(0xff00);
	map(0xff41, 0xff41).rw(FUNC(olivetti_l1_uc042_device::nmi_status_r), FUNC(olivetti_l1_uc042_device::nmi_ack_w));
	map(0xff11, 0xff11).w(FUNC(olivetti_l1_uc042_device::masto_clear_w));
	map(0xff19, 0xff19).w(FUNC(olivetti_l1_uc042_device::masto_set_w));
	map(0xffb1, 0xffb1).r(FUNC(olivetti_l1_uc042_device::masto_r));
	map(0xff60, 0xff6f).rw(FUNC(olivetti_l1_uc042_device::diagnostic_lamps_r), FUNC(olivetti_l1_uc042_device::diagnostic_lamps_w));
	map(0xff80, 0xff8f).rw(FUNC(olivetti_l1_uc042_device::arb_r), FUNC(olivetti_l1_uc042_device::arb_w));
	map(0xff00, 0xff00).r(FUNC(olivetti_l1_uc042_device::suppression_disable_r));
	map(0xff01, 0xff01).w(FUNC(olivetti_l1_uc042_device::timer_vector_w));
	map(0xffa0, 0xffa0).rw(FUNC(olivetti_l1_uc042_device::config_r), FUNC(olivetti_l1_uc042_device::acia_vector_w));
	map(0xffc0, 0xffc7).rw(FUNC(olivetti_l1_uc042_device::pit_r), FUNC(olivetti_l1_uc042_device::pit_w));
	map(0xffe0, 0xffe0).w(FUNC(olivetti_l1_uc042_device::console_w));
}

void olivetti_l1_uc042_device::sio_map(address_map &map)
{
	map.unmap_value_high();
	map(0x0000, 0x20ff).rw(FUNC(olivetti_l1_uc042_device::mmu_r), FUNC(olivetti_l1_uc042_device::mmu_w));
}

u16 olivetti_l1_uc042_device::segtack_r()
{
	// Acknowledge ends the violating instruction.  Releasing SUP here is required
	// before the trap dispatcher reads its PSA and pushes a trap frame.
	m_viol_pc = 0xffffffff;
	return m_mmu->segtack_r();
}

u16 olivetti_l1_uc042_device::nmiack_r()
{
	// NMI acknowledge likewise terminates any live suppressed instruction.
	m_viol_pc = 0xffffffff;
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
	return bus().viack_r();
}

u8 olivetti_l1_uc042_device::arb_r(offs_t offset)
{
	u8 const reg = offset & 0x0f;
	if (reg == 1)
	{
		u8 const high = (BIT(m_arb_grant, 0) ? 0x80 : 0) | (BIT(m_arb_grant, 1) ? 0x40 : 0)
			| (BIT(m_arb_grant, 2) ? 0x20 : 0) | (BIT(m_arb_grant, 3) ? 0x10 : 0);
		return high | (m_arb_grant ? 0 : 0x07) | ((m_arb_vieno || m_arb_grant) ? 0x08 : 0);
	}
	return 0;
}

void olivetti_l1_uc042_device::arb_update()
{
	u8 grant = 0;
	for (int channel = 0; channel < 4; channel++)
		if (BIT(m_arb_req, channel) && m_arb_rel >= channel)
			grant |= 1 << channel;
	m_arb_grant = grant;
	if (grant)
		m_arb_timer->adjust(attotime::from_usec(50));
}

void olivetti_l1_uc042_device::arb_w(offs_t offset, u8 data)
{
	u8 const reg = offset & 0x0f;
	switch (reg)
	{
	case 0x0: case 0x1: case 0x2: case 0x3:
		m_arb_req &= ~(1 << reg);
		if (!m_arb_req)
			m_arb_rel = 0;
		break;
	case 0x8: case 0x9: case 0xa: case 0xb:
		m_arb_req |= 1 << (reg - 8);
		break;
	case 0x5: case 0xd: if (m_arb_rel < 1) m_arb_rel = 1; break;
	case 0x6: case 0xe: if (m_arb_rel < 2) m_arb_rel = 2; break;
	case 0x7: case 0xf: if (m_arb_rel < 3) m_arb_rel = 3; break;
	default: break;
	}
	if (reg >= 0xc)
		m_arb_vieno = true;
	else if (reg >= 0x4 && reg <= 0x7)
		m_arb_vieno = false;
	arb_update();
}

TIMER_CALLBACK_MEMBER(olivetti_l1_uc042_device::arb_done)
{
	if (m_arb_grant)
		m_cpu->set_input_line(z8001_device::NVI_LINE, ASSERT_LINE);
}

u16 olivetti_l1_uc042_device::nviack_r()
{
	// The vector fetch clears NVI but leaves the grant readable until the
	// corresponding per-channel arbiter acknowledgement.
	m_cpu->set_input_line(z8001_device::NVI_LINE, CLEAR_LINE);
	return 0;
}

void olivetti_l1_uc042_device::debug_vram_w(offs_t address, u8 data, u16 mem_mask)
{
	if (!m_vram_trace)
		return;
	std::fprintf(m_vram_trace, "VRAM pc=%08X off=%04X data=%02X mask=%04X\n", unsigned(m_cpu->pc()), unsigned(address & 0xffff), data, mem_mask);
	if (m_cpu->pc() == 0x00031156)
	{
		std::fprintf(m_vram_trace, "VRAMCTX pc=00031156");
		for (int reg = 0; reg < 16; reg++)
			std::fprintf(m_vram_trace, " r%d=%04X", reg, unsigned(m_cpu->state_int(Z8000_R0 + reg)));
		std::fputc('\n', m_vram_trace);
	}
	std::fflush(m_vram_trace);
}

void olivetti_l1_uc042_device::debug_crtc_w(u8 reg, u8 data)
{
	if (m_vram_trace)
	{
		std::fprintf(m_vram_trace, "CRTC pc=%08X reg=%02X data=%02X\n", unsigned(m_cpu->pc()), reg, data);
		std::fflush(m_vram_trace);
	}
}

void olivetti_l1_uc042_device::debug_fdu(char const *event, u8 reg, u8 data)
{
	olivetti_l1_go280_device *const floppy = floppy_card();
	if (m_fdu_trace && floppy)
	{
		std::fprintf(m_fdu_trace,
			"FDU %s pc=%08X reg=%02X data=%02X pending=%d ien=%d intmo_lat=%d timer=%d intoo_lat=%d fdc=%d vec=%02X dma_hi=%02X dma_ch1=%04X dma_byte=%06X\n",
			event, unsigned(m_cpu->pc()), reg, data, floppy->pending(), floppy->interrupt_enabled(),
			floppy->timer_latched(), floppy->timer_interrupt(), floppy->fdc_latched(), floppy->fdc_interrupt(),
			floppy->vector(), floppy->dma_high(), floppy->dma_channel1(), unsigned(floppy->dma_byte()));
		std::fflush(m_fdu_trace);
	}
}

void olivetti_l1_uc042_device::debug_diag_w(offs_t logical, offs_t physical, u16 data, u16 mem_mask)
{
	if (m_fdu_trace && logical >= 0x04a480 && logical <= 0x04a4bf)
		std::fprintf(m_fdu_trace, "DIAGW pc=%08X log=%06X phys=%06X data=%04X mask=%04X\n", unsigned(m_cpu->pc()), unsigned(logical), unsigned(physical), data, mem_mask);
	if (m_fdu_trace && logical >= 0x048f40 && logical <= 0x048f80)
	{
		std::fprintf(m_fdu_trace, "ERRBUF pc=%08X log=%06X phys=%06X data=%04X mask=%04X", unsigned(m_cpu->pc()), unsigned(logical), unsigned(physical), data, mem_mask);
		for (int reg = 0; reg < 16; reg++)
			std::fprintf(m_fdu_trace, " r%d=%04X", reg, unsigned(m_cpu->state_int(Z8000_R0 + reg)));
		std::fputc('\n', m_fdu_trace);
	}
	if (m_fdu_trace)
		std::fflush(m_fdu_trace);
}

void olivetti_l1_uc042_device::debug_pc_ctx(char const *event)
{
	if (!m_fdu_trace)
		return;
	std::fprintf(m_fdu_trace, "PCCTX %s pc=%08X", event, unsigned(m_cpu->pc()));
	for (int reg = 0; reg < 16; reg++)
		std::fprintf(m_fdu_trace, " r%d=%04X", reg, unsigned(m_cpu->state_int(Z8000_R0 + reg)));
	std::fputc('\n', m_fdu_trace);
	std::fflush(m_fdu_trace);
}

void olivetti_l1_uc042_device::crtc_trace_w(offs_t offset, u8 data)
{
	debug_crtc_w(offset, data);
}

void olivetti_l1_uc042_device::floppy_trace_w(offs_t event, u32 data)
{
	static char const *const names[] = { "R", "W", "FDCINT", "TIMER", "VIACK", "INDEX", "DMAW" };
	u8 const reg = data >> 8;
	u8 const value = data;
	if (event < std::size(names) && event != olivetti_l1_go280_device::TRACE_DMA_W)
		debug_fdu(names[event], reg, value);

	if (event == olivetti_l1_go280_device::TRACE_DMA_W && m_fdu_trace)
	{
		olivetti_l1_go280_device *const floppy = floppy_card();
		u32 const position = (floppy->dma_byte() - 1) & 0xffffff;
		if (position < 0x20 || !(position & 0xff))
		{
			std::fprintf(m_fdu_trace, "DMAW pc=%08X pos=%06X phys=%06X data=%02X dma_hi=%02X dma_ch1=%04X\n",
				unsigned(m_cpu->pc()), unsigned(position), unsigned(floppy->last_dma_address()), value,
				floppy->dma_high(), floppy->dma_channel1());
			std::fflush(m_fdu_trace);
		}
	}
}

DEFINE_DEVICE_TYPE(OLIVETTI_L1_UC042, olivetti_l1_uc042_device, "olivetti_l1_uc042", "Olivetti UC042 central unit")
