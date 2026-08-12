// license:BSD-3-Clause
// copyright-holders:Salvatore Paxia
/***************************************************************************

    Z8002-demo

    Non-segmented CP/M-8000 demonstration machine with a Z80-SIO console,
    ATA storage and a system/normal banking MMU.

***************************************************************************/

#include "emu.h"

#include "cpu/z8000/z8000.h"
#include "bus/ata/ataintf.h"
#include "bus/rs232/rs232.h"
#include "machine/clock.h"
#include "machine/z80ctc.h"
#include "machine/z80sio.h"

namespace {

class z8002demo_state : public driver_device
{
public:
	z8002demo_state(machine_config const &mconfig, device_type type, char const *tag) :
		driver_device(mconfig, type, tag),
		m_maincpu(*this, "maincpu"),
		m_ctc(*this, "ctc"),
		m_sio(*this, "sio"),
		m_ata(*this, "ata"),
		m_monitor(*this, "monitor")
	{
	}

	void z8002demo(machine_config &config);

protected:
	virtual void machine_start() override ATTR_COLD;
	virtual void machine_reset() override ATTR_COLD;

private:
	void program_map(address_map &map) ATTR_COLD;
	void data_map(address_map &map) ATTR_COLD;
	void opcode_map(address_map &map) ATTR_COLD;
	void stack_map(address_map &map) ATTR_COLD;
	void io_map(address_map &map) ATTR_COLD;
	void special_io_map(address_map &map) ATTR_COLD;

	u16 program_r(offs_t offset, u16 mem_mask = ~0);
	void program_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 data_r(offs_t offset, u16 mem_mask = ~0);
	void data_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 memory_r(offs_t offset, bool program, u16 mem_mask);
	void memory_w(offs_t offset, bool program, u16 data, u16 mem_mask);
	u32 translate(u16 address, bool program) const;

	u16 ctc_r(offs_t offset, u16 mem_mask = ~0);
	void ctc_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 sio_r(offs_t offset, u16 mem_mask = ~0);
	void sio_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 ata_cs0_r(offs_t offset, u16 mem_mask = ~0);
	void ata_cs0_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 ata_cs1_r(offs_t offset, u16 mem_mask = ~0);
	void ata_cs1_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 switch_r(offs_t offset, u16 mem_mask = ~0);
	void led_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 mmu_r(offs_t offset, u16 mem_mask = ~0);
	void mmu_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	void normal_w(int state);

	u8 mmu_register(unsigned index) const;
	void set_mmu_register(unsigned index, u8 data);

	required_device<z8002_device> m_maincpu;
	required_device<z80ctc_device> m_ctc;
	required_device<z80sio_device> m_sio;
	required_device<ata_interface_device> m_ata;
	required_region_ptr<u16> m_monitor;

