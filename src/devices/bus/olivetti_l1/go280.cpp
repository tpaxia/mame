// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia

#include "emu.h"
#include "go280.h"

#include "formats/imd_dsk.h"
#include "formats/td0_dsk.h"

class go280_upd765a_device;
DECLARE_DEVICE_TYPE(GO280_UPD765A, go280_upd765a_device)

class go280_upd765a_device : public upd765_family_device
{
public:
	go280_upd765a_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock)
		: upd765_family_device(mconfig, GO280_UPD765A, tag, owner, clock)
	{
		has_dor = false;
	}

	virtual void map(address_map &map) override ATTR_COLD
	{
		map(0x0, 0x0).r(FUNC(go280_upd765a_device::msr_r));
		map(0x1, 0x1).rw(FUNC(go280_upd765a_device::fifo_r), FUNC(go280_upd765a_device::fifo_w));
	}

	void reset_w(int state)
	{
		bool const was_reset = !BIT(dor, 2);
		upd765_family_device::reset_w(state);

		if (was_reset && !state)
		{
			// Reset release produces four completion statuses, one for each drive.
			for (floppy_info &fi : flopi)
			{
				fi.pcn = 0;
				fi.st0 = ST0_ABRT | fi.id;
				fi.st0_filled = true;
			}
			irq = true;
			check_irq();
		}
	}

	bool write_gate() const
	{
		switch (cur_live.state)
		{
		case WRITE_SECTOR_DATA:
		case WRITE_SECTOR_DATA_BYTE:
		case WRITE_TRACK_PRE_SECTORS:
		case WRITE_TRACK_PRE_SECTORS_BYTE:
		case WRITE_TRACK_SECTOR:
		case WRITE_TRACK_SECTOR_BYTE:
		case WRITE_TRACK_POST_SECTORS:
		case WRITE_TRACK_POST_SECTORS_BYTE:
			return true;
		}
		return false;
	}

protected:
	virtual void command_end(floppy_info &fi, bool data_completion) override
	{
		hdl_cb(0);
		upd765_family_device::command_end(fi, data_completion);
	}
};

DEFINE_DEVICE_TYPE(GO280_UPD765A, go280_upd765a_device, "go280_upd765a", "Olivetti GO280 uPD765A FDC")

void go280_floppies(device_slot_interface &device)
{
	device.option_add("8dsdd", FLOPPY_8_DSDD);
}

olivetti_l1_go280_device::olivetti_l1_go280_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock)
	: device_t(mconfig, OLIVETTI_L1_GO280, tag, owner, clock)
	, device_olivetti_l1_card_interface(mconfig, *this)
	, m_fdc(*this, "fdc")
	, m_floppy(*this, "fdc:%u", 0U)
	, m_timer(*this, "timer")
	, m_dmac(*this, "dmac")
	, m_trace_cb(*this)
{
}


