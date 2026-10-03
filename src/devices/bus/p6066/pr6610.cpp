// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia

#include "emu.h"
#include "pr6610.h"
#include "screen.h"

DEFINE_DEVICE_TYPE(P6066_PR6610, p6066_pr6610_device, "p6066_pr6610", "Olivetti PR 6610 thermal printer (functional rendering)")

static INPUT_PORTS_START(pr6610)
	PORT_START("MANUAL_FEED")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("PR 6610 paper feed") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(p6066_pr6610_device::manual_feed), 0)
INPUT_PORTS_END

ioport_constructor p6066_pr6610_device::device_input_ports() const
{
	return INPUT_PORTS_NAME(pr6610);
}

//**************************************************************************
//  PR 6610 thermal printer
//**************************************************************************

// Geometry note: the head prints a 7-element column whose pitch is the
// 1/70" matrix column; the carriage moves while the CPU services column
// requests, so the x advance is functional (one served column = one pitch),
// not the 180-char/s motor timeline. The three leading blank columns of the
// ESE transfer cover the SLINN lead-in before the first printed column.

p6066_pr6610_device::p6066_pr6610_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: p6066_discard_printer_device(mconfig, P6066_PR6610, tag, owner, clock)
	, m_bitmap(*this, "bitmap")
	, m_columns_rendered(*this, "pr6610_columns")
	, m_feed_rows(*this, "pr6610_feed_rows")
	, m_head_x(*this, "pr6610_head_x")
	, m_head_y(*this, "pr6610_head_y")
{
}

void p6066_pr6610_device::device_add_mconfig(machine_config &config)
{
	// The shared printer retains the physical roll and PNG archive. Its
	// built-in screen remains at the default 384 rows; the PR 6610-specific
	// screen below presents a taller window without changing other printers.
	BITMAP_PRINTER(config, m_bitmap, PAPER_WIDTH, PAPER_HEIGHT, HDPI, VDPI);
	m_bitmap->set_continuous_feed(true);
	m_bitmap->set_printhead_size(2 * COLUMN_PITCH, 3 * DOT_ROWS, 1);

	screen_device &paper(SCREEN(config, "paper"));
	paper.set_refresh_hz(60);
	paper.set_size(PAPER_WIDTH, PAPER_SCREEN_HEIGHT);
	paper.set_visarea_full();
	paper.set_screen_update(FUNC(p6066_pr6610_device::screen_update));
}

u32 p6066_pr6610_device::screen_update(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect)
{
	// This is a view of the generic printer's stored roll, not a second
	// paper buffer. The print point stays 50 rows above the viewer's bottom.
	// Ruler and printhead overlays belong to the generic viewer and never
	// enter the roll; this P6066-only screen displays neither by default.
	for (int y = cliprect.min_y; y <= cliprect.max_y; ++y)
	{
		int const paper_row = y - (PAPER_SCREEN_HEIGHT - 50 - m_bitmap->m_ypos);
		for (int x = cliprect.min_x; x <= cliprect.max_x; ++x)
			bitmap.pix(y, x) = m_bitmap->get_pixel(x, paper_row);
	}
	return 0;
}

void p6066_pr6610_device::device_start()
{
	m_resync = true;
	save_item(NAME(m_columns_cached));
	save_item(NAME(m_feed_events_cached));
	save_item(NAME(m_feed_rows_total));
	save_item(NAME(m_feed_half));
	save_item(NAME(m_resync));
	machine().save().register_postload(save_prepost_delegate(FUNC(p6066_pr6610_device::resync_counters), this));
}

void p6066_pr6610_device::device_reset_after_children()
{
	// bitmap_printer_device::device_reset_after_children has positioned
	// the paper at the top margin in roll coordinates; adopt that as the
	// starting line and move the head to the left margin.
	m_resync = true;
	m_columns_cached = m_feed_events_cached = 0;
	m_feed_rows_total = 0;
	m_feed_half = 0;
	m_bitmap->m_xpos = LEFT_MARGIN;
	m_head_x = LEFT_MARGIN;
	m_head_y = m_bitmap->m_ypos;
}

void p6066_pr6610_device::resync_counters()
{
	// A restored run keeps its own printer_columns_discarded/feed history;
	// only the difference from now on is new paper motion.
	m_columns_cached = 0;
	m_feed_events_cached = 0;
	m_feed_rows_total = 0;
	m_resync = true;
}

void p6066_pr6610_device::render_column(u8 column)
{
	// Seven resistive elements at 1/70" pitch heat one matrix column; each
	// dot renders as a 2x2 pixel block. The firmware font and CONDY
	// display use bit 0 at the top; keep that ordering for printed columns.
	// Physical printhead wiring remains unverified.
	const int x = m_bitmap->m_xpos;
	const int y = m_bitmap->m_ypos;
	for (int i = 0; i != 7; ++i)
		if (BIT(column, i))
			for (int dy = 0; dy != 2; ++dy)
				for (int dx = 0; dx != 2; ++dx)
					m_bitmap->draw_pixel(x + dx, y + i * 3 + dy, 0x000000);
}

void p6066_pr6610_device::advance_feed()
{
	// One UMO20 step is 1/60" = 3.5 px at 210 dpi; work in half-pixels so
	// 10 steps make the exact 1/6" interline (35 px) and 6 the 1/10" feed.
	m_feed_half += FEED_HALF;
	const int rows = m_feed_half >> 1;
	m_feed_half &= 1;
	m_bitmap->m_ypos += rows;
	m_bitmap->check_new_page();
	m_feed_rows_total += rows;
}

INPUT_CHANGED_MEMBER(p6066_pr6610_device::manual_feed)
{
	if (!newval) return;
	// Host-operated paper advance: one 1/6" text line, with no ESE transfer,
	// guest command, printhead movement or fabricated printer handshake.
	for (int step = 0; step != 10; ++step)
		advance_feed();
	m_feed_rows = m_feed_rows_total;
	m_head_y = m_bitmap->m_ypos;
}

void p6066_pr6610_device::tick(p6066_goino_state &state)
{
	state.printer_tick();
	if (m_resync)
	{
		m_resync = false;
		if (!state.printer_attached) return;
		m_columns_cached = state.printer_columns_discarded;
		m_feed_events_cached = state.printer_feed_events;
	}

	if (state.printer_columns_discarded != m_columns_cached)
	{
		render_column(state.printer_column);
		m_columns_cached = state.printer_columns_discarded;
		if (m_bitmap->m_xpos < PAPER_WIDTH - COLUMN_PITCH)
			m_bitmap->m_xpos += COLUMN_PITCH;
	}
	if (state.printer_feed_events != m_feed_events_cached)
	{
		advance_feed();
		m_feed_events_cached = state.printer_feed_events;
	}

	// On transfer end, return the carriage even while the interline feed is
	// active. GOINO printed p.17 orders FISTN before FAINN; waiting for
	// feeding to stop can miss the entire return interval when a new
	// transfer starts immediately after FINTN.
	if (!state.printer_running && m_bitmap->m_xpos != LEFT_MARGIN)
	{
		m_bitmap->m_xpos = LEFT_MARGIN;
	}

	m_columns_rendered = state.printer_columns_discarded;
	m_feed_rows = m_feed_rows_total;
	m_head_x = m_bitmap->m_xpos;
	m_head_y = m_bitmap->m_ypos;
}
