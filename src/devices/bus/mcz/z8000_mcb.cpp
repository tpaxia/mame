// license:BSD-3-Clause
// copyright-holders:Salvatore Paxia
/***************************************************************************

    MCZ  Z80 MCB CPU Module

****************************************************************************/

#include "emu.h"
#include "z8000_mcb.h"
#include "modules.h"

#include "cpu/z8000/z8000.h"
#include "machine/clock.h"
#include "machine/ram.h"

#define MAIN_CLOCK 6000000 /* 6 MHz */
#define RAM_SIZE      0x40000

namespace {

class z8000mcb_device : public device_t, public device_mcz_sysbus_card_interface
{
public:
	// construction/destruction
	z8000mcb_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock);

	uint8_t card_r(offs_t offset) override;
	void card_w(offs_t offset, uint8_t data) override;


private:
	// device-level overrides
	virtual void device_start() override;
	virtual void device_reset() override;
	virtual void device_resolve_objects() override;
	virtual void device_add_mconfig(machine_config &config) override;
	
	virtual const tiny_rom_entry *device_rom_region() const override;

	void z8000_data_mem(address_map &map);
	void z8000_io(address_map &map);
	void z8000_program_mem(address_map &map);
	uint16_t nmi_ack_cb();
	
	void addrmap_mem(address_map &map) { map.unmap_value_high(); }
	void addrmap_io(address_map &map) { map.unmap_value_high(); }

	required_device<z8001_device> m_altCpu;
	memory_share_creator<uint16_t> m_ram;
	
	uint8_t zoom_r(offs_t offset);
	void zoom_w(offs_t offset, uint8_t data);
	int fifoState=0;
	int fifoCounter=0;
	uint8_t fifoBuffer[2048];

	void install_memory();
	
};



z8000mcb_device::z8000mcb_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: device_t(mconfig, MCZ_Z8000MCB, tag, owner, clock)
	, device_mcz_sysbus_card_interface(mconfig, *this)
	, m_altCpu(*this, "z8001_cpu")	
	, m_ram(*this, "z8000ram", RAM_SIZE, ENDIANNESS_BIG)
{
}

void z8000mcb_device::install_memory()
{

printf("Installed Memory (%d)\n",(int)(m_ram.bytes()));
	// Memsize = 256k
	

	//uint16_t *memptr = m_ram->ptr();
	address_space& pspace = m_altCpu->space(AS_PROGRAM);
	address_space& dspace = m_altCpu->space(AS_DATA);

	/* install mainboard memory */

	/* <0>0000 */
	pspace.install_ram(0x00040, 0x0ffff, 0, m_ram + 0x0000);
	dspace.install_ram(0x00040, 0x0ffff, 0, m_ram + 0x0000);

}

uint8_t z8000mcb_device::card_r(offs_t offset) 
{
	if (offset==0)
	{
		if (fifoState<2)
		{
			printf("Z80 HAS CONTROL!!!!!!!!!!!!!!!!!!!! %d\n",fifoBuffer[0]);
			fifoState=1;
			return fifoBuffer[0];
		}
	 	return 0xff; 
	}
	else
	{
		uint8_t data = 0xff;
		if (fifoState==1)
		{
			data=fifoBuffer[++fifoCounter];
			fifoCounter%=2048;
		}
		return data;
	}
}

void z8000mcb_device::card_w(offs_t offset, uint8_t data)
{
	if (offset==0)
	{
		if ((data&1)==0)
		{
			printf("*************NMI\n");
			m_altCpu->set_input_line(z8001_device::NMI_LINE, ASSERT_LINE);

		}
		
	}
	if (fifoState<2)
	{
		if (offset==0)
		{
			printf("Z80 HAS RELEASED!!!!!!!!!!!!!!!!!!!!\n");
			fifoState=0;
			fifoCounter=0;
			
		}
		else
		{
		//	printf("Z80 IS WRITING!!!!!!!!!! %d\n",data);
			fifoBuffer[fifoCounter++]=data;
			fifoCounter%=2048;

		}
	}
	//printf("BUS ZOOM card W %d %d new state = %d\n",offset,data,fifoState);
}

