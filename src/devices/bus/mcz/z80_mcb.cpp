// license:BSD-3-Clause
// copyright-holders:Salvatore Paxia
/***************************************************************************

    MCZ  Z80 MCB CPU Module

****************************************************************************/

#include "emu.h"
#include "z80_mcb.h"
#include "modules.h"

#include "cpu/z80/z80.h"
#include "machine/clock.h"
#include "machine/i8251.h"
#include "machine/z80ctc.h"
#include "bus/rs232/rs232.h"


#define SER_CLOCK 19.6608_MHz_XTAL / 8  
#define CTC_CLOCK 19.6608_MHz_XTAL / 8  
#define CPU_CLOCK 19.6608_MHz_XTAL / 8 
#define MAIN_CLOCK 19.6608_MHz_XTAL / 8 

namespace {

class z80mcb_device : public device_t, public device_mcz_sysbus_card_interface
{
public:
	// construction/destruction
	z80mcb_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock);
	void test(int state);

private:
	// device-level overrides
	virtual void device_start() override;
	virtual void device_reset() override;
	virtual void device_resolve_objects() override;
	virtual void device_add_mconfig(machine_config &config) override;
	//virtual ioport_constructor device_input_ports() const override;
	virtual const tiny_rom_entry *device_rom_region() const override;

	

	//void clk_w(int state) { m_bus->clk_w(state); }
	//void tx_w(int state) { m_bus->tx_w(state); }
	//T map(0x0000, 0x0bff).rom();
	//T map(0x0c00, 0xffff).ram();

	void addrmap_mem(address_map &map) { map.unmap_value_high(); }
	void addrmap_io(address_map &map) { map.unmap_value_high(); }

	required_device<z80_device> m_maincpu;
	required_device<i8251_device> m_serial1;
	required_device<z80ctc_device> m_ctc0;
	required_memory_region m_rom;
	std::unique_ptr<u8[]> m_ram;

	u8 mcz_config_portr();
	u8 mcz_ctc3_portr();
	u8 mcz_zoom_ctrl_r();
	void mcz_zoom_ctrl_w(offs_t offset, u8 data);
	u8 mcz_zoom_data_r();
	void mcz_zoom_data_w(offs_t offset, u8 data);

};

z80mcb_device::z80mcb_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: device_t(mconfig, MCZ_Z80MCB, tag, owner, clock)
	, device_mcz_sysbus_card_interface(mconfig, *this)
	, m_maincpu(*this, "maincpu")	
	, m_serial1(*this, "i8251_1")
	, m_ctc0(*this, "ctc0")
	, m_rom(*this, "rom")
	, m_ram(nullptr)
{
}

void z80mcb_device::device_start()
{
	// Setup CPU
	m_bus->set_bus_clock(MAIN_CLOCK);
	m_maincpu->set_daisy_config(m_bus->get_daisy_chain());

	// Setup RAM
	m_ram = std::make_unique<u8[]>(0xe400);
	std::fill_n(m_ram.get(), 0xe400, 0xff);
	save_pointer(NAME(m_ram), 0xe400);
	m_bus->installer(AS_PROGRAM)->install_ram(0x0c00, 0xffff, m_ram.get());
		//m_bus->disksector_callback().append(*this, FUNC(z80mcb_device::test));
	m_bus->assing_cpu(m_maincpu);
}
u8 z80mcb_device::mcz_config_portr()
{
	printf("returned config\n");
	return 10;
}

u8 z80mcb_device::mcz_ctc3_portr()
{
	printf("returned ctc3->ff \n");
	return 0xff;
}

u8 z80mcb_device::mcz_zoom_ctrl_r()
{
	return m_bus->bus_r(0);
}

void z80mcb_device::mcz_zoom_ctrl_w(offs_t offset, u8 data)
{
	m_bus->bus_w(0,data);
}

u8 z80mcb_device::mcz_zoom_data_r()
{
	return m_bus->bus_r(1);
}

void z80mcb_device::mcz_zoom_data_w(offs_t offset, u8 data)
{
	m_bus->bus_w(1,data);
}

void z80mcb_device::device_reset()
{

	m_bus->installer(AS_IO)->install_read_handler(0xd4, 0xd6, 0, 0xff00, 0, read8sm_delegate(*m_ctc0, FUNC(z80ctc_device::read)));
	m_bus->installer(AS_IO)->install_write_handler(0xd4, 0xd7, 0, 0xff00, 0, write8sm_delegate(*m_ctc0, FUNC(z80ctc_device::write)));
	m_bus->installer(AS_IO)->install_readwrite_handler(0xde, 0xde, 0, 0xff00, 0, read8smo_delegate(*m_serial1, FUNC(i8251_device::data_r)), write8smo_delegate(*m_serial1, FUNC(i8251_device::data_w)));
	m_bus->installer(AS_IO)->install_readwrite_handler(0xdf, 0xdf, 0, 0xff00, 0, read8smo_delegate(*m_serial1, FUNC(i8251_device::status_r)), write8smo_delegate(*m_serial1, FUNC(i8251_device::control_w)));
	m_bus->installer(AS_IO)->install_read_handler(0xdd, 0xdd, 0, 0xff00, 0,read8smo_delegate(*this, FUNC(z80mcb_device::mcz_config_portr)));
	m_bus->installer(AS_IO)->install_read_handler(0xd7, 0xd7, 0, 0xff00, 0,read8smo_delegate(*this, FUNC(z80mcb_device::mcz_ctc3_portr)));

	m_bus->installer(AS_IO)->install_read_handler(0xff, 0xff, 0, 0xff00, 0,read8smo_delegate(*this, FUNC(z80mcb_device::mcz_zoom_ctrl_r)));
	m_bus->installer(AS_IO)->install_write_handler(0xff, 0xff, 0, 0xff00, 0,write8sm_delegate(*this, FUNC(z80mcb_device::mcz_zoom_ctrl_w)));

	m_bus->installer(AS_IO)->install_read_handler(0xfd, 0xfd, 0, 0xff00, 0,read8smo_delegate(*this, FUNC(z80mcb_device::mcz_zoom_data_r)));
	m_bus->installer(AS_IO)->install_write_handler(0xfd, 0xfd, 0, 0xff00, 0,write8sm_delegate(*this, FUNC(z80mcb_device::mcz_zoom_data_w)));

	m_bus->installer(AS_PROGRAM)->install_rom(0x0000, 0x0bff, m_rom->base());
}

