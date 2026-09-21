// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
#include "emu.h"
#include "keyboard.h"

DEFINE_DEVICE_TYPE(P6066_KEYBOARD, p6066_keyboard_device, "p6066_keyboard", "Olivetti P6066 keyboard")
p6066_keyboard_device::p6066_keyboard_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
 : device_t(mconfig,P6066_KEYBOARD,tag,owner,clock), m_keys(*this,"KEYS%u",0U), m_modifiers(*this,"MODIFIERS"),
 m_data_cb(*this), m_ready_cb(*this), m_error_cb(*this), m_down_cb(*this), m_mode_cb(*this) { }
static INPUT_PORTS_START(keyboard)
#include "keyboard_ports.inc"
INPUT_PORTS_END
ioport_constructor p6066_keyboard_device::device_input_ports() const { return INPUT_PORTS_NAME(keyboard); }
void p6066_keyboard_device::device_start()
{
 m_scan=timer_alloc(FUNC(p6066_keyboard_device::scan),this);
 save_item(NAME(m_previous)); save_item(NAME(m_down_chord));
 save_item(NAME(m_repeat_key)); save_item(NAME(m_repeat_ticks)); save_item(NAME(m_ready));
}
void p6066_keyboard_device::device_reset()
{
 m_previous.fill(0); m_down_chord=false; m_repeat_key=-1; m_repeat_ticks=0; m_ready=false;
 m_ready_cb(0); m_error_cb(0); m_down_cb(0); m_mode_cb(0);
 // TM601 fig.6: ten columns at 100 us. Scan host state once per full matrix.
 m_scan->adjust(attotime::from_msec(1),0,attotime::from_msec(1));
}
void p6066_keyboard_device::emit(unsigned key, unsigned modifiers)
{
 const unsigned code=p6066_keys[key].code[((modifiers&3)?1:0)|((modifiers&4)?2:0)];
 if (m_ready) m_error_cb(1); // TASTIERA p.11: CPU failed to acknowledge old code.
 m_data_cb(code); m_ready=true; m_ready_cb(1);
 logerror("Keyboard key=%s TAS=%03X modifiers=%02X\n",p6066_keys[key].name,code,modifiers);
}
TIMER_CALLBACK_MEMBER(p6066_keyboard_device::scan)
{
 const unsigned mods=m_modifiers->read();
 std::array<u32,3> now; for(unsigned i=0;i<3;++i) now[i]=m_keys[i]->read();
 auto held=[&now](unsigned key) { return BIT(now[key/32],key%32); };
 auto chord=[](unsigned key)->int { switch(key) {
#include "keyboard_chords.inc"
 default:return -1;
 } };
 const bool prefix_held=BIT(mods,5);
 // Down is TASB, not a character. Chord prefix+Down instead selects keypad '='.
 if (!held(53)) m_down_chord=false;
 else if (!BIT(m_previous[53/32],53%32)) m_down_chord=prefix_held;
 m_down_cb(held(53) && !m_down_chord);
 m_mode_cb(BIT(mods,4));
 unsigned count=0, candidate=0;
 bool is_chord=false;
 for(unsigned key=0;key<84;++key)
 {
  if(!held(key) || BIT(m_previous[key/32],key%32)) continue;
  const int alternate=prefix_held?chord(key):-1;
  if(alternate>=0) { candidate=alternate; is_chord=true; ++count; }
  else if(key<82 && key!=53) { candidate=key; is_chord=false; ++count; }
 }
 if(count>1) m_error_cb(1); // Simultaneous new code keys; no arbitrary priority.
 else if(count==1)
 {
  emit(candidate,mods);
  m_repeat_key=is_chord?-1:int(candidate); m_repeat_ticks=0;
 }
 if(m_repeat_key>=0)
 {
  if(!held(m_repeat_key)) { m_repeat_key=-1; m_repeat_ticks=0; }
  else if(!BIT(mods,3) || prefix_held) m_repeat_ticks=0;
  else if(count==0 && ++m_repeat_ticks>=70) { emit(m_repeat_key,mods); m_repeat_ticks=0; }
 }
 m_previous=now;
}
