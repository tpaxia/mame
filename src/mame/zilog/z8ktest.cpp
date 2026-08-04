// license:BSD-3-Clause
// copyright-holders:
/***************************************************************************

    Zilog Z8000 instruction test harness

    A MAME model of the FPGA test rig used to capture golden instruction
    traces from a real Z8001 (Quartus project z8001_ext_test in the
    Z8000_FPGA/z8000_test repository).

    The rig is a Z80 supervisor sharing a dual-port BRAM with the Z8000
    under test. The host drives the Z80 over a serial link with an ASCII
    command protocol (WRn/RRn registers, WMaaaa/RMaaaa memory, WPnn/RPnn
    I/O ports, INIT, EX, RS). Reproducing the rig rather than inventing a
    new test protocol means the existing host-side runner drives this
    machine unmodified, and every semantic the golden captures depend on -
    the Z8000 bootstrap, the register dump area, the JP-to-dump-routine
    convention, HALT/TOUT detection, the cycle-based timeout, the I/O port
    model and the segmented BRAM banking - is inherited rather than
    reimplemented.

    Hardware modelled (see src/z80_harness.v and src/z8k_io_ports.v):

    Z80 memory
        0000-1fff  firmware RAM (loaded from the z80_fw ROM region)

    Z80 I/O
        00         UART data
        01         UART status: bit0 tx_ready, bit1 rx_valid
        10-11      Z8000 BRAM address register (15 bits)
        12-13      Z8000 BRAM read data
        14         control: bit0 z8k_rst_n, bit1 memory write strobe
        15         status: bit0 halt_n, bit1 bus_active, bit2 cycle_timeout
        16-19      cycle count (32 bits)
        1a-1b      fetch count (16 bits)
        1c-1f      cycle limit (32 bits, 0 = no limit)
        20-21      trace read address
        22-26      trace read data (36 bits)
        27-28      trace write count
        29         Z8000 status type (ST)
        2a-2d      instruction cycle count
        30-47      I/O port file, 12 registers x 2 bytes
        48-a7      scripted read FIFOs, 12 registers x 4 slots x 2 bytes
        a8         clear all scripted read FIFOs

    The Z80 address register addresses bytes. 0000-1fff is the active
    BRAM; 2000-3fff is the bootstrap master store, which the Z8000 cannot
    reach. The Z8000 sees the BRAM folded as {sn[0], addr[11:0]}: two 4K
    segments, so segment 0 offset 0000-0fff is BRAM 0000-0fff and segment
    1 offset 0000-0fff is BRAM 1000-1fff.

***************************************************************************/

#include "emu.h"

#include "cpu/z80/z80.h"
#include "cpu/z8000/z8000.h"
#include "machine/ram.h"

#include "fileio.h"

namespace {

// The Z8000 bus watcher latches a halt when the first word of an
// instruction fetch reads as the HALT opcode.
static constexpr u16 HALT_OPCODE = 0x7a00;

static constexpr unsigned BRAM_BYTES   = 0x2000;  // 8K, two 4K segments
static constexpr unsigned MASTER_BYTES = 0x1000;  // bootstrap master store
static constexpr unsigned IO_REGS      = 12;
static constexpr unsigned SEQ_SLOTS    = 4;

class z8ktest_state : public driver_device
{
public:
	z8ktest_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_z80(*this, "z80")
		, m_z8k(*this, "z8k")
		, m_z80ram(*this, "z80ram")
		, m_fw(*this, "z80_fw")
		, m_link(OPEN_FLAG_READ | OPEN_FLAG_WRITE)
	{
	}

	void z8ktest(machine_config &config);
	void z8ktest01(machine_config &config);
	void z8ktest02(machine_config &config);

protected:
	virtual void machine_start() override ATTR_COLD;
	virtual void machine_reset() override ATTR_COLD;

private:
	required_device<z80_device> m_z80;
	required_device<z8002_device> m_z8k;
	required_shared_ptr<u8> m_z80ram;
	required_memory_region m_fw;

	// ---- shared store ----
	std::unique_ptr<u8[]> m_bram;
	std::unique_ptr<u8[]> m_master;

	// ---- harness registers ----
	u16 m_addr_reg = 0;      // 15-bit byte address into BRAM / master
	u16 m_data_reg = 0;
	bool m_rst_n = false;
	u32 m_cycle_limit = 0;
	u16 m_trace_rd_addr = 0;

