// license: BSD-3-Clause
// copyright-holders: Salvatore Paxia
/***************************************************************************

    MCZ MDC Floppy Module

***************************************************************************/
#include <iostream>
#include <istream>
#include <ostream>
#include <iomanip>

#include "emu.h"
#include "mdc.h"

#define VERBOSE 0

// Debugging
#include "logmacro.h"
#define LOG_BUS_MASK (LOG_GENERAL << 1)
#define LOG_BUS(...) LOGMASKED(LOG_BUS_MASK, __VA_ARGS__)
#define LOG_RD_MASK (LOG_BUS_MASK << 1)
#define LOG_RD(...) LOGMASKED(LOG_RD_MASK, __VA_ARGS__)
#define LOG_WR_MASK (LOG_RD_MASK << 1)
#define LOG_WR(...) LOGMASKED(LOG_WR_MASK, __VA_ARGS__)
#define LOG_DR_MASK (LOG_WR_MASK << 1)
#define LOG_DR(...) LOGMASKED(LOG_DR_MASK, __VA_ARGS__)
#undef VERBOSE
#define LOG_LEVEL0      (0x1U << 1)
#define LOG_LEVEL1      (0x3U << 1)

//#define VERBOSE (LOG_GENERAL|LOG_BUS_MASK|LOG_RD_MASK|LOG_WR_MASK|LOG_DR_MASK| LOG_LEVEL1)
#define VERBOSE LOG_GENERAL

/*	
	| Characteristic | IBM mode  |
    |----------------+-----------|
    | Bit cell size  | 4 µs      |
    | Modulation     | FM        |
    | Bit order      | MS first  |
    | Sync bytes     | 6x 00     |
    | Formatted size | 250.25 kB |

*/

// Constants
constexpr unsigned TIMEOUT_MS = 10;     // "timeout" timer: 10 ms
constexpr unsigned HALF_BIT_CELL_US = 1;// Half bit cell duration in µs
constexpr unsigned BIT_FREQUENCY = 250000;  // Frequency of bit cells in Hz

// Timings
#define TIMEOUT_MSEC        450     // Timeout duration (ms)
#define IBMMODE_BIT_FREQ    250000  // IBM-mode bit frequency (Hz)

#define MIN_SYNC_BITS       29      // Number of bits to synchronize

constexpr uint16_t CRC_POLY = 0x1021;   // CRC-CCITT

// Macros to clear/set single bits
#define BIT_MASK(n) (1U << (n))
#define BIT_CLR(w , n)  ((w) &= ~BIT_MASK(n))
#define BIT_SET(w , n)  ((w) |= BIT_MASK(n))


mcz_floppy_device::mcz_floppy_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock) :
	device_t(mconfig, MCZ_FLOPPY, tag, owner, clock),
	device_mcz_sysbus_card_interface(mconfig, *this),
	m_floppy(*this, "%u", 0U),
	m_pio(*this, "pio")

{

}

static void floppy_formats(format_registration &fr)
{
	fr.add(FLOPPY_MCZ_FORMAT);
}

 



void mcz_floppy_device::device_add_mconfig(machine_config &config)
{
	 
	Z80PIO(config, m_pio, DERIVED_CLOCK(1, 1));  

	FLOPPY_CONNECTOR(config , m_floppy[0] , "8sssd" , FLOPPY_8_SSSD, true , floppy_formats);
	FLOPPY_CONNECTOR(config , m_floppy[1] , "8sssd" , FLOPPY_8_SSSD , true , floppy_formats);
	FLOPPY_CONNECTOR(config , m_floppy[2] , "8sssd" , FLOPPY_8_SSSD , true , floppy_formats);
	FLOPPY_CONNECTOR(config , m_floppy[3] , "8sssd" , FLOPPY_8_SSSD , true , floppy_formats);
	FLOPPY_CONNECTOR(config , m_floppy[4] , "8sssd" , FLOPPY_8_SSSD , true , floppy_formats);
	FLOPPY_CONNECTOR(config , m_floppy[5] , "8sssd" , FLOPPY_8_SSSD , true , floppy_formats);
	FLOPPY_CONNECTOR(config , m_floppy[6] , "8sssd" , FLOPPY_8_SSSD , true , floppy_formats);
	FLOPPY_CONNECTOR(config , m_floppy[7] , "8sssd" , FLOPPY_8_SSSD , true , floppy_formats);
	
}