void olivetti_l1_go280_device::device_add_mconfig(machine_config &config)
{
	GO280_UPD765A(config, m_fdc, 8_MHz_XTAL);
	m_fdc->set_ready_line_connected(true);
	m_fdc->set_select_lines_connected(true);
	m_fdc->intrq_wr_callback().set(FUNC(olivetti_l1_go280_device::fdc_intrq_w));
	m_fdc->drq_wr_callback().set(FUNC(olivetti_l1_go280_device::fdc_drq_w));
	m_fdc->hdl_wr_callback().set(FUNC(olivetti_l1_go280_device::fdu_head_load_w));
	m_fdc->idx_wr_callback().set(FUNC(olivetti_l1_go280_device::fdu_index_w));
	FLOPPY_CONNECTOR(config, "fdc:0", go280_floppies, "8dsdd", olivetti_l1_go280_device::floppy_formats);
	FLOPPY_CONNECTOR(config, "fdc:1", go280_floppies, "8dsdd", olivetti_l1_go280_device::floppy_formats);
	FLOPPY_CONNECTOR(config, "fdc:2", go280_floppies, nullptr, olivetti_l1_go280_device::floppy_formats);
	FLOPPY_CONNECTOR(config, "fdc:3", go280_floppies, nullptr, olivetti_l1_go280_device::floppy_formats);

	AM9517A(config, m_dmac, 8_MHz_XTAL / 2);
	m_dmac->out_hreq_callback().set(FUNC(olivetti_l1_go280_device::dma_hreq_w));
	m_dmac->out_eop_callback().set(FUNC(olivetti_l1_go280_device::dma_eop_w));
	m_dmac->in_memr_callback().set(FUNC(olivetti_l1_go280_device::dma_memr));
	m_dmac->out_memw_callback().set(FUNC(olivetti_l1_go280_device::dma_memw));
	m_dmac->in_ior_callback<2>().set(FUNC(olivetti_l1_go280_device::dma_fdc_r));
	m_dmac->out_iow_callback<2>().set(FUNC(olivetti_l1_go280_device::dma_fdc_w));
	m_dmac->out_dack_callback<1>().set(FUNC(olivetti_l1_go280_device::dma_dack1_w));
	m_dmac->out_dack_callback<2>().set(FUNC(olivetti_l1_go280_device::dma_dack2_w));

	PIT8253(config, m_timer);
	m_timer->set_clk<0>(1'000'000);
	m_timer->out_handler<0>().set(m_timer, FUNC(pit8253_device::write_clk1));
	m_timer->out_handler<1>().set(FUNC(olivetti_l1_go280_device::fdu_timer_out));
}


void olivetti_l1_go280_device::device_start()
{
	save_item(NAME(m_fdc_interrupt));
	save_item(NAME(m_timer_interrupt));
	save_item(NAME(m_fdc_latched));
	save_item(NAME(m_timer_latched));
	save_item(NAME(m_pending));
	save_item(NAME(m_interrupt_enable));
	save_item(NAME(m_vector));
	save_item(NAME(m_control));
	save_item(NAME(m_dma_high));
	save_item(NAME(m_dma_channel1));
	save_item(NAME(m_dma_flipflop));
	save_item(NAME(m_fdc_drq));
	save_item(NAME(m_fdc_index));
	save_item(NAME(m_fdc_head_load));
	save_item(NAME(m_dma_fdc_cycle));
	save_item(NAME(m_dma_eop));
	save_item(NAME(m_dma_channel));
	save_item(NAME(m_dma_mode));
	save_item(NAME(m_dma_buffer));
	save_item(NAME(m_dma_buffer_pos));
	save_item(NAME(m_dma_byte));
	save_item(NAME(m_last_dma_address));
	save_item(NAME(m_fumeo));
	save_item(NAME(m_perro));
}


void olivetti_l1_go280_device::device_reset()
{
	m_fdc_interrupt = false;
	m_timer_interrupt = false;
	m_fdc_latched = false;
	m_timer_latched = false;
	m_pending = false;
	m_interrupt_enable = false;
	m_vector = 0;
	m_control = 0;
	m_dma_high = 0;
	m_dma_channel1 = 0;
	m_dma_flipflop = false;
	m_fdc_drq = false;
	m_fdc_index = false;
	m_fdc_head_load = false;
	m_dma_fdc_cycle = false;
	m_dma_eop = false;
	m_dma_channel = -1;
	m_dma_mode.fill(0);
	m_dma_buffer.fill(0);
	m_dma_buffer_pos = 0;
	m_dma_byte = 0;
	m_last_dma_address = 0;
	m_fumeo = false;
	m_perro = false;
	m_fdc->set_rate(500000);
	m_fdc->set_ready_line_connected(true);
	m_dmac->dreq1_w(0);
	m_dmac->hack_w(1);
	m_dmac->ready_w(1);
	busreq_w(0);
	update_vi();
}


