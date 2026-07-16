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
#include "video/mc6845.h"
#include "emupal.h"
#include "screen.h"

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
		, m_crtc(*this, "crtc")
		, m_palette(*this, "palette")
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
	required_device<mc6845_device> m_crtc;
	required_device<palette_device> m_palette;
	required_region_ptr<uint16_t> m_rom;

	std::unique_ptr<uint8_t[]> m_vram;   // 64 KB video window @ 0xFF0000
	uint8_t *m_ramptr = nullptr;
	uint32_t m_ramsize = 0;
	uint8_t m_ff41 = 0;
	uint8_t m_mmu_mode = 0;   // shadow of Z8010 mode reg (bit7 = master enable)
	emu_timer *m_arb_timer = nullptr;
	bool m_arb_busy = false;  // a bus-arbitration cycle is in progress

	// GO252 video/keyboard governo (KDC): I/O window at slot 1 (0x1000-0x1FFF,
	// register = low byte); framebuffer is the seg-61 window (m_vram)
	bool    m_vid_live = false;  // CRTC live-signal state exposed in status bit 3

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

	// GO252 video/keyboard governo I/O (slot 1)
	uint8_t  vid_r(offs_t offset);
	void     vid_w(offs_t offset, uint8_t data);
	MC6845_UPDATE_ROW(crtc_update_row);

	// GO280 FDU floppy governo I/O (slot 2) — stub for tracing
	uint8_t  fdu_r(offs_t offset);
	void     fdu_w(offs_t offset, uint8_t data);

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
		m_mmu->write(reg, data);
		if (reg == 0x00)   // mode change -> re-map; do NOT invalidate on every
		{                  // descriptor byte (that disrupts the SOTIRB block load)
			m_mmu_mode = data;
			m_maincpu->space(AS_PROGRAM).invalidate_caches(read_or_write::READWRITE);
			m_maincpu->space(AS_DATA).invalidate_caches(read_or_write::READWRITE);
			m_maincpu->space(z8001_device::AS_STACK).invalidate_caches(read_or_write::READWRITE);
		}
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
	// GO252 video/keyboard governo — slot 1 window (register = low byte)
	map(0x1000, 0x1fff).rw(FUNC(m40_state::vid_r), FUNC(m40_state::vid_w));
	// GO280 FDU floppy governo — slot 2 window
	map(0x2000, 0x2fff).rw(FUNC(m40_state::fdu_r), FUNC(m40_state::fdu_w));
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

//**************************************************************************
//  GO252 video/keyboard governo (KDC)
//**************************************************************************
//
// The board answers on its slot's I/O window (register = low byte). The boot
// scans slots reading register 0xFF (type-ID); reporting 0xFE routes the ROM to
// the video self-test (ROM 0x0bc6), which:
//   - reads status reg 0x81: low 3 bits = monitor type (0 = 80x25), bit 3 = a
//     "live signal" (CRTC vsync/display) that it polls for a *change*;
//   - programs the MC6845 via reg 0x41 (index) / 0x43 (data);
//   - walks the seg-61 framebuffer (m_vram);
//   - arms control reg 0x01 (0x03), then enables video via reg 0x6a on success.
uint8_t m40_state::vid_r(offs_t offset)
{
	switch (offset & 0xff)
	{
	case 0xff:                     // type-ID register -> video/KDC governo
		return 0xfe;
	case 0x81:                     // status: monitor type 0 + toggling live-signal (bit 3)
		m_vid_live = !m_vid_live;
		return m_vid_live ? 0x08 : 0x00;
	default:
		return 0xff;
	}
}

void m40_state::vid_w(offs_t offset, uint8_t data)
{
	switch (offset & 0xff)
	{
	case 0x41: m_crtc->address_w(data); break;   // MC6845 address (register select)
	case 0x43: m_crtc->register_w(data); break;  // MC6845 data
	case 0x01: break;                       // control latch (armed 0x03 in self-test)
	case 0x6a: break;                       // "enable normal video"
	default:   break;
	}
}