	// ---- Z8000 run state ----
	bool m_halt_detected = false;
	bool m_bus_active = false;
	bool m_timeout = false;
	u64 m_cycle_base = 0;
	u32 m_fetch_count = 0;

	// ---- I/O port file ----
	u16 m_ioreg[IO_REGS] = { 0 };
	u16 m_seq[IO_REGS][SEQ_SLOTS] = { { 0 } };
	u8 m_seq_count[IO_REGS] = { 0 };
	u8 m_seq_pos[IO_REGS] = { 0 };

	// ---- host link ----
	emu_file m_link;
	bool m_link_open = false;
	u8 m_rx_byte = 0;
	bool m_rx_valid = false;

	void z80_mem(address_map &map) ATTR_COLD;
	void z80_io(address_map &map) ATTR_COLD;
	void z8k_mem(address_map &map) ATTR_COLD;
	void z8k_opcodes(address_map &map) ATTR_COLD;
	void z8k_mem16(address_map &map) ATTR_COLD;
	void z8k_opcodes16(address_map &map) ATTR_COLD;
	void z8k_io_std(address_map &map) ATTR_COLD;
	void z8k_io_spc(address_map &map) ATTR_COLD;

	u8 z80_io_r(offs_t offset);
	void z80_io_w(offs_t offset, u8 data);

	u16 z8k_mem_r(offs_t offset, u16 mem_mask);
	void z8k_mem_w(offs_t offset, u16 data, u16 mem_mask);
	u16 z8k_fetch_r(offs_t offset, u16 mem_mask);
	u16 z8k_io_r(offs_t offset, unsigned base);
	void z8k_io_w(offs_t offset, u16 data, u16 mem_mask, unsigned base);
	u16 z8k_io_std_r(offs_t offset) { return z8k_io_r(offset, 0); }
	void z8k_io_std_w(offs_t offset, u16 data, u16 mem_mask) { z8k_io_w(offset, data, mem_mask, 0); }
	u16 z8k_io_spc_r(offs_t offset) { return z8k_io_r(offset, 6); }
	void z8k_io_spc_w(offs_t offset, u16 data, u16 mem_mask) { z8k_io_w(offset, data, mem_mask, 6); }

	// Fold a Z8000 space address onto the BRAM: {sn[0], addr[11:0]}.
	static constexpr offs_t bram_index(offs_t addr)
	{
		return (((addr >> 16) & 1) << 12) | (addr & 0x0fff);
	}

