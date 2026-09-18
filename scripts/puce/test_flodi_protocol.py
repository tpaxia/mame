#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders: Salvatore Paxia
"""Synthetic production CPU/bus/FLODI protocol tests, without ROM or media.

MAME timer/drive plumbing is stubbed. Flux acquisition and ID search are
explicit boundaries, not passing substitutes for media-operation tests.
"""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
def method(path, signature, replacement=None):
    text=(ROOT/path).read_text(); a=text.index(signature); b=text.index('{',a); n=b+1; depth=1
    while depth:
        depth+=(text[n]=='{')-(text[n]=='}'); n+=1
    out=text[a:n]
    return out.replace(signature,replacement,1) if replacement else out
source=r'''
#include "cpu/puce/puce_state.h"
#include "bus/p6066/arbiter.h"
#include "bus/p6066/flodi_latches.h"
#include "bus/p6066/flodi_irq.h"
#include "bus/p6066/goino_state.h"
#include <cassert>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
using u8=uint8_t;using u16=uint16_t;using u32=uint32_t;using u64=uint64_t;using offs_t=unsigned;
#define BIT(v,n) (((v)>>(n))&1)
namespace util {template<typename... T>std::string string_format(const char *s,T...){return s;}}
template<typename... T>void fatalerror(const char *s,T...){throw std::runtime_error(s);}
template<typename... T>void osd_printf_info(const char *,T...){}
template<typename... T>void logerror(const char *,T...){}
struct attotime {
 long long ns=0;static const attotime never;
 static attotime from_usec(long long v){return {v*1000};}
 static attotime from_msec(long long v){return {v*1000000};}
 static attotime from_nsec(long long v){return {v};}
 static attotime from_hz(unsigned v){return {1000000000/v};}
 friend attotime operator+(attotime a,attotime b){return {a.ns+b.ns};}
 friend attotime operator-(attotime a,attotime b){return {a.ns-b.ns};}
 friend bool operator<(attotime a,attotime b){return a.ns<b.ns;}
};
const attotime attotime::never{0x7fffffffffffffff};
struct emu_timer {attotime next;void adjust(attotime v){next=v;}};
struct floppy_image_device {
 bool present=true,zero=true;unsigned steps=0;
 bool exists(){return present;}int trk00_r(){return !zero;}
 void ss_w(int){}void mon_w(int){}void dir_w(int){}void stp_w(int v){steps+=v;}
};
struct floppy_connector {floppy_image_device media;floppy_image_device*get_device(){return &media;}};
template<class T,unsigned N>struct required_device_array {
 mutable std::array<T,N> items;T*operator[](unsigned n)const{return &items[n];}
};
template<class... T>using output_finder=unsigned;
#define TIMER_CALLBACK_MEMBER(name) void name()
struct device_p6066_card_interface {
 virtual bool direct_selected() const{return false;}
 virtual unsigned irq_requests()const{return 0;}
 virtual void select(u8){}virtual void interrupt_sync(u8){} virtual void irq_ack(unsigned){} virtual void irq_end(unsigned){}
 virtual u16 name_type(unsigned){return 0;} virtual u8 input_data(unsigned){return 0;}
 virtual void command(unsigned,u8){}virtual void output_data(unsigned,u16){}
 virtual void output_data_masked(unsigned,u16,u16){} virtual void command_word(unsigned,u16,u16){}
 virtual void strobe(unsigned){}virtual void control(unsigned,u8){}virtual void controller_reset(bool){}
};
struct p6066_flodi_device:device_p6066_card_interface {
 struct machine_stub {attotime time(){return {};}};
 machine_stub machine(){return {};}
 emu_timer mechanical,bytes;unsigned captures=0,searches=0;
 p6066_flodi_device(){m_timer=&mechanical;m_byte_timer=&bytes;device_reset();controller_reset(false);}
 void device_reset();
'''
header=(ROOT/'src/devices/bus/p6066/flodi.h').read_text()
a=header.index('\tvirtual void select(');b=header.index('protected:',a)
source+=header[a:b].replace('virtual u8 irq_requests()', 'virtual unsigned irq_requests()')
a=header.index('\tfloppy_image_device *drive() const;');b=header.index('\n};',a)
source+=header[a:b]+'\n};\n'
for sig in ('floppy_image_device *p6066_flodi_device::drive()',
            'void p6066_flodi_device::device_reset()', 'void p6066_flodi_device::controller_reset(',
            'void p6066_flodi_device::select(', 'void p6066_flodi_device::interrupt_sync(',
            'void p6066_flodi_device::irq_ack(', 'void p6066_flodi_device::irq_end(',
            'void p6066_flodi_device::latch_command(', 'void p6066_flodi_device::start_transfer(',
            'u16 p6066_flodi_device::name_type(', 'u8 p6066_flodi_device::input_data(',
            'void p6066_flodi_device::command(', 'void p6066_flodi_device::strobe(',
            'void p6066_flodi_device::stop_read(', 'void p6066_flodi_device::request3(',
            'void p6066_flodi_device::output_data(', 'void p6066_flodi_device::control(',
            'void p6066_flodi_device::schedule_byte(', 'void p6066_flodi_device::index_changed(',
            'TIMER_CALLBACK_MEMBER(p6066_flodi_device::mechanical_tick)',
            'TIMER_CALLBACK_MEMBER(p6066_flodi_device::byte_tick)'):
    source+=method('src/devices/bus/p6066/flodi.cpp',sig)+'\n'
