// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
#include "emu.h"
#include "flodi.h"
#include "flodi_fm.h"
DEFINE_DEVICE_TYPE(P6066_FLODI,p6066_flodi_device,"p6066_flodi","Olivetti P6066 FLODI floppy controller")
p6066_flodi_device::p6066_flodi_device(const machine_config &mconfig,const char *tag,device_t *owner,u32 clock)
	: device_t(mconfig,P6066_FLODI,tag,owner,clock),device_p6066_card_interface(mconfig,*this),m_sector_output(*this,"sectors_read"),m_byte_output(*this,"bytes_read"),m_drives(*this,"%u",0U) { }
static void fdu_drives(device_slot_interface &device) { device.option_add("8sssd",FLOPPY_8_SSSD); }
void p6066_flodi_device::device_add_mconfig(machine_config &config)
{
	FLOPPY_CONNECTOR(config,"0",fdu_drives,"8sssd",floppy_image_device::default_fm_floppy_formats);
	FLOPPY_CONNECTOR(config,"1",fdu_drives,"8sssd",floppy_image_device::default_fm_floppy_formats);
}
floppy_image_device *p6066_flodi_device::drive() const { return m_drives[m_selected]->get_device(); }
void p6066_flodi_device::device_start()
{
	for (auto &connector : m_drives)
		if (auto *f=connector->get_device()) f->setup_index_pulse_cb(floppy_image_device::index_pulse_cb(&p6066_flodi_device::index_changed,this));
	save_item(NAME(m_track_epoch)); save_item(NAME(m_byte_time)); save_item(NAME(m_finish_byte)); save_item(NAME(m_crc_start)); save_item(NAME(m_payload_start));
	m_byte_timer=timer_alloc(FUNC(p6066_flodi_device::byte_tick),this);
	save_item(NAME(m_bits)); save_item(NAME(m_bit_count)); save_item(NAME(m_cursor)); save_item(NAME(m_id_pos)); save_item(NAME(m_data_pos));
	save_item(NAME(m_header)); save_item(NAME(m_payload)); save_item(NAME(m_length)); save_item(NAME(m_byte)); save_item(NAME(m_id_byte));
	save_item(NAME(m_reading)); save_item(NAME(m_id_phase)); save_item(NAME(m_data_irq)); save_item(NAME(m_mismatch));
	save_item(NAME(m_crc_gate)); save_item(NAME(m_last_sector)); save_item(NAME(m_id_crc)); save_item(NAME(m_data_crc));
	save_item(NAME(m_data_mark)); save_item(NAME(m_expected_mark)); save_item(NAME(m_expected_clock)); save_item(NAME(m_end_status));
	save_item(NAME(m_id_time)); save_item(NAME(m_next_index)); save_item(NAME(m_sectors_read)); save_item(NAME(m_bytes_read));
	m_timer=timer_alloc(FUNC(p6066_flodi_device::mechanical_tick),this);
	save_item(NAME(m_local)); save_item(NAME(m_motion)); save_item(NAME(m_direction)); save_item(NAME(m_settle)); save_item(NAME(m_event_status));
	save_item(NAME(m_selected)); save_item(NAME(m_requests)); save_item(NAME(m_pending_type));
	save_item(NAME(m_active_type)); save_item(NAME(m_commands)); save_item(NAME(m_command_count)); save_item(NAME(m_reset));
}
void p6066_flodi_device::device_reset() { stop_read(); m_sectors_read=m_bytes_read=0; m_sector_output=0; m_byte_output=0; m_selected=0; m_reset=true; m_requests=0; m_active_type=0; m_pending_type=0; m_command_count=0; m_local[0]=m_local[1]=false; m_motion=false; m_settle=0; m_timer->adjust(attotime::never); }
void p6066_flodi_device::controller_reset(bool asserted)
{
	m_reset=asserted;
	if (asserted) { stop_read(); m_local[0]=m_local[1]=false; m_motion=false; m_settle=0; m_timer->adjust(attotime::never); m_requests=0; m_active_type=0; m_pending_type=0; m_command_count=0; }
}
void p6066_flodi_device::select(u8 name)
{
	// Manual table 2 lists active-low bus values 9F/1F, hence logical 60/E0.
	if (m_reset || (name!=0x60 && name!=0xe0)) return;
	m_selected=BIT(name,7); m_pending_type=1; m_requests|=8;
	if (auto *f=drive()) { f->ss_w(0); f->mon_w(0); }
	logerror("FLODI select %02X, drive %u\n",name,m_selected+1);
}
void p6066_flodi_device::irq_ack(unsigned source)
{
	if (source==0 && (m_requests&1)) { m_requests&=~1; m_data_irq=true; return; }
	if ((source!=2 && source!=3) || !(m_requests&(1U<<source))) fatalerror("FLODI unsupported interrupt acknowledgement");
	m_requests&=~(1U<<source); m_active_type=m_pending_type; m_pending_type=0; m_command_count=0;
}
void p6066_flodi_device::irq_end(unsigned level)
{
	if (level==1)
	{
		m_data_irq=false;
		if (!m_reading) return;
		if (m_id_phase)
		{
			if (m_finish_byte<0 || int(m_id_byte)!=m_finish_byte)
			{
				if (++m_id_byte>8) fatalerror("FLODI ID CRC control sequence not completed");
				schedule_byte(m_byte_time+attotime::from_usec(32));
			}
			else if (m_mismatch || !m_id_crc || !m_data_pos) next_id();
			else
			{
				logerror("FLODI matched ID %02X %02X %02X %02X, length %u\n",m_header[1],m_header[2],m_header[3],m_header[4],m_length);
				m_id_phase=false; m_byte=0; m_finish_byte=-1; m_crc_start=-1; m_payload_start=true;
				m_end_status|=(m_data_mark==0xf8 ? 0x80 : 0);
				const unsigned cells=(m_data_pos+m_bit_count-m_id_pos)%m_bit_count;
				schedule_byte(m_id_time+attotime::from_nsec(u64(cells+16)*2000));
			}
		}
		else if (m_finish_byte<0 || int(m_byte)!=m_finish_byte)
		{
			if (++m_byte>m_length+3) fatalerror("FLODI data CRC control sequence not completed");
			schedule_byte(m_byte_time+attotime::from_usec(32));
		}
		else
		{
			m_end_status|=(m_data_crc && m_crc_start==int(m_length)-1) ? 0 : 0x40;
			++m_sectors_read; m_sector_output=m_sectors_read;
			logerror("FLODI sector complete C=%u H=%u R=%u bytes=%u CRC=%s\n",m_header[1],m_header[2],m_header[3],m_length,m_data_crc?"good":"BAD");
			if (m_last_sector) { m_reading=false; request3(4,m_end_status); }
			else next_id();
		}
		return;
	}
	if (level!=3) return;
	const u8 type=m_active_type;
	m_active_type=0;
	if (m_command_count) apply_commands(type);
	if (type==1 && m_command_count) { m_pending_type=3; m_requests|=8; }
}
void p6066_flodi_device::apply_commands(u8 type)
{
	const u8 first=m_commands[0], last=m_commands[m_command_count-1];
	if (type==1 && !first && !last) { m_local[m_selected]=true; return; }
	if (type==1 && (last&0xf0)==0x50)
	{
		m_motion=true; m_direction=BIT(first,3);
		// FDU pp.9-10, figs.14-18: two motor pulses per track;
		// first pair is separated by T1+T2 = 1.5+5 ms.
		m_timer->adjust(attotime::from_usec(6500)); return;
	}
	if (first==0x80 || first==0xc0)
	{
		if (m_motion || m_command_count==1)
		{
			m_motion=false; m_settle=true;
			m_timer->adjust(attotime::from_msec(4)); return;
		}
		if (m_reading) { m_last_sector=true; return; }
		// Read protocol: FLODI pp.33-36, figures 16-17. ECOFO controls
		// the CRC window and its delayed falling edge ends the byte train.
		m_settle=false; m_timer->adjust(attotime::never);
		m_reading=true; m_last_sector=false; m_end_status=0;
		load_track(); next_id(); return;
	}
	if (!first && m_command_count==1) { stop_read(); return; }
	fatalerror("FLODI unsupported command %02X %02X in type %02X",first,last,type);
}
TIMER_CALLBACK_MEMBER(p6066_flodi_device::mechanical_tick)
{
	if (m_motion)
	{
		if (auto *f=drive()) { f->dir_w(!m_direction); f->stp_w(0); f->stp_w(1); }
		// FLOD2 168664-K02 E9/G9: command bit 7 drives CATE/CATE5.
		// K07 E5/E6: TEVEO = (DIVE0 & RIF10) | !CATE5. Thus
		// positioning (CATE5=0) sets status bit 2, unlike a sector count.
		m_event_status=4;
		m_timer->adjust(attotime::from_msec(10)); // two subsequent 5 ms pulses
	}
	else if (m_settle)
	{
		m_event_status=2;
		// FLODI p.30: free-running MCOTO; firmware counts ten pulses.
		m_timer->adjust(attotime::from_msec(4));
	}
	else return;
	request3(8,m_event_status);
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
		if (m_id_phase || m_byte>=m_length) fatalerror("FLODI data read outside payload");
		++m_bytes_read; m_byte_output=m_bytes_read;
		return m_payload[m_byte];
	}
	if (level!=3 || !m_active_type) fatalerror("FLODI status without interrupt ownership");
	auto *f=drive();
	const u8 status=(m_active_type==8 || m_active_type==4) ? (m_event_status | (m_active_type==8 && f && !f->trk00_r() ? 0x40 : 0))
		: (!f || !f->exists() ? 0x80 : 0) | (f && !f->trk00_r() ? 0x40 : 0) | (m_local[m_selected] ? 0x10 : 0) | (m_motion ? 6 : 0);
	logerror("FLODI status %02X type %02X\n",status,m_active_type);
	return status;
}
void p6066_flodi_device::command(unsigned level,u8 data)
{
	if (level!=3 || !m_active_type || m_command_count>=2) fatalerror("FLODI unsupported command phase");
	m_commands[m_command_count++]=data;
	logerror("FLODI command %02X\n",data);
}
void p6066_flodi_device::strobe(unsigned level) { if (level==1 && m_data_irq) return; if (level!=3 || !m_active_type) fatalerror("FLODI strobe without owner"); }