void mcz_floppy_device::floppy_index_cb(floppy_image_device *floppy, int state)
{
	//m_fdc->index_callback(floppy, state);
	static int stateOld=-1;
	if (state!=stateOld)
	{
		stateOld=state;
		//std::cout << "Index state: "<< state << " "<< synctime << std::endl;
		//printf("Bus cards %d\n",m_bus->cards);
		m_bus->disksector_w(state);
		m_indexPulse=state;
		
	}
	//if (state)
	//	m_index_int = true;
	//update_intsel();

}

attotime mcz_floppy_device::get_half_bit_cell_period(void) const
{
	return attotime::from_hz(IBMMODE_BIT_FREQ * 2);
}

TIMER_CALLBACK_MEMBER(mcz_floppy_device::timeout_tick)
{
	
	//m_inputs[ IN_SEL_TIMEOUT ] = true;
}

void mcz_floppy_device::get_next_transition(const attotime& from_when , attotime& edge)
{
	edge = attotime::never;

#ifdef SPAM
	if (BIT(m_cntl_reg , REG_CNTL_WRITON_BIT)) {
		// Loop back write transitions into reading data path
		for (int idx = 0; idx < m_pll.write_position; idx++) {
			if (m_pll.write_buffer[ idx ] >= from_when) {
				edge = m_pll.write_buffer[ idx ];
				break;
			}
		}
	} else 
#endif	
	if (m_current_drive != nullptr) {
		edge = m_current_drive->get_next_transition(from_when);
	}
}

bool mcz_floppy_device::shift_sr(uint8_t& sr , bool input_bit)
{
	bool res;

	res = BIT(sr , 7);
	sr <<= 1;
	if (input_bit) {
		BIT_SET(sr , 0);
	}

	return res;
}

void mcz_floppy_device::read_bit(bool crc_upd)
{
	attotime edge;
	attotime tm;

	get_next_transition(m_pll.ctime, edge);
	bool clock_bit = m_pll.feed_read_data(tm , edge , attotime::never);
	get_next_transition(m_pll.ctime, edge);
	bool data_bit = m_pll.feed_read_data(tm , edge , attotime::never);
	if (clock_bit!=0)
	;
	//shift_sr(m_clock_sr, clock_bit);
	data_bit = shift_sr(m_data_sr, data_bit);

#ifdef SPAM
	if (crc_upd &&
		BIT(m_cntl_reg , REG_CNTL_CRCON_BIT) &&
		!BIT(m_cntl_reg , REG_CNTL_CRCOUT_BIT)) {
		update_crc(data_bit);
	}
#endif
}

