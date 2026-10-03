// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia

// Development-only P6066 CPU bring-up configuration.
// The merged reference CAROM is verified as an analysis input, but physical
// chip mapping/revision and installed RAM population remain unresolved.
// Removable memory, ROMCA, GOINO/CONDY, FLODI, video and DIFO/RODMA cards.
// HDU uses sector-level media and approximate mechanical timing. Serial is absent.

#include "emu.h"
#include "cpu/puce/puce.h"
#include "bus/p6066/p6066.h"
#include "bus/p6066/memory.h"
#include "bus/p6066/flodi.h"
#include "bus/p6066/goino.h"
#include "bus/p6066/go011.h"
#include "bus/p6066/rodma.h"
#include "bus/p6066/difo.h"
#include "p6066.lh"
#include "p6066_printer.lh"
#include "p6066_video.lh"
#include "p6066_video_printer.lh"
#include "emuopts.h"
#include "render.h"
#include "rendlay.h"

namespace {
class p6066_state : public driver_device
{
public:
	p6066_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig,type,tag), m_maincpu(*this,"maincpu"), m_bus(*this,"bus"), m_console(*this,"bus:console:goino"), m_floppy(*this,"bus:floppy:flodi"), m_hdu(*this,"bus:hdu:difo"), m_activity(*this,"disk_activity%u",0U) { }
	void p6066(machine_config &config);
	INPUT_CHANGED_MEMBER(restart) { if (newval) machine().schedule_soft_reset(); }
	// Layout groups are flattened by MAME. Give each of their items a live
	// bounds callback so the lower panel can keep its height-based pixel scale
	// while the window (and its side padding) changes width.
	struct responsive_item
	{
		render_target &target;
		render_bounds original;
		bool console;
		bool screen;
		float aspect;
		float panel_width;

		render_bounds bounds() const
		{
			if (!target.width() || !target.height())
				return original;
			float const fit = float(target.height()) * aspect / float(target.width());
			render_bounds result = original;
			if (console)
			{
				// A console is 89.6 units wide in either view, regardless of
				// the number of output panes above it.
				float const scale = std::min(fit, 1.0f / panel_width);
				result.x0 = 0.5f + (original.x0 - 0.5f) * scale;
				result.x1 = 0.5f + (original.x1 - 0.5f) * scale;
				if (scale < fit) // window too narrow to fit the panel
				{
					float const shrink = scale / fit;
					result.y0 = 1.0f - (1.0f - original.y0) * shrink;
					result.y1 = 1.0f - (1.0f - original.y1) * shrink;
				}
			}
			else if (screen)
			{
				// Scale the upper panes uniformly to the available width or
				// height, and center them in their row when width-limited.
				float const scale = std::min(1.0f, fit);
				float const height_scale = std::min(1.0f, 1.0f / fit);
				result.x0 = 0.5f + (original.x0 - 0.5f) * scale;
				result.x1 = 0.5f + (original.x1 - 0.5f) * scale;
				float constexpr top_height = 73.2142857f / 110.2f;
				result.y0 = (top_height * (1.0f - height_scale) / 2.0f) + original.y0 * height_scale;
				result.y1 = (top_height * (1.0f - height_scale) / 2.0f) + original.y1 * height_scale;
			}
			return result;
		}
	};

