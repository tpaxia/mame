// license:BSD-3-Clause
// copyright-holders:Salvatore Paxia
/***************************************************************************

    Plasmo

    Real homebrew Z8002 single-board computer (not an emulated-only design):
    documented by its designer "Plasmo" on the VCFED forum thread "A homebrew
    Z8002" (https://forum.vcfed.org/index.php?threads/a-homebrew-z8002.1253969/,
    read through page 6, the 2025-10-05 revision). Modeled on z8002demo.cpp,
    the generic non-segmented Z8002/CP/M-8000 machine in this same directory,
    but with real hardware facts substituted in: a CPLD-synthesized UART (no
    discrete UART chip), a CompactFlash ATA task-file interface, and a single
    2-bit pseudo-segment banking register instead of z8002demo's 6-register
    chunked MMU.

    Memory (128 KiB RAM, four fixed 32 KiB quarters -- see Z8002 MemMap.pdf
    from the forum thread, and CPM8000/src/bios/plasmo/TPA.md):

        System-Lo  0x0000-0x7fff  System mode  fixed, always this quarter
        System-Hi  0x8000-0xffff  System mode  MAPPED via port 0x85:
                                     00 -> System-Lo   (redundant)
                                     01 -> System-Hi's own dedicated RAM (reset)
                                     10 -> Normal-Lo (TPA lower 32K)
                                     11 -> Normal-Hi (TPA upper 32K)
        Normal-Lo  0x0000-0x7fff  Normal mode  fixed, always this quarter
        Normal-Hi  0x8000-0xffff  Normal mode  fixed, always this quarter

    Normal mode (the TPA) is NEVER banked -- there is no register that points
    it anywhere else, unlike z8002demo's NBANK_I/NBANK_D. Unlike z8002demo's
    mmu.v, the bank mux here does not distinguish instruction fetches from
    data accesses: any access to 0x8000-0xffff in System mode is redirected
    while port 0x85 is diverted from its idle value, instruction fetches
    included.

    I/O map (from the board's own monitor source, Z8002mon.txt, and the CPLD
    schematic top_cpld_scm_segment_regs.pdf):

        0x81       CPLD UART data (Tx/Rx)
        0x83       CPLD UART status: bit0=RxRdy (cleared on data read),
                   bit1=TxEmpty. Fixed 115200 N81, no interrupts.
        0x85       pseudo-segment bank-select latch (see above)
        0x10-0x1e  CompactFlash, standard ATA task-file register layout
                   (word-wide data reg + 7 byte task-file regs), LBA mode

    NOT modeled: the boot ROM at 0x0-0xff and the 0x87 ROM-disable port. The
    real board's bootstrap is synthesized inside the CPLD and has not been
    dumped -- there is no ROM image to load. This driver is a test harness
    for CPM8000's src/bios/plasmo BIOS package: boot a system image by hand
    with the MAME debugger (load it at 0x0000, set pc=0, go) rather than by
    modeling an undumped ROM. See src/bios/plasmo/PLASMO_TESTING.md in the CPM8000 repository.

***************************************************************************/

#include "emu.h"

#include "cpu/z8000/z8000.h"
#include "bus/ata/ataintf.h"
#include "bus/rs232/rs232.h"
#include "diserial.h"
#include "imagedev/snapquik.h"

#include <vector>

//**************************************************************************
//  CPLD UART -- Tx/Rx shift register + status, no discrete chip.
//  Fixed 115200 N81, no interrupts (matches the real board: Plasmo's own
//  post -- "Serial port is 115200N81, no initialization required. I have
//  not added interrupt circuitry right now.").
//
//  DECLARE_DEVICE_TYPE/DEFINE_DEVICE_TYPE must be at global scope (the
//  device_finder explicit instantiations they emit are not legal inside a
//  namespace), so this device -- unlike plasmo_state below -- is not wrapped
//  in the anonymous namespace even though it is only used by this driver.
//**************************************************************************

class plasmo_uart_device : public device_t, public device_serial_interface
{
public:
	plasmo_uart_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	auto txd_handler() { return m_txd_handler.bind(); }

	void rxd_w(int state) { device_serial_interface::rx_w(state); }

	u8 data_r()
	{
		m_rx_ready = false;
		return m_rx_data;
	}