u8 olivetti_l1_go280_device::io_r(offs_t offset)
{
	u8 const reg = offset & 0xff;
	u8 data;
	if (reg >= 0x40 && reg <= 0x5e && !BIT(reg, 0))
	{
		data = m_dmac->read((reg >> 1) & 0x0f);
	}
	else if (reg >= 0x99 && reg <= 0x9d && BIT(reg, 0))
	{
		data = m_timer->read((reg >> 1) & 3);
	}
	else switch (reg)
	{
	case 0x1d: data = m_fdc->msr_r(); break;
	case 0x1f: data = m_fdc->fifo_r(); break;
	case 0xe7:
		// Reading VERFN presets the write-DMA sequencer.  SCRVO gates DRQ1;
		// channel 1 fetches the first memory word for a disk write.  Disk reads
		// begin with channel 2 filling the two board buffers.
		m_dma_buffer_pos = 0;
		m_dma_byte = 0;
		m_dmac->dreq1_w(BIT(m_control, 6));
		data = 0xff;
		break;
	case 0xf7:
		data = ((m_timer_latched || m_timer_interrupt) ? 0x01 : 0)
			| ((m_fdc_latched || m_fdc_interrupt) ? 0x02 : 0)
			| (m_perro ? 0x04 : 0)
			| (m_fumeo ? 0x08 : 0);
		break;
	case 0xff: data = 0xe1; break;
	case 0xed:
		// DAW00 is the data-separator window derived from disk rotation.  The
		// diagnostic measures successive rising edges to determine spindle speed.
		data = 0x9a
			| (m_fdc_index ? 0x04 : 0x00)
			| (m_fdc_drq ? 0x20 : 0x00)
			| (m_fdc_index ? 0x40 : 0x00)
			| (downcast<go280_upd765a_device &>(*m_fdc).write_gate() ? 0x01 : 0x00);
		break;
	default: data = 0xff; break;
	}
	trace(TRACE_IO_R, reg, data);
	return data;
}


void olivetti_l1_go280_device::io_w(offs_t offset, u8 data)
{
	u8 const reg = offset & 0xff;
	if (reg >= 0x40 && reg <= 0x5e && !BIT(reg, 0))
	{
		if (reg == 0x58)
			m_dma_flipflop = false;
		else if (reg == 0x56)
			m_dma_mode[data & 3] = data;
		else if (reg == 0x44)
		{
			if (!m_dma_flipflop)
				m_dma_channel1 = (m_dma_channel1 & 0xff00) | data;
			else
			{
				m_dma_channel1 = (m_dma_channel1 & 0x00ff) | (u16(data) << 8);
				m_dma_byte = 0;
			}
			m_dma_flipflop = !m_dma_flipflop;
		}
		m_dmac->write((reg >> 1) & 0x0f, data);
	}
	else if (reg >= 0x99 && reg <= 0x9f && BIT(reg, 0))
	{
		m_timer->write((reg >> 1) & 3, data);
	}
	else switch (reg)
	{
	case 0x1f:
		m_fdc->fifo_w(data);
		break;
	case 0xe7:
		{
		bool const was_enabled = m_interrupt_enable;
		m_control = data;
		m_interrupt_enable = BIT(data, 0);
		downcast<go280_upd765a_device &>(*m_fdc).reset_w(BIT(data, 1) ? 0 : 1);
		bool const diagnostic = BIT(data, 4);
		if (!BIT(data, 1))
			m_fdc_head_load = false;
		m_fdc->set_ready_line_connected(!diagnostic);
		if (diagnostic)
			m_fdc->ready_w(false);
		m_timer->write_clk2(m_fdc_index && m_fdc_head_load);
		for (auto &connector : m_floppy)
			if (floppy_image_device *const floppy = connector->get_device())
				// GO280 is configured for 1 MB FDU drives; their spindle motors
				// run continuously, unlike the MOTO1/MOTO2-controlled MFDU case.
				floppy->mon_w(0);
		if (!was_enabled && m_interrupt_enable && (m_fdc_latched || m_timer_latched || m_fumeo || m_perro))
			m_pending = true;
		update_vi();
		break;
		}
	case 0xef:
		m_vector = data;
		break;
	case 0xf6:
		m_dma_high = data;
		break;
	case 0xff:
		m_fdc_latched = false;
		m_timer_latched = false;
		m_fumeo = false;
		m_perro = false;
		m_pending = false;
		update_vi();
		break;
	}
	trace(TRACE_IO_W, reg, data);
}


