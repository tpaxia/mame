// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia

#include "emu.h"
#include "go363.h"

#include "imagedev/harddriv.h"

#define LOG_REGISTERS (1U << 1)
#define LOG_TRANSFER  (1U << 2)

#define VERBOSE (LOG_REGISTERS | LOG_TRANSFER)
#include "logmacro.h"

DEFINE_DEVICE_TYPE(OLIVETTI_L1_GO363, olivetti_l1_go363_device, "olivetti_l1_go363", "Olivetti GO363 ST506 hard disk governo")

olivetti_l1_go363_device::olivetti_l1_go363_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock)
	: device_t(mconfig, OLIVETTI_L1_GO363, tag, owner, clock)
	, device_olivetti_l1_card_interface(mconfig, *this)
	, m_hdc(*this, "hdc")
	, m_drive(*this, "hdc:%u", 0U)
	, m_timer(*this, "timer")
{
}

void olivetti_l1_go363_device::device_add_mconfig(machine_config &config)
{
	UPD7261(config, m_hdc, 20_MHz_XTAL / 2);
	m_hdc->out_dreq().set(FUNC(olivetti_l1_go363_device::hdc_dreq_w));
	m_hdc->out_int().set(FUNC(olivetti_l1_go363_device::hdc_int_w));
	HARDDISK(config, "hdc:0");
	HARDDISK(config, "hdc:1");

	PIT8253(config, m_timer);
}

void olivetti_l1_go363_device::device_start()
{
	m_command_timer = timer_alloc(FUNC(olivetti_l1_go363_device::command_done), this);
	m_board_timer = timer_alloc(FUNC(olivetti_l1_go363_device::board_timer_done), this);

	save_item(NAME(m_dma_address));
	save_item(NAME(m_transfer_head));
	save_item(NAME(m_transfer_count));
	save_item(NAME(m_transfer_cylinder));
	save_item(NAME(m_transfer_sector));
	save_item(NAME(m_transfer_latch));
	save_item(NAME(m_transfer_pair));
	save_item(NAME(m_command));
	save_item(NAME(m_parameter));
	save_item(NAME(m_start_low));
	save_item(NAME(m_status));
	save_item(NAME(m_result));
	save_item(NAME(m_diagnostic_control));
	save_item(NAME(m_diagnostic_data));
	save_item(NAME(m_diagnostic_fifo));
	save_item(NAME(m_diagnostic_fifo_count));
	save_item(NAME(m_diagnostic_fifo_index));
	save_item(NAME(m_diagnostic_fifo_read));
	save_item(NAME(m_selected_unit));
	save_item(NAME(m_vector));
	save_item(NAME(m_vector_loaded));
	save_item(NAME(m_interrupt));
	save_item(NAME(m_hdc_interrupt));
	save_item(NAME(m_timer_interrupt));
	save_item(NAME(m_timer_interrupt_enabled));
	save_item(NAME(m_diagnostic_interrupt));
	save_item(NAME(m_diagnostic_interrupt_enabled));
	save_item(NAME(m_diagnostic_vi));
	save_item(NAME(m_timer_count));
	save_item(NAME(m_timer_write_phase));
}

void olivetti_l1_go363_device::device_reset()
{
	m_dma_address = 0;
	m_transfer_head = 0;
	m_transfer_count = 0;
	m_transfer_cylinder = 0;
	m_transfer_sector = 0;
	m_transfer_latch = 0;
	m_transfer_pair = 0;
	m_command = 0;
	m_parameter = 0;
	m_start_low = 0;
	m_status = 0x04;
	m_result = 0xffff;
	m_diagnostic_control = 0;
	m_diagnostic_data = 0;
	std::fill(std::begin(m_diagnostic_fifo), std::end(m_diagnostic_fifo), 0);
	m_diagnostic_fifo_count = 0;
	m_diagnostic_fifo_index = 0;
	m_diagnostic_fifo_read = false;
	m_selected_unit = 0;
	m_vector = 0;
	m_vector_loaded = false;
	m_interrupt = false;
	m_hdc_interrupt = false;
	m_timer_interrupt = false;
	m_timer_interrupt_enabled = false;
	m_diagnostic_interrupt = false;
	m_diagnostic_interrupt_enabled = false;
	m_diagnostic_vi = false;
	std::fill(std::begin(m_timer_count), std::end(m_timer_count), 0);
	std::fill(std::begin(m_timer_write_phase), std::end(m_timer_write_phase), 0);
	m_board_timer->adjust(attotime::never);
	update_vi();
}