TIMER_CALLBACK_MEMBER(mcz_floppy_device::half_bit_timer_tick)
{
	static double lastsynctime=0; 
 
	m_pll.ctime = machine().time();
	//std::cout << "m_lckup" << m_lckup << std::endl;
	if (m_lckup) {
		// Trying to lock on synchronization bytes
		attotime edge;
		attotime tm;
		get_next_transition(m_pll.ctime, edge);
		bool half_bit0 = m_pll.feed_read_data(tm , edge , attotime::never);
		get_next_transition(m_pll.ctime, edge);
		bool half_bit1 = m_pll.feed_read_data(tm , edge , attotime::never);
		if (half_bit0 == half_bit1) {
			// If half bits are equal, no synch
			LOGMASKED(LOG_LEVEL0, "Reset sync_cnt\n");
			m_sync_cnt = 0;
		} else if (++m_sync_cnt >= MIN_SYNC_BITS) {
			// Synchronized, now wait for AM
			double synctime = machine().time().as_double();
			LOGMASKED(LOG_LEVEL0, "Synchronized @ %.6f\n" ,synctime );
			LOGMASKED(LOG_LEVEL0, "passed @ %.6f\n" ,synctime-lastsynctime );
			lastsynctime = synctime;
		 
			m_lckup = false;
#ifdef SPAM			
			if (BIT(m_cntl_reg , REG_CNTL_WRITON_BIT)) {
				// When loopback is active, leave AM detection to byte timer as
				// byte boundary is already synchronized
				m_half_bit_timer->reset();
				return;

			} else 
#endif
			{
				// Align with bit cell
				// Synchronization bits in HP mode: 32x 1s -> C/D bits = 01010101...
				// Synchronization bits in IBM mode: 32x 0s -> C/D bits = 10101010...
				if (half_bit1!=0) {
					// Discard 1/2 bit cell if synchronization achieved in the clock part
					get_next_transition(m_pll.ctime, edge);
					m_pll.feed_read_data(tm , edge , attotime::never);
				}
			
					m_clock_sr = ~0;
					m_data_sr = 0;
			
			}
		}
	} else {
		// Looking for AM
		/// CRC is not updated because it can't be possibly enabled at this point
		read_bit(false);
		if (BIT(m_data_sr , 0)) {
			// Got AM as soon as bits being shifted into DSR change value wrt synchronization bits
			//m_amdt = true;
			// Finish the current byte
			for (unsigned i = 0; i < 7; i++) {
				read_bit(false);
			}
			//T printf("Byte timer on!\n");
			attotime adjust{m_pll.ctime - machine().time()};
			LOGMASKED(LOG_LEVEL0, "Got AM @ %.6f, ctime=%.6f, adj=%.6f, D=%02x/C=%02x\n" , machine().time().as_double() , m_pll.ctime.as_double() , adjust.as_double() , m_data_sr , m_clock_sr);
			printf("Reading Track %d sector %d \n",m_current_drive->get_cyl(),m_data_sr&0x7f);
			// Disable half-bit timer & enable byte timer
			m_half_bit_timer->reset();
			m_byte_timer->adjust(adjust);
			//T std::cout << "Restart CPU!"  <<std::endl;
			m_bus->get_maincpu()->trigger(1);
			return;
		}
	}
	 
	m_half_bit_timer->adjust(m_pll.ctime - machine().time());

}

TIMER_CALLBACK_MEMBER(mcz_floppy_device::byte_tick)
{
	if (m_accdata) {
		// Resume CPU when it's waiting for SDOK
		LOGMASKED(LOG_LEVEL0, "RESUME CPU\n");
			//T std::cout << "Restart CPU!"  <<std::endl;
		m_bus->get_maincpu()->trigger(1);
	} else {
		// No access to data register by CPU
		LOGMASKED(LOG_LEVEL1, "Data overrun\n");
		m_overrun = true;
	}
	m_accdata = false;

	attotime sdok_time{machine().time()};
	LOGMASKED(LOG_LEVEL0, "SDOK @ %.06f\n" , sdok_time.as_double());
	bool do_crc_upd = true;
	
	if (m_reading) {
		// Reading
		m_pll.ctime = sdok_time;

		for (unsigned i = 0; i < 8; i++) {
			read_bit(do_crc_upd);
		}
		LOGMASKED(LOG_LEVEL0, "RD D=%02x/C=%02x\n" , m_data_sr , m_clock_sr);
	}
	LOGMASKED(LOG_LEVEL0, "next SDOK @ %.06f\n" , m_pll.ctime.as_double());
	m_byte_timer->adjust(m_pll.ctime - sdok_time);
}

