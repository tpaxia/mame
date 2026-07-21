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
#include "machine/upd765.h"
#include "machine/am9517a.h"
#include "imagedev/floppy.h"
#include "formats/imd_dsk.h"
#include "video/mc6845.h"
#include "emupal.h"
#include "screen.h"

#include <cstdio>
#include <cstdlib>

class m40_upd765a_device;

DECLARE_DEVICE_TYPE(M40_UPD765A, m40_upd765a_device)

class m40_upd765a_device : public upd765_family_device
{
public:
	m40_upd765a_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
		: upd765_family_device(mconfig, M40_UPD765A, tag, owner, clock)
	{
		has_dor = false;
	}

	void map(address_map &map) override ATTR_COLD
	{
		map(0x0, 0x0).r(FUNC(m40_upd765a_device::msr_r));
		map(0x1, 0x1).rw(FUNC(m40_upd765a_device::fifo_r), FUNC(m40_upd765a_device::fifo_w));
	}

	void soft_reset() override
	{
		upd765_family_device::soft_reset();
		for (floppy_info &fi : flopi)
			fi.pcn = 0;
	}
};

DEFINE_DEVICE_TYPE(M40_UPD765A, m40_upd765a_device, "m40_upd765a", "Olivetti M40 GO280 uPD765A FDC")

namespace {

#ifndef M40_DEBUG_TRACE
#define M40_DEBUG_TRACE 1
#endif

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
		, m_screen(*this, "screen")
		, m_fdc(*this, "fdc")
		, m_floppy(*this, "fdc:0")
		, m_fdu_timer(*this, "fdu_timer")
		, m_dmac(*this, "dmac")
		, m_kbd(*this, "K%u", 0U)
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
	required_device<screen_device> m_screen;
	required_device<m40_upd765a_device> m_fdc;
	required_device<floppy_connector> m_floppy;
	required_device<pit8253_device> m_fdu_timer;
	required_device<am9517a_device> m_dmac;
	required_ioport_array<4> m_kbd;
	required_region_ptr<uint16_t> m_rom;

	std::unique_ptr<uint8_t[]> m_vram;   // 64 KB video window @ 0xFF0000
	uint8_t *m_ramptr = nullptr;
	uint32_t m_ramsize = 0;
	uint8_t m_ff41 = 0;
	uint8_t m_mmu_mode = 0;   // shadow of Z8010 mode reg (bit7 = master enable)
	// MB15652/UCY805 bus arbiter (0xFF80-8F), per re/UCY805_bus_arbiter.md, model
	// verified against disk test 13 and the ROM power-on test in a standalone bench.
	emu_timer *m_arb_timer = nullptr;   // delays the grant NVI to the ROM's spin-loop
	uint8_t m_arb_req = 0;    // pending channel requests   (bit0=ch0 .. bit3=ch3)
	uint8_t m_arb_grant = 0;  // channels currently granted (bit0=ch0 .. bit3=ch3)
	uint8_t m_arb_rel = 0;    // release level from 0xFF8D/8E/8F (and 0xFF85/86/87)
	bool m_arb_vieno = false; // VIENO flip-flop: 0xFF81 bit 3 (set by 0xFF8C-8F, cleared by 0xFF84-87)

	// GO252 video/keyboard governo (KDC): I/O window at slot 1 (0x1000-0x1FFF,
	// register = low byte); framebuffer is the seg-61 window (m_vram)
	bool    m_vid_live = false;  // CRTC live-signal state exposed in status bit 3
	uint8_t m_crtc_index = 0;
	uint8_t m_kdc_ctrl = 0;      // FE reg 0x01 control (bit7 = KDC interrupt enable, the FDU-EN100 analogue)
	uint8_t m_kdc_data = 0;      // UC 0xFF22 byte-data latch
	uint8_t m_kdc_status = 0;    // UC 0xFF20 status/control latch
	uint8_t m_kdc_vector = 0;    // keyboard VI vector, installed by the disk monitor's PSA
	bool    m_kdc_pending = false;
	bool    m_kdc_fe_data_armed = false;
	emu_timer *m_kdc_timer = nullptr;
	uint16_t m_kbd_prev[4] = {};
	uint8_t  m_kbd_fifo[16] = {};
	uint8_t  m_kbd_head = 0;
	uint8_t  m_kbd_tail = 0;
	uint8_t  m_kbd_count = 0;

	// GO280 FDU floppy governo (slot 2): upd765 FDC + (TODO) AM9517 DMAC
	bool    m_fdc_int = false;   // FDC INTRQ level (INTOO, reg 0xF7 bit 1)
	bool    m_timer_int = false; // 8253 ch1 end-of-count (INTMO, reg 0xF7 bit 0)
	bool    m_intoo_lat = false; // INTOO latched into RD1NT until E01NT-acknowledged
	bool    m_intmo_lat = false; // INTMO latched into RD1NT until E01NT-acknowledged
	bool    m_fdu_pending = false; // governo pending-interrupt latch INTP1 (edge-set, VIACK-cleared)
	bool    m_fdu_ien = false;   // governo interrupt enable (EN100/ENSOO, reg 0xE7 bit 0)
	uint8_t m_fdu_vector = 0;    // governo interrupt vector (VETTN, reg 0xEF)
	uint8_t  m_fdu_dma_hi = 0;   // ADRLN (0xF6): high byte of the DMA *word* address
	uint16_t m_dma_ch1 = 0;      // AM9517 ch1 = low 16 bits of the DMA word address (0x44)
	bool     m_dma_ff = false;   // flip-flop for the two-byte 0x44 address load
	uint32_t m_dma_byte = 0;     // running byte offset within the current transfer

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
	uint8_t  ff00_r();
	bool m_suppress_enabled = true;   // MMU-violation write suppression gate (0xFF00/0xFFA0)
	uint8_t m_timer_vector = 0;       // UC timer VI vector (written to 0xFF01)
	uint32_t m_viol_pc = 0xffffffff;  // PC of a suppressed (violating) instruction: SUP holds to its end
	bool m_timer_out1 = false;        // 8253 ch1 OUT level (timer VI source, gated by VIENO)
	void ff01_w(uint8_t data) { m_timer_vector = data; }
	void pit_out1_w(int state) {
		if (state && !m_timer_out1) m_timer_pending = true;   // edge-latch
		m_timer_out1 = (state != 0); update_fdu_irq();
	}
	bool m_timer_pending = false;
	uint8_t  kdc_uc_status_r();
	void     kdc_uc_status_w(uint8_t data);
	uint8_t  kdc_uc_data_r();
	void     kdc_uc_data_w(uint8_t data);
	uint8_t  pit_r(offs_t offset)            { return m_pit->read((offset >> 1) & 3); }
	void     pit_w(offs_t offset, uint8_t d) { m_pit->write((offset >> 1) & 3, d); }

	// GO252 video/keyboard governo I/O (slot 1)
	uint16_t vid16_r(offs_t offset, uint16_t mem_mask);
	void     vid16_w(offs_t offset, uint16_t data, uint16_t mem_mask);
	uint8_t  vid_r(offs_t offset);
	void     vid_w(offs_t offset, uint8_t data);
#if M40_DEBUG_TRACE
	std::FILE *m_vram_trace = nullptr;
	std::FILE *m_fdu_trace = nullptr;
	void     debug_vram_w(offs_t addr, uint8_t data, uint16_t mem_mask);
	void     debug_crtc_w(uint8_t reg, uint8_t data);
	void     debug_fdu(char const *event, uint8_t reg, uint8_t data);
	void     debug_diag_w(offs_t logical, offs_t physical, uint16_t data, uint16_t mem_mask);
	void     debug_pc_ctx(char const *event);
#endif
	void     kdc_queue(uint8_t data);
	void     kdc_update_irq();
	TIMER_CALLBACK_MEMBER(kdc_poll);
	MC6845_UPDATE_ROW(crtc_update_row);
	void palette_init(palette_device &palette) ATTR_COLD;

	// GO280 FDU floppy governo I/O (slot 2)
	uint8_t  fdu_r(offs_t offset);
	void     fdu_w(offs_t offset, uint8_t data);
	void     fdc_intrq_w(int state);
	void     fdc_drq_w(int state);
	void     dma_hreq_w(int state);           // AM9517 bus request -> hold/grant
	void     dma_eop_w(int state);            // AM9517 EOP/TC -> FDC TC
	void     fdu_index_w(int state);          // FDC index pulse -> 8253 channel 2 clock
	uint32_t dma_phys();                      // next DMA physical byte address
	uint8_t  dma_memr(offs_t offset);         // DMA physical-memory read
	void     dma_memw(offs_t offset, uint8_t data); // DMA physical-memory write
	void     fdu_timer_out(int state);   // 8253 ch1 -> INTMO
	void     update_fdu_irq();
	uint16_t vi_ack_r();   // VI vector = interrupting governo's VETTN; clears INTP1
	static void floppy_formats(format_registration &fr);

	// MB15652 bus/DMA arbiter (0xFF80-8F)
	uint8_t  arb_r(offs_t offset);
	void     arb_w(offs_t offset, uint8_t data);
	void     arb_update();
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
	bool const is_vram = (addr >= 0xff0000);
	if (addr >= 0x010000 && addr < 0x010000 + m_ramsize)
		p = m_ramptr + (addr - 0x010000);
	else if (is_vram)
		p = &m_vram[addr & 0xffff];
	else if (addr < 0x4000)
		return;                                          // ROM: ignore writes
	else { ready_fault(); return; }                      // unpopulated

	if (ACCESSING_BITS_8_15)
	{
		p[0] = data >> 8;
#if M40_DEBUG_TRACE
		if (is_vram)
			debug_vram_w(addr & 0xffff, p[0], mem_mask);
#endif
	}
	if (ACCESSING_BITS_0_7)
	{
		p[1] = data & 0xff;
#if M40_DEBUG_TRACE
		if (is_vram)
			debug_vram_w((addr + 1) & 0xffff, p[1], mem_mask);
#endif
	}
}

