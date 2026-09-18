// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia

// Development-only P6066 CPU bring-up configuration.
// The merged reference CAROM is verified as an analysis input, but physical
// chip mapping/revision and installed RAM population remain unresolved.
// Removable memory, ROMCA, GOINO/CONDY and partial FLODI cards.
// DMA and serial boards are not implemented.

#include "emu.h"
#include "cpu/puce/puce.h"
#include "bus/p6066/p6066.h"
#include "bus/p6066/memory.h"
#include "bus/p6066/flodi.h"
#include "bus/p6066/goino.h"
#include "p6066.lh"

namespace {
class p6066_state : public driver_device
{
public:
	p6066_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig,type,tag), m_maincpu(*this,"maincpu"), m_bus(*this,"bus"), m_console(*this,"bus:console:goino") { }
	void p6066(machine_config &config);
	INPUT_CHANGED_MEMBER(restart) { if (newval) machine().schedule_soft_reset(); }
private:
	required_device<puce_device> m_maincpu;
	required_device<p6066_bus_device> m_bus;
	optional_device<p6066_goino_device> m_console;
	void memory_map(address_map &map) { map(0x0000,0xffff).rw(m_bus,FUNC(p6066_bus_device::memory_r),FUNC(p6066_bus_device::memory_w)); }
	u32 screen_update(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect)
	{
		if (m_console) return m_console->screen_update(screen,bitmap,cliprect);
		bitmap.fill(rgb_t(12,16,16),cliprect); return 0;
	}
};
static void console_cards(device_slot_interface &device) { device.option_add("goino",P6066_GOINO); }
static void peripheral_cards(device_slot_interface &device) { device.option_add("flodi",P6066_FLODI); }
void p6066_state::p6066(machine_config &config)
{
	PUCE(config,m_maincpu,1'000'000); // provisional scheduling clock
	m_maincpu->set_cpu19m(true);
	P6066_BUS(config,m_bus);
	m_maincpu->set_addrmap(AS_PROGRAM,&p6066_state::memory_map);
	m_bus->invalid_cb().set([this](int state) { if (state) m_maincpu->invalid_memory_access(); });
	m_bus->ecorn_output_cb().set_output("ecorn");
	m_maincpu->select_cb().set(m_bus,FUNC(p6066_bus_device::select_w));
	m_maincpu->data_cb().set(m_bus,FUNC(p6066_bus_device::data_w));
	m_maincpu->ecorn_cb().set(m_bus,FUNC(p6066_bus_device::ecorn_w));
	m_maincpu->name_type_cb().set(m_bus,FUNC(p6066_bus_device::name_type_r));
	m_maincpu->input_data_cb().set(m_bus,FUNC(p6066_bus_device::input_data_r));
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
	screen_device &screen(SCREEN(config,"screen"));
	screen.set_refresh_hz(60); screen.set_size(888,28); screen.set_visarea_full();
	screen.set_screen_update(FUNC(p6066_state::screen_update));
	config.set_default_layout(layout_p6066);
}

static INPUT_PORTS_START(p6066)
	PORT_START("PANEL")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("Restart machine") PORT_CODE(KEYCODE_F3) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(p6066_state::restart), 0)
INPUT_PORTS_END

ROM_START(p6066)
	ROM_REGION16_BE(0x1000, "carom", 0)
	// Merged reference from dthierbach/olivetti-p6060; not physical chip dumps.
	ROM_LOAD("carom.bin", 0, 0x1000, CRC(9caca305) SHA1(63a68b4787f33bef156f05c6b50269357d0d3c2f))
ROM_END

} // anonymous namespace

COMP(19??, p6066, 0, 0, p6066, p6066, p6066_state, empty_init, "Olivetti", "P6066 (CPU bring-up)", MACHINE_NOT_WORKING | MACHINE_NO_SOUND)