	void install_responsive_view(render_target &target)
	{
		std::string_view const name = target.current_view().name();
		if (name != "Video, Printer and Console" && name != "Video and Console" && name != "Printer and Console")
			return;
		// The target is the host window, not MAME's separate snapshot target.
		target.set_keepaspect(false); // our callbacks fit the individual panes
		target.set_scale_mode(SCALE_FRACTIONAL);
		target.set_dynamic_interactive_bounds(true);
		unsigned const index = target.view();
		if (!m_responsive_views.insert(index).second)
			return;
		auto &view = target.current_view();
		for (auto &item : view.items())
		{
			render_bounds const b = item.bounds();
			bool const lower = b.y0 >= 75.0f / 110.2f - 0.0001f;
			if (!lower && !item.screen())
				continue;
			auto responsive = std::make_unique<responsive_item>(responsive_item{
					target, b, lower, bool(item.screen()), view.effective_aspect(),
					89.6f / (name == "Video, Printer and Console" ? 200.0f : 100.0f) });
			item.set_bounds_callback(layout_view_item::bounds_delegate(&responsive_item::bounds, responsive.get()));
			m_responsive_items.push_back(std::move(responsive));
		}
	}
	INPUT_CHANGED_MEMBER(select_output_view)
	{
		if (!newval) return;
		render_target *const target = machine().render().target_by_index(0);
		if (!target) return;
		// Keep the user's chosen height when changing views. The responsive
		// layout fits the paper/video row and the console independently.
		std::string_view const old_view = target->current_view().name();
		bool const was_both = old_view == "Video, Printer and Console";
		bool const was_single = old_view == "Video and Console" || old_view == "Printer and Console";
		int reference_width = 1278;
		int height = 704;
		if ((was_both || was_single) && target->width() && target->height())
		{
			reference_width = was_both ? target->width() : target->width() * 2;
			height = std::clamp(int(target->height()), 200, 32767);
			reference_width = std::clamp(reference_width, 200, 32767);
		}
		// Keep the console and host control strip on screen when switching
		// outputs. The bare "Video"/"Printer" views intentionally lack the
		// console; "Video" can also match MAME's auto-generated screen view.
		const char *const name = param == 1 ? "Video and Console" : param == 2 ? "Printer and Console" : "Video, Printer and Console";
		for (unsigned index = 0; const char *const view = target->view_name(index); ++index)
			if (name == std::string_view(view))
			{
				if (old_view == name)
					return;
				target->set_view(index);
				install_responsive_view(*target);
				// VIDEO and PRINTER occupy the same single-pane footprint.
				// Do not resize when switching between them; only change the
				// window on a one-pane/two-pane transition.
				if (!was_single || param == 3)
					target->request_window_size(param == 3 ? reference_width : reference_width / 2,
							height, reference_width);
				m_initial_window_sized = true;
				return;
			}
	}
private:
	virtual void machine_start() override
	{
		save_item(NAME(m_activity_hold)); save_item(NAME(m_activity_state));
		machine().save().register_postload(save_prepost_delegate(FUNC(p6066_state::activity_outputs),this));
		timer_alloc(FUNC(p6066_state::activity_tick),this)->adjust(attotime::zero,0,attotime::from_msec(1));
	}
	virtual void machine_reset() override { m_activity_hold.fill(0); m_activity_state.fill(0); activity_outputs(); }
	void activity_outputs() { for (unsigned i=0;i<4;++i) m_activity[i]=m_activity_state[i]; }
	TIMER_CALLBACK_MEMBER(activity_tick)
	{
		// One-time sizing after the OSD target exists. The two-output layout
		// has twice the width of either single-output view at the same height.
		if (!m_initial_window_sized)
			if (render_target *const target = machine().render().target_by_index(0))
			{
				std::string_view const view = target->current_view().name();
				if (view == "Video, Printer and Console" || view == "Video and Console" ||
						view == "Printer and Console" || view == "Console and Printer")
				{
					install_responsive_view(*target);
					target->request_window_size(view == "Video, Printer and Console" ? 1278 : 639, 704, 1278);
					m_initial_window_sized = true;
				}
			}
		// Functional panel, not a model of physical drive lamps. Hold for 100 ms.
		for (unsigned i=0;i<4;++i)
		{
			const unsigned state=i<2 ? (m_floppy ? m_floppy->activity(i) : 0) : (m_hdu ? m_hdu->activity(i-2) : 0);
			if (state) { m_activity_state[i]=state; m_activity_hold[i]=100; }
			else if (m_activity_hold[i] && !--m_activity_hold[i]) m_activity_state[i]=0;
		}
		activity_outputs();
	}
	required_device<puce_device> m_maincpu;
	required_device<p6066_bus_device> m_bus;
	optional_device<p6066_goino_device> m_console;
	optional_device<p6066_flodi_device> m_floppy;
	optional_device<p6066_difo_device> m_hdu;
	output_finder<4> m_activity;
	std::array<u8,4> m_activity_hold{},m_activity_state{};
	bool m_initial_window_sized = false;
	std::unordered_set<unsigned> m_responsive_views;
	std::vector<std::unique_ptr<responsive_item>> m_responsive_items;