TIMER_CALLBACK_MEMBER(mcz_floppy_device::f_tick)
{
	#ifdef SPAM
	m_inputs[ IN_SEL_F ] = false;
	if (m_writing) {
		write_byte();
		m_data_sr = dbus_r();
		m_byte_timer->adjust(attotime::from_usec(HALF_BIT_CELL_US * 14));
	}
	#endif
}


void mcz_floppy_device::device_reset()
{
#ifdef SPAM
	m_cpu_irq = false;
	m_current_drive = nullptr;
	m_current_drive_idx = ~0;
	for (auto& d : m_dskchg) {
		d = true;
	}
	preset_crc();
	m_crcerr_syn = false;
	m_overrun = false;
	
	m_timeout = true;
	m_cntl_reg = 0;
	m_clock_sr = 0;
	m_clock_reg = 0;
	m_data_sr = 0;
	m_wr_context = 0;
	m_had_transition = false;

	m_amdt = false;
	m_sync_cnt = 0;
	m_hiden = false;
	m_mgnena = false;
	
#endif	
 
	m_bus->installer(AS_IO)->install_readwrite_handler(0xd0, 0xd3, 0, 0xff00, 0, read8sm_delegate(*this, FUNC(mcz_floppy_device::pio_read)), write8sm_delegate(*this, FUNC(mcz_floppy_device::pio_write)));
	m_bus->installer(AS_IO)->install_readwrite_handler(0xcf, 0xcf, 0, 0xff00, 0, read8sm_delegate(*this, FUNC(mcz_floppy_device::data_read)), write8sm_delegate(*this, FUNC(mcz_floppy_device::data_write)));
	
	m_reading = false;
	m_writing = false;

	
	m_overrun = false;
	m_accdata = true;
	m_timeout = true;

	m_diskSelected = -1;
	m_current_drive = nullptr;

	m_lckup = true; // Because READON = 0

	m_timeout_timer->reset();
	m_byte_timer->reset();
	m_f_timer->reset();
	m_half_bit_timer->reset();

}

void mcz_floppy_device::device_start()
{

 
	for (auto& f : m_floppy) {
		if (f->get_device())
		{
			printf("Start device !!\n");
			f->get_device()->set_rpm(360);			
		}
	}

	m_timeout_timer = timer_alloc(FUNC(mcz_floppy_device::timeout_tick), this);
	m_byte_timer = timer_alloc(FUNC(mcz_floppy_device::byte_tick), this);
	m_f_timer = timer_alloc(FUNC(mcz_floppy_device::f_tick), this);
	m_half_bit_timer = timer_alloc(FUNC(mcz_floppy_device::half_bit_timer_tick), this);

}

