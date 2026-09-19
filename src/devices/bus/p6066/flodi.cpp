// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
#include "emu.h"
#include "flodi.h"
#include "flodi_fm.h"
#include "flodi_rotation.h"
#include <cstdlib>
DEFINE_DEVICE_TYPE(P6066_FLODI,p6066_flodi_device,"p6066_flodi","Olivetti P6066 FLODI floppy controller")
p6066_flodi_device::p6066_flodi_device(const machine_config &mconfig,const char *tag,device_t *owner,u32 clock)
	: device_t(mconfig,P6066_FLODI,tag,owner,clock),device_p6066_card_interface(mconfig,*this),m_sector_output(*this,"sectors_read"),m_byte_output(*this,"bytes_read"),m_write_output(*this,"write_gate"),m_erase_output(*this,"erase_gate"),m_local0_output(*this,"local_1"),m_local1_output(*this,"local_2"),m_changer_output(*this,"changer_request"),m_drives(*this,"%u",0U) { }
static void fdu_drives(device_slot_interface &device) { device.option_add("8sssd",FLOPPY_8_SSSD); }
void p6066_flodi_device::device_add_mconfig(machine_config &config)
{
	FLOPPY_CONNECTOR(config,"0",fdu_drives,"8sssd",floppy_image_device::default_fm_floppy_formats);
	FLOPPY_CONNECTOR(config,"1",fdu_drives,"8sssd",floppy_image_device::default_fm_floppy_formats);
}
floppy_image_device *p6066_flodi_device::drive() const { return m_drives[m_selected]->get_device(); }
void p6066_flodi_device::device_start()
{
	m_trace_enabled=std::getenv("P6066_FLODI_TRACE")!=nullptr;
	for (auto &connector : m_drives)
		if (auto *f=connector->get_device())
		{
			f->setup_index_pulse_cb(floppy_image_device::index_pulse_cb(&p6066_flodi_device::index_changed,this));
			f->setup_load_cb(floppy_image_device::load_cb(&p6066_flodi_device::media_loaded,this));
			f->setup_unload_cb(floppy_image_device::unload_cb(&p6066_flodi_device::media_unloaded,this));
		}
	save_item(NAME(m_track_epoch)); save_item(NAME(m_byte_time)); save_item(NAME(m_finish_byte)); save_item(NAME(m_crc_start)); save_item(NAME(m_payload_start));
	m_byte_timer=timer_alloc(FUNC(p6066_flodi_device::byte_tick),this);
	m_erase_timer=timer_alloc(FUNC(p6066_flodi_device::erase_tick),this);
	m_write_timer=timer_alloc(FUNC(p6066_flodi_device::write_gate_tick),this);
	save_item(NAME(m_write_end_time)); save_item(NAME(m_write_close_pending)); save_item(NAME(m_write_continue));
	save_item(NAME(m_erase_active)); save_item(NAME(m_erase_target));
	save_item(NAME(m_bits)); save_item(NAME(m_bit_count)); save_item(NAME(m_cursor)); save_item(NAME(m_id_pos)); save_item(NAME(m_data_pos));
	save_item(NAME(m_format_wait)); save_item(NAME(m_format_active)); save_item(NAME(m_format_stream));
	save_item(NAME(m_format_area)); save_item(NAME(m_format_nrem));
	save_item(NAME(m_format_fsc)); save_item(NAME(m_format_ser));
	save_item(NAME(m_writing)); save_item(NAME(m_write_preamble)); save_item(NAME(m_write_crc));
	save_item(NAME(m_scan.mod)); save_item(NAME(m_scan.different)); save_item(NAME(m_scan.found));
	save_item(NAME(m_input)); save_item(NAME(m_mask_pipe)); save_item(NAME(m_header)); save_item(NAME(m_length)); save_item(NAME(m_byte)); save_item(NAME(m_id_byte));
	save_item(NAME(m_reading)); save_item(NAME(m_id_phase)); save_item(NAME(m_data_irq)); save_item(NAME(m_mismatch));
	save_item(NAME(m_crc_gate)); save_item(NAME(m_last_sector)); save_item(NAME(m_id_crc)); save_item(NAME(m_data_crc));
	save_item(NAME(m_data_mark)); save_item(NAME(m_expected_mark)); save_item(NAME(m_expected_clock)); save_item(NAME(m_end_status));
	save_item(NAME(m_id_time)); save_item(NAME(m_next_index)); save_item(NAME(m_sectors_read)); save_item(NAME(m_bytes_read));
	m_timer=timer_alloc(FUNC(p6066_flodi_device::mechanical_tick),this);
	save_item(NAME(m_changer_door)); save_item(NAME(m_changer_busy)); save_item(NAME(m_changer_ack)); save_item(NAME(m_inop));
	save_item(NAME(m_local)); save_item(NAME(m_motion)); save_item(NAME(m_direction)); save_item(NAME(m_settle)); save_item(NAME(m_index));
	save_item(NAME(m_latches.command)); save_item(NAME(m_latches.mas)); save_item(NAME(m_latches.num));
	save_item(NAME(m_latches.prico)); save_item(NAME(m_latches.cote));
	save_item(NAME(m_selected));
	save_item(NAME(m_irq.sele)); save_item(NAME(m_irq.coma)); save_item(NAME(m_irq.command_armed));
	save_item(NAME(m_irq.fogo)); save_item(NAME(m_irq.fine)); save_item(NAME(m_irq.rili));
	save_item(NAME(m_irq.fugo)); save_item(NAME(m_irq.rifi)); save_item(NAME(m_irq.livi));
	save_item(NAME(m_active_type)); save_item(NAME(m_reset));
}
// Opt-in diagnostic window around the first duplicated sector in boot066.
// Pure observation: no emulated timing, latch or interrupt changes.
void p6066_flodi_device::trace_event(const char *event, unsigned value)
{
	if (!m_trace_enabled || m_sectors_read<367 || m_sectors_read>371) return;
	logerror("FLODI_TRACE ns=%lld event=%s value=%X completed=%u drive=%u chr=%u/%u/%u id=%u byte=%u reading=%u index=%u cote=%u active=%02X requests=%02X sele=%u coma=%u fogo=%u fugo=%u fine=%u rifi=%u rili=%u livi=%u\n",
		(long long)machine().time().as_ticks(1000000000),event,value,m_sectors_read,m_selected+1,
		m_header[1],m_header[2],m_header[3],m_id_phase,m_byte,m_reading,m_index,m_latches.cote,
		m_active_type,m_irq.requests(),m_irq.sele,m_irq.coma,m_irq.fogo,m_irq.fugo,
		m_irq.fine,m_irq.rifi,m_irq.rili,m_irq.livi);
}
void p6066_flodi_device::device_reset() { stop_read(); m_sectors_read=m_bytes_read=0; m_sector_output=0; m_byte_output=0; m_selected=0; m_reset=true; m_irq={}; m_active_type=0; m_index=false; m_latches={}; m_scan={}; m_inop=false; m_end_status=0; m_erase_active=m_erase_target=false; m_erase_output=0; m_erase_timer->adjust(attotime::never); m_local0_output=m_local1_output=m_changer_output=0; m_local[0]=m_local[1]=false; m_motion=false; m_settle=0; m_timer->adjust(attotime::never); }
void p6066_flodi_device::controller_reset(bool asserted)
{
	m_reset=asserted;
	if (asserted) { stop_read(); m_local[0]=m_local[1]=false; m_motion=false; m_settle=0; m_timer->adjust(attotime::never); m_irq={}; m_active_type=0; m_index=false; m_latches={}; m_scan={}; m_inop=false; m_end_status=0; m_erase_active=m_erase_target=false; m_erase_output=0; m_erase_timer->adjust(attotime::never); m_local0_output=m_local1_output=m_changer_output=0; }
}
void p6066_flodi_device::select(u8 name)
{
	// Manual table 2 lists active-low bus values 9F/1F, hence logical 60/E0.
	if (m_reset || (name!=0x60 && name!=0xe0)) return;
	// SEDI is held while GOCO is asserted (K02 A5). SELE still responds.
	if (!m_latches.busy(m_changer_busy)) m_selected=BIT(name,7);
	// ESE includes the selection strobe that clocks SELE (printed p.27).
	m_irq.sele=true;
	if (auto *f=drive()) { f->ss_w(0); f->mon_w(0); }
	logerror("FLODI select %02X, drive %u\n",name,m_selected+1);
}
void p6066_flodi_device::media_loaded(floppy_image_device *floppy)
{
	// Host image insertion represents the door-closing MADI1 pulse.
	// Both LOC flip-flops share that reset/clock line (K02 M3/P3).
	// It is not a fabricated acknowledgement/completion of a feeder cycle.
	if (!m_reset) operator_reset();
}
void p6066_flodi_device::media_unloaded(floppy_image_device *floppy)
{
	if (floppy==drive()) m_bit_count=0;
}
void p6066_flodi_device::changer_door_w(bool closed) { m_changer_door=closed; m_changer_output=changer_request(); }
void p6066_flodi_device::changer_busy_w(bool busy) { m_changer_busy=busy; }
void p6066_flodi_device::changer_ack_w(bool asserted)
{
	// MADIA pulses ECMAN, clearing MACON and setting INOP. CICO is a
	// separate input and holds GOCO until the physical cycle completes.
	if (asserted && !m_changer_ack && !m_reset)
	{
		operator_reset();
	}
	m_changer_ack=asserted;
}
void p6066_flodi_device::operator_reset()
{
	const u8 previous=m_latches.command;
	m_latches.command=0; m_latches.cote=true;
	latch_command(previous); m_inop=true;
	m_local[0]=m_local[1]=false;
	m_local0_output=m_local1_output=0;
}
void p6066_flodi_device::interrupt_sync(u8 mask)
{
	if (m_reset) return;
	trace_event("ECM-before",mask);
	m_irq.synchronize(mask, m_active_type && !(m_active_type & 0x0c));
	if (mask & 8) m_latches.ecm3();
	trace_event("ECM-after",mask);
}
void p6066_flodi_device::irq_ack(unsigned source)
{
	trace_event("ECC-before",source);
	if (source==0 && (m_irq.requests()&1) && !m_data_irq)
	{
		m_irq.byte_ack(); m_data_irq=true; return;
	}
	if ((source!=2 && source!=3) || !(m_irq.requests()&(1U<<source)) || m_active_type)
		fatalerror("FLODI unsupported interrupt acknowledgement");
	// ECC establishes ownership. ECOT clears FOGO/FINE; ECM updates
	// FUGO/RIFI. Neither of those operations is an ECC side effect.
	m_active_type=m_irq.type(source==3);
	trace_event("ECC-after",source);
}
void p6066_flodi_device::irq_end(unsigned level)
{
	if (level==1)
	{
		m_data_irq=false;
		if (!m_reading) return;
		if (m_format_wait) return; // The single INCO preload service.
		if (m_format_active)
		{
			if (int(m_byte)==m_finish_byte)
			{
				// COCI falling resets AREA/NREM. USETO also stops SCACO,
				// but ORRE remains on until index (pp.44-47, figs.25/26).
				m_format_area=m_format_nrem=false;
				m_finish_byte=m_crc_start=-1;
				if (m_last_sector) m_format_stream=false;
			}
			++m_byte;
			schedule_byte(m_byte_time+attotime::from_usec(32));
			return;
		}
		if (!m_id_phase) advance_data();
		if (m_id_phase)
		{
			if (m_finish_byte<0 || int(m_id_byte)!=m_finish_byte)
			{
				if (++m_id_byte>8) fatalerror("FLODI ID CRC control sequence not completed");
				schedule_byte(m_byte_time+attotime::from_usec(32));
			}
			else if (m_mismatch || !m_id_crc || (m_data_pos==m_bit_count && (m_latches.command&7)!=1)) next_id();
			else
			{
				logerror("FLODI matched ID %02X %02X %02X %02X\n",m_header[1],m_header[2],m_header[3],m_header[4]);
				m_id_phase=false; m_byte=0; m_finish_byte=-1; m_crc_start=-1; m_payload_start=true;
				if ((m_latches.command&7)!=1)
					// Documented BKB encodings are FB (normal) and F8
					// (deleted), pp.16-17. PAR's serial-stage bit numbers
					// must not be applied as bit indexes of a decoded byte.
					m_end_status|=(!BIT(m_data_mark,1) ? 0x80 : 0)
						| ((m_data_mark!=0xfb && m_data_mark!=0xf8) ? 0x40 : 0);
				m_scan.begin(m_latches.mas); m_latches.num=0;
				m_mask_pipe[0]=m_mask_pipe[1]=0xff;
				const unsigned cells=(m_data_pos+m_bit_count-m_id_pos)%m_bit_count;
				if ((m_latches.command&7)==1)
				{
					// ORRE starts in separator byte 11; bytes 12..17 are
					// the six zero synchronizing bytes (pp.37/65, fig.18).
					m_write_preamble=7; m_write_crc=0xffff;
					schedule_byte(m_id_time+attotime::from_usec(17*32));
				}
				else
				{
					// Read enables SCACO via NREMO; verify/scan enable it
					// already at ADMA (pp.35,39-41, figs.17/20/21).
					const unsigned delay=(m_latches.command&7) ? 0 : 16;
					schedule_byte(m_id_time+attotime::from_nsec(u64(cells+delay)*2000));
				}
			}
		}
		else if (m_finish_byte<0 || int(m_byte)!=m_finish_byte)
		{
			++m_byte;
			schedule_byte(m_byte_time+attotime::from_usec(32));
		}
		else
		{
			finish_sector();
		}
		return;
	}
	if (level!=3) return;
	m_active_type=0;
	// ECM has already been broadcast by the CPU; only ownership ends here.
}
void p6066_flodi_device::latch_command(u8 previous)
{
	const u8 data=m_latches.command;
	m_direction=BIT(data,3);
	const bool motion=BIT(data,4), cate=BIT(data,7);
	if (!cate)
	{
		// K03 P8/Q9 and T8/U8: CATE clear resets ERRO and ERAS.
		m_end_status=0; m_scan.found=false;
		m_settle=false;
		stop_read();
	}
	if (motion && !m_motion)
		// FLODISC figs.14-18: first pair 1.5+5 ms, subsequent pairs 10 ms.
		m_timer->adjust(attotime::from_usec(6500));
	else if (!motion && cate && !BIT(previous,7) && m_latches.cote)
	{
		m_settle=true;
		m_timer->adjust(attotime::from_usec(4096));
	}
	else if (!motion && !m_settle)
		m_timer->adjust(attotime::never);
	m_motion=motion;
	m_changer_output=changer_request();
}
void p6066_flodi_device::start_transfer()
{
	// INCO clocks MAS even for HOME/SEEK, whose cleared CATE inhibits data.
	if (!(m_latches.command&0x80)) return;
	if (m_reading) { m_last_sector=true; return; }

	m_settle=false; m_timer->adjust(attotime::never);
	m_reading=true; m_last_sector=false;
	if ((m_latches.command&7)==3)
	{
		m_format_wait=true; m_format_active=false; m_format_stream=true;
		m_format_area=m_format_nrem=false; m_format_fsc=m_format_ser=0;
		m_mismatch=false; m_id_crc=true;
		m_id_phase=false; m_byte=0; m_finish_byte=m_crc_start=-1;
		m_irq.rili=true; // RETRO.INCO.!COTE, K06 M5/M6.
	}
	else { load_track(); next_id(); }
}
TIMER_CALLBACK_MEMBER(p6066_flodi_device::mechanical_tick)
{
	if (m_motion)
	{
		if (auto *f=drive()) { f->dir_w(!m_direction); f->stp_w(0); f->stp_w(1); }
		// FLOD2 168664-K02 E9/G9: command bit 7 drives CATE/CATE5.
		// K07 E5/E6: TEVEO = (DIVE0 & RIF10) | !CATE5. Thus
		// positioning (CATE5=0) sets status bit 2, unlike a sector count.
		// K02 P9: CATE2=0 asserts the active-low preset of COTE0.
		// K07 C8/C9 routes COTE0 to logical EPD1, so positioning
		// reports both bit 1 and TEVEO bit 2, not TEVEO alone.
		m_timer->adjust(attotime::from_msec(10)); // two subsequent 5 ms pulses
	}
	else if (m_settle)
	{
		// FLODI p.30: free-running MCOTO; firmware counts ten pulses.
		m_timer->adjust(attotime::from_usec(4096));
	}
	else return;
	request3(8);
}
u16 p6066_flodi_device::name_type(unsigned level)
{
	if (level==1 && m_data_irq) return (u16(m_mismatch || !m_id_crc)<<8)|0x60;
	if (level!=3 || !m_active_type) fatalerror("FLODI input without interrupt ownership");
	return (u16(m_active_type)<<8)|0x60;
}
u8 p6066_flodi_device::input_data(unsigned level)
{
	if (level==1 && m_data_irq)
	{
		if (m_id_phase) fatalerror("FLODI data read during ID comparison");
		++m_bytes_read; m_byte_output=m_bytes_read;
		return record_byte(m_byte);
	}
	if (level!=3 || !m_active_type) fatalerror("FLODI status without interrupt ownership");
	auto *f=drive();
	const u8 status=(m_active_type&0x0c)
		? m_latches.status(BIT(m_active_type,2),m_index,f && !f->trk00_r(),m_end_status,m_scan.found,m_scan.different,m_inop)
		: (!f || !f->exists() ? 0x80 : 0) | (f && !f->trk00_r() ? 0x40 : 0) | (m_inop ? 8 : 0) | (m_local[m_selected] ? 0x10 : 0) | (m_latches.busy(m_changer_busy) ? 6 : 0);
	trace_event("status-read",status);
	logerror("FLODI status %02X type %02X\n",status,m_active_type);
	return status;
}
void p6066_flodi_device::command(unsigned level,u8 data)
{
	if (level!=3 || !m_active_type) fatalerror("FLODI command without interrupt ownership");
	trace_event("command",data);
	const u8 previous=m_latches.command;
	const bool mema=(m_active_type&0x0c)!=0;
	// K02 G6/G7: only drive 2 can load CADI from ECD5.
	switch (m_latches.write(mema,data,m_selected!=0))
	{
	case p6066_flodi_latches::effect::local:
		m_local[m_selected]=true; m_local0_output=m_local[0]; m_local1_output=m_local[1]; break;
	case p6066_flodi_latches::effect::command: latch_command(previous); break;
	case p6066_flodi_latches::effect::inco: start_transfer(); break;
	}
	if (!mema) m_irq.command_armed=true;
	strobe(level); // ECO supplies ECOC together with ECOT.
	logerror("FLODI command %02X latched=%02X PRICO=%u COTE=%u MAS=%02X\n",data,m_latches.command,m_latches.prico,m_latches.cote,m_latches.mas);
}
void p6066_flodi_device::strobe(unsigned level)
{
	if (level==1 && m_data_irq) return;
	if (level!=3 || !m_active_type) fatalerror("FLODI strobe without owner");
	if (m_active_type==3) m_inop=false; // K08 C2/C3: command-response ECOT.
	if (m_active_type&0x0c)
	{
		// K06 M2/M3 resets FOGO and FINE; FUGO/RIFI retain the
		// function/completion type until ECM3.
		trace_event("ECOT-before");
		m_index=false;
		m_irq.ecot(true);
		trace_event("ECOT-after");
	}
}

