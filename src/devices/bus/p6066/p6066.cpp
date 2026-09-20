// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
#include "emu.h"
#include "p6066.h"
#include <cstdlib>
DEFINE_DEVICE_TYPE(P6066_BUS, p6066_bus_device, "p6066_bus", "Olivetti P6066 backplane")
DEFINE_DEVICE_TYPE(P6066_SLOT, p6066_slot_device, "p6066_slot", "Olivetti P6066 board slot")
device_p6066_card_interface::device_p6066_card_interface(const machine_config &mconfig, device_t &device)
	: device_interface(device, "p6066card") { }
void device_p6066_card_interface::interface_pre_start() { if (!m_bus) throw device_missing_dependencies(); }
p6066_slot_device::p6066_slot_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: device_t(mconfig, P6066_SLOT, tag, owner, clock), device_single_card_slot_interface<device_p6066_card_interface>(mconfig, *this), m_bus(*this, DEVICE_SELF_OWNER) { }
void p6066_slot_device::device_start() { if (auto *card = get_card_device()) m_bus->add_card(m_position, m_base, *card); }
p6066_bus_device::p6066_bus_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: device_t(mconfig, P6066_BUS, tag, owner, clock), m_invalid_cb(*this), m_ecorn_output_cb(*this), m_floppy_select_output(*this, "floppy_selects") { }
void p6066_bus_device::add_card(unsigned position, u16 base, device_p6066_card_interface &card)
{
	if (position >= m_cards.size() || m_cards[position]) fatalerror("P6066: duplicate/invalid slot position %u", position);
	m_cards[position] = &card; card.m_bus = this; card.m_base = base;
}
void p6066_bus_device::device_start()
{
	m_trace_io = std::getenv("P6066_TRACE_IO") != nullptr;
	save_item(NAME(m_irq.owners)); save_item(NAME(m_floppy_selects));
	machine().save().register_postload(save_prepost_delegate(FUNC(p6066_bus_device::update_outputs), this));
}
void p6066_bus_device::update_outputs() { m_floppy_select_output = m_floppy_selects; }
void p6066_bus_device::device_reset() { m_irq.reset(); m_floppy_selects = 0; m_trace_selection = 0; update_outputs(); }
void p6066_bus_device::trace_io(const char *operation, unsigned level, u16 data, u16 mask, device_p6066_card_interface *card)
{
	if (m_trace_io && level == 4)
		logerror("IOBUS t=%s %s op=%s level=%u select=%02X data=%04X mask=%04X responder=%s\n",
			machine().time().as_string(), machine().describe_context(), operation, level,
			m_trace_selection, data, mask, card ? card->device().tag() : "none");
}
device_p6066_card_interface *p6066_bus_device::memory_card(u16 address)
{
	device_p6066_card_interface *owner = nullptr;
	for (auto *card : m_cards) if (card && card->memory_claims(address))
	{
		if (owner) fatalerror("P6066 memory decode collision at %04X: %s and %s", address, owner->device().tag(), card->device().tag());
		owner = card;
	}
	return owner;
}
u16 p6066_bus_device::memory_r(offs_t address, u16 mask)
{
	if (auto *card = memory_card(address)) return card->memory_r(address, mask);
	if (!machine().side_effects_disabled()) { logerror("%s: unclaimed read at word %04X mask %04X\n",machine().describe_context(),address,mask); m_invalid_cb(1); }
	return 0; // Invalid-cycle data is unspecified; INV00 is reported separately.
}
void p6066_bus_device::memory_w(offs_t address, u16 data, u16 mask)
{
	if (auto *card = memory_card(address)) card->memory_w(address, data, mask);
	else if (!machine().side_effects_disabled()) { logerror("%s: unclaimed write at word %04X data %04X mask %04X\n",machine().describe_context(),address,data,mask); m_invalid_cb(1); }
}
void p6066_bus_device::select_w(u8 name)
{
	m_trace_selection = name;
	for (auto *card : m_cards) if (card) card->select(name);
	if (m_trace_io) trace_io("select", 4, name, 0x00ff, channel_card(4));
	if (name == 0xe0) { ++m_floppy_selects; update_outputs(); }
}
device_p6066_card_interface *p6066_bus_device::channel_card(unsigned level)
{
	if (level < 1 || level > 4) fatalerror("P6066 invalid channel level");
	if (level < 4) return m_irq.owners[level] < 0 ? nullptr : m_cards[m_irq.owners[level]];
	device_p6066_card_interface *selected = nullptr;
	for (auto *card : m_cards) if (card && card->direct_selected())
	{
		if (selected) fatalerror("P6066: multiple direct-selection responders");
		selected = card;
	}
	return selected;
}
u16 p6066_bus_device::name_type_r(offs_t level) { auto *card=channel_card(level); const u16 data=card ? card->name_type(level) : 0; trace_io("type",level,data,0xffff,card); return data; }
u8 p6066_bus_device::input_data_r(offs_t level) { auto *card=channel_card(level); const u8 data=card ? card->input_data(level) : 0; trace_io("input",level,data,0x00ff,card); return data; }
void p6066_bus_device::data_w(offs_t level, u16 data, u16 mask) { auto *card=channel_card(level); trace_io("data",level,data,mask,card); if (card) card->output_data_masked(level, data, mask); }
void p6066_bus_device::command_w(offs_t level, u16 data, u16 mask) { auto *card=channel_card(level); trace_io("command",level,data,mask,card); if (card) card->command_word(level, data, mask); }
void p6066_bus_device::strobe_w(u8 level) { if (auto *card=channel_card(level)) card->strobe(level); }
void p6066_bus_device::control_w(offs_t level, u8 signal) { if (auto *card=channel_card(level)) card->control(level, signal); }
void p6066_bus_device::ecorn_w(int state)
{
	m_ecorn_output_cb(state);
	for (auto *card : m_cards) if (card) card->controller_reset(!state);
}

void p6066_bus_device::refresh_requests()
{
	for (unsigned i=0;i<m_cards.size();++i) m_irq.requests[i]=m_cards[i] ? m_cards[i]->irq_requests() : 0;
}
u8 p6066_bus_device::irq_r(offs_t level) { refresh_requests(); return m_irq.next(level); }
void p6066_bus_device::irq_ack_w(u8 source)
{
	refresh_requests();
	const int slot=m_irq.acknowledge(source);
	if (slot<0) fatalerror("P6066 interrupt acknowledgement without available requester");
	m_cards[slot]->irq_ack(source-1);
}
void p6066_bus_device::irq_end_w(u8 level)
{
	const int slot=m_irq.release(level);
	if (slot>=0) m_cards[slot]->irq_end(level);
}

void p6066_bus_device::interrupt_sync_w(u8 mask)
{
	for (auto *card : m_cards) if (card) card->interrupt_sync(mask);
}