void olivetti_l1_go280_device::fdc_intrq_w(int state)
{
	if (state && !m_fdc_interrupt)
	{
		m_fdc_latched = true;
		if (m_interrupt_enable)
			m_pending = true;
	}
	m_fdc_interrupt = bool(state);
	trace(TRACE_FDC_INT, 0, state ? 1 : 0);
	update_vi();
}


void olivetti_l1_go280_device::fdc_drq_w(int state)
{
	m_fdc_drq = bool(state);
	m_dmac->dreq2_w(state);
}


void olivetti_l1_go280_device::fdu_timer_out(int state)
{
	if (state && !m_timer_interrupt)
	{
		m_timer_latched = true;
		if (m_interrupt_enable)
			m_pending = true;
	}
	m_timer_interrupt = bool(state);
	trace(TRACE_TIMER, 0, state ? 1 : 0);
	update_vi();
}


void olivetti_l1_go280_device::fdu_index_w(int state)
{
	m_fdc_index = bool(state);
	m_timer->write_clk2(state && m_fdc_head_load);
	trace(TRACE_INDEX, 0, state ? 1 : 0);
}


void olivetti_l1_go280_device::fdu_head_load_w(int state)
{
	m_fdc_head_load = bool(state);
	m_timer->write_clk2(m_fdc_index && m_fdc_head_load);
}


void olivetti_l1_go280_device::dma_hreq_w(int state)
{
	// GO280 loops AM9517 HRQ back to HACK.  The gate array requests the
	// system bus separately, and only for the channel-1 memory-word cycle.
}


void olivetti_l1_go280_device::dma_eop_w(int state)
{
	m_dma_eop = bool(state);
	if (m_dma_fdc_cycle)
		m_fdc->tc_w(state);
}


void olivetti_l1_go280_device::bus_grant_w(int state)
{
	if (m_dma_channel == 1 && state)
		m_dmac->ready_w(1);
}


void olivetti_l1_go280_device::dma_dack1_w(int state)
{
	if (!state && m_dma_channel != 1)
	{
		m_dma_channel = 1;
		machine().scheduler().synchronize(timer_expired_delegate(FUNC(olivetti_l1_go280_device::dma_channel1_clear), this));
		m_dmac->ready_w(0);
		busreq_w(1);
	}
	else if (m_dma_channel == 1)
	{
		m_dma_channel = -1;
		busreq_w(0);
		m_dmac->ready_w(1);
	}
}


void olivetti_l1_go280_device::dma_dack2_w(int state)
{
	if (!state)
	{
		m_dma_channel = 2;
		m_dmac->ready_w(1);
		// A software request is sufficient to exercise the AM9517 registers,
		// but only an FDC request enables the GO280 data-transfer gates.
		m_dma_fdc_cycle = m_fdc_drq;
	}
	else if (m_dma_channel == 2)
	{
		m_dma_channel = -1;
		m_dma_fdc_cycle = false;
	}
}


TIMER_CALLBACK_MEMBER(olivetti_l1_go280_device::dma_channel1_request)
{
	m_dmac->dreq1_w(1);
}


TIMER_CALLBACK_MEMBER(olivetti_l1_go280_device::dma_channel1_clear)
{
	m_dmac->dreq1_w(0);
}


u8 olivetti_l1_go280_device::dma_fdc_r()
{
	return m_dma_fdc_cycle ? m_fdc->dma_r() : 0xff;
}