void p6066_flodi_device::stop_read()
{
	m_reading=false; m_data_irq=false; m_crc_gate=false; m_last_sector=false;
	m_irq.stop_bytes(); m_byte_timer->adjust(attotime::never);
	m_write_continue=false;
	if (m_writing) end_recording(machine().time());
	m_write_preamble=0;
	m_format_wait=m_format_active=m_format_stream=false;
}
void p6066_flodi_device::request3(u8 type,u8 status)
{
	// Source pulses set asynchronous latches; only ECM3 exposes them.
	if (m_reset) return;
	if (type==8) m_irq.fogo=true;
	else if (type==4) m_irq.fine=true;
	else fatalerror("FLODI invalid asynchronous level-3 source %u", type);
	if (status&1) m_index=true;
	trace_event("request3",(unsigned(type)<<8)|status);
}
void p6066_flodi_device::load_track()
{
	m_bits.fill(0); m_bit_count=83333; m_cursor=0;
	auto *f=drive();
	if (!f || !f->exists()) { m_bit_count=0; return; }
	// FDU STAC 1L p.1.06: 360 RPM. FLODISC p.22: FM 2 us cells.
	// Sample one revolution of the
	// actual MAME drive flux. Analogue PLL jitter/weak-cell behaviour is not
	// yet modelled; retain missing clocks and both recorded CRC fields.
	const attotime start=f->time_next_index();
	m_track_epoch=start;
	attotime transition=f->get_next_transition(start);
	while (transition!=attotime::never)
	{
		const s64 cell=(transition-start).as_ticks(500000);
		if (cell>=m_bit_count) break;
		if (cell>=0) m_bits[cell]=1;
		transition=f->get_next_transition(transition+attotime::from_nsec(1));
	}
	m_next_index=start+attotime::from_hz(6);
	logerror("FLODI track captured cylinder %u\n",f->get_cyl());
}
void p6066_flodi_device::next_id()
{
	m_id_phase=true; m_id_byte=0; m_mismatch=false; m_crc_gate=false;
	m_finish_byte=-1; m_crc_start=-1; m_payload_start=false;
	if (m_bit_count)
	{
		m_cursor=p6066_fm::rotational_cell(machine().time()-m_track_epoch,m_bit_count);
	}
	if (!m_bit_count) { m_byte_timer->adjust(attotime::from_hz(6)); return; }
	unsigned distance=0;
	while (distance<m_bit_count && p6066_fm::word(m_bits,m_bit_count,m_cursor+distance)!=0xf57e) ++distance;
	if (distance==m_bit_count) { m_byte_timer->adjust(attotime::from_hz(6)); m_id_pos=m_bit_count; return; }
	m_id_pos=(m_cursor+distance)%m_bit_count;
	for (unsigned i=0;i<5;++i) m_header[i]=p6066_fm::byte(m_bits,m_bit_count,m_id_pos+i*16);
	u16 crc=0xffff;
	for (unsigned i=0;i<7;++i) crc=p6066_fm::crc_byte(crc,p6066_fm::byte(m_bits,m_bit_count,m_id_pos+i*16));
	m_id_crc=!crc; m_data_pos=m_bit_count; m_length=0;
	// Locate the recorded mark, not a host sector length. ECOF determines
	// the payload/CRC boundary (pp.33-35); the fifth ID byte is compared
	// with the CPU like the other identifier bytes, not decoded as a size.
	for (unsigned offset=19*16;offset<m_bit_count;++offset)
	{
		const u16 raw=p6066_fm::word(m_bits,m_bit_count,m_id_pos+offset);
		if (raw==0xf57e) break;
		if (p6066_fm::clock(raw)==0xc7)
		{
			m_data_pos=(m_id_pos+offset)%m_bit_count;
			m_data_mark=p6066_fm::data(raw); break;
		}
	}
	m_cursor=(m_id_pos+(m_data_pos!=m_bit_count ? ((m_data_pos+m_bit_count-m_id_pos)%m_bit_count)+(m_length+3)*16 : 7*16))%m_bit_count;
	m_id_time=machine().time()+attotime::from_nsec(u64(distance)*2000);
	schedule_byte(m_id_time+attotime::from_usec(32));
}
TIMER_CALLBACK_MEMBER(p6066_flodi_device::byte_tick)
{
	if (!m_reading) return;
	if (m_format_active) { format_tick(); return; }
	if (m_format_wait) return;

	if (!m_bit_count || m_id_pos==m_bit_count) { load_track(); next_id(); return; }
	if (m_data_irq || m_irq.rili || m_irq.livi) fatalerror("FLODI level-1 byte overrun");
	if (!m_id_phase && (m_latches.command&7)==1)
	{
		if (m_write_preamble)
		{
			if (!m_writing) begin_recording(m_byte_time);
			write_byte(0);
			--m_write_preamble;
			schedule_byte(m_byte_time+attotime::from_usec(32));
			return;
		}
		if (!m_byte)
		{
			write_byte(m_expected_mark,m_expected_clock);
			m_write_crc=p6066_fm::crc_byte(0xffff,m_expected_mark);
		}
		else if (m_crc_start<0 || int(m_byte)<=m_crc_start)
		{
			write_byte(m_input);
			m_write_crc=p6066_fm::crc_byte(m_write_crc,m_input);
		}
		else if (int(m_byte)==m_crc_start+1) write_byte(m_write_crc>>8);
		else if (int(m_byte)==m_crc_start+2) write_byte(m_write_crc);
		else if (m_writing)
		{
			// SEOK drops at the end of COCI, ORRE follows 8 us later.
			if (auto *f=drive())
			{
				f->write_flux_change(m_byte_time+attotime::from_usec(1));
				f->write_flux_change(m_byte_time+attotime::from_usec(5));
				end_recording(m_byte_time+attotime::from_usec(8));
			}
		}
	}
	if (m_payload_start) { trace_event("payload-start"); m_payload_start=false; request3(8,0); }
	m_irq.rili=true;
}
void p6066_flodi_device::begin_recording(attotime when)
{
	m_write_timer->adjust(attotime::never); m_write_close_pending=m_write_continue=false;
	if (auto *f=drive()) f->write_start(when);
	m_writing=true; m_write_output=1;
	m_erase_target=true;
	// K04 S3/T4: RETRO starts erase with ORRE. Ordinary sector writes
	// delay erase by 128 us. MAME models recorded flux, not head geometry.
	if (m_format_active) { m_erase_active=true; m_erase_output=1; }
	m_erase_timer->adjust(when-machine().time()+attotime::from_usec(m_format_active ? 0 : 128));
}
void p6066_flodi_device::end_recording(attotime when)
{
	m_write_end_time=when; m_write_close_pending=true;
	if (when>machine().time()) m_write_timer->adjust(when-machine().time());
	else write_gate_tick(0);
}
TIMER_CALLBACK_MEMBER(p6066_flodi_device::write_gate_tick)
{
	m_write_timer->adjust(attotime::never); m_write_close_pending=false;
	if (auto *f=drive()) f->write_end(m_write_end_time);
	m_writing=false; m_write_output=0;
	m_erase_target=false;
	m_erase_timer->adjust(attotime::from_usec(464));
	if (m_write_continue)
	{
		m_write_continue=false;
		load_track(); next_id();
	}
}
TIMER_CALLBACK_MEMBER(p6066_flodi_device::erase_tick)
{
	m_erase_active=m_erase_target; m_erase_output=m_erase_active;
}
void p6066_flodi_device::format_tick()
{
	if (m_data_irq || m_irq.rili || m_irq.livi) fatalerror("FLODI write-track byte overrun");
	// CPU clock precedes data: figs.25/26 show INP -> FSC -> SER.
	// In fill mode BLAN selects constant FF/00. The previous FSC
	// supplies key clocks while the current FSC supplies key data.
	m_format_ser=m_format_fsc;
	m_format_fsc=m_input;
	const bool mark=m_input!=0xff && !m_format_nrem && !m_format_area && BIT(m_format_ser,7);
	if (mark)
	{
		m_format_area=m_format_nrem=true;
		write_byte(m_format_fsc,m_format_ser);
		request3(8);
	}
	else write_byte(m_format_area ? m_format_fsc : (m_input==0xff ? 0xff : 0));
	// BLAN inhibits subsequent key recognition during FF fill; it
	// must not turn a retained FF fill byte into a spurious new key.
	if (m_input==0xff) m_format_fsc=0;
	if (m_format_stream) m_irq.rili=true;
	else schedule_byte(m_byte_time+attotime::from_usec(32));
}
void p6066_flodi_device::write_byte(u8 data,u8 clocks)
{
	if (auto *f=drive())
	{
		const u16 encoded=p6066_fm::encode(data,clocks);
		for (unsigned cell=0;cell<16;++cell)
			if (BIT(encoded,15-cell)) f->write_flux_change(m_byte_time+attotime::from_nsec(cell*2000+1000));
	}
}
u8 p6066_flodi_device::record_byte(unsigned index) const
{
	return p6066_fm::byte(m_bits,m_bit_count,m_data_pos+(index+1)*16);
}
void p6066_flodi_device::finish_sector()
{
	// CPU-selected boundary, including both physically recorded CRC bytes.
	m_data_crc=true;
	if ((m_latches.command&7)!=1)
	{
		u16 crc=p6066_fm::crc_byte(0xffff,m_data_mark);
		for (unsigned i=0;i<m_length+2;++i) crc=p6066_fm::crc_byte(crc,record_byte(i));
		m_data_crc=!crc;
	}
	// Writing generates CRC; it does not read the new record back. A
	// subsequent verify is a distinct command, even on protected media.
	if (!m_data_crc) m_end_status|=0x40;
	trace_event("sector-complete");
	++m_sectors_read; m_sector_output=m_sectors_read;
	if (m_last_sector || m_scan.found) { m_reading=false; request3(4,m_end_status); }
	else if ((m_latches.command&7)==1 && m_write_close_pending) m_write_continue=true;
	else { if ((m_latches.command&7)==1) load_track(); next_id(); }
}
void p6066_flodi_device::output_data(unsigned level,u16 value)
{
	if (level!=1 || !m_data_irq) fatalerror("FLODI output without data-channel owner");
	if (m_id_phase)
	{
		if (m_id_byte<5) m_mismatch|=u8(value)!=m_header[m_id_byte];
		else if (m_id_byte==5) m_expected_clock=value;
		else if (m_id_byte==7) m_expected_mark=value;
		return;
	}
	m_input=value;
}
void p6066_flodi_device::advance_data()
{
	if (m_latches.command & 4)
	{
		// INP -> FSC, and PAR alignment: fig.21/22, pp.41-42. The
		// first two services prime the comparison pipeline. ECOF at
		// service 130/258 identifies the last payload byte, not a CRC.
		if (m_byte>=2 && (m_crc_start<0 || m_byte-2<m_length))
			m_scan.compare(record_byte(m_byte-2),m_mask_pipe[0],m_latches.command,m_latches.mas,m_latches.num);
		m_mask_pipe[0]=m_mask_pipe[1]; m_mask_pipe[1]=m_input;
	}
	// Verify accepts arbitrary CPU bytes and deliberately ignores them.
	// Read output also only clocks the input register; it cannot write media.
}