	void data_w(u8 data)
	{
		if (m_tx_active)
			return;			// real hardware has no way to signal overrun here either
		m_tx_active = true;
		transmit_register_setup(data);
		if (data >= 0x20 && data < 0x7f)
			logerror("console tx: %02x '%c'\n", data, char(data));
		else
			logerror("console tx: %02x\n", data);
	}

	u8 status_r()
	{
		return (m_rx_ready ? 0x01 : 0x00) | (m_tx_active ? 0x00 : 0x02);
	}

protected:
	virtual void device_start() override ATTR_COLD
	{
		set_data_frame(1, 8, PARITY_NONE, STOP_BITS_1);
		set_rcv_rate(115200);
		set_tra_rate(115200);

		save_item(NAME(m_rx_data));
		save_item(NAME(m_rx_ready));
		save_item(NAME(m_tx_active));
	}

	virtual void device_reset() override ATTR_COLD
	{
		m_rx_data = 0;
		m_rx_ready = false;
		m_tx_active = false;
		m_txd_handler(1);
	}

	virtual void rcv_complete() override
	{
		receive_register_extract();
		m_rx_data = get_received_char();
		m_rx_ready = true;
	}

	virtual void tra_callback() override
	{
		m_txd_handler(transmit_register_get_data_bit());
	}

	virtual void tra_complete() override
	{
		m_tx_active = false;
	}

private:
	devcb_write_line m_txd_handler;
	u8 m_rx_data = 0;
	bool m_rx_ready = false;
	bool m_tx_active = false;
};

DECLARE_DEVICE_TYPE(PLASMO_UART, plasmo_uart_device)
DEFINE_DEVICE_TYPE(PLASMO_UART, plasmo_uart_device, "plasmo_uart", "Plasmo CPLD UART")

plasmo_uart_device::plasmo_uart_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock) :
	device_t(mconfig, PLASMO_UART, tag, owner, clock),
	device_serial_interface(mconfig, *this),
	m_txd_handler(*this)
{
}

namespace {

//**************************************************************************
//  Main driver
//**************************************************************************

class plasmo_state : public driver_device
{
public:
	plasmo_state(machine_config const &mconfig, device_type type, char const *tag) :
		driver_device(mconfig, type, tag),
		m_maincpu(*this, "maincpu"),
		m_uart(*this, "uart"),
		m_ata(*this, "ata")
	{
	}

	void plasmo(machine_config &config);

protected:
	virtual void machine_start() override ATTR_COLD;
	virtual void machine_reset() override ATTR_COLD;

private:
	void mem_map(address_map &map) ATTR_COLD;
	void io_map(address_map &map) ATTR_COLD;

	u16 memory_r(offs_t offset, u16 mem_mask = ~0);
	void memory_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u32 translate(u16 address) const;

	u16 uart_data_r(offs_t offset, u16 mem_mask = ~0);
	void uart_data_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 uart_stat_r(offs_t offset, u16 mem_mask = ~0);
	u16 ata_r(offs_t offset, u16 mem_mask = ~0);
	void ata_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	void bank_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	void normal_w(int state);

	// Test-harness image load: pokes a flat CP/M-8000 system image (e.g.
	// build/system/plasmo/plasmo.boot) straight into RAM at 0x0000 and starts
	// execution there, standing in for the real board's undumped bootstrap
	// ROM. See this file's header comment and src/bios/plasmo/PLASMO_TESTING.md in the CPM8000 repository.
	QUICKLOAD_LOAD_MEMBER(quickload_cb);

	required_device<z8002_device> m_maincpu;
	required_device<plasmo_uart_device> m_uart;
	required_device<ata_interface_device> m_ata;

