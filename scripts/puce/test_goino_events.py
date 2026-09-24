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
 // Discard-output printer: one line, three leading blanks, two matrices,
 // then ten feed events. Real synchronization/ownership and command paths.
 {
  p6066_goino_state absent;absent.select(0);absent.data(0x0e00,4);
  absent.data(0xff00,4);absent.data(0xf200,4);absent.printer_tick();absent.synchronize(12);
  assert(!absent.printer_running && !absent.printer_feeding && !absent.irq_requests());
  p6066_goino_state p;p.printer_attached=true;p.select(0);p.data(0x0e00,4);p.data(0xff00,4);
  auto column=[&](){
   p.printer_tick();assert(p.column_request);p.synchronize(4);
   assert(p.acknowledge(1));p.printer_tick();assert(p.column_request);
   p.data(0x55,2,0xff);p.synchronize(4);p.end(2);
  };
  for(unsigned n=0;n<3;++n)column();
  for(unsigned matrix=0;matrix<2;++matrix){
   p.printer_tick();assert(p.matrix_request);p.synchronize(8);
   assert(p.acknowledge(2));assert(p.type()==0);
   p.data(0xf400,3);p.synchronize(8);p.end(3);
   for(unsigned n=0;n<7;++n)column();
  }
  p.printer_tick();p.synchronize(8);assert(p.acknowledge(2));
  p.data(0xf400,3);p.data(0xf100,3);p.data(0xf200,3);
  p.synchronize(8);p.end(3);
  for(unsigned n=0;n<10;++n){
   p.printer_tick();assert(p.matrix_request && !p.column_request);
   p.synchronize(8);assert(p.acknowledge(2));p.data(0xf400,3);
   if(n==9)p.data(0xf300,3);
   p.synchronize(8);p.end(3);
  }
  for(unsigned n=0;n<100;++n){p.printer_tick();p.synchronize(12);assert(!p.irq_requests());}
  assert(p.printer_columns_discarded==17 && p.printer_feed_events==10);
 }

 // Separate source table from encoder implementation.
 const unsigned priority[]={64,32,16,8,4,2,1};
 const unsigned type[]={0x60,0x50,0x40,0x30,0x20,0x10,0};
 // The two keyboard modes cannot be pending together.
 for(unsigned sources=0;sources<128;++sources){
  if((sources&66)==66)continue;
  for(unsigned blocked=0;blocked<2;++blocked)
   for(unsigned sync_mask=0;sync_mask<16;++sync_mask){
    p6066_goino_state s;
    s.matrix_request=sources&1;s.keyboard_request=sources&66;
    s.basic_mode=sources&2;s.timer_request=sources&4;
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
  const unsigned tes6n=!mode||tas7n!=tas9n?tas6n:!tas6n;
  result=(result&~32)|(!tes6n<<5);
  assert(s.key_data()==result);++checks;
 }
 p6066_goino_state mode;
 mode.keyboard_request=true;mode.synchronize(8);assert(mode.type()==0x10);
 mode.keyboard_code=0x169;assert(mode.key_data()==0x49);
 mode.keyboard_code=0x149;assert(mode.key_data()==0x69);
 mode.basic_mode=false;assert(mode.type()==0x60);
 mode.keyboard_code=0x169;assert(mode.key_data()==0x69);
 mode.keyboard_code=0x149;assert(mode.key_data()==0x49);
 mode.basic_mode=true;assert(mode.type()==0x10);
 p6066_goino_state running;
 assert(running.running_lamp(true) && !running.running_lamp(false));
 running.lamps=0x80;
 assert(running.running_lamp(true) && running.running_lamp(false));
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
