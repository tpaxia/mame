// license:BSD-3-Clause
// copyright-holders:Salvatore Paxia

#include "emu.h"
#include "go252.h"

olivetti_l1_go252_device::olivetti_l1_go252_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock)
	: device_t(mconfig, OLIVETTI_L1_GO252, tag, owner, clock)
	, device_olivetti_l1_card_interface(mconfig, *this)
	, m_crtc(*this, "crtc")
	, m_palette(*this, "palette")
	, m_screen(*this, "screen")
	, m_keyboard(*this, "keyboard")
	, m_chargen(*this, "chargen")
{
}


ROM_START( olivetti_l1_go252 )
	// GI 9428DS-2067 mask ROM: 256 characters x 16 rows, lit pixels stored as 0
	ROM_REGION( 0x1000, "chargen", 0 )
	ROM_LOAD( "9428ds-2067.bin", 0x0000, 0x1000, CRC(7096b8f1) SHA1(f578b43055db9910f177eba977de4dee9bda1914) )
ROM_END

const tiny_rom_entry *olivetti_l1_go252_device::device_rom_region() const
{
	return ROM_NAME( olivetti_l1_go252 );
}


void olivetti_l1_go252_device::device_add_mconfig(machine_config &config)
{
	screen_device &screen(SCREEN(config, m_screen));
	screen.set_refresh_hz(57);
	screen.set_vblank_time(ATTOSECONDS_IN_USEC(2500));
	screen.set_size(848, 442);
	screen.set_visarea(0, 640 - 1, 0, 425 - 1);
	screen.set_screen_update(m_crtc, FUNC(mc6845_device::screen_update));

	PALETTE(config, m_palette, FUNC(olivetti_l1_go252_device::palette_init), 3);

	MC6845(config, m_crtc, 32_MHz_XTAL / 12);
	m_crtc->set_screen(m_screen);
	m_crtc->set_show_border_area(false);
	m_crtc->set_char_width(8);
	m_crtc->set_update_row_callback(FUNC(olivetti_l1_go252_device::crtc_update_row));

	OLIVETTI_L1_KEYBOARD(config, m_keyboard, 0);
	m_keyboard->data_handler().set(FUNC(olivetti_l1_go252_device::kdc_queue));
}


void olivetti_l1_go252_device::device_start()
{
	m_kbd_boot_timer = timer_alloc(FUNC(olivetti_l1_go252_device::kbd_boot_announce), this);
	m_vram = std::make_unique<u8[]>(0x1000);
	save_pointer(NAME(m_vram), 0x1000);
	save_item(NAME(m_crtc_index));
	save_item(NAME(m_crtc_max_ras));
	save_item(NAME(m_kdc_ctrl));
	save_item(NAME(m_kdc_data));
	save_item(NAME(m_kdc_vector));
	save_item(NAME(m_kdc_pending));
	save_item(NAME(m_kdc_data_armed));
	save_item(NAME(m_kbd_fifo));
	save_item(NAME(m_kbd_head));
	save_item(NAME(m_kbd_tail));
	save_item(NAME(m_kbd_count));
	save_item(NAME(m_kbd_irq_mode));
	save_item(NAME(m_kbd_ident_reply));
	save_item(NAME(m_kbd_poll_status));
}


void olivetti_l1_go252_device::device_reset()
{
	m_crtc_index = 0;
	m_crtc_max_ras = 16;
	m_kdc_ctrl = 0;
	m_kdc_data = 0;
	m_kdc_vector = 0x28;
	m_kdc_pending = false;
	m_kdc_data_armed = false;
	m_kbd_head = 0;
	m_kbd_tail = 0;
	m_kbd_count = 0;
	m_kbd_irq_mode = false;
	m_kbd_ident_reply = false;
	m_kbd_poll_status = false;
	std::fill(std::begin(m_kbd_fifo), std::end(m_kbd_fifo), 0);
	// The recovered 8049 firmware announces completion of its power-on ROM/RAM
	// test with FC and repeats it until command 00 starts the foreground scanner.
	m_kbd_fifo[0] = 0xfc;
	m_kbd_head = 1;
	m_kbd_count = 1;
	m_kbd_boot_timer->adjust(attotime::from_msec(20), 0, attotime::from_msec(20));
	update_vi();
}