void z8000mcb_device::zoom_w(offs_t offset, uint8_t data)
{
	
	if (fifoState!=1)
	{
		if (offset==1)
		{
			printf("Z8000 HAS RELEASED^^^^^^^^^^^^\n");
			fifoState=0;
			fifoCounter=0;
		}
		else
		{
			fifoBuffer[fifoCounter]=data;
			fifoCounter=(++fifoCounter%2048);

		}
	}
	//printf("ZOOM card W %d %d new state = %d\n",offset,data,fifoState);
}

uint8_t z8000mcb_device::zoom_r(offs_t offset) 
{
	//printf("ZOOM card R %d \n",offset);
	if (offset==1)
	{
		if (fifoState!=1)
		{
			printf("Z8000 HAS CONTROL^^^^^^^^^^^^ %d (count=%d)\n",fifoBuffer[0],fifoCounter);
			//getchar();
			fifoState=2;
			if (fifoBuffer[0]==2 || fifoBuffer[0]==3) 
			{
					printf("I got %x:%04x count=%04x\n",fifoBuffer[1],(fifoBuffer[2]<<8)|fifoBuffer[3],(fifoBuffer[4]<<8)|fifoBuffer[5]);
					//getchar();
			}
			return fifoBuffer[0];
		}
	 	return 0xff; 
	}
	else
	{
		uint8_t data = 0xff;
		if (fifoState!=1)
		{
			data=fifoBuffer[fifoCounter];
			fifoCounter=(++fifoCounter%2048);
		}
		return data;
	}
}

void z8000mcb_device::z8000_program_mem(address_map &map)
{
	map.unmap_value_high();
	map(0x0, 0x3f).rom().region("z8000rom", 0x00000);
}

void z8000mcb_device::z8000_data_mem(address_map &map)
{
	map.unmap_value_high();
	map(0x0, 0x3f).rom().region("z8000rom", 0x00000);
}
void z8000mcb_device::z8000_io(address_map &map)
{
	map.unmap_value_high();
	map(0x1, 0x3).rw(FUNC(z8000mcb_device::zoom_r), FUNC(z8000mcb_device::zoom_w)).umask16(0x00ff);
}

uint16_t z8000mcb_device::nmi_ack_cb()
{
	printf("NMI ACK CB\n");
	m_altCpu->set_input_line(z8001_device::NMI_LINE, CLEAR_LINE);
	return 0xffff;
}

void z8000mcb_device::device_start()
{
	install_memory();
	
}

void z8000mcb_device::device_reset()
{

	
}


void z8000mcb_device::device_resolve_objects()
{
	// Setup CPU
	printf("RESOLVE OBJECTS***************\n");
	printf("Ready to append***************\n");
	printf("Append done***************\n");
		
}

void z8000mcb_device::device_add_mconfig(machine_config &config)
{
	// Setup CPU
	/* basic machine hardware */
	Z8001(config, m_altCpu, MAIN_CLOCK);
	m_altCpu->set_addrmap(AS_PROGRAM, &z8000mcb_device::z8000_program_mem);
	m_altCpu->set_addrmap(AS_DATA, &z8000mcb_device::z8000_data_mem);
	m_altCpu->set_addrmap(AS_IO, &z8000mcb_device::z8000_io);
	m_altCpu->nmiack().set(FUNC(z8000mcb_device::nmi_ack_cb));
  
	//printf("added ram default size\n");
	//RAM(config, "z8000ram").set_default_value(0);
	//m_ram->set_default_size("256K");
}

ROM_START(mcz8000)
	ROM_REGION16_BE(0x40, "z8000rom",0)
	ROM_LOAD("mcb8000.bin",   0x0000, 0x0040, CRC(be1bd915) SHA1(27eeb18d87532de96c7cf9f44c8294c3b2fe05ef))
ROM_END

const tiny_rom_entry *z8000mcb_device::device_rom_region() const
{
	return ROM_NAME( mcz8000 );
}


}
//**************************************************************************
//  DEVICE DEFINITIONS
//**************************************************************************

DEFINE_DEVICE_TYPE_PRIVATE(MCZ_Z8000MCB, device_mcz_sysbus_card_interface, z8000mcb_device, "mcz_z8000mcb", "MCZ Z8000 MCB module")
