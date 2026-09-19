#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders: Salvatore Paxia
"""Manual truth tables and event sequences against live GOINO state."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
SOURCE=r'''
#include "bus/p6066/goino_state.h"
#include <cassert>
#include <cstdio>
int main(){
 unsigned checks=0;
 // Separate source table from encoder implementation: priority order follows
 // Fig.1.2 top priority BASIC,error,PIPPO,buttons,timer,normal,printer.
 const unsigned priority[]={64,32,16,8,4,2,1};
 const unsigned type[]={0x60,0x50,0x40,0x30,0x20,0x10,0};
 // The two keyboard modes cannot be pending together.
 for(unsigned sources=0;sources<128;++sources){
  if((sources&66)==66)continue;
  for(unsigned blocked=0;blocked<2;++blocked)
   for(unsigned sync_mask=0;sync_mask<16;++sync_mask){
    p6066_goino_state s;
    s.matrix_request=sources&1;s.keyboard_request=sources&66;
    s.basic_mode=sources&64;s.timer_request=sources&4;
    s.button_request=sources&8;s.pippo_request=sources&16;
    s.double_key_request=sources&32;s.column_request=true;
    s.interrupts_blocked=blocked;
    assert(s.irq_requests()==0); // async alone must not request service
    s.synchronize(sync_mask);
    unsigned expected_type=0;
    if(sync_mask&8)for(unsigned i=0;i<7;++i)if(sources&priority[i]){expected_type=type[i];break;}
    assert(s.type()==expected_type);
    assert(s.irq_requests()==(((sync_mask&4)?2:0)|((sync_mask&8)&&sources&&!blocked?4:0)));
    ++checks;
   }
 }
 for(unsigned mask=0;mask<256;++mask){
  p6066_goino_state s;s.buttons_w(mask);
  // GOINO fig.1.6 columns CON0N, CON1N, CON2N, transcribed LSB first.
  static constexpr unsigned code[]={0b000,0b001,0b010,0b011,0b100,0b101,0b110,0b111};
  unsigned expected=0;
  for(unsigned bit=0;bit<8;++bit)if(mask&(1<<bit)){expected=(~code[bit])&7;break;}
  assert(s.button_code()==expected && s.button_request==bool(mask));
  s.command(5);assert(!s.button_request);s.buttons_w(mask);assert(!s.button_request);
  s.buttons_w(0);s.buttons_w(mask);assert(s.button_request==bool(mask));++checks;
 }
 for(unsigned mode=0;mode<2;++mode)for(unsigned raw=0;raw<512;++raw){
  p6066_goino_state s;s.basic_mode=mode;s.keyboard_code=raw;
  unsigned result=raw&255;
  // Work in physical pin polarity, independently of the implementation.
  const unsigned tas6n=!(raw&32),tas7n=!(raw&64),tas9n=!(raw&256);
  const unsigned tes6n=mode||tas7n!=tas9n?tas6n:!tas6n;
  result=(result&~32)|(!tes6n<<5);
  assert(s.key_data()==result);++checks;
 }
 p6066_goino_state mode;
 mode.keyboard_request=true;mode.synchronize(8);assert(mode.type()==0x60);
 mode.basic_mode=false;assert(mode.type()==0x10); // ARDIO routes the same MODE0 latch
 mode.basic_mode=true;assert(mode.type()==0x60);
 p6066_goino_state s;s.select(0);s.command(12);s.timer_tick();
 assert(s.timer_request && !s.irq_requests());s.synchronize(8);
 assert(!s.irq_requests());s.command(14);assert(s.irq_requests()==4);
 assert(s.acknowledge(2));assert(!s.acknowledge(2));
 assert(s.enabled(3)&&!s.enabled(1)&&!s.enabled(2));
 // Only the grant admits interrupt-owned output; command clears async only.
 assert(s.data(0x0800,3));assert(!s.timer_request && s.synchronized3==4);
 s.timer_tick();s.synchronize(8);s.end(3);assert(s.irq_requests()==4);
 assert(s.acknowledge(2));s.data(0x0800,3);s.synchronize(8);s.end(3);
 assert(s.irq_requests()==0);
 // Disable does not erase a pending event; RETIN is the separate clear.
 s.timer_tick();s.command(13);s.synchronize(8);assert(s.irq_requests()==4);
 s.command(8);s.timer_tick();s.synchronize(8);assert(s.irq_requests()==0);
 // L2 preempts L3 while retaining separate ownership and command state.
 s.timer_request=true;s.column_request=true;s.synchronize(12);
 assert(s.acknowledge(2));assert(s.acknowledge(1));
 const auto commands=s.commands_seen;s.data(0xffff,2,0x00ff);
 assert(s.printer_column==127 && !s.column_request && s.commands_seen==commands);
 assert(s.synchronized2);s.synchronize(4);s.end(2);
 assert(!s.owned2 && s.owned3 && s.irq_requests()==4);
 // Undefined low ECD lanes propagate validity, rather than fabricated dots.
 s.data(0x2000,3,0xff00);
 s.data(0x4000,3,0xff00);
 assert(s.display_known[0]==0 && !(s.lamp_known_shift&1));
 std::printf("PASS: %u GOINO truth-table cases plus source/sync/owner, timer, reset-command and nested-service sequences\n",checks);
}
'''
with tempfile.TemporaryDirectory(prefix='p6066-goino-events-') as tmp:
    path=Path(tmp)/'test.cpp';path.write_text(SOURCE)
    binary=Path(tmp)/'test'
    subprocess.run(['c++','-std=c++17','-O2','-I',str(ROOT/'src/devices'),str(path),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