#if M40_DEBUG_TRACE
void m40_state::debug_vram_w(offs_t addr, uint8_t data, uint16_t mem_mask)
{
	if (m_vram_trace)
	{
		std::fprintf(m_vram_trace, "VRAM pc=%08X off=%04X data=%02X mask=%04X\n",
			unsigned(m_maincpu->pc()), unsigned(addr & 0xffff), data, mem_mask);
		if (m_maincpu->pc() == 0x00031156)
		{
			std::fprintf(m_vram_trace,
				"VRAMCTX pc=00031156 r0=%04X r1=%04X r2=%04X r3=%04X r4=%04X r5=%04X r6=%04X r7=%04X r8=%04X r9=%04X r10=%04X r11=%04X r12=%04X r13=%04X r14=%04X r15=%04X\n",
				unsigned(m_maincpu->state_int(Z8000_R0)), unsigned(m_maincpu->state_int(Z8000_R1)),
				unsigned(m_maincpu->state_int(Z8000_R2)), unsigned(m_maincpu->state_int(Z8000_R3)),
				unsigned(m_maincpu->state_int(Z8000_R4)), unsigned(m_maincpu->state_int(Z8000_R5)),
				unsigned(m_maincpu->state_int(Z8000_R6)), unsigned(m_maincpu->state_int(Z8000_R7)),
				unsigned(m_maincpu->state_int(Z8000_R8)), unsigned(m_maincpu->state_int(Z8000_R9)),
				unsigned(m_maincpu->state_int(Z8000_R10)), unsigned(m_maincpu->state_int(Z8000_R11)),
				unsigned(m_maincpu->state_int(Z8000_R12)), unsigned(m_maincpu->state_int(Z8000_R13)),
				unsigned(m_maincpu->state_int(Z8000_R14)), unsigned(m_maincpu->state_int(Z8000_R15)));
		}
		std::fflush(m_vram_trace);
	}
}

void m40_state::debug_crtc_w(uint8_t reg, uint8_t data)
{
	if (m_vram_trace)
	{
		std::fprintf(m_vram_trace, "CRTC pc=%08X reg=%02X data=%02X\n",
			unsigned(m_maincpu->pc()), reg, data);
		std::fflush(m_vram_trace);
	}
}

void m40_state::debug_fdu(char const *event, uint8_t reg, uint8_t data)
{
	if (m_fdu_trace)
	{
		std::fprintf(m_fdu_trace,
			"FDU %s pc=%08X reg=%02X data=%02X pending=%d ien=%d intmo_lat=%d timer=%d intoo_lat=%d fdc=%d vec=%02X dma_hi=%02X dma_ch1=%04X dma_byte=%06X\n",
			event, unsigned(m_maincpu->pc()), reg, data,
			m_fdu_pending ? 1 : 0, m_fdu_ien ? 1 : 0,
			m_intmo_lat ? 1 : 0, m_timer_int ? 1 : 0,
			m_intoo_lat ? 1 : 0, m_fdc_int ? 1 : 0,
			m_fdu_vector, m_fdu_dma_hi, m_dma_ch1, unsigned(m_dma_byte));
		std::fflush(m_fdu_trace);
	}
}

void m40_state::debug_diag_w(offs_t logical, offs_t physical, uint16_t data, uint16_t mem_mask)
{
	if (m_fdu_trace && logical >= 0x04a480 && logical <= 0x04a4bf)
	{
		std::fprintf(m_fdu_trace,
			"DIAGW pc=%08X log=%06X phys=%06X data=%04X mask=%04X\n",
			unsigned(m_maincpu->pc()), unsigned(logical), unsigned(physical),
			unsigned(data), unsigned(mem_mask));
		std::fflush(m_fdu_trace);
	}
	if (m_fdu_trace && logical >= 0x048f40 && logical <= 0x048f80)
	{
		std::fprintf(m_fdu_trace,
			"ERRBUF pc=%08X log=%06X phys=%06X data=%04X mask=%04X "
			"r0=%04X r1=%04X r2=%04X r3=%04X r4=%04X r5=%04X r6=%04X r7=%04X "
			"r8=%04X r9=%04X r10=%04X r11=%04X r12=%04X r13=%04X r14=%04X r15=%04X\n",
			unsigned(m_maincpu->pc()), unsigned(logical), unsigned(physical),
			unsigned(data), unsigned(mem_mask),
			unsigned(m_maincpu->state_int(Z8000_R0)), unsigned(m_maincpu->state_int(Z8000_R1)),
			unsigned(m_maincpu->state_int(Z8000_R2)), unsigned(m_maincpu->state_int(Z8000_R3)),
			unsigned(m_maincpu->state_int(Z8000_R4)), unsigned(m_maincpu->state_int(Z8000_R5)),
			unsigned(m_maincpu->state_int(Z8000_R6)), unsigned(m_maincpu->state_int(Z8000_R7)),
			unsigned(m_maincpu->state_int(Z8000_R8)), unsigned(m_maincpu->state_int(Z8000_R9)),
			unsigned(m_maincpu->state_int(Z8000_R10)), unsigned(m_maincpu->state_int(Z8000_R11)),
			unsigned(m_maincpu->state_int(Z8000_R12)), unsigned(m_maincpu->state_int(Z8000_R13)),
			unsigned(m_maincpu->state_int(Z8000_R14)), unsigned(m_maincpu->state_int(Z8000_R15)));
		std::fflush(m_fdu_trace);
	}
}

void m40_state::debug_pc_ctx(char const *event)
{
	if (!m_fdu_trace)
		return;
	std::fprintf(m_fdu_trace,
		"PCCTX %s pc=%08X r0=%04X r1=%04X r2=%04X r3=%04X r4=%04X r5=%04X r6=%04X r7=%04X "
		"r8=%04X r9=%04X r10=%04X r11=%04X r12=%04X r13=%04X r14=%04X r15=%04X\n",
		event, unsigned(m_maincpu->pc()),
		unsigned(m_maincpu->state_int(Z8000_R0)), unsigned(m_maincpu->state_int(Z8000_R1)),
		unsigned(m_maincpu->state_int(Z8000_R2)), unsigned(m_maincpu->state_int(Z8000_R3)),
		unsigned(m_maincpu->state_int(Z8000_R4)), unsigned(m_maincpu->state_int(Z8000_R5)),
		unsigned(m_maincpu->state_int(Z8000_R6)), unsigned(m_maincpu->state_int(Z8000_R7)),
		unsigned(m_maincpu->state_int(Z8000_R8)), unsigned(m_maincpu->state_int(Z8000_R9)),
		unsigned(m_maincpu->state_int(Z8000_R10)), unsigned(m_maincpu->state_int(Z8000_R11)),
		unsigned(m_maincpu->state_int(Z8000_R12)), unsigned(m_maincpu->state_int(Z8000_R13)),
		unsigned(m_maincpu->state_int(Z8000_R14)), unsigned(m_maincpu->state_int(Z8000_R15)));
	std::fflush(m_fdu_trace);
}
#endif

bool m40_state::xlate(int spacenum, bool write, offs_t &addr)
{
	int st;
	if (spacenum == AS_PROGRAM)
	{
		st = m_maincpu->is_ifetch1() ? z8002_device::ST_IFETCH_1 : z8002_device::ST_IFETCH_N;
		if (st == z8002_device::ST_IFETCH_1)
			m_viol_pc = 0xffffffff;   // new instruction begins -> SUP released
	}
	else if (spacenum == z8001_device::AS_STACK)
		st = z8002_device::ST_REQ_STACK;
	else
		st = z8002_device::ST_REQ_DATA;

	if (!(m_mmu_mode & 0x80))
		return true;    // MMU not master-enabled -> transparent (physical == segmented addr)
	addr &= 0x3fffff;   // mask URS bit (single-range like the L1)
	// N/S~ pin = the CPU's actual mode (FCW bit 14), latched into the MMU's bus
	// cycle status and checked by the system-violation attribute.
	bool const sys = BIT(m_maincpu->state_int(Z8000_FCW), 14);
	return m_mmu->translate(addr, write, sys, /*dma=*/false, st, m_maincpu->pc());
}