u8 olivetti_l1_go363_device::io_r(offs_t offset)
{
	u8 const reg = offset & 0xff;
	u8 data = 0xff;

	switch (reg)
	{
	case 0x46: data = m_timer->read(0); break;
	case 0x47: data = m_timer->read(1); break;
	case 0x56: data = m_timer->read(2); break;
	case 0x57: data = m_timer->read(3); break;
	case 0xc0: data = m_timer->read(0); break;
	case 0xc1: data = m_timer->read(1); break;
	case 0xc2: data = m_timer->read(2); break;
	case 0xc3: data = m_timer->read(3); break;
	case 0x01:
		if (m_diagnostic_fifo_read && m_diagnostic_fifo_index < m_diagnostic_fifo_count)
		{
			data = m_diagnostic_fifo[m_diagnostic_fifo_index++];
			if (m_diagnostic_fifo_index == m_diagnostic_fifo_count)
				m_diagnostic_fifo_read = false;
		}
		else
			data = m_hdc->read(0);
		break;
	case 0x10:
	case 0x11: data = m_hdc->read(1); break;
	case 0x42:
	case 0x43:
	case 0x4a: data = 0x00; break;
	case 0x4b: data = (m_diagnostic_interrupt || m_hdc_interrupt || m_timer_interrupt) ? 0x28 : 0x00; break;
	case 0x80: data = m_result >> 8; break;
	case 0x81: data = m_result; break;
	case 0x90: data = 0x00; break;
	case 0x91:
		data = m_status;
		if (!m_selected_unit || m_selected_unit > m_drive.size()
			|| !m_drive[m_selected_unit - 1] || !m_drive[m_selected_unit - 1]->exists())
			data |= 0x01;
		break;
	case 0xff: data = 0x65; break;
	}

	LOGMASKED(LOG_REGISTERS, "register 0x%02x read 0x%02x (%s)\n", reg, data, machine().describe_context());
	return data;
}