void z80mcb_device::test(int state)
{
	//printf("MCB CB %d\n",(int)state);
	//while (true) {};

}

void z80mcb_device::device_resolve_objects()
{
	// Setup CPU
	printf("RESOLVE OBJECTS***************\n");
	
	m_bus->assign_installer(AS_PROGRAM, &m_maincpu->space(AS_PROGRAM));
	m_bus->assign_installer(AS_IO, &m_maincpu->space(AS_IO));

	m_bus->add_to_daisy_chain(m_ctc0->tag());
	m_bus->disksector_callback().append(m_ctc0, FUNC(z80ctc_device::trg0));
	
	printf("Append done to append***************\n");
		
}

static DEVICE_INPUT_DEFAULTS_START( terminal )
	DEVICE_INPUT_DEFAULTS( "RS232_TXBAUD", 0xff, RS232_BAUD_9600 )
	DEVICE_INPUT_DEFAULTS( "RS232_RXBAUD", 0xff, RS232_BAUD_9600 )
	DEVICE_INPUT_DEFAULTS( "RS232_DATABITS", 0xff, RS232_DATABITS_8 )
	DEVICE_INPUT_DEFAULTS( "RS232_PARITY", 0xff, RS232_PARITY_NONE )
	DEVICE_INPUT_DEFAULTS( "RS232_STOPBITS", 0xff, RS232_STOPBITS_1 )
DEVICE_INPUT_DEFAULTS_END

void z80mcb_device::device_add_mconfig(machine_config &config)
{
	Z80(config, m_maincpu, CPU_CLOCK);
	m_maincpu->set_addrmap(AS_PROGRAM, &z80mcb_device::addrmap_mem);
	m_maincpu->set_addrmap(AS_IO, &z80mcb_device::addrmap_io);

	Z80CTC(config, m_ctc0, XTAL(CTC_CLOCK));
	m_ctc0->intr_callback().set_inputline(m_maincpu, INPUT_LINE_IRQ0);
	
	

	I8251(config, m_serial1, CTC_CLOCK);
	m_serial1->txd_handler().set("rs232", FUNC(rs232_port_device::write_txd));
	m_serial1->dtr_handler().set("rs232", FUNC(rs232_port_device::write_dtr));
	m_serial1->rts_handler().set("rs232", FUNC(rs232_port_device::write_rts));
	
	clock_device &uart_clock(CLOCK(config, "uart_clock", 9600 * 16));
	uart_clock.signal_handler().set(m_serial1, FUNC(i8251_device::write_rxc));
	uart_clock.signal_handler().append(m_serial1, FUNC(i8251_device::write_txc));


	auto &rs232_1(RS232_PORT(config, "rs232", default_rs232_devices, "terminal"));
	rs232_1.rxd_handler().set(m_serial1, FUNC(i8251_device::write_rxd));
	rs232_1.dsr_handler().set(m_serial1, FUNC(i8251_device::write_dsr));
	rs232_1.cts_handler().set(m_serial1, FUNC(i8251_device::write_cts));
	rs232_1.set_option_device_input_defaults("terminal", DEVICE_INPUT_DEFAULTS_NAME(terminal));

}

 
ROM_START(mcz)
	ROM_REGION(0x2000, "rom",0)
	//ROM_LOAD("PROM.78089.BIN",   0x0000, 0x0c00, CRC(6884e85c) SHA1(6928dd283ba817c53766b1d8a9199dcd9f4341fc))
	ROM_LOAD("PROM.79318.BIN",   0x0000, 0x0c00, CRC(80d58210) SHA1(f58bfe8d7e20e8fe5ba9c882122005576359d99c))
	//ROM_LOAD("PROM.79219_HD.BIN",   0x0000, 0x0c00, CRC(ce561609) SHA1(bd4953cb9359aa2037c225e7bb55639352bfb5d3))


ROM_END

const tiny_rom_entry *z80mcb_device::device_rom_region() const
{
	return ROM_NAME( mcz );
}


}
//**************************************************************************
//  DEVICE DEFINITIONS
//**************************************************************************

DEFINE_DEVICE_TYPE_PRIVATE(MCZ_Z80MCB, device_mcz_sysbus_card_interface, z80mcb_device, "mcz_z80mcb", "MCZ Z80 MCB module")