// GO280 FDU floppy governo (type 0xE1) — recognition stub.
//
// Reporting 0xE1 at register 0xFF makes the boot select the FDU and run its full
// init + IPL disk-read sequence (reset FDC via 0xE7, int-vector 0xEF, 8253 motor
// timing 0x9x, then poll FDC main-status 0x1D, program the AM9517 DMAC 0x40-0x5E,
// and spin on <<1>>0x02fc at ROM 0x06be waiting for load-complete). Booting the
// diagnostic still needs the actual devices modelled here:
//   TODO: upd765 FDC (0x1D main-status / 0x1F data) + i8237/am9517 DMAC (0x40-0x5E)
//   + 0xF6 DMA high-address latch + control 0xE7 / int-status 0xF7 + a floppy with
//   the diag_A image, DMAing the boot track into logical segment 60 and validating
//   the "SYS0" header (see HARDWARE.md §6.3). Until then the IPL read never
//   completes and the ROM retries.
uint8_t m40_state::fdu_r(offs_t offset)
{
	switch (offset & 0xff)
	{
	case 0xff: return 0xe1;          // identifier -> FDU
	default:   return 0xff;
	}
}

void m40_state::fdu_w(offs_t offset, uint8_t data)
{
}

// The framebuffer holds 2 bytes/cell in the seg-61 window; the character code is
// the low (odd) byte of each big-endian word. No character-generator ROM is
// dumped yet, so glyphs are a placeholder: non-blank cells are drawn solid so the
// on-screen text layout is visible. Swap in a font ROM here for real glyphs.
MC6845_UPDATE_ROW(m40_state::crtc_update_row)
{
	uint32_t *p = &bitmap.pix(y);
	rgb_t const *const pal = m_palette->palette()->entry_list_raw();
	for (int col = 0; col < x_count; col++)
	{
		uint8_t const ch = m_vram[(((ma + col) << 1) + 1) & 0xffff];
		bool const cursor = (col == cursor_x);
		bool const on = (ch > 0x20);            // placeholder glyph: solid if not blank
		rgb_t const fg = pal[(on ^ cursor) ? 1 : 0];
		for (int b = 0; b < 8; b++)
			*p++ = fg;
	}
}

// MB15652 arbiter: a DMA-request write (0xFF84-87) starts one bus-arbitration
// cycle (ignored while one is in progress); it completes after a fixed latency
// and raises the NVI. 0xFF80-83 = per-channel acknowledge, 0xFF8C-8F = DMA
// control (writing control must NOT trigger arbitration — the device-enumeration
// loop writes 0xFF8C, and a spurious NVI there resumes via a stale rr12).
// NOTE: the latency is a plausible approximation (no MB15652 datasheet); it must
// outlast the ROM's request-write burst. To be refined against disk-A's arbiter test.
void m40_state::arb_w(offs_t offset, uint8_t data)
{
	uint8_t const reg = offset & 0x0f;
	if (reg >= 4 && reg <= 7 && !m_arb_busy)             // request registers only
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
	save_item(NAME(m_vid_live));
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
	// ch0 (mode 2 rate gen) prescales ch1 (mode 0) — ch0 OUT -> ch1 CLK cascade
	m_pit->set_clk<0>(32_MHz_XTAL / 16);
	m_pit->out_handler<0>().set(m_pit, FUNC(pit8253_device::write_clk1));
	m_pit->set_clk<2>(32_MHz_XTAL / 16);

	RAM(config, m_ram).set_default_size("512K").set_default_value(0)
		.set_extra_options("128K,256K,384K,640K,768K,896K,1024K");

	// GO252 video/keyboard governo — MC6845 CRTC over the seg-61 framebuffer.
	// Monitor type 0 (80x25, 17-line cells): the ROM programs H-total 106,
	// V-total 26 char rows. Char width (dots) is not yet known — assume 8; the
	// char clock is chosen for a ~57 Hz frame and will be refined with the dot
	// clock. No character-generator ROM is dumped, so glyphs are placeholders.
	screen_device &screen(SCREEN(config, "screen", SCREEN_TYPE_RASTER));
	screen.set_refresh_hz(57);
	screen.set_vblank_time(ATTOSECONDS_IN_USEC(2500));
	screen.set_size(848, 442);
	screen.set_visarea(0, 640 - 1, 0, 425 - 1);
	screen.set_screen_update("crtc", FUNC(mc6845_device::screen_update));
	PALETTE(config, m_palette, palette_device::MONOCHROME);

	MC6845(config, m_crtc, 32_MHz_XTAL / 12);   // ~2.67 MHz char clock (dots/char assumed 8)
	m_crtc->set_screen("screen");
	m_crtc->set_show_border_area(false);
	m_crtc->set_char_width(8);
	m_crtc->set_update_row_callback(FUNC(m40_state::crtc_update_row));
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
