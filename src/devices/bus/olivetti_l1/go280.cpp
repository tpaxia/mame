// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia

#include "emu.h"
#include "go280.h"

#include "formats/imd_dsk.h"

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

	virtual void soft_reset() override
	{
		upd765_family_device::soft_reset();
		for (floppy_info &fi : flopi)
		{
			fi.pcn = 0;
			fi.st0 = ST0_ABRT | fi.id;
			fi.st0_filled = true;
		}
		irq = true;
		check_irq();
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
	m_fdc->set_ready_line_connected(false);
	m_fdc->set_select_lines_connected(true);
	m_fdc->intrq_wr_callback().set(FUNC(olivetti_l1_go280_device::fdc_intrq_w));
	m_fdc->drq_wr_callback().set(FUNC(olivetti_l1_go280_device::fdc_drq_w));
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
	m_dmac->in_ior_callback<2>().set(m_fdc, FUNC(upd765_family_device::dma_r));
	m_dmac->out_iow_callback<2>().set(m_fdc, FUNC(upd765_family_device::dma_w));

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
	save_item(NAME(m_dma_high));
	save_item(NAME(m_dma_channel1));
	save_item(NAME(m_dma_flipflop));
	save_item(NAME(m_dma_byte));
	save_item(NAME(m_last_dma_address));
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
	m_dma_high = 0;
	m_dma_channel1 = 0;
	m_dma_flipflop = false;
	m_dma_byte = 0;
	m_last_dma_address = 0;
	m_fdc->set_rate(500000);
	m_fdc->ready_w(false);
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
	case 0xf7:
		data = ((m_timer_latched || m_timer_interrupt) ? 0x01 : 0)
			| ((m_fdc_latched || m_fdc_interrupt) ? 0x02 : 0);
		break;
	case 0xff: data = 0xe1; break;
	case 0xed: data = 0xff; break;
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
		m_interrupt_enable = BIT(data, 0);
		m_fdc->reset_w(BIT(data, 1) ? 0 : 1);
		m_fdc->ready_w(false);
		for (auto &connector : m_floppy)
			if (floppy_image_device *const floppy = connector->get_device())
				floppy->mon_w(0);
		update_vi();
		break;
	case 0xef:
		m_vector = data;
		break;
	case 0xf6:
		m_dma_high = data;
		break;
	case 0xff:
		m_fdc_latched = false;
		m_timer_latched = false;
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
		m_pending = true;
		m_fdc_latched = true;
	}
	m_fdc_interrupt = bool(state);
	trace(TRACE_FDC_INT, 0, state ? 1 : 0);
	update_vi();
}


void olivetti_l1_go280_device::fdc_drq_w(int state)
{
	m_dmac->dreq2_w(state);
}


void olivetti_l1_go280_device::fdu_timer_out(int state)
{
	if (state && !m_timer_interrupt)
	{
		m_pending = true;
		m_timer_latched = true;
	}
	m_timer_interrupt = bool(state);
	trace(TRACE_TIMER, 0, state ? 1 : 0);
	update_vi();
}


void olivetti_l1_go280_device::fdu_index_w(int state)
{
	m_timer->write_clk2(state);
	trace(TRACE_INDEX, 0, state ? 1 : 0);
}


void olivetti_l1_go280_device::dma_hreq_w(int state)
{
	busreq_w(state);
}


void olivetti_l1_go280_device::dma_eop_w(int state)
{
	m_fdc->tc_w(state);
}


u32 olivetti_l1_go280_device::dma_phys()
{
	u32 const base = ((u32(m_dma_high) << 16) | m_dma_channel1) << 1;
	m_last_dma_address = (base + m_dma_byte++) & 0xffffff;
	return m_last_dma_address;
}


u8 olivetti_l1_go280_device::dma_memr(offs_t offset)
{
	return physical_r(dma_phys());
}


void olivetti_l1_go280_device::dma_memw(offs_t offset, u8 data)
{
	physical_w(dma_phys(), data);
	trace(TRACE_DMA_W, 0, data);
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
}


DEFINE_DEVICE_TYPE(OLIVETTI_L1_GO280, olivetti_l1_go280_device, "olivetti_l1_go280", "Olivetti GO280 floppy governo")