	void memory_map(address_map &map) { map(0x0000,0xffff).rw(m_bus,FUNC(p6066_bus_device::memory_r),FUNC(p6066_bus_device::memory_w)); }
	u32 screen_update(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect)
	{
		if (m_console) return m_console->screen_update(screen,bitmap,cliprect);
		bitmap.fill(rgb_t(12,16,16),cliprect); return 0;
	}
};
static void console_cards(device_slot_interface &device) { device.option_add("goino",P6066_GOINO); }
static void peripheral_cards(device_slot_interface &device) { device.option_add("flodi",P6066_FLODI); }
static void hdu_cards(device_slot_interface &device)
{
	device.option_add("difo", P6066_DIFO).machine_config([](device_t *card) {
		downcast<p6066_difo_device &>(*card).set_dma_position(0);
	});
}
static void dma_cards(device_slot_interface &device)
{
	device.option_add("rodma", P6066_RODMA).machine_config([](device_t *card) {
		// Development topology: low 32 Kwords shared, upper CPU RAM private.
		// RODMA fig.1.25 documents this partition; not a recovered fitted chassis.
		downcast<p6066_rodma_device &>(*card).set_partition(p6066_dma_arbiter::LOW, 0x8000);
	});
}
static void video_cards(device_slot_interface &device) { device.option_add("go011",P6066_GO011); }
void p6066_state::p6066(machine_config &config)
{
	PUCE(config,m_maincpu,1'000'000); // provisional scheduling clock
	m_maincpu->set_cpu19m(true);
	P6066_BUS(config,m_bus);
	m_maincpu->set_addrmap(AS_PROGRAM,&p6066_state::memory_map);
	m_bus->invalid_cb().set([this](int state) { if (state) m_maincpu->invalid_memory_access(); });
	m_bus->ecorn_output_cb().set_output("ecorn");
	m_maincpu->shared_memory_cb().set(m_bus, FUNC(p6066_bus_device::shared_memory_r));
	m_maincpu->memory_begin_cb().set(m_bus, FUNC(p6066_bus_device::cpu_memory_begin));
	m_maincpu->memory_ready_cb().set(m_bus, FUNC(p6066_bus_device::cpu_memory_ready));
	m_maincpu->memory_data_cb().set(m_bus, FUNC(p6066_bus_device::cpu_memory_data));
	m_maincpu->phase_cb().set(m_bus, FUNC(p6066_bus_device::cpu_phase_w));
	m_maincpu->select_cb().set(m_bus,FUNC(p6066_bus_device::select_w));
	m_maincpu->data_cb().set(m_bus,FUNC(p6066_bus_device::data_w));
	m_maincpu->ecorn_cb().set(m_bus,FUNC(p6066_bus_device::ecorn_w));
	m_maincpu->name_type_cb().set(m_bus,FUNC(p6066_bus_device::name_type_r));
	m_maincpu->input_data_cb().set(m_bus,FUNC(p6066_bus_device::input_data_r));
	m_maincpu->interrupt_sync_cb().set(m_bus,FUNC(p6066_bus_device::interrupt_sync_w));
	m_maincpu->irq_request_cb().set(m_bus,FUNC(p6066_bus_device::irq_r));
	m_maincpu->irq_ack_cb().set(m_bus,FUNC(p6066_bus_device::irq_ack_w));
	m_maincpu->irq_end_cb().set(m_bus,FUNC(p6066_bus_device::irq_end_w));
	m_maincpu->command_cb().set(m_bus,FUNC(p6066_bus_device::command_w));
	m_maincpu->strobe_cb().set(m_bus,FUNC(p6066_bus_device::strobe_w));
	m_maincpu->control_cb().set(m_bus,FUNC(p6066_bus_device::control_w));
	m_maincpu->service_console_cb().set_output("suce2_data");
	m_maincpu->stopped_cb().set_output("cpu_stopped");
	m_maincpu->set_hold_on_unsupported(true);
	// Development population, not a claim about the final chassis connector numbers.
	for (unsigned i=0;i<4;++i)
	{
		auto &slot=P6066_SLOT(config,util::string_format("bus:ram%u",i).c_str(),p6066_memory_cards,"ram16");
		slot.set_position(i); slot.set_base(i*0x2000);
	}
	auto &rom=P6066_SLOT(config,"bus:rom",p6066_memory_cards,"romca"); rom.set_position(4); rom.set_base(0x8000);
	auto &console=P6066_SLOT(config,"bus:console",console_cards,"goino"); console.set_position(5);
	auto &floppy=P6066_SLOT(config,"bus:floppy",peripheral_cards,"flodi"); floppy.set_position(6);
	// ME006 pp.2.01-2.02: microprogram storage in the CPU zone; one
	// excluded 2-Kword bank leaves the merged CAROM at 8000-87FF.
	auto &microprogram=P6066_SLOT(config,"bus:microcode",p6066_microprogram_cards,"me006"); microprogram.set_position(7);
	// STAC-2 printed31: non-DMA video has minimum expansion priority.
	// Development index8 is not a recovered physical connector number.
	auto &video=P6066_SLOT(config,"bus:video",video_cards,nullptr); video.set_position(8);
	auto &dma=P6066_SLOT(config,"bus:dma",dma_cards,nullptr); dma.set_position(9);
	// DIFO assembly consumes development position10 plus reserved position11.
	// Historical physical connectors are DIFO1=03,DIFO2=02, not these indices.
	auto &hdu=P6066_SLOT(config,"bus:hdu",hdu_cards,nullptr); hdu.set_position(10);
	screen_device &screen(SCREEN(config,"screen"));
	screen.set_refresh_hz(60); screen.set_size(888,28); screen.set_visarea_full();
	screen.set_screen_update(FUNC(p6066_state::screen_update));
	const auto *video_option = config.options().find_slot_option("bus:video");
	const auto *printer_option = config.options().find_slot_option("bus:console:goino:options");
	if (printer_option && printer_option->value() == "pr6610" && video_option && video_option->value() == "go011")
		config.set_default_layout(layout_p6066_video_printer);
	else if (printer_option && printer_option->value() == "pr6610")
		config.set_default_layout(layout_p6066_printer);
	else if (video_option && video_option->value() == "go011")
		config.set_default_layout(layout_p6066_video);
	else
		config.set_default_layout(layout_p6066);
}

static INPUT_PORTS_START(p6066)
	PORT_START("PANEL")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("Restart machine") PORT_CODE(KEYCODE_ESC) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(p6066_state::restart), 0)
	// Host display selection only; these buttons are absent from single-output layouts.
	PORT_START("OUTPUT_VIEW")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("Show video") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(p6066_state::select_output_view), 1)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("Show printer") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(p6066_state::select_output_view), 2)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("Show video and printer") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(p6066_state::select_output_view), 3)
INPUT_PORTS_END

ROM_START(p6066)
	ROM_REGION16_BE(0x1000, "carom", 0)
	// Merged reference from dthierbach/olivetti-p6060; not physical chip dumps.
	ROM_LOAD("carom.bin", 0, 0x1000, CRC(9caca305) SHA1(63a68b4787f33bef156f05c6b50269357d0d3c2f))
ROM_END

} // anonymous namespace

COMP(19??, p6066, 0, 0, p6066, p6066, p6066_state, empty_init, "Olivetti", "P6066 (CPU bring-up)", MACHINE_NOT_WORKING)
