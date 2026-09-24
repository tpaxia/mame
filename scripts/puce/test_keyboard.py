#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders: Salvatore Paxia
"""Exercise production keyboard callbacks with independent host stimuli."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
src=(root/'src/devices/bus/p6066/keyboard.cpp').read_text()
methods=src[src.index('void p6066_keyboard_device::emit'):].replace('TIMER_CALLBACK_MEMBER(p6066_keyboard_device::scan)','void p6066_keyboard_device::scan()')
source=r'''
#include <array>
#include <cassert>
#include <cstdio>
#include "keyboard_codes.h"
#include "goino_state.h"
using u32=unsigned;
#define BIT(x,n) (((x)>>(n))&1)
struct port { unsigned value=0; port *operator->(){return this;} unsigned read(){return value;} };
struct p6066_keyboard_device {
 std::array<port,3> m_keys;port m_modifiers;
 std::array<u32,3> m_previous{};bool m_down_chord=false;
 int m_repeat_key=-1;unsigned m_repeat_ticks=0;bool m_ready=false;
 bool error=false,down=false;unsigned last=0,emitted=0;
 void m_data_cb(unsigned v){last=v;++emitted;}void m_ready_cb(int){}
 void m_error_cb(int v){error=v;}void m_down_cb(int v){down=v;}void m_mode_cb(int){}
 void logerror(const char*,...){}
 void emit(unsigned,unsigned);void scan();
 void key(unsigned k,bool v){if(v)m_keys[k/32].value|=1U<<(k%32);else m_keys[k/32].value&=~(1U<<(k%32));}
 void ack(){m_ready=false;}
};
'''+methods+r'''
int main(){
 // NORMAL-mode must preserve every nonalphabetic code.
 for(unsigned key=26;key<91;++key)for(unsigned m=0;m<4;++m){
  if(key==53)continue;
  p6066_goino_state g;g.basic_mode=false;g.keyboard_code=p6066_keys[key].code[m];
  assert(g.key_data()==(p6066_keys[key].code[m]&255));
 }
 p6066_keyboard_device k;
 k.m_modifiers.value=1;k.key(55,true);k.scan();assert((k.last&255)==0x87 && k.m_ready);
 k.ack();k.scan();assert(k.emitted==1);k.key(55,false);k.scan();
 for(unsigned mods:{0U,1U,2U,4U,5U,6U}){
  k.m_modifiers.value=mods;k.key(0,true);k.scan();
  assert(k.last==((mods&4)?0x101:(mods&3)?0x141:0x161));k.ack();k.key(0,false);k.scan();
 }
 const unsigned host[]={82,54,55,83,39,50,51,53,52};
 const unsigned want[]={0xf0,0xf4,0xf6,0xf1,0xfa,0xe3,0xe7,0xe0,0xe4};
 for(unsigned i=0;i<9;++i){
  k.m_modifiers.value=32;k.key(host[i],true);unsigned n=k.emitted;k.scan();
  assert(k.emitted==n+1 && (k.last&255)==want[i] && !k.down);k.ack();
  for(int t=0;t<150;++t)k.scan();assert(k.emitted==n+1);
  k.m_modifiers.value=0;k.scan();assert(!k.down);
  k.key(host[i],false);k.scan();
 }
 k.m_modifiers.value=0;k.key(49,true);k.scan();assert((k.last&255)==0x8e);k.ack();k.key(49,false);k.scan();
 k.key(73,true);k.scan();assert((k.last&255)==0xfe);k.ack();k.key(73,false);k.scan();
 unsigned n=k.emitted;k.key(53,true);k.scan();assert(k.down && k.emitted==n);k.key(53,false);k.scan();assert(!k.down);
 k.m_modifiers.value=8;k.scan();assert(k.emitted==n);
 k.key(1,true);k.scan();k.ack();n=k.emitted;
 for(int t=0;t<69;++t)k.scan();assert(k.emitted==n);k.scan();assert(k.emitted==n+1);k.ack();
 k.key(1,false);k.scan();k.m_modifiers.value=0;
 // Alt alone and Alt+ordinary letter must not trigger repeat.
 k.m_modifiers.value=32;n=k.emitted;k.scan();assert(k.emitted==n);
 k.key(0,true);k.scan();k.ack();n=k.emitted;
 for(int t=0;t<150;++t)k.scan();assert(k.emitted==n);
 k.key(0,false);k.scan();
 // Repeat+Delete is ordinary Delete, not the Alt+Delete command.
 k.m_modifiers.value=8;k.key(54,true);k.scan();assert((k.last&255)==(p6066_keys[54].code[0]&255));k.ack();n=k.emitted;
 for(int t=0;t<70;++t)k.scan();assert(k.emitted==n+1);k.ack();k.key(54,false);k.scan();
 k.m_modifiers.value=0;k.key(2,true);k.scan();k.key(3,true);k.scan();assert(k.error);
 p6066_keyboard_device s;s.key(0,true);s.key(1,true);s.scan();assert(s.error && !s.emitted);
 std::puts("PASS: production keyboard modifiers, nine chords, EOL, repeat, TASB and error paths");
}
'''
# Verify that host bindings and clickable panel targets match the agreement.
import re,xml.etree.ElementTree as ET
ports=(root/'src/devices/bus/p6066/keyboard_ports.inc').read_text()
bindings={m[0]:m[1] for m in re.findall(r'PORT_NAME\("([^"]+)"\) PORT_CODE\(KEYCODE_([A-Z0-9_]+)\)',ports)}
for c in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789': assert bindings[c]==c
for i in range(1,9): assert bindings[f'F{i}']==f'F{i}'
for i in range(10): assert bindings[f'Keypad {i}']==f'{i}_PAD'
for name,host in {'Clear Recall':'INSERT','Char delete':'DEL','At':'EQUALS',
 'Colon':'QUOTE','Semicolon':'COLON','Backslash':'BACKSLASH','Up character':'TILDE',
 'End of line':'ENTER','Keypad end of line':'ENTER_PAD','KB Mode':'F9',
 'Repeat':'LWIN','Chord Prefix':'LALT','Left Shift':'LSHIFT','Right Shift':'RSHIFT'}.items():
 assert bindings[name]==host
assert 'KEYCODE_CAPSLOCK' not in ports
board=(root/'src/devices/bus/p6066/goino.cpp').read_text()
assert not re.search(r'KEYCODE_[1-7]_PAD|KEYCODE_CAPSLOCK',board)
assert 'PORT_CODE(KEYCODE_ESC)' in (root/'src/mame/olivetti/p6066.cpp').read_text()
layout=ET.parse(root/'src/mame/layout/p6066.lay')
assert {int(e.attrib['inputmask'],0) for e in layout.findall('.//element') if e.get('inputtag')=='bus:console:goino:BUTTONS'}=={2,4,8,16,32,64,128}
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'test.cpp').write_text(source)
 subprocess.run(['c++','-std=c++17','-I',str(root/'src/devices/bus/p6066'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