uint16_t m40_state::mem_r(address_space &space, offs_t offset, uint16_t mem_mask)
{
	offs_t addr = offset << 1;
	// SUP suppresses the violating transfer AND all subsequent CPU accesses to the
	// end of the current instruction (datasheet); track the violating PC and hold
	// suppression until the next first-word instruction fetch clears it (in xlate).
	if (!xlate(space.spacenum(), false, addr))
	{
		if (m_suppress_enabled)
			m_viol_pc = m_maincpu->pc();
		return (space.spacenum() == AS_PROGRAM) ? 0x8d07 : 0xffff;  // seg violation -> NOP
	}
	if (m_viol_pc == m_maincpu->pc() && space.spacenum() != AS_PROGRAM)
		return 0xffff;   // rest-of-instruction data access under SUP
#if M40_DEBUG_TRACE
	if (space.spacenum() == AS_PROGRAM)
	{
		static uint32_t last_pc = 0xffffffff;
		uint32_t const pc = m_maincpu->pc();
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
			// UC3003 (loaded in seg 0x21) control-flow probes
			case 0x00210304: debug_pc_ctx("uc-0304-gate"); break;
			case 0x00210330: debug_pc_ctx("uc-dispatch"); break;      // r1 = test index
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
#endif
	return phys_r(addr, mem_mask);
}

void m40_state::mem_w(address_space &space, offs_t offset, uint16_t data, uint16_t mem_mask)
{
	offs_t addr = offset << 1;
	offs_t const logical = addr;
	bool ok = xlate(space.spacenum(), true, addr);
	if (!ok && !m_suppress_enabled)
	{
		// Suppression gate disabled (0xFF00): the violating write reaches memory.
		phys_w(addr, data, mem_mask);
		return;
	}
	if (!ok)
		m_viol_pc = m_maincpu->pc();       // SUP holds to the end of this instruction
	else if (m_viol_pc == m_maincpu->pc())
		ok = false;                        // rest-of-instruction write under SUP
	if (ok)
	{
#if M40_DEBUG_TRACE
		debug_diag_w(logical, addr, data, mem_mask);
#endif
		phys_w(addr, data, mem_mask);
	}
}

//**************************************************************************
//  MMU programming (special I/O)
//**************************************************************************

uint8_t m40_state::mmu_r(offs_t offset)
{
	if (!BIT(offset, 0))
	{
		uint8_t const reg = (uint8_t)(offset >> 8);
		uint8_t const data = m_mmu->read(reg);
#if M40_DEBUG_TRACE
		if (m_fdu_trace && (reg == 0x0b || reg == 0x01 || reg == 0x20 || reg == 0x0f || reg == 0x05))
		{
			std::fprintf(m_fdu_trace, "MMU R pc=%08X off=%04X reg=%02X data=%02X\n",
				unsigned(m_maincpu->pc()), unsigned(offset), reg, data);
			std::fflush(m_fdu_trace);
		}
#endif
		return data;
	}
	return 0xff;
}

void m40_state::mmu_w(offs_t offset, uint8_t data)
{
	if (!BIT(offset, 0))
	{
		uint8_t reg = (uint8_t)(offset >> 8);
#if M40_DEBUG_TRACE
		if (m_fdu_trace && (reg == 0x0b || reg == 0x01 || reg == 0x20 || reg == 0x0f || reg == 0x05))
		{
			std::fprintf(m_fdu_trace, "MMU W pc=%08X off=%04X reg=%02X data=%02X\n",
				unsigned(m_maincpu->pc()), unsigned(offset), reg, data);
			std::fflush(m_fdu_trace);
		}
#endif
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
	// Reading 0xFFA0 also re-enables the MMU suppression gate (see ff00_r).
	if (!machine().side_effects_disabled())
		m_suppress_enabled = true;
	return 0xff;   // config/jumpers — all-ones for now
}

// UC3003 TST01 "DISABLE INHIBITION MEMORY" sub-case: reading UC reg 0xFF00
// disables the gate that suppresses memory writes on an MMU violation (the
// violating write then reaches memory); reading 0xFFA0 restores it.
uint8_t m40_state::ff00_r()
{
	if (!machine().side_effects_disabled())
		m_suppress_enabled = false;
	return 0xff;
}

uint8_t m40_state::kdc_uc_status_r()
{
	// Disk-B's resident FE/KDC handler reads 0xFF20 before dispatching keyboard
	// callbacks. Bit 2 is the only status bit currently backed by trace evidence:
	// when set, the handler reads the data byte from 0xFF22 into rl0.
	return m_kbd_count ? 0x04 : 0x00;
}

void m40_state::kdc_uc_status_w(uint8_t data)
{
	m_kdc_status = data;
}

uint8_t m40_state::kdc_uc_data_r()
{
	if (m_kbd_count)
	{
		m_kdc_data = m_kbd_fifo[m_kbd_tail];
		m_kbd_tail = (m_kbd_tail + 1) & 0x0f;
		m_kbd_count--;
	}
	m_kdc_pending = (m_kbd_count != 0);
	kdc_update_irq();
	return m_kdc_data;
}

void m40_state::kdc_uc_data_w(uint8_t data)
{
	// Several resident handlers write the byte latch as an acknowledge/echo path.
	// Keep the value visible without feeding it back into the host-key queue.
	m_kdc_data = data;
}

void m40_state::io_map(address_map &map)
{
	map.unmap_value_high();
	// GO252 video/keyboard governo — slot 1 window (register = low byte)
	map(0x1000, 0x1fff).rw(FUNC(m40_state::vid16_r), FUNC(m40_state::vid16_w));
	// GO280 FDU floppy governo — slot 2 window
	map(0x2000, 0x2fff).rw(FUNC(m40_state::fdu_r), FUNC(m40_state::fdu_w));
	// UC (slot 15) on-board registers, byte-wide
	map(0xff20, 0xff21).rw(FUNC(m40_state::kdc_uc_status_r), FUNC(m40_state::kdc_uc_status_w)).umask16(0xff00);
	map(0xff22, 0xff23).rw(FUNC(m40_state::kdc_uc_data_r), FUNC(m40_state::kdc_uc_data_w)).umask16(0xff00);
	map(0xff41, 0xff41).rw(FUNC(m40_state::ff41_r), FUNC(m40_state::ff41_w));
	map(0xff80, 0xff8f).rw(FUNC(m40_state::arb_r), FUNC(m40_state::arb_w)); // MB15652 arbiter
	map(0xff00, 0xff00).r(FUNC(m40_state::ff00_r));
	map(0xff01, 0xff01).w(FUNC(m40_state::ff01_w));
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

void m40_state::kdc_queue(uint8_t data)
{
	if (m_kbd_count == sizeof(m_kbd_fifo))
		return;

	m_kbd_fifo[m_kbd_head] = data;
	m_kbd_head = (m_kbd_head + 1) & 0x0f;
	m_kbd_count++;
	m_kdc_pending = true;
	kdc_update_irq();
}

void m40_state::kdc_update_irq()
{
	update_fdu_irq();
}

TIMER_CALLBACK_MEMBER(m40_state::kdc_poll)
{
	// ANK1426 (105-key, layout 3) positional scancodes. Verified against KEYTE1's
	// scancode tables (seg21:0x04c0 alpha block, seg21:0x308c numeric keypad) and the
	// on-screen diagram order (seg21:0x2b40); EXIT=0x3d from the abort compares. See
	// re/GO252_keyboard_scancodes.md. Each entry is the raw byte the keyboard MCU
	// would send; the matrix bit that carries it is the matching ioport PORT_BIT.
	static constexpr uint8_t codes[4][16] =
	{
		// K0: PC number row 1..0 -> the numeric-KEYPAD scancodes (5f 60 5d 57 58 55
		// 4f 50 4d 67).  The diagnostic monitor menu ("HIT 1..4 + ENTER") and the boot
		// prompt read the KEYPAD, not the main number row, so the PC top row must send
		// keypad codes or menu selection stops working.  Then  '  \  <-  DEL  TAB
		// ENTER(0x61).  NOTE 0x61 vs 0x52: both terminate line input, but they are two
		// distinct keys — DCOS's go/skip prompt (disk-A monitor seg03:0x1bf2) decodes
		// scancode 0x61 as ENTER (flag 1) and 0x52 as SKIP (flag 2).  Sending 0x52 for
		// PC-Enter made UC3003's "HIT ENTER TO GO ON / SKIP TO GO BACK" prompt silently
		// skip back to the menu.
		{ 0x5f,0x60,0x5d,0x57,0x58,0x55,0x4f,0x50,0x4d,0x67,0x2c,0x2b,0x31,0x06,0x05,0x61 },
		// K1: Q..P  [  ]  A S D F
		{ 0x03,0x0c,0x08,0x1f,0x11,0x14,0x19,0x25,0x26,0x30,0x2a,0x36,0x02,0x09,0x0f,0x0d },
		// K2: G H J K L  ;  Z X C V B N M  ,  .  /
		{ 0x18,0x15,0x1b,0x1a,0x28,0x22,0x0b,0x0e,0x10,0x20,0x1c,0x16,0x27,0x23,0x2d,0x0a },
		// K3: keypad 7 8 9 4 5 6 1 2 3 0 . - SKIP(0x52, on PC numpad-Enter)  SPACE
		// EXIT(ESC)  SH
		{ 0x4f,0x50,0x4d,0x57,0x58,0x55,0x5f,0x60,0x5d,0x67,0x62,0x59,0x52,0x12,0x3d,0x6e }
	};

	for (int row = 0; row < 4; row++)
	{
		uint16_t const now = m_kbd[row]->read();
		uint16_t const changed = now ^ m_kbd_prev[row];
		uint16_t const pressed = changed & now;
		for (int bit = 0; bit < 16; bit++)
			if (BIT(pressed, bit))
				kdc_queue(codes[row][bit]);
		m_kbd_prev[row] = now;
	}
}

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
uint16_t m40_state::vid16_r(offs_t offset, uint16_t mem_mask)
{
	uint8_t const reg = ((offset << 1) + (ACCESSING_BITS_0_7 ? 1 : 0)) & 0xff;
	uint8_t const data = vid_r(reg);
	return (data << 8) | data;
}

void m40_state::vid16_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	uint8_t const reg = (offset << 1) & 0xff;
	if (ACCESSING_BITS_8_15)
		vid_w(reg, data >> 8);
	if (ACCESSING_BITS_0_7)
		vid_w((reg + 1) & 0xff, data & 0xff);
}

uint8_t m40_state::vid_r(offs_t offset)
{
	switch (offset & 0xff)
	{
	case 0x00:
	case 0x01:                     // KDC status (read side of the control reg)
		// bit 1 = TX ready (transmitter always empty in this model), tested by
		// the direct-send helper (1d:043c) before writing reg 3; bit 2 = RX byte
		// available — the vector-0x2c handler branches on it: set -> read reg 3
		// (data path), clear -> TX/command-completion path. Both bits are status,
		// so they combine (returning them exclusively made a pending key byte
		// fail the TX-ready test with error 0x8006).
		if (m_kbd_count)
			m_kdc_fe_data_armed = true;
		return 0x02 | (m_kbd_count ? 0x04 : 0x00);
	case 0x02:
	case 0x03:                     // FE/KDC data register selected by resident vector 0x2c
		if (m_kdc_fe_data_armed && m_kbd_count)
		{
			m_kdc_data = m_kbd_fifo[m_kbd_tail];
			m_kbd_tail = (m_kbd_tail + 1) & 0x0f;
			m_kbd_count--;
			m_kdc_fe_data_armed = false;
			m_kdc_pending = (m_kbd_count != 0);
			kdc_update_irq();
			return m_kdc_data;
		}
		return m_kdc_data;
	case 0xfe:
	case 0xff:                     // type-ID register -> video/KDC governo
		return 0xfe;
	case 0x80:
	case 0x81:                     // status: monitor type 0 + toggling live-signal (bit 3)
		m_vid_live = !m_vid_live;
		return m_vid_live ? 0x08 : 0x00;
	case 0x42:
	case 0x43:
		return m_crtc->register_r();
	default:
		return 0xff;
	}
}

void m40_state::vid_w(offs_t offset, uint8_t data)
{
	switch (offset & 0xff)
	{
	case 0x40:
	case 0x41:
		m_crtc_index = data & 0x1f;
		m_crtc->address_w(data);                 // MC6845 address (register select)
		break;
	case 0x42:
	case 0x43:
		m_crtc->register_w(data);                // MC6845 data
#if M40_DEBUG_TRACE
		debug_crtc_w(m_crtc_index, data);
#endif
		break;
	case 0x00:
	case 0x01:
		// KDC control (decoded from the resident driver's mask/set table at
		// 1d:042c and the vector-0x2c handler 1d:068e):
		//   bit 5 = TX interrupt enable — transmitter is always empty here, so
		//           setting it raises the "command/completion" VI at once; the
		//           handler's TX branch fetches the byte to send via the test's
		//           callback (which clears its busy flag) and writes it to reg 3.
		//   bit 6 = TX handshake for the direct-send path (helper cmd 9)
		//   bit 7 = RX interrupt enable (keyboard data)
		m_kdc_ctrl = data;
		m_kdc_fe_data_armed = false;
		kdc_update_irq();
		break;
	case 0x02:
	case 0x03:
		// Data register write = byte transmitted to the KDC (keyboard MCU) —
		// KEYTE1's init commands 0x06/0x08/0x0a/0x0c/0x10/0x02 arrive here.
		// The transfer completes immediately; if TX VI (bit 5) is still armed
		// (multi-byte command), the level VI re-asserts for the next byte.
		m_kdc_data = data;
		m_kdc_fe_data_armed = false;
		// Command 0x02 = read keyboard ID/jumpers: the MCU answers 0xFB then a
		// config byte, low 5 bits = layout (KEYTE1 table at 21:22d0: 0=INTER-
		// NATIONAL .. 10=ITALY, 11=JAPAN/Kana, 17=USA ASCII), high 3 = jumpers.
		// KEYTE1 blocks dequeuing its FIFO until the 0xFB arrives (21:062c).
		if (data == 0x02)
		{
			kdc_queue(0xfb);
			kdc_queue(0xf1);   // USA ASCII (layout 17), jumpers 7 = D.P.
		}
		kdc_update_irq();
		break;
	case 0x20:
	case 0x21: m_kdc_vector = data; break;       // keyboard/FE interrupt vector latch
	case 0x6a: break;                       // "enable normal video"
	default:   break;
	}
}

// GO280 FDU floppy governo (type 0xE1). The µPD765 FDC (P8272) is wired to the
// governo's register window: 0x1D = main status, 0x1F = command/data. Identifier
// 0xFF = 0xE1. The boot's IPL then programs the interrupt vector (0xEF), 8253
// motor timing (0x9x), control (0xE7), and the AM9517 DMAC (0x40-0x5E, high byte
// 0xF6) and issues a µPD765 READ, DMAing the boot track into logical segment 60,
// then validates the "SYS0" header (HARDWARE.md §6.3).
//
// TODO (to complete the boot): the AM9517 DMA path (transfer FDC bytes to the
// physical address 0xF6<<16|ch2-addr, i.e. segment 60), the exact 0xE7 CONTR bit
// map (RESFD/EN10/MOTO/SCRVO), and the 0xF7/0xFF interrupt-status polling. Right
// now the ROM reaches the READ but the transfer never completes, so it retries.
uint8_t m40_state::fdu_r(offs_t offset)
{
	uint8_t const reg = offset & 0xff;
	uint8_t data;
	switch (reg)
	{
	case 0x1d: data = m_fdc->msr_r(); break;         // uPD765 main status
	case 0x1f: data = m_fdc->fifo_r(); break;        // uPD765 data
	case 0x40: case 0x42: case 0x44: case 0x46:      // AM9517A DMAC internal regs
	case 0x48: case 0x4a: case 0x4c: case 0x4e:      // (byte-spaced: reg = (addr>>1)&0x0f)
	case 0x50: case 0x52: case 0x54: case 0x56:
	case 0x58: case 0x5a: case 0x5c: case 0x5e:
		data = m_dmac->read((reg >> 1) & 0x0f); break;
	case 0x99: case 0x9b: case 0x9d:                 // 8253 timer (ch0/ch1/ch2 read)
		data = m_fdu_timer->read((reg >> 1) & 3); break;
	case 0xf7:                                       // RD1NT: INTMO (bit0) | INTOO (bit1)
		data = ((m_intmo_lat || m_timer_int) ? 0x01 : 0)
			| ((m_intoo_lat || m_fdc_int) ? 0x02 : 0);
		break;
	case 0xff: data = 0xe1;            break;        // RD1DN identifier: 0xE0 | NOM10(=1 FDU)
	case 0xed: data = 0xff;            break;        // RDGNN diagnostic port
	default:   data = 0xff;            break;
	}
#if M40_DEBUG_TRACE
	debug_fdu("R", reg, data);
#endif
	return data;
}

void m40_state::fdu_w(offs_t offset, uint8_t data)
{
	uint8_t const reg = offset & 0xff;
	switch (reg)
	{
	case 0x1f: m_fdc->fifo_w(data); break;           // uPD765 command/parameter
	case 0x40: case 0x42: case 0x44: case 0x46:      // AM9517A DMAC internal regs
	case 0x48: case 0x4a: case 0x4c: case 0x4e:
	case 0x50: case 0x52: case 0x54: case 0x56:
	case 0x58: case 0x5a: case 0x5c: case 0x5e:
		// Capture the ch1 (0x44) load — it carries the real transfer address (low
		// 16 bits of the DMA *word* address); ch2 only counts bytes. 0x58 clears
		// the address flip-flop and starts a fresh transfer.
		if (reg == 0x58) { m_dma_ff = false; m_dma_byte = 0; }
		else if (reg == 0x44)
		{
			if (!m_dma_ff) m_dma_ch1 = (m_dma_ch1 & 0xff00) | data;
			else           m_dma_ch1 = (m_dma_ch1 & 0x00ff) | (uint16_t(data) << 8);
			m_dma_ff = !m_dma_ff;
		}
		m_dmac->write((reg >> 1) & 0x0f, data);
		break;
	case 0xf6: m_fdu_dma_hi = data; break;           // ADRLN — high byte of the word address
	case 0xff:                                        // E01NT — acknowledge/reset the pending interrupt
		m_intoo_lat = false;
		m_intmo_lat = false;
		m_fdu_pending = false;
		update_fdu_irq();
		break;
	case 0x99: case 0x9b: case 0x9d: case 0x9f:      // 8253 timer (mode 0x9f, ch2 0x9d, ch1 0x9b, ch0 0x99)
		m_fdu_timer->write((reg >> 1) & 3, data); break;
	case 0xef: m_fdu_vector = data; break;           // VETTN — governo interrupt vector
	case 0xe7:                                        // CONTR (manual 3963590 p.3-5)
		m_fdu_ien = BIT(data, 0);                     // EN100 — interrupt-request enable
		m_fdc->reset_w(BIT(data, 1) ? 0 : 1);         // RESFD — reset FDC (active low)
		update_fdu_irq();
		if (floppy_image_device *f = m_floppy->get_device()) f->mon_w(0);  // FDU motor runs
		break;                                        // bit6 SCRVO dir, bit3/7 MOTO1/2 (MFDU)
	default: break;                                   // TODO: AM9517 DMAC 0x40-5E, 0xF6
	}
#if M40_DEBUG_TRACE
	debug_fdu("W", reg, data);
#endif
}

// Governo interrupt logic (manual 3963590 §3.5): the FDC INTRQ (INTOO) is latched
// on its rising edge into the pending flag INTP1; when INTP1 and the enable
// (ENSOO/EN100) are both set the governo raises the CPU's VI. INTP1 is cleared by
// the VI-acknowledge (the same strobe that gates the vector onto the bus).
void m40_state::update_fdu_irq()
{
	// KDC VI causes: RX (queued keyboard byte, edge-latched in m_kdc_pending,
	// enabled by ctrl bit 7) and TX (level: transmitter empty — always — while
	// ctrl bit 5 is set; the resident driver clears bit 5 after the last byte).
	bool const kdc_vi = (m_kdc_pending && BIT(m_kdc_ctrl, 7)) || BIT(m_kdc_ctrl, 5);
	// UC timer VI: 8253 ch1 OUT (level), enabled by the VIENO flip-flop; vector
	// from 0xFF01 (UC3003 TST03 counter-1).
	bool const timer_vi = m_timer_pending && m_arb_vieno;
	m_maincpu->set_input_line(z8001_device::VI_LINE,
		((m_fdu_pending && m_fdu_ien) || kdc_vi || timer_vi) ? ASSERT_LINE : CLEAR_LINE);
}

void m40_state::fdc_intrq_w(int state)
{
	if (state && !m_fdc_int)     // rising edge latches INTP1 and the INTOO source
	{
		m_fdu_pending = true;
		m_intoo_lat = true;
	}
	m_fdc_int = bool(state);
#if M40_DEBUG_TRACE
	debug_fdu("FDCINT", 0x00, state ? 1 : 0);
#endif
	update_fdu_irq();
}

// 8253 channel-1 end-of-count = INTMO (manual §3.4): motor spin-up (500 ms),
// motor-off (2 s), and the read/write time-out (800 ms). Same INTP1 latch path.
void m40_state::fdu_timer_out(int state)
{
	if (state && !m_timer_int)   // rising edge latches INTP1 and the INTMO source
	{
		m_fdu_pending = true;
		m_intmo_lat = true;
	}
	m_timer_int = bool(state);
#if M40_DEBUG_TRACE
	debug_fdu("TIMER", 0x00, state ? 1 : 0);
#endif
	update_fdu_irq();
}

uint16_t m40_state::vi_ack_r()
{
	// UC timer VI (ch1 OUT & VIENO): supply the 0xFF01 vector unless a governo/KDC
	// source is pending (they take priority on the shared line).
	if (m_timer_pending && m_arb_vieno
		&& !((m_kdc_pending && BIT(m_kdc_ctrl, 7)) || BIT(m_kdc_ctrl, 5))
		&& !(m_fdu_pending && m_fdu_ien))
	{
		m_timer_pending = false;   // edge-latched: served by this ack
		update_fdu_irq();
		return m_timer_vector;
	}
	if ((m_kdc_pending && BIT(m_kdc_ctrl, 7)) || BIT(m_kdc_ctrl, 5))
	{
		// One vector for both KDC causes: the handler reads the status register
		// and dispatches (bit 2 set -> RX, clear -> TX/completion). RX pending is
		// edge-latched, ack'd here; the TX cause is level and only drops when the
		// driver clears ctrl bit 5.
		if (m_kdc_pending && BIT(m_kdc_ctrl, 7))
			m_kdc_pending = false;
		update_fdu_irq();
		return m_kdc_vector;
	}

	m_fdu_pending = false;       // vector-enable strobe also resets INTP1
#if M40_DEBUG_TRACE
	debug_fdu("VIACK", 0x00, m_fdu_vector);
#endif
	update_fdu_irq();
	return m_fdu_vector;
}

void m40_state::fdc_drq_w(int state)
{
	// FDC DMARO -> DMAC channel-2 request (manual §3.3.2: the FDC data channel).
	m_dmac->dreq2_w(state);
}

void m40_state::dma_hreq_w(int state)
{
	// The governo requests the bus (BAXXN); we grant immediately and hold the CPU.
	m_maincpu->set_input_line(INPUT_LINE_HALT, state ? ASSERT_LINE : CLEAR_LINE);
	m_dmac->hack_w(state);
}

void m40_state::dma_eop_w(int state)
{
	// AM9517 TC (end of block) -> µPD765 terminal count, ending its transfer.
	m_fdc->tc_w(state);
}

void m40_state::fdu_index_w(int state)
{
	m_fdu_timer->write_clk2(state);
#if M40_DEBUG_TRACE
	debug_fdu("INDEX", 0x00, state ? 1 : 0);
#endif
}

// GO280's DMA uses an "anomalous" 2-channel scheme (manual §3.3.2): ch2 streams
// the FDC bytes while ch1 (+ the 0xF6 latch) holds the *word* address, which the
// ROM formed by shifting the byte address right by one (0f96: srll rr2,#1). So the
// running physical byte address is (word_addr << 1) + byte_offset, incrementing per
// byte. ch2's own AM9517 address (0xFFFF) is ignored. DMA bypasses the MMU. RAM is
// stored big-endian (byte X of a word at even A is the MS byte), so a physical byte
// address maps straight onto the backing array.
uint32_t m40_state::dma_phys()
{
	uint32_t const base = ((uint32_t(m_fdu_dma_hi) << 16) | m_dma_ch1) << 1;
	return (base + m_dma_byte++) & 0xffffff;
}

uint8_t m40_state::dma_memr(offs_t /*offset*/)
{
	uint32_t const addr = dma_phys();
	if (addr >= 0x010000 && addr < 0x010000 + m_ramsize)
		return m_ramptr[addr - 0x010000];
	if (addr >= 0xff0000)
		return m_vram[addr & 0xffff];
	if (addr < 0x4000)
		return (addr & 1) ? (m_rom[addr >> 1] & 0xff) : (m_rom[addr >> 1] >> 8);
	return 0xff;
}

void m40_state::dma_memw(offs_t /*offset*/, uint8_t data)
{
	uint32_t const addr = dma_phys();
	if (addr >= 0x010000 && addr < 0x010000 + m_ramsize)
		m_ramptr[addr - 0x010000] = data;
	else if (addr >= 0xff0000)
		m_vram[addr & 0xffff] = data;
#if M40_DEBUG_TRACE
	if (m_fdu_trace)
	{
		uint32_t const pos = (m_dma_byte - 1) & 0xffffff;
		if (pos < 0x20 || (pos & 0xff) == 0)
		{
			std::fprintf(m_fdu_trace,
				"DMAW pc=%08X pos=%06X phys=%06X data=%02X dma_hi=%02X dma_ch1=%04X\n",
				unsigned(m_maincpu->pc()), unsigned(pos), unsigned(addr), data,
				m_fdu_dma_hi, m_dma_ch1);
			std::fflush(m_fdu_trace);
		}
	}
#endif
	// ROM / unpopulated: ignore (no READY fault during DMA)
}

void m40_state::floppy_formats(format_registration &fr)
{
	fr.add(FLOPPY_IMD_FORMAT);
}

static void m40_floppies(device_slot_interface &device)
{
	device.option_add("8dsdd", FLOPPY_8_DSDD);
}

// Character generator derived from the Olivetti M20/L1 house font
// (~/Projects/M20/include/font.inc): a 5x7 dot-matrix set, ASCII 0x20-0x7E.
// The M20 is the same L1 product line as the M40, and a photo of a live L1/ESE
// console (re/ESE.jpg) shows the same font — matching slashed zero and glyph
// shapes — so this is very likely the GO252 GI 9428DS char-gen, pending a dump
// or a CRTAN5 CRT-ROM-pattern grab to confirm glyph-exact.  Each M20 5-wide row
// is shifted left 1 and the 10-row glyph is centred at rows 3-12 of the 16-line cell.
static const uint8_t s_chargen[96 * 16] =
{
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,  // 0x20 ' '
	0x00,0x00,0x00,0x00,0x08,0x08,0x08,0x08,0x08,0x00,0x08,0x00,0x00,0x00,0x00,0x00,  // 0x21 '!'
	0x00,0x00,0x00,0x00,0x12,0x12,0x24,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,  // 0x22 '"'
	0x00,0x00,0x00,0x00,0x14,0x14,0x3e,0x14,0x3e,0x14,0x14,0x00,0x00,0x00,0x00,0x00,  // 0x23 '#'
	0x00,0x00,0x00,0x00,0x08,0x1e,0x28,0x1c,0x0a,0x3c,0x08,0x00,0x00,0x00,0x00,0x00,  // 0x24 '$'
	0x00,0x00,0x00,0x00,0x30,0x32,0x04,0x08,0x10,0x26,0x06,0x00,0x00,0x00,0x00,0x00,  // 0x25 '%'
	0x00,0x00,0x00,0x00,0x08,0x14,0x14,0x10,0x2a,0x24,0x1a,0x00,0x00,0x00,0x00,0x00,  // 0x26 '&'
	0x00,0x00,0x00,0x02,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,  // 0x27 '\''
	0x00,0x00,0x00,0x00,0x0c,0x10,0x10,0x10,0x10,0x10,0x0c,0x00,0x00,0x00,0x00,0x00,  // 0x28 '('
	0x00,0x00,0x00,0x00,0x18,0x04,0x04,0x04,0x04,0x04,0x18,0x00,0x00,0x00,0x00,0x00,  // 0x29 ')'
	0x00,0x00,0x00,0x00,0x08,0x2a,0x1c,0x2a,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,  // 0x2a '*'
	0x00,0x00,0x00,0x00,0x00,0x08,0x08,0x3e,0x08,0x08,0x00,0x00,0x00,0x00,0x00,0x00,  // 0x2b '+'
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x08,0x10,0x00,0x00,0x00,0x00,  // 0x2c ','
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x3e,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,  // 0x2d '-'
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x08,0x00,0x00,0x00,0x00,0x00,  // 0x2e '.'
	0x00,0x00,0x00,0x00,0x00,0x02,0x04,0x08,0x10,0x20,0x00,0x00,0x00,0x00,0x00,0x00,  // 0x2f '/'
	0x00,0x00,0x00,0x00,0x1c,0x22,0x32,0x2a,0x26,0x22,0x1c,0x00,0x00,0x00,0x00,0x00,  // 0x30 '0'
	0x00,0x00,0x00,0x00,0x04,0x0c,0x14,0x04,0x04,0x04,0x04,0x00,0x00,0x00,0x00,0x00,  // 0x31 '1'
	0x00,0x00,0x00,0x00,0x1c,0x22,0x02,0x04,0x08,0x10,0x3e,0x00,0x00,0x00,0x00,0x00,  // 0x32 '2'
	0x00,0x00,0x00,0x00,0x3e,0x02,0x04,0x0c,0x02,0x22,0x1c,0x00,0x00,0x00,0x00,0x00,  // 0x33 '3'
	0x00,0x00,0x00,0x00,0x04,0x08,0x10,0x24,0x3e,0x04,0x04,0x00,0x00,0x00,0x00,0x00,  // 0x34 '4'
	0x00,0x00,0x00,0x00,0x1e,0x10,0x1c,0x02,0x02,0x22,0x1c,0x00,0x00,0x00,0x00,0x00,  // 0x35 '5'
	0x00,0x00,0x00,0x00,0x1c,0x20,0x20,0x3c,0x22,0x22,0x1c,0x00,0x00,0x00,0x00,0x00,  // 0x36 '6'
	0x00,0x00,0x00,0x00,0x3e,0x02,0x04,0x08,0x10,0x10,0x10,0x00,0x00,0x00,0x00,0x00,  // 0x37 '7'
	0x00,0x00,0x00,0x00,0x1c,0x22,0x22,0x1c,0x22,0x22,0x1c,0x00,0x00,0x00,0x00,0x00,  // 0x38 '8'
	0x00,0x00,0x00,0x00,0x1c,0x22,0x22,0x1e,0x02,0x02,0x1c,0x00,0x00,0x00,0x00,0x00,  // 0x39 '9'
	0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x00,0x08,0x00,0x00,0x00,0x00,0x00,0x00,  // 0x3a ':'
	0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x00,0x08,0x08,0x10,0x00,0x00,0x00,0x00,  // 0x3b ';'
	0x00,0x00,0x00,0x00,0x04,0x08,0x10,0x20,0x10,0x08,0x04,0x00,0x00,0x00,0x00,0x00,  // 0x3c '<'
	0x00,0x00,0x00,0x00,0x00,0x00,0x3e,0x00,0x3e,0x00,0x00,0x00,0x00,0x00,0x00,0x00,  // 0x3d '='
	0x00,0x00,0x00,0x00,0x10,0x08,0x04,0x02,0x04,0x08,0x10,0x00,0x00,0x00,0x00,0x00,  // 0x3e '>'
	0x00,0x00,0x00,0x00,0x1c,0x22,0x02,0x04,0x08,0x00,0x08,0x00,0x00,0x00,0x00,0x00,  // 0x3f '?'
	0x00,0x00,0x00,0x00,0x1c,0x22,0x02,0x1a,0x2a,0x2a,0x1c,0x00,0x00,0x00,0x00,0x00,  // 0x40 '@'
	0x00,0x00,0x00,0x00,0x1c,0x22,0x22,0x22,0x3e,0x22,0x22,0x00,0x00,0x00,0x00,0x00,  // 0x41 'A'
	0x00,0x00,0x00,0x00,0x3c,0x22,0x22,0x3c,0x22,0x22,0x3c,0x00,0x00,0x00,0x00,0x00,  // 0x42 'B'
	0x00,0x00,0x00,0x00,0x1c,0x22,0x20,0x20,0x20,0x22,0x1c,0x00,0x00,0x00,0x00,0x00,  // 0x43 'C'
	0x00,0x00,0x00,0x00,0x38,0x24,0x22,0x22,0x22,0x24,0x38,0x00,0x00,0x00,0x00,0x00,  // 0x44 'D'
	0x00,0x00,0x00,0x00,0x3e,0x20,0x20,0x3c,0x20,0x20,0x3e,0x00,0x00,0x00,0x00,0x00,  // 0x45 'E'
	0x00,0x00,0x00,0x00,0x3e,0x20,0x20,0x3c,0x20,0x20,0x20,0x00,0x00,0x00,0x00,0x00,  // 0x46 'F'
	0x00,0x00,0x00,0x00,0x1c,0x22,0x20,0x20,0x2e,0x22,0x1c,0x00,0x00,0x00,0x00,0x00,  // 0x47 'G'
	0x00,0x00,0x00,0x00,0x22,0x22,0x22,0x3e,0x22,0x22,0x22,0x00,0x00,0x00,0x00,0x00,  // 0x48 'H'
	0x00,0x00,0x00,0x00,0x1c,0x08,0x08,0x08,0x08,0x08,0x1c,0x00,0x00,0x00,0x00,0x00,  // 0x49 'I'
	0x00,0x00,0x00,0x00,0x02,0x02,0x02,0x02,0x22,0x22,0x1c,0x00,0x00,0x00,0x00,0x00,  // 0x4a 'J'
	0x00,0x00,0x00,0x00,0x22,0x24,0x28,0x30,0x28,0x24,0x22,0x00,0x00,0x00,0x00,0x00,  // 0x4b 'K'
	0x00,0x00,0x00,0x00,0x20,0x20,0x20,0x20,0x20,0x20,0x3e,0x00,0x00,0x00,0x00,0x00,  // 0x4c 'L'
	0x00,0x00,0x00,0x00,0x22,0x36,0x2a,0x2a,0x22,0x22,0x22,0x00,0x00,0x00,0x00,0x00,  // 0x4d 'M'
	0x00,0x00,0x00,0x00,0x22,0x32,0x2a,0x26,0x22,0x22,0x22,0x00,0x00,0x00,0x00,0x00,  // 0x4e 'N'
	0x00,0x00,0x00,0x00,0x1c,0x22,0x22,0x22,0x22,0x22,0x1c,0x00,0x00,0x00,0x00,0x00,  // 0x4f 'O'
	0x00,0x00,0x00,0x00,0x3c,0x22,0x22,0x3c,0x20,0x20,0x20,0x00,0x00,0x00,0x00,0x00,  // 0x50 'P'
	0x00,0x00,0x00,0x00,0x1c,0x22,0x22,0x22,0x2a,0x24,0x1a,0x00,0x00,0x00,0x00,0x00,  // 0x51 'Q'
	0x00,0x00,0x00,0x00,0x3c,0x22,0x22,0x3c,0x28,0x24,0x22,0x00,0x00,0x00,0x00,0x00,  // 0x52 'R'
	0x00,0x00,0x00,0x00,0x1c,0x20,0x20,0x1c,0x02,0x02,0x3c,0x00,0x00,0x00,0x00,0x00,  // 0x53 'S'
	0x00,0x00,0x00,0x00,0x3e,0x08,0x08,0x08,0x08,0x08,0x08,0x00,0x00,0x00,0x00,0x00,  // 0x54 'T'
	0x00,0x00,0x00,0x00,0x22,0x22,0x22,0x22,0x22,0x22,0x1c,0x00,0x00,0x00,0x00,0x00,  // 0x55 'U'
	0x00,0x00,0x00,0x00,0x22,0x22,0x22,0x22,0x22,0x14,0x08,0x00,0x00,0x00,0x00,0x00,  // 0x56 'V'
	0x00,0x00,0x00,0x00,0x22,0x22,0x2a,0x2a,0x2a,0x2a,0x14,0x00,0x00,0x00,0x00,0x00,  // 0x57 'W'
	0x00,0x00,0x00,0x00,0x22,0x22,0x14,0x08,0x14,0x22,0x22,0x00,0x00,0x00,0x00,0x00,  // 0x58 'X'
	0x00,0x00,0x00,0x00,0x22,0x22,0x14,0x08,0x08,0x08,0x08,0x00,0x00,0x00,0x00,0x00,  // 0x59 'Y'
	0x00,0x00,0x00,0x00,0x3e,0x02,0x04,0x08,0x10,0x20,0x3e,0x00,0x00,0x00,0x00,0x00,  // 0x5a 'Z'
	0x00,0x00,0x00,0x00,0x1c,0x10,0x10,0x10,0x10,0x10,0x1c,0x00,0x00,0x00,0x00,0x00,  // 0x5b '['
	0x00,0x00,0x00,0x00,0x00,0x20,0x10,0x08,0x04,0x02,0x00,0x00,0x00,0x00,0x00,0x00,  // 0x5c '\\'
	0x00,0x00,0x00,0x00,0x1c,0x04,0x04,0x04,0x04,0x04,0x1c,0x00,0x00,0x00,0x00,0x00,  // 0x5d ']'
	0x00,0x00,0x00,0x08,0x14,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,  // 0x5e '^'
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x3e,0x00,0x00,0x00,0x00,0x00,  // 0x5f '_'
	0x00,0x00,0x00,0x20,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,  // 0x60 '`'
	0x00,0x00,0x00,0x00,0x00,0x00,0x1c,0x02,0x1e,0x22,0x1e,0x00,0x00,0x00,0x00,0x00,  // 0x61 'a'
	0x00,0x00,0x00,0x00,0x20,0x20,0x3c,0x22,0x22,0x22,0x3c,0x00,0x00,0x00,0x00,0x00,  // 0x62 'b'
	0x00,0x00,0x00,0x00,0x00,0x00,0x1c,0x22,0x20,0x20,0x1e,0x00,0x00,0x00,0x00,0x00,  // 0x63 'c'
	0x00,0x00,0x00,0x00,0x02,0x02,0x1e,0x22,0x22,0x22,0x1e,0x00,0x00,0x00,0x00,0x00,  // 0x64 'd'
	0x00,0x00,0x00,0x00,0x00,0x00,0x1c,0x22,0x3e,0x20,0x1e,0x00,0x00,0x00,0x00,0x00,  // 0x65 'e'
	0x00,0x00,0x00,0x00,0x06,0x08,0x1e,0x08,0x08,0x08,0x08,0x00,0x00,0x00,0x00,0x00,  // 0x66 'f'
	0x00,0x00,0x00,0x00,0x00,0x00,0x1e,0x22,0x22,0x22,0x1e,0x02,0x3c,0x00,0x00,0x00,  // 0x67 'g'
	0x00,0x00,0x00,0x00,0x20,0x20,0x3c,0x22,0x22,0x22,0x22,0x00,0x00,0x00,0x00,0x00,  // 0x68 'h'
	0x00,0x00,0x00,0x00,0x08,0x00,0x18,0x08,0x08,0x08,0x1c,0x00,0x00,0x00,0x00,0x00,  // 0x69 'i'
	0x00,0x00,0x00,0x00,0x04,0x00,0x04,0x04,0x04,0x04,0x04,0x24,0x18,0x00,0x00,0x00,  // 0x6a 'j'
	0x00,0x00,0x00,0x00,0x20,0x20,0x22,0x24,0x38,0x24,0x22,0x00,0x00,0x00,0x00,0x00,  // 0x6b 'k'
	0x00,0x00,0x00,0x00,0x18,0x08,0x08,0x08,0x08,0x08,0x1c,0x00,0x00,0x00,0x00,0x00,  // 0x6c 'l'
	0x00,0x00,0x00,0x00,0x00,0x00,0x34,0x2a,0x2a,0x2a,0x2a,0x00,0x00,0x00,0x00,0x00,  // 0x6d 'm'
	0x00,0x00,0x00,0x00,0x00,0x00,0x3c,0x22,0x22,0x22,0x22,0x00,0x00,0x00,0x00,0x00,  // 0x6e 'n'
	0x00,0x00,0x00,0x00,0x00,0x00,0x1c,0x22,0x22,0x22,0x1c,0x00,0x00,0x00,0x00,0x00,  // 0x6f 'o'
	0x00,0x00,0x00,0x00,0x00,0x00,0x3c,0x22,0x22,0x22,0x3c,0x20,0x20,0x00,0x00,0x00,  // 0x70 'p'
	0x00,0x00,0x00,0x00,0x00,0x00,0x1e,0x22,0x22,0x22,0x1e,0x02,0x02,0x00,0x00,0x00,  // 0x71 'q'
	0x00,0x00,0x00,0x00,0x00,0x00,0x2c,0x12,0x10,0x10,0x10,0x00,0x00,0x00,0x00,0x00,  // 0x72 'r'
	0x00,0x00,0x00,0x00,0x00,0x00,0x1c,0x20,0x1c,0x02,0x3c,0x00,0x00,0x00,0x00,0x00,  // 0x73 's'
	0x00,0x00,0x00,0x00,0x00,0x10,0x3c,0x10,0x10,0x10,0x0c,0x00,0x00,0x00,0x00,0x00,  // 0x74 't'
	0x00,0x00,0x00,0x00,0x00,0x00,0x22,0x22,0x22,0x22,0x1e,0x00,0x00,0x00,0x00,0x00,  // 0x75 'u'
	0x00,0x00,0x00,0x00,0x00,0x00,0x22,0x22,0x22,0x14,0x08,0x00,0x00,0x00,0x00,0x00,  // 0x76 'v'
	0x00,0x00,0x00,0x00,0x00,0x00,0x22,0x2a,0x2a,0x2a,0x14,0x00,0x00,0x00,0x00,0x00,  // 0x77 'w'
	0x00,0x00,0x00,0x00,0x00,0x00,0x22,0x14,0x08,0x14,0x22,0x00,0x00,0x00,0x00,0x00,  // 0x78 'x'
	0x00,0x00,0x00,0x00,0x00,0x00,0x22,0x22,0x22,0x14,0x08,0x08,0x30,0x00,0x00,0x00,  // 0x79 'y'
	0x00,0x00,0x00,0x00,0x00,0x00,0x3e,0x04,0x08,0x10,0x3e,0x00,0x00,0x00,0x00,0x00,  // 0x7a 'z'
	0x00,0x00,0x00,0x04,0x08,0x08,0x08,0x10,0x08,0x08,0x08,0x04,0x00,0x00,0x00,0x00,  // 0x7b '{'
	0x00,0x00,0x00,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x00,0x00,0x00,0x00,  // 0x7c '|'
	0x00,0x00,0x00,0x10,0x08,0x08,0x08,0x04,0x08,0x08,0x08,0x10,0x00,0x00,0x00,0x00,  // 0x7d '}'
	0x00,0x00,0x00,0x3e,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,  // 0x7e '~'
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,  // 0x7f '.'
};

// The framebuffer holds 2 bytes/cell in the seg-61 window; the character code is
// the low (odd) byte of each big-endian word. The real board uses a GI character
// generator (not dumped); s_chargen above is an 8x16 stand-in so text is readable.
// Palette: pen 0 = background (off), pen 1 = normal intensity, pen 2 = HIGH LIGHT.
// A real GO252 mono CRT runs normal text dimmed and "high light" at full beam.
void m40_state::palette_init(palette_device &palette)
{
	palette.set_pen_color(0, rgb_t::black());
	palette.set_pen_color(1, rgb_t(0xc0, 0xc0, 0xc0));   // normal
	palette.set_pen_color(2, rgb_t(0xff, 0xff, 0xff));   // high light
}

// Each screen cell is two bytes: even = attribute, odd = character. CRTAN5's
// attribute test names the effects in this order — HIGH/LOW/LEFT/RIGHT LINE,
// BLINKING(BL), HIGH LIGHT(HL), REVERSE VIDEO(RV) — and the resident monitor is
// observed writing attribute bytes 0x00/0x20/0x40/0x50. Both are consistent with
// bits 0..6 in that order (0x10=blink, 0x20=high light, 0x40=reverse; the monitor's
// 0x50 header = reverse+blink). PROVISIONAL until CRTAN5's seg-0x21 attribute-fill
// code is disassembled — see re/CRTAN5_video_test.md.
static constexpr int ATTR_HIGH_LINE  = 0;   // 0x01 line along top of cell
static constexpr int ATTR_LOW_LINE   = 1;   // 0x02 line along bottom of cell
static constexpr int ATTR_LEFT_LINE  = 2;   // 0x04 line down left edge
static constexpr int ATTR_RIGHT_LINE = 3;   // 0x08 line down right edge
static constexpr int ATTR_BLINK      = 4;   // 0x10 blink (field-rate)
static constexpr int ATTR_HILIGHT    = 5;   // 0x20 high light (full intensity)
static constexpr int ATTR_REVERSE    = 6;   // 0x40 reverse video

MC6845_UPDATE_ROW(m40_state::crtc_update_row)
{
	uint32_t *p = &bitmap.pix(y);
	rgb_t const *const pal = m_palette->palette()->entry_list_raw();
	// Character-blink phase, clocked by the field/frame counter (~1.5 Hz), the way
	// MAME MC6845 text drivers derive it (attribute blink is board logic gated by
	// VSYNC, not a CRTC function).
	bool const blink_off = BIT(m_screen->frame_number(), 4);
	int const last_ra = 15;
	for (int col = 0; col < x_count; col++)
	{
		uint16_t const cell = (ma + col) << 1;
		uint8_t const attr = m_vram[cell & 0xffff];              // even byte = attribute
		uint8_t const ch   = m_vram[(cell + 1) & 0xffff];        // odd byte  = character
		uint8_t bits = (ch >= 0x20 && ch < 0x80 && ra < 16) ? s_chargen[(ch - 0x20) * 16 + ra] : 0;

		// Line attributes: force pixels along the requested cell edge.
		if (BIT(attr, ATTR_HIGH_LINE) && ra == 0)        bits = 0xff;
		if (BIT(attr, ATTR_LOW_LINE)  && ra == last_ra)  bits = 0xff;
		if (BIT(attr, ATTR_LEFT_LINE))                   bits |= 0x80;
		if (BIT(attr, ATTR_RIGHT_LINE))                  bits |= 0x01;

		// Blink: blank the glyph on the off phase.
		if (BIT(attr, ATTR_BLINK) && blink_off)          bits = 0;

		// Reverse video: swap foreground/background.
		if (BIT(attr, ATTR_REVERSE))                     bits ^= 0xff;

		if (col == cursor_x) bits ^= 0xff;                       // block cursor

		uint8_t const fg = BIT(attr, ATTR_HILIGHT) ? 2 : 1;      // high light -> bright pen
		for (int b = 0; b < 8; b++)
			*p++ = pal[BIT(bits, 7 - b) ? fg : 0];
	}
}

// MB15652 arbiter: a DMA-request write (0xFF84-87) starts one bus-arbitration
// cycle (ignored while one is in progress); it completes after a fixed latency
// and raises the NVI. 0xFF80-83 = per-channel acknowledge, 0xFF8C-8F = DMA
// control (writing control must NOT trigger arbitration — the device-enumeration
// loop writes 0xFF8C, and a spurious NVI there resumes via a stale rr12).
// NOTE: the latency is a plausible approximation (no MB15652 datasheet); it must
// outlast the ROM's request-write burst. To be refined against disk-A's arbiter test.
// 0xFF81 exposes the grant bitmap in the high nibble (ch0=0x80 .. ch3=0x10) plus
// an idle/active low nibble (0x0f idle, 0x08 when any channel is granted). The
// ROM reads this in the NVI handler to learn which channel was granted.
uint8_t m40_state::arb_r(offs_t offset)
{
	uint8_t const reg = offset & 0x0f;
	if (reg == 1)
	{
		uint8_t const hi = (BIT(m_arb_grant, 0) ? 0x80 : 0) | (BIT(m_arb_grant, 1) ? 0x40 : 0)
		                 | (BIT(m_arb_grant, 2) ? 0x20 : 0) | (BIT(m_arb_grant, 3) ? 0x10 : 0);
		// Low nibble: bits 0-2 = idle marker (clear while any grant is active);
		// bit 3 = VIENO FF or any-grant.  Satisfies both UCY805 (0x0F after ack-all
		// following 0xFF8D-8F writes; 0xF8 with all granted) and UC3003 test 2
		// (bit 3: 0 at entry, 1 after 0xFF8C, 0 after 0xFF84).
		return hi | (m_arb_grant ? 0 : 0x07) | ((m_arb_vieno || m_arb_grant) ? 0x08 : 0);
	}
	return 0;
}

// Priority ch0 > ch1 > ch2 > ch3: chN is granted only once requested AND the
// release level has reached N (ch0 at once; ch1 needs 0xFF8D, ch2 8D+8E, ch3
// 8D+8E+8F). A pending grant raises NVI, but delayed by m_arb_timer so it lands in
// the ROM's post-`ei nvi` spin-loop (matching the real board's arbitration latency)
// rather than mid-setup.
void m40_state::arb_update()
{
	uint8_t g = 0;
	for (int ch = 0; ch < 4; ch++)
		if (BIT(m_arb_req, ch) && m_arb_rel >= ch)
			g |= (1 << ch);
	m_arb_grant = g;
	if (g)
		m_arb_timer->adjust(attotime::from_usec(50));
}

void m40_state::arb_w(offs_t offset, uint8_t data)
{
	uint8_t const reg = offset & 0x0f;
	switch (reg)
	{
	case 0x0: case 0x1: case 0x2: case 0x3:          // ack/clear channel reg
		m_arb_req &= ~(1 << reg);
		if (m_arb_req == 0) m_arb_rel = 0;           // idle -> reset release level
		break;
	case 0x8: case 0x9: case 0xa: case 0xb:          // request channel reg-8
		m_arb_req |= (1 << (reg - 8));
		break;
	case 0x5: case 0xd: if (m_arb_rel < 1) m_arb_rel = 1; break;   // release ch1
	case 0x6: case 0xe: if (m_arb_rel < 2) m_arb_rel = 2; break;   // release ch2
	case 0x7: case 0xf: if (m_arb_rel < 3) m_arb_rel = 3; break;   // release ch3
	default: break;                                  // 0xFF84 / 0xFF8C: boot bus gate
	}
	// VIENO flip-flop (UC3003 test 2 defines it): SET by any 0xFF8C-8F write,
	// CLEARED by any 0xFF84-87 write.  Read back as 0xFF81 bit 3 (below).
	if (reg >= 0xc)              m_arb_vieno = true;
	else if (reg >= 0x4 && reg <= 0x7) m_arb_vieno = false;
	arb_update();
}

TIMER_CALLBACK_MEMBER(m40_state::arb_done)
{
	if (m_arb_grant)                                 // grant still pending -> raise NVI
		m_maincpu->set_input_line(z8001_device::NVI_LINE, ASSERT_LINE);
}

uint16_t m40_state::nviack_r()
{
	// Vector fetch clears only the NVI line; the grant persists so the handler can
	// still read 0xFF81, and is cleared by the per-channel ack (0xFF80..0xFF83).
	m_maincpu->set_input_line(z8001_device::NVI_LINE, CLEAR_LINE);
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
	m_kdc_timer = timer_alloc(FUNC(m40_state::kdc_poll), this);
#if M40_DEBUG_TRACE
	if (char const *const path = std::getenv("M40_VRAM_TRACE"))
		{
			if (path[0] != '\0')
				m_vram_trace = std::fopen(path, "w");
		}
		if (char const *const path = std::getenv("M40_FDU_TRACE"))
		{
			if (path[0] != '\0')
				m_fdu_trace = std::fopen(path, "w");
		}
	#endif

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
	save_item(NAME(m_crtc_index));
	save_item(NAME(m_kdc_ctrl));
	save_item(NAME(m_kdc_data));
	save_item(NAME(m_kdc_status));
	save_item(NAME(m_kdc_vector));
	save_item(NAME(m_kdc_pending));
	save_item(NAME(m_kdc_fe_data_armed));
	save_item(NAME(m_kbd_prev));
	save_item(NAME(m_kbd_fifo));
	save_item(NAME(m_kbd_head));
	save_item(NAME(m_kbd_tail));
	save_item(NAME(m_kbd_count));
	save_item(NAME(m_fdc_int));
	save_item(NAME(m_timer_int));
	save_item(NAME(m_fdu_pending));
	save_item(NAME(m_fdu_vector));
}

void m40_state::machine_reset()
{
	m_ff41 = 0x01;   // BBU-valid clear? start with a defined value
	m_crtc_index = 0;
	m_kdc_ctrl = 0;
	m_kdc_data = 0;
	m_kdc_status = 0;
	m_kdc_vector = 0x28;
	m_kdc_pending = false;
	m_kdc_fe_data_armed = false;
	for (auto &v : m_kbd_prev)
		v = 0;
	m_kbd_head = 0;
	m_kbd_tail = 0;
	m_kbd_count = 0;
	for (auto &v : m_kbd_fifo)
		v = 0;
	m_kdc_timer->adjust(attotime::from_hz(120), 0, attotime::from_hz(120));
	m_fdc->set_floppy(m_floppy->get_device());
	// 8" governo runs at a fixed 500 kbps data rate (there is no rate register; the
	// FDC's MF bit picks FM vs MFM per command). MAME's µPD765 defaults to 250 kbps,
	// which halves the cell clock and makes every read miss the address mark.
	m_fdc->set_rate(500000);
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
	m_maincpu->viack().set(FUNC(m40_state::vi_ack_r));   // governo VI vector (VETTN)
	m_maincpu->nviack().set(FUNC(m40_state::nviack_r));
	// Segment-trap acknowledge: the CPU's SEGT-ack cycle reads the identifier word
	// from the MMU (which also drops the SEGT line).
	m_maincpu->segtack().set(FUNC(m40_state::segtack_r));

	Z8010(config, m_mmu, 32_MHz_XTAL / 8);
	// MMU violation -> Z8001 segment trap (exercised by UC3003's TRAP REQUEST TEST:
	// it write-protects a segment via the descriptor attributes, performs a violating
	// write, and expects the trap handler it installed at PSA+0x20 to run).
	m_mmu->out_segt_cb().set_inputline(m_maincpu, z8001_device::SEGT_LINE);

	PIT8253(config, m_pit);
	// ch0 (mode 2 rate gen) prescales ch1 (mode 0) — ch0 OUT -> ch1 CLK cascade
	m_pit->set_clk<0>(32_MHz_XTAL / 16);
	m_pit->out_handler<0>().set(m_pit, FUNC(pit8253_device::write_clk1));
	m_pit->out_handler<1>().set(FUNC(m40_state::pit_out1_w));   // ch1 OUT -> UC timer VI (gated by VIENO)
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
	PALETTE(config, m_palette, FUNC(m40_state::palette_init), 3);

	MC6845(config, m_crtc, 32_MHz_XTAL / 12);   // ~2.67 MHz char clock (dots/char assumed 8)
	m_crtc->set_screen("screen");
	m_crtc->set_show_border_area(false);
	m_crtc->set_char_width(8);
	m_crtc->set_update_row_callback(FUNC(m40_state::crtc_update_row));

	// GO280 FDU floppy governo — uPD765 FDC (P8272) + 8" drive
	M40_UPD765A(config, m_fdc, 8_MHz_XTAL);
	m_fdc->set_ready_line_connected(true);
	m_fdc->set_select_lines_connected(true);
	m_fdc->intrq_wr_callback().set(FUNC(m40_state::fdc_intrq_w));
	m_fdc->drq_wr_callback().set(FUNC(m40_state::fdc_drq_w));
	m_fdc->idx_wr_callback().set(FUNC(m40_state::fdu_index_w));
	FLOPPY_CONNECTOR(config, "fdc:0", m40_floppies, "8dsdd", m40_state::floppy_formats);

	// AM9517A DMAC (manual §3.3): channel 2 is the FDC data channel. The FDC's
	// DMARO drives DREQ2; the DMAC reads/writes the FDC data register on DACK and
	// moves bytes to/from physical memory (24-bit address via the 0xF6 latch). TC
	// terminates the µPD765 transfer.
	AM9517A(config, m_dmac, 8_MHz_XTAL / 2);
	m_dmac->out_hreq_callback().set(FUNC(m40_state::dma_hreq_w));
	m_dmac->out_eop_callback().set(FUNC(m40_state::dma_eop_w));
	m_dmac->in_memr_callback().set(FUNC(m40_state::dma_memr));
	m_dmac->out_memw_callback().set(FUNC(m40_state::dma_memw));
	m_dmac->in_ior_callback<2>().set(m_fdc, FUNC(m40_upd765a_device::dma_r));
	m_dmac->out_iow_callback<2>().set(m_fdc, FUNC(m40_upd765a_device::dma_w));

	// FDU on-board 8253 (manual §3.4): ch0 = ~10 ms time base from CLK10 (1 us),
	// cascaded to ch1 which generates INTMO (motor spin-up 500 ms / rd-wr time-out
	// 800 ms); ch2 masks the FDC index signal.
	PIT8253(config, m_fdu_timer);
	m_fdu_timer->set_clk<0>(1'000'000);   // CLK10 = 1 us
	m_fdu_timer->out_handler<0>().set(m_fdu_timer, FUNC(pit8253_device::write_clk1));
	m_fdu_timer->out_handler<1>().set(FUNC(m40_state::fdu_timer_out));   // ch1 -> INTMO
}

// ANK1426 (105-key) keyboard. Each PORT_BIT position must line up with the
// matching entry in codes[4][16] (kdc_poll), which holds the M40 scancode the
// keyboard MCU sends for that key. A standard PC/PS2 keyboard is mapped onto the
// M40 physical layout; ESC = EXIT (scancode 0x3d) to abort diagnostics.
static INPUT_PORTS_START( m40 )
	PORT_START("K0")  // number row, ' \ <-, DEL, TAB, RETURN
	PORT_BIT(0x0001, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_1) PORT_CHAR('1')
	PORT_BIT(0x0002, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_2) PORT_CHAR('2')
	PORT_BIT(0x0004, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_3) PORT_CHAR('3')
	PORT_BIT(0x0008, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_4) PORT_CHAR('4')
	PORT_BIT(0x0010, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_5) PORT_CHAR('5')
	PORT_BIT(0x0020, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_6) PORT_CHAR('6')
	PORT_BIT(0x0040, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_7) PORT_CHAR('7')
	PORT_BIT(0x0080, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_8) PORT_CHAR('8')
	PORT_BIT(0x0100, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_9) PORT_CHAR('9')
	PORT_BIT(0x0200, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_0) PORT_CHAR('0')
	PORT_BIT(0x0400, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_QUOTE) PORT_CHAR('\'')
	PORT_BIT(0x0800, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_BACKSLASH) PORT_CHAR('\\')
	PORT_BIT(0x1000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_LEFT) PORT_NAME("Back-tab <-") PORT_CHAR(UCHAR_MAMEKEY(LEFT))
	PORT_BIT(0x2000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_BACKSPACE) PORT_NAME("DEL") PORT_CHAR(8)
	PORT_BIT(0x4000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_TAB) PORT_CHAR(9)
	PORT_BIT(0x8000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_ENTER) PORT_NAME("RETURN") PORT_CHAR(13)

	PORT_START("K1")  // Q..P [ ] , A S D F
	PORT_BIT(0x0001, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_Q) PORT_CHAR('q')
	PORT_BIT(0x0002, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_W) PORT_CHAR('w')
	PORT_BIT(0x0004, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_E) PORT_CHAR('e')
	PORT_BIT(0x0008, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_R) PORT_CHAR('r')
	PORT_BIT(0x0010, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_T) PORT_CHAR('t')
	PORT_BIT(0x0020, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_Y) PORT_CHAR('y')
	PORT_BIT(0x0040, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_U) PORT_CHAR('u')
	PORT_BIT(0x0080, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_I) PORT_CHAR('i')
	PORT_BIT(0x0100, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_O) PORT_CHAR('o')
	PORT_BIT(0x0200, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_P) PORT_CHAR('p')
	PORT_BIT(0x0400, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_OPENBRACE) PORT_CHAR('[')
	PORT_BIT(0x0800, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_CLOSEBRACE) PORT_CHAR(']')
	PORT_BIT(0x1000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_A) PORT_CHAR('a')
	PORT_BIT(0x2000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_S) PORT_CHAR('s')
	PORT_BIT(0x4000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_D) PORT_CHAR('d')
	PORT_BIT(0x8000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_F) PORT_CHAR('f')

	PORT_START("K2")  // G H J K L ; Z X C V B N M , . /
	PORT_BIT(0x0001, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_G) PORT_CHAR('g')
	PORT_BIT(0x0002, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_H) PORT_CHAR('h')
	PORT_BIT(0x0004, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_J) PORT_CHAR('j')
	PORT_BIT(0x0008, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_K) PORT_CHAR('k')
	PORT_BIT(0x0010, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_L) PORT_CHAR('l')
	PORT_BIT(0x0020, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_COLON) PORT_CHAR(';')
	PORT_BIT(0x0040, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_Z) PORT_CHAR('z')
	PORT_BIT(0x0080, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_X) PORT_CHAR('x')
	PORT_BIT(0x0100, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_C) PORT_CHAR('c')
	PORT_BIT(0x0200, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_V) PORT_CHAR('v')
	PORT_BIT(0x0400, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_B) PORT_CHAR('b')
	PORT_BIT(0x0800, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_N) PORT_CHAR('n')
	PORT_BIT(0x1000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_M) PORT_CHAR('m')
	PORT_BIT(0x2000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_COMMA) PORT_CHAR(',')
	PORT_BIT(0x4000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_STOP) PORT_CHAR('.')
	PORT_BIT(0x8000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_SLASH) PORT_CHAR('/')

	PORT_START("K3")  // numeric keypad, SPACE, EXIT(F12), SHIFT
	PORT_BIT(0x0001, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_7_PAD) PORT_CHAR(UCHAR_MAMEKEY(7_PAD))
	PORT_BIT(0x0002, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_8_PAD) PORT_CHAR(UCHAR_MAMEKEY(8_PAD))
	PORT_BIT(0x0004, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_9_PAD) PORT_CHAR(UCHAR_MAMEKEY(9_PAD))
	PORT_BIT(0x0008, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_4_PAD) PORT_CHAR(UCHAR_MAMEKEY(4_PAD))
	PORT_BIT(0x0010, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_5_PAD) PORT_CHAR(UCHAR_MAMEKEY(5_PAD))
	PORT_BIT(0x0020, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_6_PAD) PORT_CHAR(UCHAR_MAMEKEY(6_PAD))
	PORT_BIT(0x0040, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_1_PAD) PORT_CHAR(UCHAR_MAMEKEY(1_PAD))
	PORT_BIT(0x0080, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_2_PAD) PORT_CHAR(UCHAR_MAMEKEY(2_PAD))
	PORT_BIT(0x0100, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_3_PAD) PORT_CHAR(UCHAR_MAMEKEY(3_PAD))
	PORT_BIT(0x0200, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_0_PAD) PORT_CHAR(UCHAR_MAMEKEY(0_PAD))
	PORT_BIT(0x0400, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_DEL_PAD) PORT_CHAR(UCHAR_MAMEKEY(DEL_PAD))
	PORT_BIT(0x0800, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_MINUS_PAD) PORT_CHAR(UCHAR_MAMEKEY(MINUS_PAD))
	PORT_BIT(0x1000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_ENTER_PAD) PORT_NAME("SKIP") PORT_CHAR(UCHAR_MAMEKEY(ENTER_PAD))
	PORT_BIT(0x2000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_SPACE) PORT_CHAR(' ')
	PORT_BIT(0x4000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_ESC) PORT_NAME("EXIT") PORT_CHAR(UCHAR_MAMEKEY(ESC))
	PORT_BIT(0x8000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_LSHIFT) PORT_NAME("SHIFT") PORT_CHAR(UCHAR_SHIFT_1)
INPUT_PORTS_END

//**************************************************************************
//  ROM
//**************************************************************************

ROM_START( m40 )
	ROM_REGION16_BE( 0x4000, "maincpu", 0 )
	ROM_LOAD( "m40rom-6.0.bin", 0x0000, 0x4000, CRC(8114ebec) SHA1(4e2c65b95718c77a87dbee0288f323bd1c8837a3) )
ROM_END

} // anonymous namespace

//    YEAR  NAME  PARENT  COMPAT  MACHINE  INPUT  CLASS      INIT        COMPANY     FULLNAME           FLAGS
COMP( 1982, m40,  0,      0,      m40,     m40,  m40_state, empty_init, "Olivetti", "M40 (L1)",        MACHINE_NOT_WORKING | MACHINE_NO_SOUND )