	std::unique_ptr<u16[]> m_ram;
	u8 m_nbank_i = 4;
	u8 m_nbank_d = 4;
	u8 m_shome = 4;
	u8 m_ssel = 0;
	u8 m_sap0 = 0;
	u8 m_sap1 = 0;
	u8 m_led = 0;
	bool m_normal = false;
};

u32 z8002demo_state::translate(u16 address, bool program) const
{
	if (m_normal)
		return (u32(program ? m_nbank_i : m_nbank_d) << 16) | address;

	u8 const logical_chunk = address >> 14;
	u8 physical_chunk;
	if (!BIT(m_ssel, logical_chunk))
	{
		physical_chunk = (m_shome << 2) | logical_chunk;
	}
	else
	{
		bool later_aperture = false;
		for (unsigned chunk = 0; chunk < logical_chunk; ++chunk)
			later_aperture |= BIT(m_ssel, chunk);
		physical_chunk = later_aperture ? m_sap1 : m_sap0;
	}

	return (u32(physical_chunk) << 14) | (address & 0x3fff);
}

u16 z8002demo_state::memory_r(offs_t offset, bool program, u16 mem_mask)
{
	u32 const physical = translate(u16(offset << 1), program);
	if (physical < 0x40000)
		return m_ram[physical >> 1];
	if ((physical & 0x70000) == 0x40000)
		return m_monitor[(physical & 0x3fff) >> 1];
	return 0xffff;
}

void z8002demo_state::memory_w(offs_t offset, bool program, u16 data, u16 mem_mask)
{
	u32 const physical = translate(u16(offset << 1), program);
	if (physical < 0x40000)
		COMBINE_DATA(&m_ram[physical >> 1]);
}

u16 z8002demo_state::program_r(offs_t offset, u16 mem_mask)
{
	return memory_r(offset, true, mem_mask);
}

void z8002demo_state::program_w(offs_t offset, u16 data, u16 mem_mask)
{
	memory_w(offset, true, data, mem_mask);
}

u16 z8002demo_state::data_r(offs_t offset, u16 mem_mask)
{
	return memory_r(offset, false, mem_mask);
}

void z8002demo_state::data_w(offs_t offset, u16 data, u16 mem_mask)
{
	memory_w(offset, false, data, mem_mask);
}

u16 z8002demo_state::ctc_r(offs_t offset, u16 mem_mask)
{
	return 0xff00 | m_ctc->read(offset);
}

void z8002demo_state::ctc_w(offs_t offset, u16 data, u16 mem_mask)
{
	if (ACCESSING_BITS_0_7)
		m_ctc->write(offset, data);
}

u16 z8002demo_state::sio_r(offs_t offset, u16 mem_mask)
{
	return 0xff00 | m_sio->cd_ba_r(offset);
}

void z8002demo_state::sio_w(offs_t offset, u16 data, u16 mem_mask)
{
	if (ACCESSING_BITS_0_7)
		m_sio->cd_ba_w(offset, data);
}

u16 z8002demo_state::ata_cs0_r(offs_t offset, u16 mem_mask)
{
	return m_ata->cs0_r(offset, mem_mask);
}

void z8002demo_state::ata_cs0_w(offs_t offset, u16 data, u16 mem_mask)
{
	m_ata->cs0_w(offset, data, mem_mask);
}

u16 z8002demo_state::ata_cs1_r(offs_t offset, u16 mem_mask)
{
	return m_ata->cs1_r(offset, mem_mask);
}

void z8002demo_state::ata_cs1_w(offs_t offset, u16 data, u16 mem_mask)
{
	m_ata->cs1_w(offset, data, mem_mask);
}

u16 z8002demo_state::switch_r(offs_t offset, u16 mem_mask)
{
	return 0xff88;
}

void z8002demo_state::led_w(offs_t offset, u16 data, u16 mem_mask)
{
	if (ACCESSING_BITS_0_7)
		m_led = data & 3;
}

u8 z8002demo_state::mmu_register(unsigned index) const
{
	switch (index)
	{
	case 0: return m_nbank_i;
	case 1: return m_nbank_d;
	case 2: return m_shome;
	case 3: return m_ssel;
	case 4: return m_sap0;
	case 5: return m_sap1;
	}
	return 0xff;
}

void z8002demo_state::set_mmu_register(unsigned index, u8 data)
{
	switch (index)
	{
	case 0: m_nbank_i = data & 7; break;
	case 1: m_nbank_d = data & 7; break;
	case 2: m_shome = data & 7; break;
	case 3: m_ssel = data & 15; break;
	case 4: m_sap0 = data & 31; break;
	case 5: m_sap1 = data & 31; break;
	}
}

u16 z8002demo_state::mmu_r(offs_t offset, u16 mem_mask)
{
	u16 data = 0xffff;
	if (ACCESSING_BITS_8_15)
		data = (data & 0x00ff) | (u16(mmu_register(offset * 2)) << 8);
	if (ACCESSING_BITS_0_7)
		data = (data & 0xff00) | mmu_register(offset * 2 + 1);
	return data;
}

void z8002demo_state::mmu_w(offs_t offset, u16 data, u16 mem_mask)
{
	if (m_normal)
		return;
	if (ACCESSING_BITS_8_15)
		set_mmu_register(offset * 2, data >> 8);
	if (ACCESSING_BITS_0_7)
		set_mmu_register(offset * 2 + 1, data);
}

void z8002demo_state::normal_w(int state)
{
	m_normal = bool(state);
}

void z8002demo_state::program_map(address_map &map)
{
	map(0x0000, 0xffff).rw(FUNC(z8002demo_state::program_r), FUNC(z8002demo_state::program_w));
}

void z8002demo_state::data_map(address_map &map)
{
	map(0x0000, 0xffff).rw(FUNC(z8002demo_state::data_r), FUNC(z8002demo_state::data_w));
}

void z8002demo_state::opcode_map(address_map &map)
{
	map(0x0000, 0xffff).rw(FUNC(z8002demo_state::program_r), FUNC(z8002demo_state::program_w));
}

void z8002demo_state::stack_map(address_map &map)
{
	map(0x0000, 0xffff).rw(FUNC(z8002demo_state::data_r), FUNC(z8002demo_state::data_w));
}

void z8002demo_state::io_map(address_map &map)
{
	map.unmap_value_high();
	map(0x0004, 0x0005).w(FUNC(z8002demo_state::led_w));
	map(0x0020, 0x002f).rw(FUNC(z8002demo_state::ata_cs0_r), FUNC(z8002demo_state::ata_cs0_w));
	map(0x0030, 0x003f).rw(FUNC(z8002demo_state::ata_cs1_r), FUNC(z8002demo_state::ata_cs1_w));
	map(0xff10, 0xff17).rw(FUNC(z8002demo_state::ctc_r), FUNC(z8002demo_state::ctc_w));
	map(0xff18, 0xff1f).rw(FUNC(z8002demo_state::sio_r), FUNC(z8002demo_state::sio_w));
	map(0xff20, 0xff21).r(FUNC(z8002demo_state::switch_r));
}

void z8002demo_state::special_io_map(address_map &map)
{
	map.unmap_value_high();
	map(0xffe0, 0xffe5).rw(FUNC(z8002demo_state::mmu_r), FUNC(z8002demo_state::mmu_w));
}

void z8002demo_state::machine_start()
{
	m_ram = make_unique_clear<u16[]>(0x20000);
	save_pointer(NAME(m_ram), 0x20000);
	save_item(NAME(m_nbank_i));
	save_item(NAME(m_nbank_d));
	save_item(NAME(m_shome));
	save_item(NAME(m_ssel));
	save_item(NAME(m_sap0));
	save_item(NAME(m_sap1));
	save_item(NAME(m_led));
	save_item(NAME(m_normal));
}

void z8002demo_state::machine_reset()
{
	m_nbank_i = 4;
	m_nbank_d = 4;
	m_shome = 4;
	m_ssel = 0;
	m_sap0 = 0;
	m_sap1 = 0;
	m_led = 0;
	m_normal = false;
}

static DEVICE_INPUT_DEFAULTS_START(terminal)
	DEVICE_INPUT_DEFAULTS("RS232_RXBAUD", 0xff, RS232_BAUD_9600)
	DEVICE_INPUT_DEFAULTS("RS232_TXBAUD", 0xff, RS232_BAUD_9600)
	DEVICE_INPUT_DEFAULTS("RS232_DATABITS", 0xff, RS232_DATABITS_8)
	DEVICE_INPUT_DEFAULTS("RS232_PARITY", 0xff, RS232_PARITY_NONE)
	DEVICE_INPUT_DEFAULTS("RS232_STOPBITS", 0xff, RS232_STOPBITS_2)
DEVICE_INPUT_DEFAULTS_END

void z8002demo_state::z8002demo(machine_config &config)
{
	Z8002(config, m_maincpu, 16.2_MHz_XTAL / 4);
	m_maincpu->set_addrmap(AS_PROGRAM, &z8002demo_state::program_map);
	m_maincpu->set_addrmap(AS_DATA, &z8002demo_state::data_map);
	m_maincpu->set_addrmap(AS_OPCODES, &z8002demo_state::opcode_map);
	m_maincpu->set_addrmap(z8002_device::AS_STACK, &z8002demo_state::stack_map);
	m_maincpu->set_addrmap(AS_IO, &z8002demo_state::io_map);
	m_maincpu->set_addrmap(z8002_device::AS_SIO, &z8002demo_state::special_io_map);
	m_maincpu->ns().set(FUNC(z8002demo_state::normal_w));

	Z80CTC(config, m_ctc, 16.2_MHz_XTAL);
	m_ctc->zc_callback<1>().set(m_sio, FUNC(z80sio_device::txca_w));
	m_ctc->zc_callback<1>().append(m_sio, FUNC(z80sio_device::rxca_w));
	m_ctc->zc_callback<2>().set(m_sio, FUNC(z80sio_device::rxtxcb_w));
	clock_device &ctc_clock(CLOCK(config, "ctc_clock", 2.4576_MHz_XTAL));
	ctc_clock.signal_handler().set(m_ctc, FUNC(z80ctc_device::trg1));
	ctc_clock.signal_handler().append(m_ctc, FUNC(z80ctc_device::trg2));

	Z80SIO(config, m_sio, 16.2_MHz_XTAL);
	m_sio->out_txdb_callback().set("rs232", FUNC(rs232_port_device::write_txd));
	m_sio->out_dtrb_callback().set("rs232", FUNC(rs232_port_device::write_dtr));
	m_sio->out_rtsb_callback().set("rs232", FUNC(rs232_port_device::write_rts));

	rs232_port_device &rs232(RS232_PORT(config, "rs232", default_rs232_devices, "terminal"));
	rs232.rxd_handler().set(m_sio, FUNC(z80sio_device::rxb_w));
	rs232.cts_handler().set(m_sio, FUNC(z80sio_device::ctsb_w));
	rs232.dcd_handler().set(m_sio, FUNC(z80sio_device::dcdb_w));
	rs232.set_option_device_input_defaults("terminal", DEVICE_INPUT_DEFAULTS_NAME(terminal));
	rs232.set_option_device_input_defaults("pty", DEVICE_INPUT_DEFAULTS_NAME(terminal));
	rs232.set_option_device_input_defaults("null_modem", DEVICE_INPUT_DEFAULTS_NAME(terminal));

	ATA_INTERFACE(config, m_ata).options(ata_devices, "hdd", nullptr, false);
}

ROM_START(z8002demo)
	ROM_REGION16_BE(0x4000, "monitor", ROMREGION_ERASE00)
	ROM_LOAD("z8kmon.bin", 0x0000, 0x0972, CRC(5e3726d3) SHA1(f8698f60a660768c41a335e0b50a35e4b036e717))
ROM_END

} // anonymous namespace

//    YEAR  NAME       PARENT  COMPAT  MACHINE    INPUT  CLASS              INIT        COMPANY   FULLNAME      FLAGS
COMP(2026, z8002demo, 0,      0,      z8002demo, 0,     z8002demo_state, empty_init, "Homebrew", "Z8002-demo", MACHINE_NO_SOUND_HW | MACHINE_SUPPORTS_SAVE)