void mcz_floppy_device::set_rd_wr(bool new_rd , bool new_wr)
{
	//T printf("set_rd_w %d %d\n",m_reading,new_rd);
	if (!m_reading && new_rd) {
		// Start reading
		//T printf("Start RD @%.6f\n" , machine().time().as_double());
		
		m_pll.set_clock(get_half_bit_cell_period());
		m_pll.read_reset(machine().time());

		// Search for next SYNC (16x 1 and a 0)
		m_byte_timer->reset();
		m_f_timer->reset();
		m_sync_cnt = 0;
		m_half_bit_timer->adjust(get_half_bit_cell_period());
#ifdef SPAM
		unsigned cnt_trans = 0;
		attotime rot_period = attotime::from_hz(6);
		while ((m_pll.ctime - machine().time()) < rot_period) {
			attotime edge = m_current_drive->get_next_transition(m_pll.ctime);
			if (edge.is_never()) {
				break;
			}
			attotime tm;
			bool bit = m_pll.feed_read_data(tm , edge , attotime::never);
			if (cnt_trans < 32) {
				if (!(BIT(cnt_trans , 0) ^ bit)) {
					cnt_trans++;
				} else {
					cnt_trans = 0;
				}
			} else if (cnt_trans == 32) {
				if (!bit) {
					cnt_trans++;
				} else {
					cnt_trans = 0;
				}
			} else {
				if (!bit) {
					printf("SYNC found @%.6f\n" , m_pll.ctime.as_double());
					// GOT SYNC!
					//Tif (m_crc_enabled) {
						// CRC shouldn't be enabled here or register won't get cleared
						//TLOG("Huh? CRC enabled during SYNC scan?\n");
					//T}
					//Tm_crc = 0;

					// Load the "0" bit into data/clock SR
					m_data_sr = 0;
					m_clock_sr = 0;
					// Read 7 more bits to make a full byte
					rd_bits(7);
					// Timer to go off at end of 8th bit of AM byte (when F signal goes high)
					m_byte_timer->adjust(m_pll.ctime - machine().time());
					break;
				} else {
					cnt_trans = 32;
				}
			}
		}
#endif
	} else if (m_reading && !new_rd) {
		// Stop reading
		//T printf("Stop RD\n");
		m_byte_timer->reset();
		m_f_timer->reset();
		m_half_bit_timer->reset();
			
		m_lckup = true;
			//m_amdt = false;

	}
	m_reading = new_rd;
#ifdef SPAM
	if (!m_writing && new_wr) {
		// Start writing
		LOG_WR("Start WR\n");
		m_pll.set_clock(attotime::from_usec(HALF_BIT_CELL_US));
		m_pll.start_writing(machine().time());
		m_pll.ctime = machine().time();
		m_last_data_bit = false;
		m_byte_timer->adjust(attotime::from_usec(HALF_BIT_CELL_US * 14));
	} else if (m_writing && !new_wr) {
		// Stop writing
		LOG_WR("Stop WR\n");
		m_pll.stop_writing(m_current_drive , machine().time());
		m_byte_timer->reset();
		m_f_timer->reset();
		m_inputs[ IN_SEL_F ] = false;
	}
	m_writing = new_wr;
	#endif
}

uint8_t mcz_floppy_device::aligned_rd_data(uint16_t sr)
{
	#ifdef SPAM
	attotime tmp{ machine().time() - m_last_f_time };

	// Compute how many bit cells have gone by since the last time F went high
	unsigned bits = tmp.as_ticks(BIT_FREQUENCY);
	if (bits) {
		LOG_RD("Aligning by %u bits\n" , bits);
		sr <<= bits;
	}
	return uint8_t(sr >> 8);
	#endif
	return 0;
}

bool mcz_floppy_device::update_crc(bool bit)
{
	bool out = BIT(m_crc , 15);

	if (m_crc_enabled && (out ^ bit)) {
		m_crc = (m_crc << 1) ^ CRC_POLY;
	} else {
		m_crc <<= 1;
	}

	return out;
}

void mcz_floppy_device::rd_bits(unsigned n)
{
 #ifdef SPAM
	while (n--) {
		attotime edge = m_current_drive->get_next_transition(m_pll.ctime);
		if (edge.is_never()) {
			break;
		}
		attotime tm;
		bool clock_bit = m_pll.feed_read_data(tm , edge , attotime::never);
		edge = m_current_drive->get_next_transition(m_pll.ctime);
		if (edge.is_never()) {
			break;
		}
		bool data_bit = m_pll.feed_read_data(tm , edge , attotime::never);

		m_clock_sr = (m_clock_sr << 1) | clock_bit;
		bool crc_bit = BIT(m_data_sr , 15);
		m_data_sr = (m_data_sr << 1) | data_bit;
		update_crc(crc_bit);
	}
	printf("CLK %04x DT %04x CRC %04x\n" , m_clock_sr , m_data_sr , m_crc);
#endif
}

uint8_t mcz_floppy_device::data_read(offs_t o)
{
 
	
	if (!m_accdata) {
		m_accdata = true;
		//T std::cout << "Suspend CPU (D)!"  <<std::endl;
		m_bus->get_maincpu()->suspend_until_trigger(1 , true);
	}
	LOGMASKED(LOG_LEVEL0, "R DATA=%02x\n" , m_data_sr);
	// CPU stalls until next SDOK


	//T std::cout << "DATA R "<< o << ">" << std::setfill('0') << std::setw(2) << std::hex << (int)m_data_sr << " " <<std::endl;
	
	return m_data_sr;

}
void mcz_floppy_device::data_write(offs_t o, uint8_t data)
{
	//T std::cout << "DATA W " << o << " " << std::setfill('0') << std::setw(2) << std::hex << (int)data << std::endl;
}