source+='void p6066_flodi_device::load_track(){++captures;}\nvoid p6066_flodi_device::next_id(){++searches;}\n'
source+=r'''
struct p6066_bus_device {
 std::array<device_p6066_card_interface*,16> m_cards{};p6066_irq_arbiter m_irq;
 device_p6066_card_interface*channel_card(unsigned);void refresh_requests();
 void interrupt_sync_w(u8);u8 irq_r(offs_t);void irq_ack_w(u8);void irq_end_w(u8);
 u16 name_type_r(offs_t);u8 input_data_r(offs_t);void command_w(offs_t,u16,u16);
 void data_w(offs_t,u16,u16);void strobe_w(u8);void select_w(u8);void control_w(offs_t,u8);
 unsigned m_floppy_selects=0;void update_outputs(){}
};
'''
for sig in ('device_p6066_card_interface *p6066_bus_device::channel_card(',
            'void p6066_bus_device::refresh_requests(', 'void p6066_bus_device::interrupt_sync_w(',
            'u8 p6066_bus_device::irq_r(', 'void p6066_bus_device::irq_ack_w(',
            'void p6066_bus_device::irq_end_w(', 'u16 p6066_bus_device::name_type_r(',
            'u8 p6066_bus_device::input_data_r(', 'void p6066_bus_device::command_w(',
            'void p6066_bus_device::select_w(', 'void p6066_bus_device::control_w(', 'void p6066_bus_device::data_w(', 'void p6066_bus_device::strobe_w('):
    source+=method('src/devices/bus/p6066/p6066.cpp',sig)+'\n'
