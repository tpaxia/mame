#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders: Salvatore Paxia
"""Compile production CPU loop, bus routing and GOINO callbacks; no guest ROM."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
def method(path,signature,replacement=None):
    s=(ROOT/path).read_text();a=s.index(signature);b=s.index('{',a);n=b+1;depth=1
    while depth:
        depth+=(s[n]=='{')-(s[n]=='}');n+=1
    out=s[a:n]
    return out.replace(signature,replacement,1) if replacement else out
header='src/devices/bus/p6066/goino.h'
source=r'''
#include "cpu/puce/puce_state.h"
#include "bus/p6066/arbiter.h"
#include "bus/p6066/goino_state.h"
#include <cassert>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
using u8=uint8_t;using u16=uint16_t;using u32=uint32_t;using offs_t=unsigned;
namespace util {template<typename... T>std::string string_format(const char *s,T...){return s;}}
template<typename... T>void fatalerror(const char *s,T...){throw std::runtime_error(s);}
template<typename... T>void logerror(const char *,T...){}
template<typename... T>void osd_printf_info(const char *,T...){}
struct device_p6066_card_interface {
 virtual bool direct_selected() const{return false;}
 virtual unsigned irq_requests()const{return 0;}
 virtual void interrupt_sync(u8){} virtual void irq_ack(unsigned){} virtual void irq_end(unsigned){}
 virtual u16 name_type(unsigned){return 0;} virtual u8 input_data(unsigned){return 0;}
 virtual void output_data_masked(unsigned,u16,u16){} virtual void command_word(unsigned,u16,u16){}
 virtual void strobe(unsigned){}
};
struct p6066_goino_device:device_p6066_card_interface {
 bool m_keyboard_down=false,m_mode_down=false;
 u8 m_decimal_position=15;
 struct printer_device {void tick(p6066_goino_state &state){state.printer_tick();} u8 status(const p6066_goino_state &state)const{return (state.printer_running||state.printer_feeding)?0x10:0;}} printer;
 struct printer_slot {printer_device *card=nullptr;printer_device *get_card_device(){return card;}printer_slot *operator->(){return this;}} slot;
 printer_slot *m_printer_slot=&slot;
 struct beeper_device {int state=0;void set_state(int value){state=value;}beeper_device *operator->(){return this;}} beeper;
 beeper_device *m_beeper=&beeper;
 void keyboard_command(unsigned, u16) {}
 p6066_goino_state m_state;
 std::array<u8,16> m_lamps{};
 unsigned ticks=0;
 struct machine_stub {unsigned ticks;const char *describe_context(){return "fixture";}struct time_stub {unsigned ticks;unsigned as_ticks(unsigned)const{return ticks;}};time_stub time(){return {ticks};}};
 machine_stub machine(){return {ticks};}
 struct auxiliary {bool isunset(){return true;} unsigned operator()(unsigned){assert(false);return 0;}}m_auxiliary_input_cb;
 struct time_value {int us;static time_value from_usec(int v){return {v};}static time_value from_msec(int v){return {v*1000};}};
 struct timer_stub {int first=0,period=0;bool enabled=true;void adjust(time_value a,int=0,time_value b={0}){first=a.us;period=b.us;enabled=true;}void enable(bool value){enabled=value;}}timer,beep_timer;
 using attotime=time_value;timer_stub *m_timer=&timer,*m_beep_timer=&beep_timer;
 void device_reset();void update_outputs(){}
 bool direct_selected()const override{return m_state.selected;}
 unsigned irq_requests()const override{return m_state.irq_requests();}
 void interrupt_sync(u8 mask)override{m_state.synchronize(mask);}
 void irq_end(unsigned level)override{m_state.end(level);}
 u16 name_type(unsigned level)override{return name_type_r(level);}
 u8 input_data(unsigned level)override{return input_data_r(level);}
 void data_w(offs_t,u16,u16=0xffff);
 void irq_ack(unsigned)override;u16 name_type_r(offs_t);u8 input_data_r(offs_t);
 void tick();void beep_off();
'''
for sig in ('virtual void output_data_masked(', 'virtual void command_word(', 'virtual void strobe('):
    source+=method(header,sig)+'\n'
source+='};\n'
for sig in ('void p6066_goino_device::device_reset()', 'void p6066_goino_device::data_w(', 'void p6066_goino_device::irq_ack(', 'u16 p6066_goino_device::name_type_r(', 'u8 p6066_goino_device::input_data_r('):
    source+=method('src/devices/bus/p6066/goino.cpp',sig)+'\n'
source+=method('src/devices/bus/p6066/goino.cpp','TIMER_CALLBACK_MEMBER(p6066_goino_device::timer_tick)','void p6066_goino_device::tick()')+'\n'
source+=method('src/devices/bus/p6066/goino.cpp','TIMER_CALLBACK_MEMBER(p6066_goino_device::beep_off)','void p6066_goino_device::beep_off()')+'\n'
source+=r'''
struct p6066_bus_device {
 void trace_io(const char *,unsigned,u16,u16,device_p6066_card_interface *) {} // diagnostic observer only
 std::array<device_p6066_card_interface*,16> m_cards{};p6066_irq_arbiter m_irq;
 device_p6066_card_interface*channel_card(unsigned);void refresh_requests();
 void interrupt_sync_w(u8);u8 irq_r(offs_t);void irq_ack_w(u8);void irq_end_w(u8);
 u16 name_type_r(offs_t);u8 input_data_r(offs_t);void command_w(offs_t,u16,u16);
 void data_w(offs_t,u16,u16);void strobe_w(u8);
};
'''
for sig in ('device_p6066_card_interface *p6066_bus_device::channel_card(',
            'void p6066_bus_device::refresh_requests(', 'void p6066_bus_device::interrupt_sync_w(',
            'u8 p6066_bus_device::irq_r(', 'void p6066_bus_device::irq_ack_w(',
            'void p6066_bus_device::irq_end_w(', 'u16 p6066_bus_device::name_type_r(',
            'u8 p6066_bus_device::input_data_r(', 'void p6066_bus_device::command_w(',
            'void p6066_bus_device::data_w(', 'void p6066_bus_device::strobe_w('):
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
  unsigned name_type(){return c.bus.name_type_r(c.m_core.external_channel_level());}
  unsigned input(){return c.bus.input_data_r(c.m_core.external_channel_level());}
  void output(unsigned v,unsigned mask){c.bus.data_w(c.m_core.external_channel_level(),v,mask);}
  void command(unsigned v,unsigned mask){c.bus.command_w(c.m_core.external_channel_level(),v,mask);}
  void select(unsigned){}void strobe(){c.bus.strobe_w(c.m_core.external_channel_level());}
  void control(unsigned){}void console_control(unsigned){}void console_output(unsigned){}
  unsigned console_input(unsigned){return 0;}bool ecof(){return false;}
 };
 // This existing fixture tests the non-DMA synchronous configuration.
 bool m_memory_active=false; unsigned m_sampled=0,m_fetch_invalid_before=0;
 struct memory_wait {};
 bool m_memory_ready_cb(){return true;} void m_phase_cb(unsigned){}
 u16 memory_read(u16 a,u16 mask=65535){return m_program.read_word(a,mask);}
 void memory_write(u16 a,u16 v,u16 mask=65535){m_program.write_word(a,v,mask);}
 void execute_run();
 void instruction(unsigned op){assert(m_phase==0);word=op;m_icount=2;execute_run();assert(m_phase==0);}
 cpu_fixture(){m_core.level=4;m_core.active=16;m_core.cpu19m=true;m_core.l[0]=0x2000;m_core.l[1]=0x3000;}
 void command(unsigned v){m_core.l[2]=v;instruction(0xfc22);}
};
'''
source+=method('src/devices/cpu/puce/puce.cpp','void puce_device::execute_run()','void cpu_fixture::execute_run()')+'\n'
source+=r'''
int main(){
 cpu_fixture c;p6066_goino_device g;
 g.m_state.owned3=true;g.m_state.timer_request=true;g.m_state.basic_mode=false;
 g.device_reset();assert(!g.m_state.owned3&&!g.m_state.timer_request&&g.m_state.basic_mode);
 assert(g.beeper.state==0&&!g.beep_timer.enabled);
 assert(!g.m_state.printer_attached);
 g.m_state.select(0);g.m_state.data(0xff00,4);g.tick();g.m_state.synchronize(12);
 assert(!g.m_state.printer_running&&!g.m_state.column_request&&!g.m_state.matrix_request);
 assert(g.m_lamps[7]==1);g.ticks=1;g.tick();assert(g.m_lamps[7]==0);
 g.m_state.input_select=1;assert(g.input_data_r(4)==0x4f);
 c.bus.m_cards[4]=&g;c.m_core.l[2]=0x9000;c.instruction(0xfb28);
 assert(c.m_core.a(2)==0x4f);
 for(unsigned position=0;position<16;++position){
  g.m_decimal_position=position;c.m_core.l[2]=0x9000;c.instruction(0xfb28);
  assert(c.m_core.a(2)==(0x40|position));
 }
 g.m_decimal_position=3;g.slot.card=&g.printer;g.device_reset();
 assert(g.m_state.printer_attached && g.m_decimal_position==3);
 g.m_state.select(0);g.m_state.input_select=1;assert(g.input_data_r(4)==3);
 g.m_decimal_position=15;
 assert(g.timer.first==6300&&g.timer.period==6300);
 c.bus.m_cards[4]=&g;g.m_state.select(0);
 g.m_state.input_select=1;c.instruction(0xfb28);assert(c.m_core.a(2)==15);
 for(int bit=15;bit>=0;--bit)c.command(0x4000|((0x80>>bit)&1));
 assert(g.m_state.lamps==0x80 && g.beeper.state==0);g.tick();assert(g.m_lamps[7]==1);
 for(int bit=15;bit>=0;--bit)c.command(0x4001);
 assert(g.m_state.lamps==0xffff && g.beeper.state==1 && g.beep_timer.first==200000);
 g.beep_off();assert(g.beeper.state==0);
 for(int bit=15;bit>=0;--bit)c.command(0x4000|((0x8084>>bit)&1));
 assert(g.m_state.lamps==0x8084 && g.beeper.state==1 && g.beep_timer.first==200000);
 g.beep_off();assert(g.beeper.state==0);
 c.command(0x0c00);assert(g.m_state.timer_enabled);g.tick();
 c.instruction(0xc900);assert(c.m_core.level==4 && g.m_state.synchronized3==4); // ASPEO
 c.command(0x0e00);c.instruction(0xc900);
 assert(c.m_core.level==3 && c.bus.m_irq.owners[3]==4 && g.m_state.owned3);
 c.instruction(0xaab0);assert(c.m_core.l[11]==0x2000); // timer type, GOINO name 00
 c.command(0x0800);assert(!g.m_state.timer_request && g.m_state.synchronized3==4);
 c.instruction(0xbd00);assert(c.m_core.level==4 && c.bus.m_irq.owners[3]<0 && !g.m_state.owned3);
 assert(g.irq_requests()==0);
 // INOF masks acceptance, not the hardware synchronization strobe.
 c.instruction(0xbd21);g.tick();c.instruction(0xc900);
 assert(c.m_core.level==4 && g.m_state.synchronized3==4 && c.sync.back()==14);
 c.instruction(0xbd20);c.instruction(0xc900);assert(c.m_core.level==3);
 c.command(0x0800);c.instruction(0xbd00);c.command(0x0d00);
 // Internal COM1 has no external ECC acknowledge: direct selection persists.
 c.instruction(0xbd10);c.instruction(0xc900);
 assert(c.m_core.level==3 && c.m_core.internal_name==2 && c.bus.m_irq.owners[3]<0);
 c.instruction(0xaab0);assert((c.m_core.l[11]&255)==2);
 for(int i=15;i>=0;--i)c.command(0x4000|((0x8421>>i)&1));
 assert(g.m_state.lamps==0x8421);
 c.instruction(0xbd00);assert(c.m_core.level==4);
 // Keyboard: async ready -> ECM3 -> grant -> DEA -> RECAN -> COM0.
 g.m_state.keyboard_code=0x165;g.m_state.keyboard_request=true;
 c.instruction(0xc900);assert(c.m_core.level==3);
 c.instruction(0xaab0);assert(c.m_core.l[11]==0x1000);
 c.m_core.l[2]=0x2000;c.instruction(0xfb28);assert(c.m_core.a(2)==0x45);
 c.command(0x0700);assert(g.m_state.synchronized3==2);c.instruction(0xbd00);
 assert(c.m_core.level==4&&!g.irq_requests());
 // Button selection, read after RECON and lamp output at owned level 3.
 g.m_state.buttons_w(4);c.instruction(0xc900);assert(c.m_core.level==3);
 c.command(0x0500);c.m_core.l[2]=0;c.instruction(0xfb28);assert(c.m_core.a(2)==5);
 for(int i=15;i>=0;--i)c.command(0x4000|((0x5aa5>>i)&1));
 assert(g.m_state.lamps==0x5aa5);c.instruction(0xbd00);assert(c.m_core.level==4);
 // A level-2 column preempts the same board's level-3 service.
 g.m_state.timer_request=true;c.instruction(0xc900);assert(c.m_core.level==3);
 g.m_state.column_request=true;c.instruction(0xc900);
 assert(c.m_core.level==2 && g.m_state.owned2 && g.m_state.owned3);
 c.m_core.l[2]=0x55;c.instruction(0xfc20);assert(g.m_state.printer_column==0x55);
 c.instruction(0xbd00);assert(c.m_core.level==3&&!g.m_state.owned2&&g.m_state.owned3);
 c.command(0x0800);c.instruction(0xbd00);assert(c.m_core.level==4);
 // Discard printer traverses real CPU arbitration and board callbacks.
 // Printer/keyboard overlap must allow an incidental stale PROM-mux read.
 g.m_state.timer_request=true;c.command(0xf000);c.instruction(0xc900);
 assert(c.m_core.level==3 && g.input_data_r(3)==0);
 c.command(0xf800);c.instruction(0xbd00);
 c.command(0xff00);
 g.m_state.input_select=1;assert(g.input_data_r(4)==0x1f);
 for(unsigned n=0;n<3;++n){
  g.m_state.printer_tick();c.instruction(0xc900);assert(c.m_core.level==2);
  c.m_core.l[2]=n;c.instruction(0xfc20);c.instruction(0xbd00);
 }
 g.m_state.printer_tick();c.instruction(0xc900);assert(c.m_core.level==3);
 c.command(0xf400);c.instruction(0xbd00);
 for(unsigned n=0;n<7;++n){
  g.m_state.printer_tick();c.instruction(0xc900);assert(c.m_core.level==2);
  c.m_core.l[2]=0x7f-n;c.instruction(0xfc20);c.instruction(0xbd00);
 }
 c.command(0xf100);c.command(0xf200);
 g.m_state.input_select=1;assert(g.input_data_r(4)==0x1f);
 for(unsigned n=0;n<10;++n){
  g.m_state.printer_tick();c.instruction(0xc900);assert(c.m_core.level==3);
  c.command(0xf400);if(n==9)c.command(0xf300);c.instruction(0xbd00);
 }
 assert(g.m_state.printer_columns_discarded==11); // includes earlier nested test
 g.m_state.printer_tick();assert(!g.irq_requests());
 // Decimal bits remain readable independently of printer status.
 g.m_state.input_select=1;assert(g.input_data_r(4)==15);
 bool stopped=false;g.m_state.input_select=3;
 try{g.input_data_r(4);}catch(const std::runtime_error&){stopped=true;}assert(stopped);
 std::puts("PASS: production CPU/bus/GOINO timer, masks, type, keyboard, buttons, lamp output and nested level-2/3 service");
}
'''
with tempfile.TemporaryDirectory(prefix='p6066-goino-integration-') as tmp:
    path=Path(tmp)/'test.cpp';path.write_text(source)
    binary=Path(tmp)/'test'
    subprocess.run(['c++','-std=c++17','-O2','-I',str(ROOT/'src/devices'),str(path),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