void p6066_flodi_device::stop_read()
{
	m_reading=false; m_data_irq=false; m_crc_gate=false; m_last_sector=false;
	m_requests&=~1; m_byte_timer->adjust(attotime::never);
}
void p6066_flodi_device::request3(u8 type,u8 status)
{
	if (m_requests&12) fatalerror("FLODI pending level-3 event overrun");
	m_pending_type=type; m_event_status=status; m_requests|=(type==1 || type==3) ? 8 : 4;
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
		const s64 elapsed=(machine().time()-m_track_epoch).as_ticks(500000);
		m_cursor=((elapsed % s64(m_bit_count))+m_bit_count)%m_bit_count;
	}
	if (!m_bit_count) { m_byte_timer->adjust(attotime::from_hz(6)); return; }
	unsigned distance=0;
	while (distance<m_bit_count && p6066_fm::word(m_bits,m_bit_count,m_cursor+distance)!=0xf57e) ++distance;
	if (distance==m_bit_count) { m_byte_timer->adjust(attotime::from_hz(6)); m_id_pos=m_bit_count; return; }
	m_id_pos=(m_cursor+distance)%m_bit_count;
	for (unsigned i=0;i<5;++i) m_header[i]=p6066_fm::byte(m_bits,m_bit_count,m_id_pos+i*16);
	u16 crc=0xffff;
	for (unsigned i=0;i<7;++i) crc=p6066_fm::crc_byte(crc,p6066_fm::byte(m_bits,m_bit_count,m_id_pos+i*16));
	m_id_crc=!crc; m_data_pos=0; m_length=0;
	// An address mark must precede the next ID and fit the supported buffer.
	for (unsigned offset=7*16;offset<160*16;++offset)
	{
		const u16 raw=p6066_fm::word(m_bits,m_bit_count,m_id_pos+offset);
		if (raw==0xf57e) break;
		if (p6066_fm::clock(raw)==0xc7 && (p6066_fm::data(raw)==0xfb || p6066_fm::data(raw)==0xf8))
		{
			if (m_header[4]>3) fatalerror("FLODI unsupported sector size code %u",m_header[4]);
			m_data_pos=(m_id_pos+offset)%m_bit_count; m_data_mark=p6066_fm::data(raw); m_length=128U<<m_header[4];
			crc=p6066_fm::crc_byte(0xffff,m_data_mark);
			for (unsigned i=0;i<m_length+2;++i)
			{
				const u8 value=p6066_fm::byte(m_bits,m_bit_count,m_data_pos+(i+1)*16);
				if (i<m_length) m_payload[i]=value;
				crc=p6066_fm::crc_byte(crc,value);
			}
			m_data_crc=!crc; break;
		}
	}
	m_cursor=(m_id_pos+(m_data_pos ? ((m_data_pos+m_bit_count-m_id_pos)%m_bit_count)+(m_length+3)*16 : 7*16))%m_bit_count;
	m_id_time=machine().time()+attotime::from_nsec(u64(distance)*2000);
	schedule_byte(m_id_time+attotime::from_usec(32));
}
TIMER_CALLBACK_MEMBER(p6066_flodi_device::byte_tick)
{
	if (!m_reading) return;

	if (!m_bit_count || m_id_pos==m_bit_count) { m_byte_timer->adjust(attotime::from_hz(6)); return; }
	if (m_data_irq || (m_requests&1)) fatalerror("FLODI level-1 byte overrun");
	if (m_payload_start) { m_payload_start=false; request3(8,0); }
	m_requests|=1;
}
void p6066_flodi_device::output_data(unsigned level,u16 value)
{
	if (level!=1 || !m_data_irq || !m_id_phase) fatalerror("FLODI write data is not implemented");
	if (m_id_byte<5) m_mismatch|=u8(value)!=m_header[m_id_byte];
	else if (m_id_byte==5) m_expected_clock=value;
	else if (m_id_byte==7) { m_expected_mark=value; m_mismatch|=m_expected_clock!=0xc7 || (m_data_mark!=m_expected_mark && m_data_mark!=0xf8); }
	logerror("FLODI ID byte %u expected %02X actual %02X mismatch %u\n",m_id_byte,u8(value),m_id_byte<5?m_header[m_id_byte]:m_data_mark,m_mismatch);
}
void p6066_flodi_device::control(unsigned level,u8 signal)
{
	if (level!=1 || !m_data_irq) fatalerror("FLODI control without data-channel owner");
	if (signal==7 || signal==0)
	{
		m_crc_gate=true; m_crc_start=m_id_phase ? m_id_byte : m_byte;
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
	if (state && floppy==drive() && m_reading) request3(8,1);
}