void olivetti_l1_go363_device::io_w(offs_t offset, u8 data)
{
	u8 const reg = offset & 0xff;
	LOGMASKED(LOG_REGISTERS, "register 0x%02x write 0x%02x (%s)\n", reg, data, machine().describe_context());

	switch (reg)
	{
	case 0x46: timer_w(0, data); break;
	case 0x47: timer_w(1, data); break;
	case 0x56: m_timer->write(2, data); break;
	case 0x57: timer_control_w(data); break;
	case 0xc0: timer_w(0, data); break;
	case 0xc1: timer_w(1, data); break;
	case 0xc2: m_timer->write(2, data); break;
	case 0xc3: timer_control_w(data); break;
	case 0x01:
		m_hdc->write(0, data);
		if (m_diagnostic_fifo_count < std::size(m_diagnostic_fifo))
			m_diagnostic_fifo[m_diagnostic_fifo_count++] = data;
		break;
	case 0x4a: m_vector = data; break;
	case 0x48: m_diagnostic_data = (m_diagnostic_data & 0x00ff) | (u16(data) << 8); break;
	case 0x49:
		m_diagnostic_data = (m_diagnostic_data & 0xff00) | data;
		if (data == 0x02)
		{
			m_diagnostic_vi = false;
			// HDC505 uses 0002 for polled timer completion and ff02 when
			// terminal count is also to set the board's VI request latch.
			m_timer_interrupt_enabled = (m_diagnostic_data & 0xff00) == 0xff00;
		}
		else if (data == 0x40)
		{
			m_timer_interrupt = false;
			m_timer_interrupt_enabled = false;
			m_diagnostic_vi = false;
			m_board_timer->adjust(attotime::never);
		}
		update_vi();
		break;
	case 0x4c: m_diagnostic_control = (m_diagnostic_control & 0x00ff) | (u16(data) << 8); break;
	case 0x4d:
		m_diagnostic_control = (m_diagnostic_control & 0xff00) | data;
		if (m_diagnostic_control == 0x0b00)
		{
			m_diagnostic_interrupt = true;
			m_diagnostic_vi = m_diagnostic_interrupt_enabled;
			update_vi();
		}
		else if (m_diagnostic_control == 0x0400)
		{
			m_diagnostic_interrupt_enabled = (m_diagnostic_data == 0x0003);
			update_vi();
		}
		else if (m_diagnostic_control == 0x3900)
		{
			m_diagnostic_interrupt = false;
			m_diagnostic_interrupt_enabled = false;
			m_diagnostic_vi = false;
			m_interrupt = false;
			m_hdc_interrupt = false;
			m_timer_interrupt = false;
			m_timer_interrupt_enabled = false;
			m_board_timer->adjust(attotime::never);
			m_diagnostic_fifo_count = 0;
			m_diagnostic_fifo_index = 0;
			m_diagnostic_fifo_read = false;
			update_vi();
		}
		else if (m_diagnostic_control == 0x4000 || m_diagnostic_control == 0x4100)
		{
			LOGMASKED(LOG_REGISTERS, "timer command %04x count0=%04x count1=%04x\n",
				m_diagnostic_control, m_timer_count[0], m_timer_count[1]);
		}
		else if (m_diagnostic_control == 0x4500)
		{
			m_diagnostic_fifo_index = 0;
			m_diagnostic_fifo_read = true;
		}
		break;
	case 0x10:
		m_hdc->write(1, data);
		if (data & 0x03)
		{
			m_diagnostic_fifo_count = 0;
			m_diagnostic_fifo_index = 0;
			m_diagnostic_fifo_read = false;
		}
		break;
	case 0x11: m_hdc->write(1, data); break;
	case 0x20: m_transfer_latch = (m_transfer_latch & 0x00ff) | (u16(data) << 8); break;
	case 0x21:
	{
		u16 const value = (m_transfer_latch & 0xff00) | data;
		if (m_transfer_pair++ & 1)
			m_transfer_count = (value << 8) | (value >> 8);
		else
			m_transfer_head = value;
		break;
	}
	case 0x22: m_transfer_latch = (m_transfer_latch & 0x00ff) | (u16(data) << 8); break;
	case 0x23:
	{
		u16 const value = (m_transfer_latch & 0xff00) | data;
		m_transfer_cylinder = (value << 8) | (value >> 8);
		break;
	}
	case 0x80:
		if (!m_vector_loaded)
		{
			m_vector = data;
			m_vector_loaded = true;
		}
		else
			m_dma_address = (m_dma_address & 0xffff00ffU) | (u32(data) << 8);
		break;
	case 0x81: m_dma_address = (m_dma_address & 0xffffff00U) | data; break;
	case 0x82:
		m_start_low = data;
		break;
	case 0x83:
		if ((u16(data) << 8 | m_start_low) == m_command)
			start_command();
		else
			m_dma_address = (m_dma_address & 0x0000ffffU) | (u32(data) << 16) | (u32(m_start_low) << 24);
		break;
	case 0x71:
		LOGMASKED(LOG_TRANSFER, "control 0x%02x command 0x%04x\n", data, m_command);
		if (data == 0x83 && (m_command >> 8) == 0x0d)
			start_transfer();
		break;
	case 0xaa: m_vector = data; break;
	case 0xb0: m_command = (m_command & 0x00ff) | (u16(data) << 8); break;
	case 0xb1:
	{
		bool const start = BIT(data, 7) && !BIT(m_command, 7);
		m_command = (m_command & 0xff00) | data;
		if (start)
			start_command();
		break;
	}
	case 0xe0: m_parameter = (m_parameter & 0x00ff) | (u16(data) << 8); break;
	case 0xe1:
		m_parameter = (m_parameter & 0xff00) | data;
		if (u8(m_parameter) >= 1 && u8(m_parameter) <= 4)
			m_selected_unit = u8(m_parameter);
		break;
	}
}

void olivetti_l1_go363_device::start_transfer()
{
	m_status = 0x04;
	m_result = 0x00;

	if (!m_selected_unit || m_selected_unit > m_drive.size()
		|| !m_drive[m_selected_unit - 1] || !m_drive[m_selected_unit - 1]->exists())
	{
		m_result = 0x0d;
	}
	else
	{
		harddisk_image_device &drive = *m_drive[m_selected_unit - 1];
		hard_disk_file::info const &info = drive.get_info();
		u32 const sector_bytes = info.sectorbytes;
		std::vector<u8> sector(sector_bytes);
		u32 const lba = ((u32(m_transfer_cylinder) * info.heads) + m_transfer_head) * info.sectors + m_transfer_sector;
		u32 const count = m_transfer_count ? m_transfer_count : 1;
		u32 address = (m_dma_address << 1) & 0xffffff;
		LOGMASKED(LOG_TRANSFER, "read C=%u H=%u S=%u count=%u LBA=%u DMA=%06x\n",
			m_transfer_cylinder, m_transfer_head, m_transfer_sector, count, lba, address);
		for (u32 block = 0; block < count; block++)
		{
			if (!drive.read(lba + block, sector.data()))
			{
				m_result = 0x0d;
				break;
			}
			for (u32 i = 0; i < sector_bytes; i++)
				physical_w(address++ & 0xffffff, sector[i]);
		}
	}
	m_transfer_pair = 0;

	m_command_timer->adjust(attotime::from_usec(10));
}