	std::unique_ptr<u16[]> m_ram;	// 4 x 32K quarters, 0x10000 words total
	u8 m_bank = 1;			// port 0x85 latch; reset default = 01
	bool m_normal = false;
};

// Quarter layout in m_ram (word index):
//   0x0000-0x3fff  System-Lo           (0x0000-0x7fff bytes)
//   0x4000-0x7fff  System-Hi dedicated (0x8000-0xffff bytes)
//   0x8000-0xbfff  Normal-Lo / TPA lo  (0x0000-0x7fff bytes, Normal mode)
//   0xc000-0xffff  Normal-Hi / TPA hi  (0x8000-0xffff bytes, Normal mode)
static constexpr u32 Q_SYSLO = 0x0000;
static constexpr u32 Q_SYSHI = 0x4000;
static constexpr u32 Q_NRMLO = 0x8000;
static constexpr u32 Q_NRMHI = 0xc000;

u32 plasmo_state::translate(u16 address) const
{
	u16 const word_addr = address >> 1;	// m_ram is word-indexed

	if (m_normal)
		return (BIT(address, 15) ? Q_NRMHI : Q_NRMLO) | (word_addr & 0x3fff);

	if (!BIT(address, 15))
		return Q_SYSLO | word_addr;

	switch (m_bank)
	{
	case 0: return Q_SYSLO | (word_addr & 0x3fff);	// "not useful but can be done"
	default:
	case 1: return Q_SYSHI | (word_addr & 0x3fff);	// reset/idle default
	case 2: return Q_NRMLO | (word_addr & 0x3fff);
	case 3: return Q_NRMHI | (word_addr & 0x3fff);
	}
}

u16 plasmo_state::memory_r(offs_t offset, u16 mem_mask)
{
	return m_ram[translate(u16(offset << 1))];
}

void plasmo_state::memory_w(offs_t offset, u16 data, u16 mem_mask)
{
	COMBINE_DATA(&m_ram[translate(u16(offset << 1))]);
}

QUICKLOAD_LOAD_MEMBER(plasmo_state::quickload_cb)
{
	u32 const length = image.length();
	if (length == 0 || length > 0x10000)
		return std::make_pair(image_error::INVALIDLENGTH, std::string());

	std::vector<u8> buffer(length);
	if (image.fread(&buffer[0], length) != length)
		return std::make_pair(image_error::UNSPECIFIED, std::string());

	// Fresh reset state: System mode, bank=1 (System-Hi = dedicated RAM), so
	// logical 0x0000-0xffff is one flat 64K view -- exactly matching where
	// biosboot.8kn expects the whole system image to land.
	address_space &space = m_maincpu->space(AS_PROGRAM);
	for (u32 i = 0; i < length; i++)
		space.write_byte(i, buffer[i]);

	m_maincpu->set_state_int(STATE_GENPC, 0);

	return std::make_pair(std::error_condition(), std::string());
}

u16 plasmo_state::uart_data_r(offs_t offset, u16 mem_mask)
{
	return 0xff00 | m_uart->data_r();
}

void plasmo_state::uart_data_w(offs_t offset, u16 data, u16 mem_mask)
{
	if (ACCESSING_BITS_0_7)
		m_uart->data_w(u8(data));
}

u16 plasmo_state::uart_stat_r(offs_t offset, u16 mem_mask)
{
	return 0xff00 | m_uart->status_r();
}

u16 plasmo_state::ata_r(offs_t offset, u16 mem_mask)
{
	u16 const data = m_ata->cs0_r(offset);

	// The task-file registers (offset 1-7: error/sectcnt/LBA/dev-head/status)
	// are byte-wide and cs0_r() returns them in the LOW byte. The real
	// board wires CF to D15-D8 (the HIGH byte at an even address), so mirror
	// the byte into both lanes -- matching how WRPORT_B already mirrors
	// outgoing byte writes into both lanes.
	//
	// Offset 0 (DATA) is different: it's a genuine 16-bit word transfer
	// (rdsec/wrsec use `in`/`out`, not `inb`/`outb`), and command_r()
	// already returns the full word there -- must NOT be masked/mirrored.
	if (offset == 0)
		return data;
	u16 const byte = data & 0xff;
	return byte | (byte << 8);
}

void plasmo_state::ata_w(offs_t offset, u16 data, u16 mem_mask)
{
	m_ata->cs0_w(offset, data);
}

void plasmo_state::bank_w(offs_t offset, u16 data, u16 mem_mask)
{
	if (ACCESSING_BITS_0_7)
		m_bank = data & 3;
}

void plasmo_state::normal_w(int state)
{
	m_normal = bool(state);
}

void plasmo_state::mem_map(address_map &map)
{
	map(0x0000, 0xffff).rw(FUNC(plasmo_state::memory_r), FUNC(plasmo_state::memory_w));
}

void plasmo_state::io_map(address_map &map)
{
	map.unmap_value_high();
	map(0x0010, 0x001f).rw(FUNC(plasmo_state::ata_r), FUNC(plasmo_state::ata_w));
	map(0x0080, 0x0081).rw(FUNC(plasmo_state::uart_data_r), FUNC(plasmo_state::uart_data_w));
	map(0x0082, 0x0083).r(FUNC(plasmo_state::uart_stat_r));
	map(0x0084, 0x0085).w(FUNC(plasmo_state::bank_w));
}

void plasmo_state::machine_start()
{
	m_ram = make_unique_clear<u16[]>(0x10000);
	save_pointer(NAME(m_ram), 0x10000);
	save_item(NAME(m_bank));
	save_item(NAME(m_normal));
}

void plasmo_state::machine_reset()
{
	m_bank = 1;
	m_normal = false;
}

static DEVICE_INPUT_DEFAULTS_START(terminal)
	DEVICE_INPUT_DEFAULTS("RS232_RXBAUD", 0xff, RS232_BAUD_115200)
	DEVICE_INPUT_DEFAULTS("RS232_TXBAUD", 0xff, RS232_BAUD_115200)
	DEVICE_INPUT_DEFAULTS("RS232_DATABITS", 0xff, RS232_DATABITS_8)
	DEVICE_INPUT_DEFAULTS("RS232_PARITY", 0xff, RS232_PARITY_NONE)
	DEVICE_INPUT_DEFAULTS("RS232_STOPBITS", 0xff, RS232_STOPBITS_1)
DEVICE_INPUT_DEFAULTS_END

void plasmo_state::plasmo(machine_config &config)
{
	// "Clock is 3.68MHz" -- top_cpld_scm_segment_regs.pdf. Later posts tested
	// faster CPU parts (7.37/14.7/25/29.49 MHz) against the same CPLD logic,
	// but 3.68 MHz is the one clock the schematic itself documents.
	Z8002(config, m_maincpu, 3.68_MHz_XTAL);
	m_maincpu->set_addrmap(AS_PROGRAM, &plasmo_state::mem_map);
	m_maincpu->set_addrmap(AS_DATA, &plasmo_state::mem_map);
	m_maincpu->set_addrmap(AS_OPCODES, &plasmo_state::mem_map);
	m_maincpu->set_addrmap(z8002_device::AS_STACK, &plasmo_state::mem_map);
	m_maincpu->set_addrmap(AS_IO, &plasmo_state::io_map);
	m_maincpu->ns().set(FUNC(plasmo_state::normal_w));

	PLASMO_UART(config, m_uart, 0);
	m_uart->txd_handler().set("rs232", FUNC(rs232_port_device::write_txd));

	rs232_port_device &rs232(RS232_PORT(config, "rs232", default_rs232_devices, "terminal"));
	rs232.rxd_handler().set(m_uart, FUNC(plasmo_uart_device::rxd_w));
	rs232.set_option_device_input_defaults("terminal", DEVICE_INPUT_DEFAULTS_NAME(terminal));
	rs232.set_option_device_input_defaults("pty", DEVICE_INPUT_DEFAULTS_NAME(terminal));
	rs232.set_option_device_input_defaults("null_modem", DEVICE_INPUT_DEFAULTS_NAME(terminal));

	ATA_INTERFACE(config, m_ata).options(ata_devices, "hdd", nullptr, false);

	// Test-harness image load (-quik <file>) -- see quickload_cb's comment.
	QUICKLOAD(config, "quickload", "boot,bin,img").set_load_callback(FUNC(plasmo_state::quickload_cb));
}

ROM_START(plasmo)
	// No boot ROM: the real board's CPLD-resident bootstrap has not been
	// dumped. Load a system image with the debugger instead -- see
	// src/bios/plasmo/PLASMO_TESTING.md in the CPM8000 repository.
ROM_END

} // anonymous namespace

//    YEAR  NAME    PARENT  COMPAT  MACHINE  INPUT  CLASS         INIT        COMPANY   FULLNAME   FLAGS
COMP(2026, plasmo, 0,      0,      plasmo,  0,     plasmo_state, empty_init, "Homebrew", "Plasmo", MACHINE_NO_SOUND_HW | MACHINE_SUPPORTS_SAVE)
