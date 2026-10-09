// license:BSD-3-Clause
// copyright-holders:Salvatore Paxia
// Z8001-unix: the same board interface as emu/test_driver.cpp.
// ROM loads the disk boot block; /boot loads /unix through the V7 filesystem.
#include "emu.h"
#include "cpu/z8000/z8000.h"
#include "imagedev/harddriv.h"
#include "machine/terminal.h"
#include "machine/ram.h"
#include "machine/timer.h"

namespace {
class z8001unix_state : public driver_device
{
public:
	z8001unix_state(machine_config const &c, device_type t, char const *tag) :
		driver_device(c,t,tag), m_cpu(*this,"maincpu"), m_terminal(*this,"terminal"),
		m_disk(*this,"harddisk"), m_rom(*this,"boot"), m_ram(*this,RAM_TAG) { }
	void z8001unix(machine_config &config);
protected:
	void machine_start() override;
	void machine_reset() override;
private:
	void program_map(address_map &map);
	void opcode_map(address_map &map);
	void data_map(address_map &map);
	void io_map(address_map &map);
	u16 memory_r(offs_t off, u16 mask, bool program, bool first);
	void memory_w(offs_t off, u16 data, u16 mask, bool program);
	u32 translate(u32 address, unsigned size, bool write, bool program);
	void fault(u32 address, unsigned size, u16 reason);
	u16 io_r(offs_t off, u16 mask = ~0);
	void io_w(offs_t off, u16 data, u16 mask = ~0);
	u8 byte_r(u16 port);
	void byte_w(u16 port, u8 value);
	void key(u8 data);
	void command(u8 value);
	void complete_write();
	void update_vi();
	void upage(u16 frame);
	TIMER_DEVICE_CALLBACK_MEMBER(tick);
	u16 nvi_ack() { m_cpu->set_input_line(z8001_device::NVI_LINE, CLEAR_LINE); return 0; }
	u16 vi_ack() { m_disk_irq=false; update_vi(); return 0; }
	u16 seg_ack() { m_cpu->set_input_line(z8001_device::SEGT_LINE, CLEAR_LINE); return 0; }
	required_device<z8001_device> m_cpu;
	required_device<generic_terminal_device> m_terminal;
	required_device<harddisk_image_device> m_disk;
	required_region_ptr<u8> m_rom;
	required_device<ram_device> m_ram;
	std::unique_ptr<u8[]> m_epu;
	u8 *physical(u32 p) { return p>=0x7f0000 ? &m_epu[p-0x7f0000] : &m_ram->pointer()[p]; }
	u16 physical_r(u32 p) { u8 *b=physical(p); return (b[0]<<8)|b[1]; }
	std::unique_ptr<u8[]> m_swap;
	u16 m_pages[4096]{}, m_attr[4096]{}, m_stack[128]{};
	u8 m_imap[128]{};
	u16 m_select=0, m_stacksel=0, m_fault=0, m_fseg=0, m_flow=0, m_fhigh=0;
	u32 m_first=0, m_fpc=0;
	bool m_normal=false, m_disk_irq=false;
	u8 m_rx[256]{};
	u16 m_rxin=0, m_rxout=0;
	u8 m_sc=0,m_sn=0,m_cl=0,m_ch=0,m_dh=0,m_status=0x40,m_error=0;
	u8 m_sector[512]{};
	u16 m_index=0;
	bool m_reading=false,m_writing=false;
	static constexpr u16 RO=0x8000, SYS=0x4000;
	static constexpr unsigned SWAP_BYTES=4*1024*1024;
};

void z8001unix_state::fault(u32 address, unsigned size, u16 reason)
{
	if (!m_fault) { m_fseg=(address>>16)&127; m_flow=address; m_fhigh=u16(address)+size-1; m_fpc=m_first; }
	else {
		if (((address>>16)&127)!=m_fseg || m_first!=m_fpc) reason|=128;
		m_flow=std::min(m_flow,u16(address)); m_fhigh=std::max(m_fhigh,u16(address+size-1));
	}
	m_fault|=1|reason;
	m_cpu->set_input_line(z8001_device::SEGT_LINE, ASSERT_LINE);
}

u32 z8001unix_state::translate(u32 address, unsigned size, bool write, bool program)
{
	unsigned const seg=(address>>16)&127, entry=(address>>11)&4095;
	u16 const frame=m_pages[entry], attr=m_attr[entry];
	u16 const reason=(write?2:4)|(program?8:0);
	// Reset vector is fetched before the CPU has loaded the system-mode FCW.
	if (frame==0xffff) { fault(address,size,reason|16); return ~0U; }
	if ((write&&(attr&RO)) || (m_normal&&(attr&SYS))) { fault(address,size,reason|32); return ~0U; }
	u32 const physical=(u32(frame)<<11)|(address&2047);
	if (physical+size>m_ram->size() && !(physical>=0x7f0000 && physical+size<=0x800000)) { fault(address,size,reason|32); return ~0U; }
	if (m_normal&&write&&!program&&m_stack[seg]!=0xffff &&
		u16(address)>=m_stack[seg] && u16(address)<unsigned(m_stack[seg])+256)
		fault(address,size,reason|64);
	return physical;
}

u16 z8001unix_state::memory_r(offs_t off, u16 mask, bool program, bool first)
{
	u32 address=off<<1;
	if (machine().side_effects_disabled()) {
		if (program) address=(u32(m_imap[(address>>16)&127])<<16)|(address&65535);
		u16 frame=m_pages[(address>>11)&4095];
		if (frame==0xffff || frame>=4096) return 0xffff;
		u32 physical=(u32(frame)<<11)|(address&2047);
		if (physical+2>m_ram->size() && physical<0x7f0000) return 0xffff;
		return physical<2048 ? (m_rom[physical]<<8)|m_rom[physical+1] : physical_r(physical);
	}
	if (first && m_cpu->is_ifetch1()) m_first=address;
	if (program) address=(u32(m_imap[(address>>16)&127])<<16)|(address&65535);
	unsigned size=(mask==0xffff)?2:1;
	u32 p=translate(address+(mask==0x00ff),size,false,program);
	if (p==~0U) return 0;
	p&=~1U;
	return p<2048 ? (m_rom[p]<<8)|m_rom[p+1] : physical_r(p);
}
void z8001unix_state::memory_w(offs_t off, u16 data, u16 mask, bool program)
{
	u32 address=off<<1;
	if (program) address=(u32(m_imap[(address>>16)&127])<<16)|(address&65535);
	u32 p=translate(address+(mask==0x00ff),mask==0xffff?2:1,true,program);
	if (p==~0U || p<2048) return;
	u8 *b=physical(p&~1U);
	if(mask&0xff00) b[0]=data>>8;
	if(mask&0x00ff) b[1]=data;
}
void z8001unix_state::upage(u16 frame)
{
	m_pages[62]=m_pages[4094]=frame;
	m_pages[63]=m_pages[4095]=frame+1;
}
void z8001unix_state::update_vi()
{
	m_cpu->set_input_line(z8001_device::VI_LINE, (m_disk_irq||m_rxin!=m_rxout)?ASSERT_LINE:CLEAR_LINE);
}
void z8001unix_state::key(u8 data)
{
	if (u16(m_rxin-m_rxout)<256) m_rx[(m_rxin++)&255]=data;
	update_vi();
}
TIMER_DEVICE_CALLBACK_MEMBER(z8001unix_state::tick)
{
	// A one-bit pending latch, held until interrupt acknowledgement.
	m_cpu->set_input_line(z8001_device::NVI_LINE, ASSERT_LINE);
}
u8 z8001unix_state::byte_r(u16 port)
{
	switch(port) {
	case 0xb0: return m_pages[62];
	case 0xf0: { u8 v=0; if(m_rxin!=m_rxout) v=m_rx[(m_rxout++)&255]; update_vi(); return v; }
	case 0xf2: return 1|(m_rxin!=m_rxout?2:0);
	case 0x1f1: return m_error;
	case 0x1f7: m_disk_irq=false; update_vi(); return m_status;
	default: return 0xde;
	}
}
void z8001unix_state::byte_w(u16 port,u8 value)
{
	switch(port) {
	case 0xf0: m_terminal->write(value & 0x7f); break;
	case 0x1f2: m_sc=value; break;
	case 0x1f3: m_sn=value; break;
	case 0x1f4: m_cl=value; break;
	case 0x1f5: m_ch=value; break;
	case 0x1f6: m_dh=value; break;
	case 0x1f7: command(value); break;
	}
}
void z8001unix_state::command(u8 value)
{
	u32 const lba=m_sn|(u32(m_cl)<<8)|(u32(m_ch)<<16)|(u32(m_dh&15)<<24);
	bool const swap=BIT(m_dh,4);
	m_index=0; m_reading=m_writing=false; m_error=0;
	bool ok=m_sc==1 && (value==0x20||value==0x30);
	if(swap) ok=ok && lba<SWAP_BYTES/512;
	else ok=ok && m_disk->exists() && lba<u64(m_disk->get_info().cylinders)*m_disk->get_info().heads*m_disk->get_info().sectors;
	if(ok && value==0x20) {
		if(swap) std::copy_n(&m_swap[lba*512],512,m_sector);
		else ok=m_disk->read(lba,m_sector);
	}
	if(!ok) { m_status=0x41; m_error=0x10; m_disk_irq=true; }
	else { m_status=0x48; m_reading=value==0x20; m_writing=value==0x30; m_disk_irq=m_reading; }
	update_vi();
}
void z8001unix_state::complete_write()
{
	u32 const lba=m_sn|(u32(m_cl)<<8)|(u32(m_ch)<<16)|(u32(m_dh&15)<<24);
	bool ok=true;
	if(BIT(m_dh,4)) std::copy_n(m_sector,512,&m_swap[lba*512]);
	else ok=m_disk->write(lba,m_sector);
	m_writing=false; m_status=ok?0x40:0x41; m_error=ok?0:0x10;
	m_disk_irq=true; update_vi();
}
u16 z8001unix_state::io_r(offs_t off,u16 mask)
{
	u16 const port=off<<1;
	if(mask!=0xffff) return mask==0xff00?u16(byte_r(port))<<8:byte_r(port+1);
	switch(port) {
	case 0xb2: return SWAP_BYTES/512;
	case 0xb6: return m_disk->exists() ? std::min<u64>(u64(m_disk->get_info().cylinders)*m_disk->get_info().heads*m_disk->get_info().sectors,65535) : 0;
	case 0xba: return m_ram->size()/2048;
	case 0xc0: return m_fault;
	case 0xc2: return m_fseg;
	case 0xc4: return m_flow;
	case 0xc6: return m_fhigh;
	case 0xc8: return (m_fpc>>16)&127;
	case 0xca: return m_fpc&65535;
	case 0x1f0:
		if(m_reading&&m_index<512) {
			u16 v=(m_sector[m_index]<<8)|m_sector[m_index+1]; m_index+=2;
			if(m_index==512) { m_reading=false; m_status=0x40; }
			return v;
		}
	}
	return 0xdead;
}
void z8001unix_state::io_w(offs_t off,u16 data,u16 mask)
{
	u16 const port=off<<1;
	if(mask!=0xffff) { byte_w(port+(mask==0xff),mask==0xff00?data>>8:data); return; }
	switch(port) {
	case 0xb0: upage(data); break;
	case 0xb4: m_pages[60]=data; m_pages[61]=data+1; break;
	case 0xb8: m_imap[(data>>8)&127]=data&127; break;
	case 0xbc: m_select=data&4095; break;
	case 0xbe: m_pages[m_select]=data==0xffff?data:data&~(RO|SYS); m_attr[m_select]=data&(RO|SYS); break;
	case 0xcc: m_fault=0; break;
	case 0xd0: m_stacksel=data&127; break;
	case 0xd2: m_stack[m_stacksel]=data; break;
	case 0x1f0:
		if(m_writing&&m_index<512) { m_sector[m_index++]=data>>8; m_sector[m_index++]=data; if(m_index==512) complete_write(); }
		break;
	}
}
void z8001unix_state::program_map(address_map &map)
{
	map(0,0x7fffff).lrw16(NAME([this](offs_t o,u16 m){return memory_r(o,m,true,false);}), NAME([this](offs_t o,u16 d,u16 m){memory_w(o,d,m,true);}));
}
void z8001unix_state::opcode_map(address_map &map)
{
	map(0,0x7fffff).lr16(NAME([this](offs_t o,u16 m){return memory_r(o,m,true,true);}));
}
void z8001unix_state::data_map(address_map &map)
{
	map(0,0x7fffff).lrw16(NAME([this](offs_t o,u16 m){return memory_r(o,m,false,false);}), NAME([this](offs_t o,u16 d,u16 m){memory_w(o,d,m,false);}));
}
void z8001unix_state::io_map(address_map &map)
{
	map(0,0xffff).rw(FUNC(z8001unix_state::io_r),FUNC(z8001unix_state::io_w));
}
void z8001unix_state::machine_start()
{
	m_epu=make_unique_clear<u8[]>(65536);
	m_swap=make_unique_clear<u8[]>(SWAP_BYTES);
}
void z8001unix_state::machine_reset()
{
	std::fill_n(m_ram->pointer(),m_ram->size(),0);
	std::fill_n(m_epu.get(),65536,0);
	std::fill_n(m_swap.get(),SWAP_BYTES,0);
	for(unsigned s=0;s<128;s++) {
		m_imap[s]=s; m_stack[s]=0xffff;
		for(unsigned p=0;p<32;p++) { unsigned i=s*32+p; m_pages[i]=(s<=1||s==127)?i:0xffff; m_attr[i]=(s<=1||s==127)?SYS:0; }
	}
	upage(62);
	m_select=m_stacksel=m_fault=m_fseg=m_flow=m_fhigh=0;
	m_first=m_fpc=0; m_normal=m_disk_irq=false; m_rxin=m_rxout=0;
	m_sc=m_sn=m_cl=m_ch=m_dh=m_error=0; m_status=0x40; m_index=0;
	m_reading=m_writing=false;
}
void z8001unix_state::z8001unix(machine_config &config)
{
	Z8001(config,m_cpu,4'000'000);
	m_cpu->set_addrmap(AS_PROGRAM,&z8001unix_state::program_map);
	m_cpu->set_addrmap(AS_OPCODES,&z8001unix_state::opcode_map);
	m_cpu->set_addrmap(AS_DATA,&z8001unix_state::data_map);
	m_cpu->set_addrmap(z8001_device::AS_STACK,&z8001unix_state::data_map);
	m_cpu->set_addrmap(AS_IO,&z8001unix_state::io_map);
	m_cpu->ns().set([this](int state){m_normal=bool(state);});
	m_cpu->nviack().set(FUNC(z8001unix_state::nvi_ack));
	m_cpu->viack().set(FUNC(z8001unix_state::vi_ack));
	m_cpu->segtack().set(FUNC(z8001unix_state::seg_ack));
	GENERIC_TERMINAL(config,m_terminal,0);
	m_terminal->set_keyboard_callback(FUNC(z8001unix_state::key));
	HARDDISK(config,m_disk,0);
	RAM(config,m_ram).set_default_size("8M").set_extra_options("320K,322K,384K,1M,2M,4M");
	TIMER(config,"clock").configure_periodic(FUNC(z8001unix_state::tick),attotime::from_hz(60));
}
ROM_START(z8001unix)
	ROM_REGION(0x800,"boot",0)
	ROM_LOAD("unix.rom",0,0x800,NO_DUMP)
ROM_END
} // anonymous namespace
COMP(2026,z8001unix,0,0,z8001unix,0,z8001unix_state,empty_init,"Homebrew","Z8001-unix",MACHINE_NO_SOUND_HW)
