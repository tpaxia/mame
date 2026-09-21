// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
#include "emu.h"
#include "difo.h"
DEFINE_DEVICE_TYPE(P6066_DIFO,p6066_difo_device,"p6066_difo","Olivetti P6066 DIFO1/DIFO2 HDU controller")
p6066_difo_device::p6066_difo_device(const machine_config &mconfig,const char *tag,device_t *owner,u32 clock)
	: device_t(mconfig,P6066_DIFO,tag,owner,clock), device_p6066_card_interface(mconfig,*this),m_drives(*this,"hdu%u",0U) { }
void p6066_difo_device::device_add_mconfig(machine_config &config)
{
	for(unsigned unit=0;unit<2;++unit) P6066_HDU(config,m_drives[unit]);
}
void p6066_difo_device::device_start()
{
	m_timer=timer_alloc(FUNC(p6066_difo_device::tick),this);
	m_watchdog=timer_alloc(FUNC(p6066_difo_device::timeout),this);
	save_item(NAME(m_dma_granted));save_item(NAME(m_finishing));save_item(NAME(m_dummy));save_item(NAME(m_reset_dma));
	save_item(NAME(m_format));save_item(NAME(m_preamble));save_item(NAME(m_key_count));
	save_item(NAME(m_compare));save_item(NAME(m_matched));
	save_item(NAME(m_buffer));save_item(NAME(m_byte));save_item(NAME(m_dma_byte));save_item(NAME(m_dma_count));
	save_item(NAME(m_slot));save_item(NAME(m_revolutions));save_item(NAME(m_phase));save_item(NAME(m_active_unit));save_item(NAME(m_data_crc));
	save_item(NAME(m_state.name));save_item(NAME(m_state.unit));save_item(NAME(m_state.cylinder));
	save_item(NAME(m_state.sector));save_item(NAME(m_state.length));save_item(NAME(m_state.scan));
	save_item(NAME(m_state.key));save_item(NAME(m_state.execute));save_item(NAME(m_state.word));
	save_item(NAME(m_state.odd));save_item(NAME(m_state.home));save_item(NAME(m_state.selected));
	save_item(NAME(m_state.busy));save_item(NAME(m_state.status));save_item(NAME(m_state.keys));
	save_item(NAME(m_state.response));save_item(NAME(m_state.completion));save_item(NAME(m_state.request));
	save_item(NAME(m_state.servicing));save_item(NAME(m_dma_pending));
	save_item(NAME(m_cycle.address));save_item(NAME(m_cycle.data));
	save_item(NAME(m_cycle.mask));save_item(NAME(m_cycle.write));
}
void p6066_difo_device::device_reset() { m_state.reset();m_dma_pending=m_dma_granted=m_finishing=m_reset_dma=false;m_phase=0;m_timer->adjust(attotime::never);m_watchdog->adjust(attotime::never); }
void p6066_difo_device::controller_reset(bool asserted)
{
	if(!asserted) return;
	// ECOR resets the controller, not the mounted medium. A started RODMA
	// cycle still has an owner until DONE; discard its controller-side result.
	if(m_state.busy && m_state.operation_bits()==p6066_difo_state::WRITE && (m_phase==3||m_phase==4))
		m_drives[m_active_unit]->record_data(m_state.head(),m_slot,m_buffer,false);
	m_timer->adjust(attotime::never);m_watchdog->adjust(attotime::never);
	if(m_dma_pending && !m_dma_granted) { m_bus->dma_request(*this,false);m_dma_pending=false; }
	m_reset_dma=m_dma_pending;m_state.reset();m_state.busy=m_reset_dma;
	m_phase=0;m_finishing=false;
}
void p6066_difo_device::command_word(unsigned level,u16 data,u16 mask)
{
	if(!m_state.selected && !m_state.servicing) return;
	if(mask!=0xffff) fatalerror("DIFO requires selector ECD9-B and payload ECD8: byte command is incomplete");
	if(m_state.busy || m_state.completion) fatalerror("DIFO command overlap/reset requires remaining latch evidence");
	logerror("DIFO command level=%u data=%04X selected=%u service=%u\n",level,data,m_state.selected,m_state.servicing);
	if(m_state.latch(data)) execute();
}
void p6066_difo_device::output_data_masked(unsigned level,u16 data,u16 mask)
{
	// DIFO PDF26-28: parameter latches require ECOC + ECOT. A plain
	// data output only strobes ECOT; ECD is not a command on this path.
	// It can advance an owned completion response, just like a read strobe.
	m_state.strobe();
}
void p6066_difo_device::irq_ack(unsigned source)
{
	if(source!=2 || !m_state.request) fatalerror("DIFO invalid IRQ acknowledgement");
	m_state.acknowledge();
}
void p6066_difo_device::irq_end(unsigned level)
{
	// COM0 does not replace the documented ECOT/ECM3 response sequence.
	if(level==3 && m_state.servicing) logerror("DIFO IRQ returned before consuming completion response\n");
}

