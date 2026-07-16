// license:BSD-3-Clause
// copyright-holders:
/***************************************************************************

    Olivetti M40 (L1 line) — first-pass bring-up skeleton

    Central unit (UC042): Zilog Z8001 (segmented) + Z8010 MMU + 8253 PIT,
    running the resident autodiagnostic ROM (REL 6.0).

    This is a bring-up harness to see how far the ROM's self-test gets:
      * Z8001 + a single Z8010 MMU (from the System 8000 wiring pattern).
      * ROM at physical 0; contiguous RAM from bank 1 (0x010000); a 64 KB
        video window at 0xFF0000.
      * Unpopulated physical access -> NMI with 0xFF41 bit 6 set (the READY
        fault the ROM's RAM sizing / slot scan rely on).
      * The diagnostic console code latch (I/O 0xFFE0) is printed to stdout.

    See ../../../../L1_M30_M40/HARDWARE.md for the reverse-engineered spec.

***************************************************************************/

#include "emu.h"

#include "cpu/z8000/z8000.h"
#include "machine/z8010.h"
#include "machine/pit8253.h"
#include "machine/ram.h"

namespace {

class m40_state : public driver_device
{
public:
	m40_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
		, m_mmu(*this, "mmu")
		, m_pit(*this, "pit")
		, m_ram(*this, RAM_TAG)
		, m_rom(*this, "maincpu")
	{ }

	void m40(machine_config &config);

protected:
	virtual void machine_start() override ATTR_COLD;
	virtual void machine_reset() override ATTR_COLD;

private:
	required_device<z8001_device> m_maincpu;
	required_device<z8010_device> m_mmu;
	required_device<pit8253_device> m_pit;
	required_device<ram_device> m_ram;
	required_region_ptr<uint16_t> m_rom;

	std::unique_ptr<uint8_t[]> m_vram;   // 64 KB video window @ 0xFF0000
	uint8_t *m_ramptr = nullptr;
	uint32_t m_ramsize = 0;
	uint8_t m_ff41 = 0;
	uint8_t m_mmu_mode = 0;   // shadow of Z8010 mode reg (bit7 = master enable)
	emu_timer *m_arb_timer = nullptr;
	bool m_arb_busy = false;  // a bus-arbitration cycle is in progress

	// translated memory access over the whole segmented space
	uint16_t mem_r(address_space &space, offs_t offset, uint16_t mem_mask);
	void     mem_w(address_space &space, offs_t offset, uint16_t data, uint16_t mem_mask);
	bool     xlate(int spacenum, bool write, offs_t &addr);
	uint16_t phys_r(offs_t addr, uint16_t mem_mask);
	void     phys_w(offs_t addr, uint16_t data, uint16_t mem_mask);
	void     ready_fault();

	// MMU special-I/O programming
	uint8_t  mmu_r(offs_t offset);
	void     mmu_w(offs_t offset, uint8_t data);

	// UC I/O registers
	void     console_w(uint8_t data);       // 0xFFE0 diagnostic code latch -> stdout
	uint8_t  ff41_r();
	void     ff41_w(uint8_t data);
	uint8_t  ffa0_r();
	uint8_t  pit_r(offs_t offset)            { return m_pit->read((offset >> 1) & 3); }
	void     pit_w(offs_t offset, uint8_t d) { m_pit->write((offset >> 1) & 3, d); }

	// MB15652 bus/DMA arbiter (0xFF80-8F)
	uint8_t  arb_r(offs_t offset) { return 0; }   // grant register (stub)
	void     arb_w(offs_t offset, uint8_t data);
	TIMER_CALLBACK_MEMBER(arb_done);
	uint16_t nviack_r();

	void mem_map(address_map &map) ATTR_COLD {}   // empty; real handlers installed in machine_start
	void io_map(address_map &map) ATTR_COLD;
	void sio_map(address_map &map) ATTR_COLD;