void olivetti_l1_go363_device::start_command()
{
	// The gate array starts a board command on the rising edge of control bit 7.
	// Command 7 is the board/controller initialization handshake used by IPL.
	m_status = 0x04;
	if (BIT(m_command, 4) && (m_command >> 8) == 0x02)
	{
		// IPL complements these results.  Parameter 0x68 returns the logical
		// head count in the high nibble; parameter 0x88 selects the 40 MB
		// geometry class when its decoded value is one.
		if (m_parameter == 0x0068)
			m_result = 0x3f;
		else if (m_parameter == 0x0088)
			m_result = 0xfe;
		else
			m_result = 0xff;
	}
	else if (BIT(m_command, 4) && (m_command >> 8) == 0x0a)
	{
		if (u8(m_parameter) == 0x50)
		{
			// The unit probe passes the test byte in the first byte written at
			// E0.  IPL complements the result and requires it to echo that byte.
			m_result = u8(~(m_parameter >> 8));
		}
		else
			m_result = 0x00;
	}
	if ((m_command >> 8) == 0x07 || BIT(m_command, 4))
		m_command_timer->adjust(attotime::from_usec(10));
	else
		m_status = 0x06;
}

TIMER_CALLBACK_MEMBER(olivetti_l1_go363_device::command_done)
{
	m_status = 0x06;
	m_interrupt = true;
	update_vi();
}

void olivetti_l1_go363_device::timer_control_w(u8 data)
{
	m_timer->write(3, data);
	unsigned const channel = data >> 6;
	if (channel < 2)
		m_timer_write_phase[channel] = 0;
}

void olivetti_l1_go363_device::timer_w(unsigned channel, u8 data)
{
	m_timer->write(channel, data);
	bool const high_byte = bool(m_timer_write_phase[channel]++);
	if (high_byte)
		m_timer_count[channel] = (m_timer_count[channel] & 0x00ff) | (u16(data) << 8);
	else
		m_timer_count[channel] = (m_timer_count[channel] & 0xff00) | data;
	m_timer_write_phase[channel] &= 1;

	if (channel == 1 && high_byte)
	{
		// The board's 20 MHz oscillator clocks counter 0, whose output clocks
		// counter 1.  Loading counter 1 starts the cascade; the later private
		// diagnostic command only reports its result.  Schedule terminal count
		// as one event to avoid millions of unobservable PIT callbacks.
		m_timer_interrupt = false;
		m_interrupt = false;
		u64 const count0 = m_timer_count[0] ? m_timer_count[0] : 0x10000;
		u64 const count1 = m_timer_count[1] ? m_timer_count[1] : 0x10000;
		LOGMASKED(LOG_REGISTERS, "timer start count0=%04x count1=%04x\n",
			m_timer_count[0], m_timer_count[1]);
		m_board_timer->adjust(attotime::from_ticks(count0 * count1, 20_MHz_XTAL));
		update_vi();
	}
}

TIMER_CALLBACK_MEMBER(olivetti_l1_go363_device::board_timer_done)
{
	m_timer_interrupt = true;
	if (m_timer_interrupt_enabled)
		m_interrupt = true;
	LOGMASKED(LOG_REGISTERS, "timer done command=%04x vi=%u\n", m_diagnostic_control, m_interrupt);
	update_vi();
}

void olivetti_l1_go363_device::hdc_dreq_w(int state)
{
	// The GO363 gate arrays transfer the controller data phase through 8 KiB
	// of local SRAM before performing word-addressed DMA on the L1 bus.
}

void olivetti_l1_go363_device::hdc_int_w(int state)
{
	m_hdc_interrupt = bool(state);
	update_vi();
}

void olivetti_l1_go363_device::update_vi()
{
	vi_w(m_interrupt || (m_diagnostic_interrupt_enabled && m_hdc_interrupt) || m_diagnostic_vi);
}

u16 olivetti_l1_go363_device::viack_r()
{
	LOGMASKED(LOG_REGISTERS, "VI acknowledge vector 0x%02x timer=%u diagnostic=%u hdc=%u (%s)\n",
		m_vector, m_timer_interrupt, m_diagnostic_interrupt, m_hdc_interrupt, machine().describe_context());
	m_interrupt = false;
	m_hdc_interrupt = false;
	update_vi();
	return m_vector;
}