// Functional mechanical model: 40 ms/revolution (G mean latency 20 ms),
// 320 ns/bit from DIFO PDF52. Seek interpolates 4..164 ms; not an actuator model.
// Phases: 0 idle, 1 seek, 2 ID search, 3 data, 4 trailer, 5 preamble,
// 6 format slot, 7 closing index. All RAM traffic still uses the shared bus.
void p6066_difo_device::execute()
{
	if(!m_state.valid_operation()) fatalerror("DIFO multiple execute bits %02X require decoder evidence",m_state.execute);
	m_state.status=0;m_state.busy=true;m_state.keys=0xff;
	m_active_unit=m_state.unit;m_finishing=false;
	if(!drive().ready()) { finish(0x20);return; }
	const unsigned op=m_state.operation_bits();
	if(m_state.cylinder>=202 || (op && m_state.sector>=192))
		fatalerror("DIFO out-of-range address requires boundary evidence");
	m_slot=0;m_revolutions=0;m_byte=0;
	const unsigned target=m_state.home?0:m_state.cylinder;
	const unsigned current=drive().cylinder();
	if(current!=target)
	{
		const unsigned distance=current>target?current-target:target-current;
		m_phase=1;m_timer->adjust(attotime::from_usec(4000+800*(distance-1)));
	}
	else begin_media();
}
void p6066_difo_device::begin_media()
{
	if(!m_state.operation_bits()) { m_state.busy=false;m_phase=0;return; }
	if(m_state.operation_bits()==p6066_difo_state::FORMAT)
	{
		m_phase=6;m_timer->adjust(attotime::from_msec(40)); // next index
	}
	else
	{
		m_phase=2;m_timer->adjust(attotime::from_msec(40)/49);
		m_watchdog->adjust(attotime::from_msec(320)); // LEGIO, command-wide
	}
}
void p6066_difo_device::finish(u8 errors)
{
	logerror("DIFO finish op=%02X CY=%u ST=%u remaining=%u errors=%02X DMA=%u/%u\n",m_state.execute,m_state.cylinder,m_state.sector,m_state.length,errors,m_dma_pending,m_dma_granted);
	m_state.status|=errors;
	if(errors && m_state.operation_bits()==p6066_difo_state::WRITE && (m_phase==3||m_phase==4))
		m_drives[m_active_unit]->record_data(m_state.head(),m_slot,m_buffer,false);
	m_timer->adjust(attotime::never);m_watchdog->adjust(attotime::never);
	// A granted RAM cycle belongs to RODMA until DONE; don't let a new command
	// steal its response. Ungranted requests can be withdrawn immediately.
	if(m_dma_pending && m_dma_granted) { m_finishing=true;return; }
	if(m_dma_pending) { m_bus->dma_request(*this,false);m_dma_pending=false; }
	m_finishing=false;m_phase=0;m_state.finish();
}
TIMER_CALLBACK_MEMBER(p6066_difo_device::timeout) { finish(0x80); }
void p6066_difo_device::next_sector()
{
	if(m_state.operation_bits()==p6066_difo_state::SCAN)
	{
		m_state.word-=128; // PDF57: rewind template, preserve CARL
		if(m_matched) { finish();return; }
		m_state.sector+=m_state.scan&15; // raw PAS, not software P+1
	}
	else ++m_state.sector;
	// P6066 generator064 PUCE 0EA1-0EA2 and master068 FW +CEC6
	// both send N-1. This conflicts with PDF38/47's prose saying to
	// test zero after decrement; sample terminal state before the clock.
	const bool last = m_state.length == 0;
	--m_state.length;
	if(last) { finish();return; }
	// No invented cylinder carry. Out-of-range ST cannot match a normal ID;
	// continue the search until the command-wide position watchdog fires.
	m_phase=2;m_timer->adjust(attotime::from_usec(100));
}
void p6066_difo_device::request_data(bool dummy)
{
	if(m_dma_pending)
	{
		// No FIFO: late DMA damages CRC (PDF66). Keep the already-owned cycle
		// and terminate with an error after it drains, not with a false success.
		finish(0x40);return;
	}
	const unsigned address=m_state.byte_address();
	const unsigned size=m_state.operation_bits()==p6066_difo_state::FORMAT?20:256;
	m_dummy=dummy;m_dma_byte=m_byte;
	m_dma_count=dummy?2:std::min<unsigned>(2-(address&1),size-m_byte);
	const bool to_memory=m_state.operation_bits()==p6066_difo_state::READ;
	m_cycle.address=m_state.word;m_cycle.write=to_memory;
	// Read cycles always request the entire RAM word; lane extraction is in
	// DIFO. The byte masks describe write-to-RAM cycles only.
	m_cycle.mask=(!to_memory||m_dma_count==2)?0xffff:(address&1)?0x00ff:0xff00;
	m_cycle.data=0;
	if(to_memory) m_cycle.data=m_dma_count==2 ? (u16(m_buffer[m_byte])<<8)|m_buffer[m_byte+1]
		: u16(m_buffer[m_byte])<<((address&1)?0:8);
	m_dma_pending=true;m_dma_granted=false;m_bus->dma_request(*this,true);
	if(!dummy)
	{
		m_byte+=m_dma_count;
		const unsigned next=(address+m_dma_count)&0x1ffff;
		m_state.word=next>>1;m_state.odd=next&1;
	}
}
void p6066_difo_device::compare_byte(u8 value,unsigned index)
{
	if(m_matched) return;
	if(!m_compare && value!=0xff && value!=m_buffer[index])
		m_compare=value>m_buffer[index]?1:-1; // TOV1=RAM>disk, TOV2=RAM<disk
	// PDF56: load SHL, increment per byte, terminal carry of both nibbles.
	// Raw seed is -key_length modulo 256; software CCF conversion is separate.
	if(++m_key_count) return;
	++m_state.keys;
	m_matched=!m_compare || (m_compare>0 && (m_state.scan&0x10)) || (m_compare<0 && (m_state.scan&0x20));
	if(m_matched) m_state.status|=0x08|(m_compare?0x04:0);
	else { m_compare=0;m_key_count=m_state.key; }
}
TIMER_CALLBACK_MEMBER(p6066_difo_device::tick)
{
	if(!m_phase || m_finishing) return;
	if(!m_drives[m_active_unit]->ready()) { finish(0x20);return; }
	const unsigned op=m_state.operation_bits();
	if(m_phase==1)
	{
		m_drives[m_active_unit]->seek(m_state.home?0:m_state.cylinder);
		begin_media();return;
	}
	if(m_phase==7) { finish();return; }
	if(m_phase==6)
	{
		m_byte=0;m_format.fill(0xff);m_preamble=7;m_phase=5;
		m_timer->adjust(attotime::from_nsec(5120));return;
	}
	if(m_phase==2)
	{
		if(m_slot==49) { m_slot=0;++m_revolutions; }
		if(m_state.head()>=4) { m_timer->adjust(attotime::from_msec(40)/49);return; }
		const auto record=m_drives[m_active_unit]->sector(m_state.head(),m_slot);
		if(!record.id_present||!record.id_crc_valid||record.cylinder!=m_state.cylinder||record.sector!=m_state.sector
			|| (!record.data_present && op!=p6066_difo_state::WRITE))
		{
			++m_slot;m_timer->adjust(attotime::from_msec(40)/49);return;
		}
		m_buffer=record.data;m_data_crc=record.data_crc_valid;m_byte=0;
		m_matched=false;m_compare=0;m_key_count=m_state.key;m_state.keys=0xff;
		if(op==p6066_difo_state::WRITE)
		{
			m_buffer.fill(0xff);m_data_crc=true;m_preamble=4;m_phase=5;
		}
		else
		{
			m_phase=3;
			if(op==p6066_difo_state::SCAN) request_data(); // before data field, PDF54
		}
		m_timer->adjust(attotime::from_nsec(5120));return;
	}
	if(m_phase==5)
	{
		if(m_preamble) { --m_preamble;request_data(true); }
		else m_phase=3;
		if(!m_phase||m_finishing) return;
		m_timer->adjust(attotime::from_nsec(5120));return;
	}
	if(m_phase==3)
	{
		if(op==p6066_difo_state::VERIFY) m_byte+=2;
		else request_data();
		if(!m_phase||m_finishing) return;
		if(m_byte>=(op==p6066_difo_state::FORMAT?20:256)) m_phase=4;
		m_timer->adjust(attotime::from_nsec(5120));return;
	}
	if(m_phase==4)
	{
		if(m_dma_pending) { finish(0x40);return; }
		if(op==p6066_difo_state::FORMAT)
		{
			if(!m_drives[m_active_unit]->record_format(m_state.head(),m_slot,m_format,true)) { finish(0x20);return; }
			++m_slot;m_phase=m_slot==49?7:6;
			m_timer->adjust(attotime::from_nsec(719046)); // remaining slot -> next pulse/index
			return;
		}
		if(op==p6066_difo_state::WRITE)
		{
			if(!m_drives[m_active_unit]->record_data(m_state.head(),m_slot,m_buffer,true)) { finish(0x20);return; }
		}
		else if(!m_data_crc) m_state.status|=0x40;
		++m_slot;next_sector();
	}
}
p6066_dma_cycle p6066_difo_device::dma_grant()
{
	if(!m_dma_pending) fatalerror("DIFO unsolicited DMA grant");
	m_bus->dma_request(*this,false);m_dma_granted=true;
	return m_cycle;
}
void p6066_difo_device::dma_done(u16 data,bool invalid)
{
	if(!m_dma_pending) fatalerror("DIFO unsolicited DMA completion");
	m_dma_pending=m_dma_granted=false;
	if(m_reset_dma) { m_reset_dma=false;m_state.busy=false;return; }
	if(invalid) { finish(0x22);return; } // FUME + FUNE, no direct CPU invalid trap
	if(m_finishing) { finish();return; }
	if(m_dummy||m_cycle.write) return;
	const unsigned op=m_state.operation_bits();
	// Odd first byte is the low RAM lane; a final singleton is the high lane.
	for(unsigned i=0;i<m_dma_count;++i)
	{
		const unsigned lane=(m_dma_count==1 && m_dma_byte==0)?0:8-8*i;
		const u8 value=data>>lane;
		if(op==p6066_difo_state::FORMAT) m_format[m_dma_byte+i]=value;
		else if(op==p6066_difo_state::WRITE) m_buffer[m_dma_byte+i]=value;
		else if(op==p6066_difo_state::SCAN) compare_byte(value,m_dma_byte+i);
	}
}