TIMER_CALLBACK_MEMBER(olivetti_l1_go252_device::kbd_boot_announce)
{
	// The hardware has a one-byte transmit holding register rather than a FIFO:
	// retain at most one unconsumed announcement in the HLE queue.
	if (!m_kbd_irq_mode && !m_kbd_count)
		kdc_queue_internal(0xfc, true);
}


void olivetti_l1_go252_device::kdc_queue(u8 data)
{
	kdc_queue_internal(data, true);
}


void olivetti_l1_go252_device::kdc_queue_internal(u8 data, bool interrupt)
{
	if (m_kbd_count == std::size(m_kbd_fifo))
		return;

	m_kbd_fifo[m_kbd_head] = data;
	m_kbd_head = (m_kbd_head + 1) & 0x0f;
	m_kbd_count++;
	m_kdc_pending = interrupt && m_kbd_irq_mode;
	update_vi();
}


u8 olivetti_l1_go252_device::keyboard_data_r()
{
	if (m_kbd_count)
	{
		m_kdc_data = m_kbd_fifo[m_kbd_tail];
		m_kbd_tail = (m_kbd_tail + 1) & 0x0f;
		m_kbd_count--;
	}
	m_kdc_pending = !m_kbd_ident_reply && m_kbd_irq_mode && (m_kbd_count != 0);
	if (!m_kbd_count)
		m_kbd_ident_reply = false;
	update_vi();
	return m_kdc_data;
}


u8 olivetti_l1_go252_device::io_r(offs_t offset)
{
	switch (offset & 0xfe)
	{
	case 0x00:
		if (m_kbd_count)
			m_kdc_data_armed = true;
		// In RX-interrupt mode a normal byte sets RDRF (bit 0). Bit 2 is
		// a status-change event: BCOS IKYB treats it as a keyboard reset,
		// so asserting it for each byte restarts the initialization handshake.
		// Keep the IRQ indication (bit 7) with RDRF until data is consumed;
		// 1KYB otherwise mistakes the received event for transmit completion.
		if (m_kbd_count && BIT(m_kdc_ctrl, 7))
			return 0x83;
		return 0x02 | (m_kbd_count ? (m_kbd_poll_status ? 0x01 : (m_kbd_irq_mode ? 0x04 : 0x01)) : 0x00);

	case 0x02:
		if (m_kdc_data_armed && m_kbd_count)
		{
			m_kdc_data_armed = false;
			return keyboard_data_r();
		}
		return m_kdc_data;

	case 0x42:
		return m_crtc->register_r();

	case 0x80:
		// Bit 3 is the live video/retrace signal.  Firmware polls for an
		// actual transition; reading the status register has no side effect.
		return m_screen->vblank() ? 0x08 : 0x00;

	case 0xfe:
		return 0xfe;

	default:
		return 0xff;
	}
}


void olivetti_l1_go252_device::io_w(offs_t offset, u8 data)
{
	switch (offset & 0xfe)
	{
	case 0x00:
		m_kdc_ctrl = data;
		m_kdc_data_armed = false;
		// Software drives this register with MC6850 control words; 03 is the
		// ACIA master reset, which empties the receiver.  FE#I issues it
		// before enabling receive, and 1KYB then takes the next byte as a
		// status reply: a reply left unread by an earlier requester (the HD
		// boot stage sends command 02 and never reads FB/config) must not
		// survive it.  The keyboard itself is not reset, so before command 00
		// its start-up FC reappears on the next announce tick.
		if ((data & 0x03) == 0x03)
		{
			m_kbd_head = 0;
			m_kbd_tail = 0;
			m_kbd_count = 0;
			m_kdc_pending = false;
			m_kbd_ident_reply = false;
			m_kbd_poll_status = false;
		}
		// Data can have been queued while receive interrupts were disabled (most
		// notably the firmware's startup FC).  Enabling RX must expose it now.
		if (!m_kbd_ident_reply && m_kbd_count && (BIT(m_kdc_ctrl, 7) || m_kbd_irq_mode))
			m_kdc_pending = true;
		update_vi();
		break;

	case 0x02:
	{
		m_kdc_data = data;
		m_kdc_data_armed = false;
		// The recovered 8049 firmware accepts independent commands 00-10.
		// Command 00 completes its startup handshake, 01 reports the ROM/RAM
		// self-test result, and 02 reports FB followed by the sampled keyboard
		// configuration. Forward indicator commands to the keyboard outputs;
		// MCU-local scan modes and beeper timing remain unimplemented.
		m_keyboard->command_w(data);
		//
		// Gardini talks to the keyboard before command 00 and polls status bit 0;
		// the diagnostic and resident drivers issue 00 and use keyboard VI.
		if (data == 0x00)
		{
			m_kbd_irq_mode = true;
			m_kbd_poll_status = false;
			m_kbd_boot_timer->adjust(attotime::never);
			// Firmware reports non-default contacts after startup, and later
			// sends another FD/status pair whenever a key switch changes.
			m_keyboard->report_key_switches(true);
		}
		else if (data == 0x01 || data == 0x02)
		{
			bool const polled = !m_kbd_irq_mode;
			m_kbd_ident_reply = polled;
			m_kbd_poll_status = polled;
			if (data == 0x01)
			{
				kdc_queue_internal(0xfa, !polled);
			}
			else
			{
				kdc_queue_internal(0xfb, !polled);
				kdc_queue_internal(0xf1, !polled);
			}
		}
		update_vi();
		break;
	}

	case 0x20:
		m_kdc_vector = data;
		break;

	case 0x40:
		m_crtc_index = data & 0x1f;
		m_crtc->address_w(data);
		break;

	case 0x42:
		if (m_crtc_index == 0x09)
			m_crtc_max_ras = data & 0x1f;
		m_crtc->register_w(data);
		break;

	case 0x6a:
		break;
	}
}