// 0xCF Write data port
// 0xD0 => "DSKCOM/DSSTAT",
            // bit 0 (OUT): DIRECT (1 OUT --decrease track?)
            // bit 1 (OUT): HDSTEP (step track)
			// bit 2 (OUT): READ MODE
			// bit 3 (OUT): WRITE MODE
			// bit 4 (OUT): ENABLE CRC
            // bit 5 (IN): READY, disk ready
            // bit 6 (IN): track 0
            // bit 7 (IN): CRC, crc error
  //      0xD1 => "DSKSEL",
            // bits 2-1-0: disk number from 0 to 7
            // bit 3: 1 deselect all disks 0 selects one of the 8
			// bit 5: sector pulse (active low)
            // bit 6: drive present (active low)
            // bit 7: WRTPTC, is disk write protected (active low)
  //      0xD2 => "DSKCOM1",
  //      0xD3 => "DSKSEL1",

void mcz_floppy_device::pio_write(offs_t o, uint8_t data)
{
	//T std::cout << "PIO W " << o << " " << std::setfill('0') << std::setw(2) << std::hex << (int)data << std::endl;
 
	switch (o)
	{
		    // bit 0 (OUT): DIRECT (1 OUT --decrease track?)
            // bit 1 (OUT): HDSTEP (step track)
			// bit 2 (OUT): READ MODE
			// bit 3 (OUT): WRITE MODE
			// bit 4 (OUT): ENABLE CRC
		case 0: 
				if (m_current_drive)
				{	
					//auto dev=m_floppy[m_diskSelected]->get_device();
					std::cout << "In write CYL : "<<m_current_drive->get_cyl() << std::endl;
					m_current_drive->dir_w(!(data&1));
					m_current_drive->stp_w((data&2)>>1);
					
					if (data&1)
					{
						std::cout << "DIRECT   "<< (data&1) << std::endl;
					}
					if ((data&2) >0)
					{
						std::cout << "HDSTEP   "<< ((data&2)>0) << std::endl;
					}
					if ((data&4) >0)
					{
						std::cout << "READ MODE   "<< ((data&4)>0) << " PC=" << std::setw(2) << std::hex << (int)(m_bus->get_maincpu()->state_int(Z80_PC)) << std::endl;
						set_rd_wr(true, m_writing);
						m_accdata=false;
						//T std::cout << "Suspend CPU (0)!"  <<std::endl;
						m_bus->get_maincpu()->suspend_until_trigger(1 , true);
					}
					else
					{
						std::cout << "READ MODE OFF!"  << std::endl;
						set_rd_wr(false, m_writing);
					}
					if ((data&8) >0)
					{
						std::cout << "WRITE MODE !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!  "<< ((data&8)>0) << std::endl;
					}
				}
			break;
			// bits 2-1-0: disk number from 0 to 7
            // bit 3: 1 deselect all disks 0 selects one of the 8

		
		case 1: if ((data&8) >0)
			    {
					int diskToSelect = data & 0x7;
					std::cout << "Select Disk  "<< (int)diskToSelect << std::endl;
					if (m_diskSelected>=0) {
						m_current_drive->setup_index_pulse_cb(floppy_image_device::index_pulse_cb());
					}
						
					m_current_drive=m_floppy[diskToSelect]->get_device();
					if (!m_current_drive->ready_r())
					{
						
						m_current_drive->setup_index_pulse_cb(floppy_image_device::index_pulse_cb(&mcz_floppy_device::floppy_index_cb, this));
						m_current_drive->stp_w(1);
						m_current_drive->dir_w(1);
						m_diskSelected=diskToSelect;
					}
					else
					{
						std::cout << "Selected Disk not ready  "  << std::endl;
						m_diskSelected=-1;
					}
				}
				else
				{
					m_diskSelected = -1;
					m_current_drive->setup_index_pulse_cb(floppy_image_device::index_pulse_cb());
					m_current_drive = nullptr;
				}
			break;
		default:
			break;
	}
	
	//0x9f8
}

