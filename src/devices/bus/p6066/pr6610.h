// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
#ifndef MAME_MACHINE_P6066_PR6610_H
#define MAME_MACHINE_P6066_PR6610_H
#pragma once

#include "goino.h"
#include "machine/bitmap_printer.h"

// Olivetti PR 6610 integrated thermal printer (P6066 ASTAM board).
// Renders the GOINO column-transfer output on a bitmap roll instead of
// discarding it. The handshake itself stays in p6066_goino_state; this
// device observes the resulting column/feed events and models the paper.
// See docs/p6066/pr6610.md for evidence and functional abstractions.
class p6066_pr6610_device : public p6066_discard_printer_device
{
public:
	p6066_pr6610_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock = 0);

	virtual void tick(p6066_goino_state &state) override;

protected:
	virtual void device_add_mconfig(machine_config &config) override ATTR_COLD;
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset_after_children() override;

private:
	// Rendering geometry: 210 dpi both axes. Column and dot pitch are
	// 1/70" (three pixels, the 7x7 matrix in a 1/10" character cell).
	// One paper-feed step is 1/60" = 3.5 pixels, tracked in half-pixels
	// so the 1/6" interline (35 px) and 1/10" plotter feed (21 px) are
	// exact after their 10 and 6 steps.
	static constexpr int PAPER_WIDTH  = 1736; // 210 mm roll
	static constexpr int PAPER_HEIGHT = 630;  // 3 inch ring; continuous feed retires rows to PNG pages
	static constexpr int HDPI = 210, VDPI = 210;
	static constexpr int LEFT_MARGIN   = 28;
	static constexpr int COLUMN_PITCH  = 3;   // 1/70" per matrix column
	static constexpr int DOT_ROWS      = 7;   // resistive elements
	static constexpr int FEED_HALF     = 7;   // 1/60" step in half-pixels

	void render_column(u8 column);
	void advance_feed();
	void resync_counters();

	required_device<bitmap_printer_device> m_bitmap;
	output_finder<> m_columns_rendered, m_feed_rows, m_head_x, m_head_y;

	std::uint32_t m_columns_cached = 0, m_feed_events_cached = 0, m_feed_rows_total = 0;
	unsigned m_feed_half = 0;
	bool m_resync = true; // first tick after reset/load only resynchronises counters
};
DECLARE_DEVICE_TYPE(P6066_PR6610, p6066_pr6610_device)
#endif