	u32 cycle_count() const;
	void set_z8k_reset(bool run);
	void poll_link();
};


//**************************************************************************
//  Z80 side
//**************************************************************************

void z8ktest_state::z80_mem(address_map &map)
{
	map(0x0000, 0x1fff).ram().share("z80ram");
}

void z8ktest_state::z80_io(address_map &map)
{
	map.global_mask(0xff);
	map(0x00, 0xff).rw(FUNC(z8ktest_state::z80_io_r), FUNC(z8ktest_state::z80_io_w));
}

u32 z8ktest_state::cycle_count() const
{
	// The device's cycle accounting restarts when the reset it is holding
	// actually takes effect, so the base can only be taken once the CPU is
	// demonstrably running - i.e. at the first opcode fetch.
	if (!m_rst_n || !m_bus_active)
		return 0;
	const u64 now = m_z8k->total_cycles();
	return (now < m_cycle_base) ? 0 : u32(now - m_cycle_base);
}

u8 z8ktest_state::z80_io_r(offs_t offset)
{
	const u8 port = offset & 0xff;

	// I/O port file: 0x30-0x47, register = (port-0x30)/2, odd = high byte
	if (port >= 0x30 && port <= 0x47)
	{
		const unsigned idx = (port - 0x30) >> 1;
		return BIT(port, 0) ? (m_ioreg[idx] >> 8) : (m_ioreg[idx] & 0xff);
	}

	switch (port)
	{
	case 0x00:
	{
		// Latch the byte being consumed before pulling the next one in;
		// poll_link() overwrites m_rx_byte.
		const u8 value = m_rx_byte;
		if (!machine().side_effects_disabled())
		{
			m_rx_valid = false;
			poll_link();
		}
		return value;
	}

	case 0x01:
		if (!machine().side_effects_disabled())
			poll_link();
		// bit0 tx_ready (always), bit1 rx_valid
		return 0x01 | (m_rx_valid ? 0x02 : 0x00);

	case 0x10: return m_addr_reg & 0xff;
	case 0x11: return (m_addr_reg >> 8) & 0x7f;

	case 0x12:
	case 0x13:
	{
		// Reads target the master store above 0x2000, the BRAM below.
		u16 value;
		if (BIT(m_addr_reg, 13))
		{
			const offs_t a = (m_addr_reg & (MASTER_BYTES - 1)) & ~1;
			value = (m_master[a] << 8) | m_master[a + 1];
		}
		else
		{
			const offs_t a = (m_addr_reg & (BRAM_BYTES - 1)) & ~1;
			value = (m_bram[a] << 8) | m_bram[a + 1];
		}
		return (port == 0x12) ? (value & 0xff) : (value >> 8);
	}

	case 0x14: return m_rst_n ? 1 : 0;

	case 0x15:
		return (m_halt_detected ? 0 : 0x01)     // halt_n
			 | (m_bus_active ? 0x02 : 0x00)
			 | (m_timeout ? 0x04 : 0x00);

	case 0x16: return cycle_count() & 0xff;
	case 0x17: return (cycle_count() >> 8) & 0xff;
	case 0x18: return (cycle_count() >> 16) & 0xff;
	case 0x19: return (cycle_count() >> 24) & 0xff;

	case 0x1a: return m_fetch_count & 0xff;
	case 0x1b: return (m_fetch_count >> 8) & 0xff;

	// Trace buffer: not modelled. Report an empty buffer so the host-side
	// trace read terminates immediately; golden comparison ignores traces.
	case 0x20: return m_trace_rd_addr & 0xff;
	case 0x21: return (m_trace_rd_addr >> 8) & 0x03;
	case 0x22: case 0x23: case 0x24: case 0x25: case 0x26:
		return 0x00;
	case 0x27: case 0x28:
		return 0x00;

	case 0x29: return 0x00;  // Z8000 ST

	case 0x2a: return cycle_count() & 0xff;
	case 0x2b: return (cycle_count() >> 8) & 0xff;
	case 0x2c: return (cycle_count() >> 16) & 0xff;
	case 0x2d: return (cycle_count() >> 24) & 0xff;
	}

	return 0xff;
}

void z8ktest_state::z80_io_w(offs_t offset, u8 data)
{
	const u8 port = offset & 0xff;

	// I/O port file
	if (port >= 0x30 && port <= 0x47)
	{
		const unsigned idx = (port - 0x30) >> 1;
		if (BIT(port, 0))
			m_ioreg[idx] = (m_ioreg[idx] & 0x00ff) | (data << 8);
		else
			m_ioreg[idx] = (m_ioreg[idx] & 0xff00) | data;
		return;
	}

	// Scripted read FIFOs: 0x48-0xa7, 12 regs x 4 slots x 2 bytes.
	// Writing any slot arms the sequence through that slot and rewinds.
	if (port >= 0x48 && port <= 0xa7)
	{
		const unsigned off = port - 0x48;
		const unsigned word = off >> 1;
		const unsigned idx = word >> 2;
		const unsigned slot = word & 3;
		if (BIT(off, 0))
			m_seq[idx][slot] = (m_seq[idx][slot] & 0x00ff) | (data << 8);
		else
			m_seq[idx][slot] = (m_seq[idx][slot] & 0xff00) | data;
		if (m_seq_count[idx] < slot + 1)
			m_seq_count[idx] = slot + 1;
		m_seq_pos[idx] = 0;
		return;
	}

	if (port == 0xa8)
	{
		std::fill(std::begin(m_seq_count), std::end(m_seq_count), 0);
		std::fill(std::begin(m_seq_pos), std::end(m_seq_pos), 0);
		return;
	}

	switch (port)
	{
	case 0x00:
		if (m_link_open)
			m_link.write(&data, 1);
		break;

	case 0x10: m_addr_reg = (m_addr_reg & 0x7f00) | data; break;
	case 0x11: m_addr_reg = (m_addr_reg & 0x00ff) | ((data & 0x7f) << 8); break;
	case 0x12: m_data_reg = (m_data_reg & 0xff00) | data; break;
	case 0x13: m_data_reg = (m_data_reg & 0x00ff) | (data << 8); break;

	case 0x14:
		if (BIT(data, 1))
		{
			// Write strobe: master store above 0x2000, BRAM below.
			if (BIT(m_addr_reg, 13))
			{
				const offs_t a = (m_addr_reg & (MASTER_BYTES - 1)) & ~1;
				m_master[a] = m_data_reg >> 8;
				m_master[a + 1] = m_data_reg & 0xff;
			}
			else
			{
				const offs_t a = (m_addr_reg & (BRAM_BYTES - 1)) & ~1;
				m_bram[a] = m_data_reg >> 8;
				m_bram[a + 1] = m_data_reg & 0xff;
			}
		}
		set_z8k_reset(BIT(data, 0));
		break;

	case 0x1c: m_cycle_limit = (m_cycle_limit & 0xffffff00) | data; break;
	case 0x1d: m_cycle_limit = (m_cycle_limit & 0xffff00ff) | (data << 8); break;
	case 0x1e: m_cycle_limit = (m_cycle_limit & 0xff00ffff) | (data << 16); break;
	case 0x1f: m_cycle_limit = (m_cycle_limit & 0x00ffffff) | (data << 24); break;

	case 0x20: m_trace_rd_addr = (m_trace_rd_addr & 0x0300) | data; break;
	case 0x21: m_trace_rd_addr = (m_trace_rd_addr & 0x00ff) | ((data & 3) << 8); break;
	}
}

void z8ktest_state::set_z8k_reset(bool run)
{
	if (run == m_rst_n)
		return;

	m_rst_n = run;
	if (run)
	{
		// Reset release starts a new execution: clear instrumentation.
		m_halt_detected = false;
		m_bus_active = false;
		m_timeout = false;
		m_fetch_count = 0;
		m_cycle_base = 0;
		m_z8k->set_input_line(INPUT_LINE_RESET, CLEAR_LINE);
		m_z8k->resume(SUSPEND_REASON_HALT);
	}
	else
	{
		m_z8k->set_input_line(INPUT_LINE_RESET, ASSERT_LINE);
		m_z8k->suspend(SUSPEND_REASON_HALT, 0);
	}
}


//**************************************************************************
//  Z8000 side
//**************************************************************************

void z8ktest_state::z8k_mem(address_map &map)
{
	map(0x000000, 0x7fffff).rw(FUNC(z8ktest_state::z8k_mem_r), FUNC(z8ktest_state::z8k_mem_w));
}

void z8ktest_state::z8k_opcodes(address_map &map)
{
	map(0x000000, 0x7fffff).r(FUNC(z8ktest_state::z8k_fetch_r));
}

// The Z8002 has a flat 16-bit space; segment folding still applies with
// sn tied to zero, so only the lower 4K of the BRAM is reachable.
void z8ktest_state::z8k_mem16(address_map &map)
{
	map(0x0000, 0xffff).rw(FUNC(z8ktest_state::z8k_mem_r), FUNC(z8ktest_state::z8k_mem_w));
}

void z8ktest_state::z8k_opcodes16(address_map &map)
{
	map(0x0000, 0xffff).r(FUNC(z8ktest_state::z8k_fetch_r));
}

u16 z8ktest_state::z8k_mem_r(offs_t offset, u16 mem_mask)
{
	const offs_t a = bram_index(offset << 1) & ~1;
	return (m_bram[a] << 8) | m_bram[a + 1];
}

void z8ktest_state::z8k_mem_w(offs_t offset, u16 data, u16 mem_mask)
{
	const offs_t a = bram_index(offset << 1) & ~1;
	if (ACCESSING_BITS_8_15)
		m_bram[a] = data >> 8;
	if (ACCESSING_BITS_0_7)
		m_bram[a + 1] = data & 0xff;
}

u16 z8ktest_state::z8k_fetch_r(offs_t offset, u16 mem_mask)
{
	const offs_t a = bram_index(offset << 1) & ~1;
	const u16 word = (m_bram[a] << 8) | m_bram[a + 1];

	if (!machine().side_effects_disabled())
	{
		if (!m_bus_active)
		{
			m_bus_active = true;
			m_cycle_base = m_z8k->total_cycles();
		}
		m_fetch_count++;

		// The rig latches a halt when the first word of an instruction
		// fetch reads as HALT, and stops counting from that point.
		if (word == HALT_OPCODE)
		{
			m_halt_detected = true;
			m_z8k->suspend(SUSPEND_REASON_HALT, 0);
		}
		else if (m_cycle_limit != 0 && cycle_count() >= m_cycle_limit)
		{
			m_timeout = true;
			m_z8k->suspend(SUSPEND_REASON_HALT, 0);
		}
	}

	return word;
}

// Standard I/O (regs 0-5) and special I/O (regs 6-11) both decode
// addresses 0x0100-0x010a; the register index is addr[3:1] plus the bank.
u16 z8ktest_state::z8k_io_r(offs_t offset, unsigned base)
{
	const offs_t addr = offset << 1;
	if ((addr & 0xfff0) != 0x0100)
		return 0xffff;

	const unsigned idx = base + ((addr >> 1) & 7);
	if (idx >= IO_REGS)
		return 0xffff;

	// A scripted sequence, once armed, is consumed one entry per read and
	// then sticks at its last entry.
	u16 value;
	if (m_seq_count[idx] != 0)
	{
		const unsigned slot = (m_seq_pos[idx] >= m_seq_count[idx])
			? unsigned(m_seq_count[idx] - 1) : unsigned(m_seq_pos[idx]);
		value = m_seq[idx][slot];
		if (!machine().side_effects_disabled() && m_seq_pos[idx] < m_seq_count[idx])
			m_seq_pos[idx]++;
	}
	else
	{
		value = m_ioreg[idx];
	}
	return value;
}

void z8ktest_state::z8k_io_w(offs_t offset, u16 data, u16 mem_mask, unsigned base)
{
	const offs_t addr = offset << 1;
	if ((addr & 0xfff0) != 0x0100)
		return;

	const unsigned idx = base + ((addr >> 1) & 7);
	if (idx >= IO_REGS)
		return;

	if (ACCESSING_BITS_8_15)
		m_ioreg[idx] = (m_ioreg[idx] & 0x00ff) | (data & 0xff00);
	if (ACCESSING_BITS_0_7)
		m_ioreg[idx] = (m_ioreg[idx] & 0xff00) | (data & 0x00ff);
}

void z8ktest_state::z8k_io_std(address_map &map)
{
	map(0x0000, 0xffff).rw(FUNC(z8ktest_state::z8k_io_std_r), FUNC(z8ktest_state::z8k_io_std_w));
}

void z8ktest_state::z8k_io_spc(address_map &map)
{
	map(0x0000, 0xffff).rw(FUNC(z8ktest_state::z8k_io_spc_r), FUNC(z8ktest_state::z8k_io_spc_w));
}


//**************************************************************************
//  Host link
//**************************************************************************

void z8ktest_state::poll_link()
{
	if (!m_link_open || m_rx_valid)
		return;

	u8 byte;
	if (m_link.read(&byte, 1) == 1)
	{
		m_rx_byte = byte;
		m_rx_valid = true;
	}
}


//**************************************************************************
//  Machine
//**************************************************************************

void z8ktest_state::machine_start()
{
	m_bram = std::make_unique<u8[]>(BRAM_BYTES);
	m_master = std::make_unique<u8[]>(MASTER_BYTES);
	std::fill_n(m_bram.get(), BRAM_BYTES, 0);
	std::fill_n(m_master.get(), MASTER_BYTES, 0);

	save_pointer(NAME(m_bram), BRAM_BYTES);
	save_pointer(NAME(m_master), MASTER_BYTES);
	save_item(NAME(m_addr_reg));
	save_item(NAME(m_data_reg));
	save_item(NAME(m_rst_n));
	save_item(NAME(m_cycle_limit));
	save_item(NAME(m_halt_detected));
	save_item(NAME(m_bus_active));
	save_item(NAME(m_timeout));
	save_item(NAME(m_fetch_count));
	save_item(NAME(m_ioreg));
	save_item(NAME(m_seq));
	save_item(NAME(m_seq_count));
	save_item(NAME(m_seq_pos));

	// The host link is a listening socket; the test runner connects to it
	// in place of the rig's serial port.
	const char *const spec = "socket.127.0.0.1:5800";
	if (!m_link.open(spec))
		m_link_open = true;
	else
		logerror("z8ktest: could not open host link %s\n", spec);
}

void z8ktest_state::machine_reset()
{
	// Firmware is copied into Z80 RAM, as the FPGA initialises its block RAM.
	std::copy_n(m_fw->base(), std::min<size_t>(m_fw->bytes(), 0x2000), &m_z80ram[0]);

	m_addr_reg = 0;
	m_data_reg = 0;
	m_cycle_limit = 0;
	m_rx_valid = false;
	std::fill(std::begin(m_ioreg), std::end(m_ioreg), 0);
	std::fill(std::begin(m_seq_count), std::end(m_seq_count), 0);
	std::fill(std::begin(m_seq_pos), std::end(m_seq_pos), 0);

	m_rst_n = true;      // force set_z8k_reset to act
	set_z8k_reset(false);
}

void z8ktest_state::z8ktest(machine_config &config)
{
	Z80(config, m_z80, 27_MHz_XTAL / 2);
	m_z80->set_addrmap(AS_PROGRAM, &z8ktest_state::z80_mem);
	m_z80->set_addrmap(AS_IO, &z8ktest_state::z80_io);

	// The supervisor polls the Z8000's bus-active and halt status in tight
	// loops with a bounded retry count, so the two CPUs have to interleave
	// at instruction granularity or the poll expires before the Z8000 has
	// executed anything.
	config.set_perfect_quantum(m_z80);
}

void z8ktest_state::z8ktest01(machine_config &config)
{
	z8ktest(config);

	z8001_device &cpu(Z8001(config, m_z8k, 4_MHz_XTAL));
	cpu.set_addrmap(AS_PROGRAM, &z8ktest_state::z8k_mem);
	cpu.set_addrmap(AS_DATA, &z8ktest_state::z8k_mem);
	cpu.set_addrmap(z8002_device::AS_STACK, &z8ktest_state::z8k_mem);
	cpu.set_addrmap(AS_OPCODES, &z8ktest_state::z8k_opcodes);
	cpu.set_addrmap(AS_IO, &z8ktest_state::z8k_io_std);
	cpu.set_addrmap(z8002_device::AS_SIO, &z8ktest_state::z8k_io_spc);
}

void z8ktest_state::z8ktest02(machine_config &config)
{
	z8ktest(config);

	z8002_device &cpu(Z8002(config, m_z8k, 4_MHz_XTAL));
	cpu.set_addrmap(AS_PROGRAM, &z8ktest_state::z8k_mem16);
	cpu.set_addrmap(AS_DATA, &z8ktest_state::z8k_mem16);
	cpu.set_addrmap(z8002_device::AS_STACK, &z8ktest_state::z8k_mem16);
	cpu.set_addrmap(AS_OPCODES, &z8ktest_state::z8k_opcodes16);
	cpu.set_addrmap(AS_IO, &z8ktest_state::z8k_io_std);
	cpu.set_addrmap(z8002_device::AS_SIO, &z8ktest_state::z8k_io_spc);
}

static INPUT_PORTS_START(z8ktest)
INPUT_PORTS_END

// Supervisor firmware, built from src/z80_fw.asm in the z8000_test repository.
#define Z8KTEST_FIRMWARE \
	ROM_REGION(0x2000, "z80_fw", ROMREGION_ERASE00) \
	ROM_LOAD("z80_fw.bin", 0x0000, 0x09f0, CRC(5f090142) SHA1(84fe6e77dfcd513ce81f4b73033d1cef0870198f))

ROM_START(z8ktest01)
	Z8KTEST_FIRMWARE
ROM_END

ROM_START(z8ktest02)
	Z8KTEST_FIRMWARE
ROM_END

} // anonymous namespace

//    YEAR  NAME       PARENT  COMPAT  MACHINE    INPUT    CLASS          INIT        COMPANY  FULLNAME                          FLAGS
COMP( 2026, z8ktest01, 0,      0,      z8ktest01, z8ktest, z8ktest_state, empty_init, "Zilog", "Z8001 instruction test harness",  MACHINE_NO_SOUND_HW )
COMP( 2026, z8ktest02, 0,      0,      z8ktest02, z8ktest, z8ktest_state, empty_init, "Zilog", "Z8002 instruction test harness",  MACHINE_NO_SOUND_HW )