void olivetti_l1_go280_device::dma_fdc_w(u8 data)
{
	if (m_dma_fdc_cycle)
		m_fdc->dma_w(data);
}


u32 olivetti_l1_go280_device::dma_phys(u16 word_address, unsigned byte)
{
	u32 const base = ((u32(m_dma_high) << 16) | word_address) << 1;
	m_last_dma_address = (base + byte) & 0xffffff;
	return m_last_dma_address;
}


void olivetti_l1_go280_device::dma_memory_fault()
{
	m_fumeo = true;
	if (m_interrupt_enable)
		m_pending = true;
	m_fdc->tc_w(1);
	update_vi();
}


u8 olivetti_l1_go280_device::dma_memr(offs_t offset)
{
	if (m_dma_channel == 1)
	{
		u16 const word_address = offset;
		bool const disk_write = BIT(m_control, 6);
		for (unsigned byte = 0; byte < 2; byte++)
		{
			u32 const address = dma_phys(word_address, byte);
			if (disk_write)
			{
				u8 data;
				if (!physical_try_r(address, data))
					dma_memory_fault();
				else
				{
					m_dma_buffer[byte] = data;
					trace(TRACE_DMA_R, 0, data);
				}
			}
			else
			{
				if (!physical_try_w(address, m_dma_buffer[byte]))
					dma_memory_fault();
				else
					trace(TRACE_DMA_W, 0, m_dma_buffer[byte]);
			}
		}

		bool const decrement = BIT(m_dma_mode[1], 5);
		if (decrement && word_address == 0x0000)
			--m_dma_high;
		else if (!decrement && word_address == 0xffff)
			++m_dma_high;
		m_dma_channel1 = decrement ? word_address - 1 : word_address + 1;
		m_dma_buffer_pos = 0;
		m_dma_byte += 2;
		// REQ00/BAXXN cover this memory-word transaction, not the complete
		// internal AM9517 channel-1 acknowledge interval.
		busreq_w(0);
		return 0xff;
	}
	else if (m_dma_channel == 2 && m_dma_fdc_cycle)
	{
		u8 const data = m_dma_buffer[m_dma_buffer_pos++];
		if (m_dma_buffer_pos == 2)
		{
			m_dma_buffer_pos = 0;
			// A write starts with a channel-1 prefetch, so terminal count
			// suppresses the otherwise superfluous next-word request.
			if (!m_dma_eop)
				machine().scheduler().synchronize(timer_expired_delegate(FUNC(olivetti_l1_go280_device::dma_channel1_request), this));
		}
		return data;
	}
	return 0xff;
}


void olivetti_l1_go280_device::dma_memw(offs_t offset, u8 data)
{
	if (m_dma_channel == 2 && m_dma_fdc_cycle)
	{
		m_dma_buffer[m_dma_buffer_pos++] = data;
		if (m_dma_buffer_pos == 2)
		{
			m_dma_buffer_pos = 0;
			// A read commits every complete word, including the final pair.
			machine().scheduler().synchronize(timer_expired_delegate(FUNC(olivetti_l1_go280_device::dma_channel1_request), this));
		}
	}
}


void olivetti_l1_go280_device::update_vi()
{
	vi_w(m_pending && m_interrupt_enable);
}


u16 olivetti_l1_go280_device::viack_r()
{
	m_pending = false;
	trace(TRACE_VI_ACK, 0, m_vector);
	update_vi();
	return m_vector;
}


void olivetti_l1_go280_device::floppy_formats(format_registration &fr)
{
	fr.add(FLOPPY_IMD_FORMAT);
	fr.add(FLOPPY_TD0_FORMAT);
}


DEFINE_DEVICE_TYPE(OLIVETTI_L1_GO280, olivetti_l1_go280_device, "olivetti_l1_go280", "Olivetti GO280 floppy governo")