void p6066_flodi_device::control(unsigned level,u8 signal)
{
	if (level!=1 || !m_data_irq) fatalerror("FLODI control without data-channel owner");
	if (signal==7 || signal==0)
	{
		m_crc_gate=true; m_crc_start=m_id_phase ? m_id_byte : m_byte;
		if (!m_id_phase && !m_format_active && !m_format_wait)
		{
			// Read: 128th, write/verify:129th, scan:130th service.
			const unsigned lead=(m_latches.command&4) ? 2 : ((m_latches.command&3) ? 1 : 0);
			m_length=m_byte+1>=lead ? m_byte+1-lead : 0;
		}
	}
	else if (signal==8)
	{
		if (!m_crc_gate) fatalerror("FLODI CRC falling edge without preceding assertion");
		m_crc_gate=false;
		// FITUCO/FNCO and COCI follow ECOFO by a byte (manual fig.16).
		m_finish_byte=(m_id_phase ? m_id_byte : m_byte)+1;
	}
	else fatalerror("FLODI unknown channel control %u",signal);
}

void p6066_flodi_device::schedule_byte(attotime when)
{
	if (when<machine().time()) fatalerror("FLODI byte deadline missed: CPU timing/overrun handling needs verification");
	m_byte_time=when; m_byte_timer->adjust(when-machine().time());
}
void p6066_flodi_device::index_changed(floppy_image_device *floppy,int state)
{
	// Printed p.31 / K04 G1: COTEN qualifies INDO, independently of
	// the data engine. Index counting continues after data service stops.
	if (floppy==drive()) trace_event("index-edge",state);
	if (state && floppy==drive() && !m_latches.cote)
	{
		if (m_format_active && m_last_sector)
		{
			end_recording(machine().time());
			m_format_active=m_format_stream=m_reading=false;
			m_byte_timer->adjust(attotime::never);
			request3(4);
		}
		else if (m_format_wait)
		{
			m_format_wait=false; m_format_active=true;
			begin_recording(machine().time());
			m_byte_time=machine().time(); write_byte(0xff); // BLASN forced first byte.
			schedule_byte(m_byte_time+attotime::from_usec(32));
		}
		request3(8,1);
	}
}