	uint16_t segtack_r();
	uint16_t nmiack_r();
};

//**************************************************************************
//  MEMORY (segmented -> MMU -> physical)
//**************************************************************************

void m40_state::ready_fault()
{
	// no READY -> NMI. The RAM-sizing NMI handler reads 0xFF41 and, when
	// bit6 is CLEAR (a plain READY/unpopulated fault), resumes via rr12 to
	// record the boundary; bit6 SET would make it keep scanning. Mark the
	// NMI cause in bit7 and leave bit6 clear.
	m_ff41 = (m_ff41 & ~0x40) | 0x80;
	m_maincpu->set_input_line(z8001_device::NMI_LINE, ASSERT_LINE);
}

uint16_t m40_state::phys_r(offs_t addr, uint16_t mem_mask)
{
	addr &= 0xffffff;
	if (addr < 0x4000)                                   // ROM (16 KB @ bank 0)
		return m_rom[addr >> 1];
	if (addr >= 0x010000 && addr < 0x010000 + m_ramsize) // contiguous RAM from bank 1
	{
		uint8_t *p = m_ramptr + (addr - 0x010000);
		return (p[0] << 8) | p[1];
	}
	if (addr >= 0xff0000)                                // video window (64 KB)
	{
		uint8_t *p = &m_vram[addr & 0xffff];
		return (p[0] << 8) | p[1];
	}
	ready_fault();                                       // unpopulated
	return 0xffff;
}

void m40_state::phys_w(offs_t addr, uint16_t data, uint16_t mem_mask)
{
	addr &= 0xffffff;
	uint8_t *p = nullptr;
	if (addr >= 0x010000 && addr < 0x010000 + m_ramsize)
		p = m_ramptr + (addr - 0x010000);
	else if (addr >= 0xff0000)
		p = &m_vram[addr & 0xffff];
	else if (addr < 0x4000)
		return;                                          // ROM: ignore writes
	else { ready_fault(); return; }                      // unpopulated

	if (ACCESSING_BITS_8_15) p[0] = data >> 8;
	if (ACCESSING_BITS_0_7)  p[1] = data & 0xff;
}

bool m40_state::xlate(int spacenum, bool write, offs_t &addr)
{
	int st;
	if (spacenum == AS_PROGRAM)
		st = m_maincpu->is_ifetch1() ? z8002_device::ST_IFETCH_1 : z8002_device::ST_IFETCH_N;
	else if (spacenum == z8001_device::AS_STACK)
		st = z8002_device::ST_REQ_STACK;
	else
		st = z8002_device::ST_REQ_DATA;

	if (!(m_mmu_mode & 0x80))
		return true;    // MMU not master-enabled -> transparent (physical == segmented addr)
	addr &= 0x3fffff;   // mask URS bit (single-range like the L1)
	return m_mmu->translate(addr, write, /*sys=*/true, /*dma=*/false, st);
}

uint16_t m40_state::mem_r(address_space &space, offs_t offset, uint16_t mem_mask)
{
	offs_t addr = offset << 1;
	if (!xlate(space.spacenum(), false, addr))
		return (space.spacenum() == AS_PROGRAM) ? 0x8d07 : 0xffff;  // seg violation -> NOP
	return phys_r(addr, mem_mask);
}

void m40_state::mem_w(address_space &space, offs_t offset, uint16_t data, uint16_t mem_mask)
{
	offs_t addr = offset << 1;
	if (xlate(space.spacenum(), true, addr))
		phys_w(addr, data, mem_mask);
}

//**************************************************************************
//  MMU programming (special I/O)
//**************************************************************************

uint8_t m40_state::mmu_r(offs_t offset)
{
	if (!BIT(offset, 0))
		return m_mmu->read((uint8_t)(offset >> 8));
	return 0xff;
}

void m40_state::mmu_w(offs_t offset, uint8_t data)
{
	if (!BIT(offset, 0))
	{
		uint8_t reg = (uint8_t)(offset >> 8);
		if (reg == 0x00) m_mmu_mode = data;   // shadow master-enable / translate bits
		m_mmu->write(reg, data);
		m_maincpu->space(AS_PROGRAM).invalidate_caches(read_or_write::READWRITE);
		m_maincpu->space(AS_DATA).invalidate_caches(read_or_write::READWRITE);
		m_maincpu->space(z8001_device::AS_STACK).invalidate_caches(read_or_write::READWRITE);
	}
}

//**************************************************************************
//  UC I/O registers
//**************************************************************************

void m40_state::console_w(uint8_t data)
{
	// diagnostic step / error code latch (0xFFE0) -> stdout
	osd_printf_info("[M40 console] code = 0x%02X (%d)\n", data, data);
}

uint8_t m40_state::ff41_r()
{
	// bit0 BBU-valid, bit1 ISL (0=FDU-first), bit6 READY fault, bit7 NMI-cause
	return m_ff41;
}

void m40_state::ff41_w(uint8_t data)
{
	// write = clear / re-arm the NMI latch
	m_ff41 &= ~0x40;
	m_maincpu->set_input_line(z8001_device::NMI_LINE, CLEAR_LINE);
}

uint8_t m40_state::ffa0_r()
{
	return 0xff;   // config/jumpers — all-ones for now
}

void m40_state::io_map(address_map &map)
{
	map.unmap_value_high();
	// UC (slot 15) on-board registers, byte-wide
	map(0xff41, 0xff41).rw(FUNC(m40_state::ff41_r), FUNC(m40_state::ff41_w));
	map(0xff80, 0xff8f).rw(FUNC(m40_state::arb_r), FUNC(m40_state::arb_w)); // MB15652 arbiter
	map(0xffa0, 0xffa0).r(FUNC(m40_state::ffa0_r));
	map(0xffc0, 0xffc7).rw(FUNC(m40_state::pit_r), FUNC(m40_state::pit_w)); // 8253: 0xC1/C3/C5=ch0-2, C7=ctl
	map(0xffe0, 0xffe0).w(FUNC(m40_state::console_w));
}

void m40_state::sio_map(address_map &map)
{
	map.unmap_value_high();
	map(0x0000, 0x20ff).rw(FUNC(m40_state::mmu_r), FUNC(m40_state::mmu_w));
}

//**************************************************************************
//  CPU acknowledge callbacks
//**************************************************************************

uint16_t m40_state::segtack_r() { return m_mmu->segtack_r(); }
uint16_t m40_state::nmiack_r()  { return 0; }

// MB15652 arbiter: a request write (0xFF84-8F) starts one bus-arbitration cycle
// (ignored while one is in progress); it completes after a fixed latency and
// raises the NVI. 0xFF80-83 are the per-channel acknowledge registers.
// NOTE: the latency is a plausible approximation (no MB15652 datasheet); it must
// outlast the ROM's request-write burst. To be refined against disk-A's arbiter test.
void m40_state::arb_w(offs_t offset, uint8_t data)
{
	if ((offset & 0x0f) >= 4 && !m_arb_busy)
	{
		m_arb_busy = true;
		m_arb_timer->adjust(attotime::from_usec(50));
	}
}

TIMER_CALLBACK_MEMBER(m40_state::arb_done)
{
	m_arb_busy = false;
	m_maincpu->set_input_line(z8001_device::NVI_LINE, ASSERT_LINE);   // arbitration done -> NVI
}

uint16_t m40_state::nviack_r()
{
	m_maincpu->set_input_line(z8001_device::NVI_LINE, CLEAR_LINE);    // acknowledge
	return 0;
}

//**************************************************************************
//  MACHINE
//**************************************************************************

void m40_state::machine_start()
{
	m_vram = std::make_unique<uint8_t[]>(0x10000);
	m_ramptr = m_ram->pointer();
	m_ramsize = m_ram->size();
	m_arb_timer = timer_alloc(FUNC(m40_state::arb_done), this);

	// translating handlers over the whole 24-bit segmented byte space
	// (installed on program / data / stack)
	for (int spc : { (int)AS_PROGRAM, (int)AS_DATA, (int)z8001_device::AS_STACK })
	{
		address_space &s = m_maincpu->space(spc);
		s.install_readwrite_handler(0x000000, 0x7fffff,
			read16_delegate(*this, FUNC(m40_state::mem_r)),
			write16_delegate(*this, FUNC(m40_state::mem_w)));
	}

	save_item(NAME(m_ff41));
}

void m40_state::machine_reset()
{
	m_ff41 = 0x01;   // BBU-valid clear? start with a defined value
}

void m40_state::m40(machine_config &config)
{
	Z8001(config, m_maincpu, 32_MHz_XTAL / 8);   // 4 MHz (32 MHz master / 8)
	m_maincpu->set_m20_hack(false);
	m_maincpu->set_addrmap(AS_PROGRAM, &m40_state::mem_map);
	m_maincpu->set_addrmap(AS_DATA, &m40_state::mem_map);
	m_maincpu->set_addrmap(z8001_device::AS_STACK, &m40_state::mem_map);
	m_maincpu->set_addrmap(AS_IO, &m40_state::io_map);
	m_maincpu->set_addrmap(z8001_device::AS_SIO, &m40_state::sio_map);
	m_maincpu->viack().set(FUNC(m40_state::nmiack_r));
	m_maincpu->nviack().set(FUNC(m40_state::nviack_r));

	Z8010(config, m_mmu, 32_MHz_XTAL / 8);

	PIT8253(config, m_pit);
	m_pit->set_clk<0>(32_MHz_XTAL / 16);
	m_pit->set_clk<1>(32_MHz_XTAL / 16);
	m_pit->set_clk<2>(32_MHz_XTAL / 16);

	RAM(config, m_ram).set_default_size("512K").set_default_value(0)
		.set_extra_options("128K,256K,384K,640K,768K,896K,1024K");
}

//**************************************************************************
//  ROM
//**************************************************************************

ROM_START( m40 )
	ROM_REGION16_BE( 0x4000, "maincpu", 0 )
	ROM_LOAD( "m40rom-6.0.bin", 0x0000, 0x4000, CRC(8114ebec) SHA1(4e2c65b95718c77a87dbee0288f323bd1c8837a3) )
ROM_END

} // anonymous namespace

//    YEAR  NAME  PARENT  COMPAT  MACHINE  INPUT  CLASS      INIT        COMPANY     FULLNAME           FLAGS
COMP( 1982, m40,  0,      0,      m40,     0,     m40_state, empty_init, "Olivetti", "M40 (L1)",        MACHINE_NOT_WORKING | MACHINE_NO_SOUND )