void olivetti_l1_go252_device::update_vi()
{
	vi_w((m_kdc_pending && BIT(m_kdc_ctrl, 7)) || BIT(m_kdc_ctrl, 5));
}


u16 olivetti_l1_go252_device::viack_r()
{
	if (m_kdc_pending && BIT(m_kdc_ctrl, 7))
		m_kdc_pending = false;
	update_vi();
	return m_kdc_vector;
}


void olivetti_l1_go252_device::palette_init(palette_device &palette)
{
	palette.set_pen_color(0, rgb_t::black());
	palette.set_pen_color(1, rgb_t(0xc0, 0xc0, 0xc0));
	palette.set_pen_color(2, rgb_t(0xff, 0xff, 0xff));
}


MC6845_UPDATE_ROW(olivetti_l1_go252_device::crtc_update_row)
{
	// Attribute bits are verified by the CRTAN5 GO252 diagnostic.
	static constexpr int ATTR_HIGH_LINE  = 0;
	static constexpr int ATTR_LOW_LINE   = 1;
	static constexpr int ATTR_LEFT_LINE  = 2;
	static constexpr int ATTR_RIGHT_LINE = 3;
	static constexpr int ATTR_BLINK      = 4;
	static constexpr int ATTR_HILIGHT    = 5;
	static constexpr int ATTR_REVERSE    = 6;

	u32 *p = &bitmap.pix(y);
	rgb_t const *const pal = m_palette->palette()->entry_list_raw();
	bool const blink_off = BIT(m_screen->frame_number(), 4);
	for (int col = 0; col < x_count; col++)
	{
		u16 const cell = (ma + col) << 1;
		u8 const attr = m_vram[cell & 0x0fff];
		u8 const ch = m_vram[(cell + 1) & 0x0fff];
		u8 bits = (ra < 16) ? u8(~m_chargen[(ch << 4) | ra]) : 0;

		if (BIT(attr, ATTR_HIGH_LINE) && ra == 0)             bits = 0xff;
		if (BIT(attr, ATTR_LOW_LINE) && ra == m_crtc_max_ras) bits = 0xff;
		if (BIT(attr, ATTR_LEFT_LINE))                         bits |= 0x80;
		if (BIT(attr, ATTR_RIGHT_LINE))                        bits |= 0x01;
		if (BIT(attr, ATTR_BLINK) && blink_off)                bits = 0;
		if (BIT(attr, ATTR_REVERSE))                           bits ^= 0xff;
		if (col == cursor_x)                                   bits ^= 0xff;

		u8 const fg = BIT(attr, ATTR_HILIGHT) ? 2 : 1;
		for (int bit = 0; bit < 8; bit++)
			*p++ = pal[BIT(bits, 7 - bit) ? fg : 0];
	}
}


DEFINE_DEVICE_TYPE(OLIVETTI_L1_GO252, olivetti_l1_go252_device, "olivetti_l1_go252", "Olivetti GO252 video/keyboard governo")