source+=r'''
struct cpu_fixture {
 puce_state m_core;p6066_bus_device bus;
 bool m_stopped=false,m_invalid_pending=false,m_hold_on_unsupported=false;
 unsigned m_phase=0,m_invalid_cycles=0,m_fetch_pc=0,m_ir=0,word=0xc900;
 int m_icount=0;std::vector<unsigned>sync;
 struct memory {cpu_fixture &c;u16 read_word(unsigned,unsigned=65535){return c.word;}
 void write_word(unsigned,unsigned,unsigned=65535){}}m_program{*this};
 void debugger_instruction_hook(unsigned){}void m_stopped_cb(unsigned){}void update_ecorn(){}
 void m_interrupt_sync_cb(unsigned mask){sync.push_back(mask);bus.interrupt_sync_w(mask);}
 unsigned m_irq_request_cb(unsigned level){return bus.irq_r(level);}
 void m_irq_ack_cb(unsigned src){bus.irq_ack_w(src);}void m_irq_end_cb(unsigned level){bus.irq_end_w(level);}
 struct channel_adapter {
  cpu_fixture &c;
  unsigned read_word(unsigned){return 0;}unsigned read_byte(unsigned){return 0;}
  void write_word(unsigned,unsigned){}void write_byte(unsigned,unsigned){}
  unsigned name_type(){return c.bus.name_type_r(c.m_core.level);}
  unsigned input(){return c.bus.input_data_r(c.m_core.level);}
  void output(unsigned v,unsigned mask){c.bus.data_w(c.m_core.level,v,mask);}
  void command(unsigned v,unsigned mask){c.bus.command_w(c.m_core.level,v,mask);}
  void select(unsigned v){c.bus.select_w(v);}void strobe(){c.bus.strobe_w(c.m_core.level);}
  void control(unsigned v){c.bus.control_w(c.m_core.level,v);}void console_control(unsigned){}void console_output(unsigned){}
  unsigned console_input(unsigned){return 0;}bool ecof(){return false;}
 };
 void execute_run();
 void instruction(unsigned op){assert(m_phase==0);word=op;m_icount=2;execute_run();assert(m_phase==0);}
 cpu_fixture(){m_core.level=4;m_core.active=16;m_core.cpu19m=true;m_core.l[0]=0x2000;m_core.l[1]=0x3000;}
 void command(unsigned v){m_core.l[2]=v;instruction(0xfc22);}
};
'''
source+=method('src/devices/cpu/puce/puce.cpp','void puce_device::execute_run()','void cpu_fixture::execute_run()')+'\n'
source+=r'''
struct console_fixture:device_p6066_card_interface {
 p6066_goino_state state;
 unsigned irq_requests()const override{return state.irq_requests();}
 void interrupt_sync(u8 m)override{state.synchronize(m);}
 void irq_ack(unsigned s)override{assert(state.acknowledge(s));}
 void irq_end(unsigned l)override{state.end(l);}
};
int main(){
 unsigned checks=0;
 // K02 M1: all 256 bus patterns, both service classes and PRICO states.
 // High ECD6/7 are the only data inputs to COLON; other bits are don't-care.
 for(unsigned word=0;word<256;++word) for(unsigned mema=0;mema<2;++mema)
 for(unsigned second=0;second<2;++second){
  p6066_flodi_latches f;f.command=0x82;f.mas=0x59;f.prico=second;
  const auto effect=f.write(mema,word);
  if(!mema && word<64){assert(effect==p6066_flodi_latches::effect::local);assert(f.command==0x82&&f.mas==0x59);}
  else if(mema&&second){assert(effect==p6066_flodi_latches::effect::inco);assert(f.command==0x82&&f.mas==word);}
  else {assert(effect==p6066_flodi_latches::effect::command);assert(f.command==word&&f.mas==0x59);}
  assert(f.prico);++checks;
 }
 // All source combinations and ECM masks; the physical active-low type table
 // supplies the independently inverted logical type (function/end/both).
 const unsigned type_from_pins[]={0xff,0xf7,0xfb,0xf3};
 for(unsigned events=0;events<8;++events)for(unsigned mask=0;mask<16;++mask){
  p6066_flodi_device f;
  if(events&1)f.request3(8);if(events&2)f.request3(4);
  f.m_irq.rili=events&4;
  assert(f.irq_requests()==0);f.interrupt_sync(mask);
  const unsigned expected=((events&3)&&(mask&8)?4:0)|((events&4)&&(mask&2)?1:0);
  assert(f.irq_requests()==expected);
  if(expected&4){f.irq_ack(2);assert(f.name_type(3)==((255-type_from_pins[events&3])<<8|0x60));}
  if(expected&1){f.irq_ack(0);assert(f.m_data_irq&&!f.m_irq.rili&&f.m_irq.livi);}
  ++checks;
 }
 // ECOT clears sources, including FINE, but does not erase their synchronized
 // type in the current service. New events after ECOT survive the next ECM3.
 for(unsigned before=1;before<4;++before)for(unsigned after=0;after<4;++after){
  p6066_flodi_device f;
  if(before&1)f.request3(8);if(before&2)f.request3(4);
  f.interrupt_sync(8);f.irq_ack(2);
  assert(f.m_irq.fogo==bool(before&1)&&f.m_irq.fine==bool(before&2));
  f.strobe(3);assert(!f.m_irq.fogo&&!f.m_irq.fine);
  assert(f.name_type(3)==((255-type_from_pins[before])<<8|0x60));
  if(after&1)f.request3(8);if(after&2)f.request3(4);
  f.interrupt_sync(8);f.irq_end(3);
  assert(f.irq_requests()==(after?4:0));
  if(after){f.irq_ack(2);assert(f.name_type(3)==((255-type_from_pins[after])<<8|0x60));}
  ++checks;
 }
 // An unacknowledged selection survives broadcast ECM3 while another board
 // is serviced; SELE and COMA are independent of function/completion requests.
 {p6066_flodi_device f;f.select(0xe0);f.request3(8);f.interrupt_sync(8);
  assert(f.irq_requests()==12);f.irq_ack(2);f.strobe(3);f.interrupt_sync(8);f.irq_end(3);
  assert(f.irq_requests()==8);f.irq_ack(3);assert(f.name_type(3)==0x160);
  f.command(3,0x42);f.command(3,0x52);assert(f.m_motion&&f.m_irq.command_armed);
  assert(f.name_type(3)==0x160);f.interrupt_sync(8);f.irq_end(3);
  assert(f.irq_requests()==8);f.irq_ack(3);assert(f.name_type(3)==0x360);
  f.interrupt_sync(8);f.irq_end(3);assert(f.irq_requests()==0);
 }
 // All command-byte patterns: drive-1 CADI is gated off; COLON cannot load
 // MACON even when its ignored high bits are nonzero. Run through callbacks.
 for(unsigned drive=0;drive<2;++drive)for(unsigned word=0;word<256;++word){
  p6066_flodi_device f;f.select(drive?0xe0:0x60);f.irq_ack(3);f.command(3,word);
  if(word<64){assert(f.m_local[drive]&&f.m_latches.command==0);}
  else {assert(!f.m_local[drive]);assert(f.m_latches.command==(word&(drive?255:223)));}
  ++checks;
 }
 // The CADI drive gate belongs to MACON, never the second-byte MAS latch.
 for(unsigned drive=0;drive<2;++drive)for(unsigned word=0;word<256;++word){
  p6066_flodi_device f;f.m_selected=drive;f.request3(8);f.interrupt_sync(8);f.irq_ack(2);
  f.command(3,0);f.command(3,word);assert(f.m_latches.mas==word);++checks;
 }
 // Class-C status -> NUM happens before ECM3; a mere input strobe must not
 // set PRICO. Table 4 PIFU switches track-zero to ERRO at completion.
 for(unsigned end=0;end<2;++end)for(unsigned zero=0;zero<2;++zero)
 for(unsigned result=0;result<4;++result){
  p6066_flodi_device f;f.drive()->zero=zero;f.m_latches.command=0x80;f.m_latches.cote=false;
  f.m_end_status=result<<6;f.request3(end?4:8);f.interrupt_sync(8);f.irq_ack(2);
  const unsigned expected=(result&2?128:0)|(end?(result&1?64:0):(zero?64:0));
  assert(f.input_data(3)==expected);f.strobe(3);assert(f.input_data(3)==expected);
  f.m_latches.num=0x73;f.command(3,0);assert(f.input_data(3)==0x73);
  assert(f.m_irq.rifi==bool(end));f.interrupt_sync(8);f.irq_end(3);assert(f.irq_requests()==0);
  ++checks;
 }
 // Index is gated by COTE, not the host read-engine flag; only the selected
 // drive and active edge feed INDO. Reset blocks requests.
 for(unsigned cote=0;cote<2;++cote)for(unsigned reading=0;reading<2;++reading)
 for(unsigned selected=0;selected<2;++selected)for(unsigned edge=0;edge<2;++edge)
 for(unsigned reset=0;reset<2;++reset){
  p6066_flodi_device f;f.controller_reset(reset);f.m_latches.cote=cote;f.m_reading=reading;
  f.index_changed(f.m_drives[selected?0:1]->get_device(),edge);
  const bool event=!reset&&!cote&&selected&&edge;
  assert(f.m_index==event&&f.m_irq.fogo==event&&!f.irq_requests());
  f.interrupt_sync(8);assert(f.irq_requests()==(event?4:0));++checks;
 }
 // Exercise the actual byte timer callback and level-1 payload callback.
 // Payload is injected at the decoded-byte boundary, not claimed as flux QA.
 {p6066_flodi_device f;f.m_reading=true;f.m_bit_count=100;f.m_id_pos=0;
  f.m_length=128;f.m_payload[0]=0xa6;f.m_payload_start=true;
  f.byte_tick();assert(f.m_irq.rili&&f.m_irq.fogo&&!f.irq_requests());
  f.interrupt_sync(10);assert(f.irq_requests()==5);f.irq_ack(0);
  assert(f.input_data(1)==0xa6&&f.m_bytes_read==1&&!f.m_irq.rili&&f.m_irq.livi);
  f.interrupt_sync(2);f.irq_end(1);assert(!f.m_data_irq&&!f.m_irq.livi);
  assert(f.m_byte==1&&f.bytes.next.ns==32000&&f.irq_requests()==4);
 }
 // Completion is not consumed by acknowledgement or ownership release.
 {p6066_flodi_device f;f.request3(4);f.interrupt_sync(8);f.irq_ack(2);
  f.interrupt_sync(8);f.irq_end(3);assert(f.irq_requests()==4);
  f.irq_ack(2);f.strobe(3);assert(f.m_irq.rifi&&!f.m_irq.fine);
  f.interrupt_sync(8);f.irq_end(3);assert(!f.irq_requests());
 }
 // Actual CPU ALFA, ECC, CAE, COM0 and bus callbacks, no guest ROM.
 {cpu_fixture c;p6066_flodi_device f;console_fixture g;c.bus.m_cards[5]=&f;c.bus.m_cards[4]=&g;
  f.select(0x60);g.state.interrupts_blocked=false;g.state.timer_request=true;
  c.instruction(0xc900);assert(c.m_core.level==3&&g.state.owned3&&f.m_active_type==0);
  g.state.timer_request=false;c.instruction(0xbd00);assert(c.m_core.level==4);
  c.instruction(0xc900);assert(c.m_core.level==3&&f.m_active_type==1);
  c.command(0x42);c.command(0x52);c.instruction(0xbd00);
  assert(f.m_irq.coma&&!f.m_irq.sele);c.instruction(0xc900);assert(f.m_active_type==3);
  c.instruction(0xbd00);assert(f.irq_requests()==0);
  f.mechanical_tick();assert(!f.irq_requests());c.instruction(0xc900);assert(f.m_active_type==8);
  c.command(0xc0);assert(f.m_settle&&f.mechanical.next.ns==4096000);c.instruction(0xbd00);
  // A level-1 byte request preempts this board's function service.
  f.mechanical_tick();c.instruction(0xc900);assert(c.m_core.level==3);
  f.m_irq.rili=true;c.instruction(0xc900);assert(c.m_core.level==1&&f.m_data_irq&&f.m_active_type==8);
  c.instruction(0xbd00);assert(c.m_core.level==3&&!f.m_data_irq&&!f.m_irq.livi&&f.m_active_type==8);
  c.bus.strobe_w(3);c.instruction(0xbd00);assert(c.m_core.level==4&&f.irq_requests()==0);
  // Broadcast ECM3 resets PRICO even without this board being the owner.
  f.m_latches.prico=true;c.instruction(0xc900);assert(!f.m_latches.prico);
 }
 // Reset during simultaneous pending/service activity clears all sources,
 // synchronized latches, commands and both timer deadlines.
 {p6066_flodi_device f;f.select(0xe0);f.request3(4);f.request3(8);f.m_irq.rili=true;
  f.interrupt_sync(10);f.irq_ack(2);f.irq_ack(0);f.controller_reset(true);
  assert(!f.irq_requests()&&!f.m_active_type&&!f.m_data_irq&&!f.m_irq.fine&&!f.m_irq.fogo&&!f.m_irq.rili);
  assert(f.bytes.next.ns==attotime::never.ns&&f.mechanical.next.ns==attotime::never.ns);
  f.request3(8);f.select(0xe0);f.interrupt_sync(10);assert(!f.irq_requests());
 }
 printf("PASS: %u FLODI command/source/status cases; production CPU/bus ECM, competing GOINO, nested byte service, response retention and reset sequences\n",checks);
}
'''
with tempfile.TemporaryDirectory(prefix='p6066-flodi-protocol-') as tmp:
    path=Path(tmp)/'test.cpp';path.write_text(source)
    binary=Path(tmp)/'test'
    subprocess.run(['c++','-std=c++17','-O2','-I',str(ROOT/'src/devices'),str(path),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