uint8_t mcz_floppy_device::pio_read(offs_t o)
{
	uint8_t data=0;

	switch (o)
	{
			case 0: // bit 5 (IN): READY, disk ready
					// bit 6 (IN): TO, track 0
					data = 1<<6; // Not on Track 0 
					if (m_diskSelected>=0)
					{	
						auto dev= m_floppy[m_diskSelected]->get_device();
						std::cout << "CYL : "<<dev->get_cyl() << std::endl;

						if (dev->get_cyl()==0) 
							data &= 0xbf; // Track 0 
					} 
					else
					{
						data=1<<5; // Not Ready
					}
				break;
			// bit 5: sector pulse (active low)
            // bit 6: drive present (active low)
            // bit 7: WRTPTC, is disk write protected (active low)

		case 1: if (m_diskSelected>=0) 
				{
					data =1<<3 | 0<<6; // Disk selected & attached
				}	
				else
				{
					data =0<<3 | 0<<6; // Disk selected & attached
				}
				data|m_indexPulse<<5;
			break;
		default:
			break;
	}
	//T std::cout << "PIO R "<< o << ">" << std::setfill('0') << std::setw(2) << std::hex << (int)data << " " <<std::endl;
	return data;
}


#ifdef SPAM
uint16_t mcz_floppy_device::floppy_r(offs_t offset, uint16_t mem_mask)
{
	uint16_t data = 0xffff;
/*
	if (ACCESSING_BITS_8_15)
	{
		switch (offset & 0x07)
		{
		case 0:
		case 1:
		case 2:
		case 3:
			data &= (m_fdc->read(offset & 0x03) << 8) | 0xff;
			break;

		case 4:
			// 7-------  intrq from fdc
			// -6------  drq from fdc
			// --543210  write only

			data &= (m_latch << 8) | 0xff;
		}
	}
*/
	return data & mem_mask;
}

void mcz_floppy_device::floppy_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	if (VERBOSE)
		logerror("floppy_w: %06x = %04x & %04x\n", offset, data, mem_mask);

	if (ACCESSING_BITS_8_15)
	{
		data >>= 8;

		switch (offset & 0x07)
		{
		case 0:
		case 1:
		case 2:
		case 3:
			m_fdc->write(offset & 0x03, data);
			break;

		case 4:
			// 76------  read only
			// --5-----  side select
			// ---4----  force ready
			// ----3---  density select
			// -----2--  bus select
			// ------10  drive select

			floppy_image_device *floppy = nullptr;

			if (BIT(data, 2))
			{
				// bus 1, drives 0 to 3 at 1 MHz
				floppy = m_floppy[0 + (data & 0x03)]->get_device();
				m_fdc->set_clock_scale(0.5);
			}
			else
			{
				// bus 2, drives 4 to 7 at 2 MHz
				floppy = m_floppy[4 + (data & 0x03)]->get_device();
				m_fdc->set_clock_scale(1.0);
			}

			m_fdc->set_floppy(floppy);

			if (floppy)
			{
				// TODO: motor should only run for a few seconds after each access (for bus 1)
				floppy->mon_w(0);
				floppy->ss_w(BIT(data, 5));
			}

			m_fdc->dden_w(BIT(data, 3));
			m_fdc->set_force_ready(BIT(data, 4));
		}
	}
	
}
#endif



DEFINE_DEVICE_TYPE(MCZ_FLOPPY, mcz_floppy_device, "mcz_floppy", "mcz Floppy Interface")