#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders: Salvatore Paxia
"""Test GO011 through production CPU, bus and board transaction bodies.

MAME plumbing/logging is stubbed; no ROM, disk, boot address or A5-only oracle.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

def method(path, signature):
    text = (ROOT/path).read_text()
    start = text.index(signature)
    end = text.index('{', start)+1
    depth = 1
    while depth:
        depth += (text[end] == '{')-(text[end] == '}')
        end += 1
    return text[start:end]+'\n'

source = r'''
#include "bus/p6066/go011_state.h"
#include "bus/p6066/arbiter.h"
#include "cpu/puce/puce_state.h"
#include <cassert>
#include <cstdio>
#include <stdexcept>
using u8=std::uint8_t;using u16=std::uint16_t;using offs_t=unsigned;
template<typename... T> void fatalerror(const char*,T...){throw std::runtime_error("boundary");}
struct device_p6066_card_interface {
 virtual void select(u8){} virtual bool direct_selected() const {return false;}
 virtual void controller_reset(bool){}
 virtual u16 name_type(unsigned){return 0;}virtual u8 input_data(unsigned){return 0;}
 virtual void command_word(unsigned,u16,u16){}virtual void output_data_masked(unsigned,u16,u16){}
 struct device_stub {const char *tag(){return "fixture";}};device_stub device(){return {};}
};
struct p6066_go011_device:device_p6066_card_interface {
 p6066_go011_state m_state;
 struct machine_stub {const char *describe_context(){return "fixture";}};machine_stub machine(){return {};}
 void device_reset();void command_word(unsigned,u16,u16) override;
 void output_data_masked(unsigned,u16,u16) override;u8 input_data(unsigned) override;
 u16 name_type(unsigned) override;void control(unsigned,u8);void strobe(unsigned);
 void check(p6066_go011_state::result,const char*,u16,u16);
'''
for sig in ('virtual void select(', 'virtual bool direct_selected(', 'virtual void controller_reset('):
    source += method('src/devices/bus/p6066/go011.h', sig)
source += '};\n'
for sig in ('void p6066_go011_device::device_reset(', 'void p6066_go011_device::check(',
            'void p6066_go011_device::command_word(', 'void p6066_go011_device::output_data_masked(',
            'u8 p6066_go011_device::input_data(', 'u16 p6066_go011_device::name_type(',
            'void p6066_go011_device::control(', 'void p6066_go011_device::strobe('):
    source += method('src/devices/bus/p6066/go011.cpp', sig)
source += r'''
struct p6066_bus_device {
 std::array<device_p6066_card_interface*,16> m_cards{};p6066_irq_arbiter m_irq;
 bool m_trace_io=false;u8 m_trace_selection=0;unsigned m_floppy_selects=0;
 void update_outputs(){}void trace_io(const char*,unsigned,u16,u16,device_p6066_card_interface*){}
 device_p6066_card_interface *channel_card(unsigned);void select_w(u8);
 u16 name_type_r(offs_t);u8 input_data_r(offs_t);
 void command_w(offs_t,u16,u16);void data_w(offs_t,u16,u16);
};
'''
for sig in ('device_p6066_card_interface *p6066_bus_device::channel_card(',
            'void p6066_bus_device::select_w(', 'u16 p6066_bus_device::name_type_r(',
            'u8 p6066_bus_device::input_data_r(', 'void p6066_bus_device::command_w(',
            'void p6066_bus_device::data_w('):
    source += method('src/devices/bus/p6066/p6066.cpp', sig)
source += r'''
struct fixture {
 p6066_go011_device card;p6066_bus_device bus;puce_state cpu;
 fixture(){bus.m_cards[8]=&card;cpu.level=4;cpu.active=16;cpu.cpu19m=true;}
 struct io {
  fixture &f;
  void command(unsigned v,unsigned mask){f.bus.command_w(4,v,mask);}
  void output(unsigned v,unsigned mask){f.bus.data_w(4,v,mask);}
  unsigned input(){return f.bus.input_data_r(4);}unsigned name_type(){return f.bus.name_type_r(4);}
  void select(unsigned v){f.bus.select_w(v);}unsigned read_byte(unsigned){return 0xff;}
  unsigned read_word(unsigned){return 0;}void write_byte(unsigned,unsigned){}void write_word(unsigned,unsigned){}
  void strobe(){f.card.strobe(4);}void control(unsigned v){f.card.control(4,v);}
  bool ecof(){return false;}void console_control(unsigned){}void console_output(unsigned){}
  unsigned console_input(unsigned){return 0;}
 } port{*this};
 void exec(unsigned op){assert(cpu.execute_channel(op,port));}
 void select(){exec(0xb1e4);} // ESE A14; fixture byte source supplies FF
 void address(unsigned a){cpu.l[7]=a;exec(0xfc72);}
 void write(unsigned v,unsigned op=0x58){cpu.l[9]=(op<<8)|v;exec(0xfc90);}
 unsigned read(){exec(0xa98f);return cpu.b(8);}
};
template<typename F> void rejects(F f){bool caught=false;try{f();}catch(const std::runtime_error&){caught=true;}assert(caught);}
int main(){
 fixture pointer;pointer.select();
 // Independent public-coordinate oracle, through real CPU/bus/card methods.
 for(unsigned x=0;x<560;++x)for(unsigned y=0;y<392;++y){
  pointer.cpu.l[7]=0x1000 | (y+10);pointer.exec(0xfc70);
  pointer.cpu.l[8]=0x0c00 | (1023-x);pointer.exec(0xfc80);
  assert(pointer.card.m_state.pointer_x_known && pointer.card.m_state.pointer_y_known);
  assert(pointer.card.m_state.pointer_x()==x && pointer.card.m_state.pointer_y()==y);
 }
 // Test every counter encoding, including off-screen values; no clamping.
 for(unsigned n=0;n<1024;++n){
  pointer.bus.data_w(4,0x0c00|n,0xffff);
  pointer.bus.data_w(4,0x1000|n,0xffff);
  assert(pointer.card.m_state.pointer_x_counter==n);
  assert(pointer.card.m_state.pointer_y_counter==n);
 }
 for(unsigned mask=0;mask<65535;++mask){
  rejects([&]{pointer.bus.data_w(4,0x0c00,mask);});
  rejects([&]{pointer.bus.data_w(4,0x1000,mask);});
 }
 assert(pointer.card.m_state.pointer_x_counter==1023);
 assert(pointer.card.m_state.pointer_y_counter==1023);
 pointer.card.device_reset();
 assert(!pointer.card.m_state.pointer_x_known && !pointer.card.m_state.pointer_y_known);

 // Independent coordinate pattern written through CPU/bus/card. Include
 // both service rows, byte edges and the final pixel, not just the page.
 fixture raster;raster.select();raster.address(0);
 auto lit=[](unsigned x,unsigned y){return (x%19)==(y%19) || y==393 || (y==409 && x==559);};
 for(unsigned y=0;y<410;++y)for(unsigned byte=0;byte<70;++byte){
  unsigned value=0;
  for(unsigned bit=0;bit<8;++bit)value=value*2+lit(byte*8+bit,y);
  raster.write(value,0x54);
 }
 for(unsigned y=0;y<410;++y)for(unsigned x=0;x<560;++x)
  assert(raster.card.m_state.diagnostic_pixel(x,y)==lit(x,y));
 assert(!raster.card.m_state.diagnostic_pixel(560,0));
 assert(!raster.card.m_state.diagnostic_pixel(0,410));
 raster.card.device_reset();
 for(unsigned y=0;y<410;++y)for(unsigned x=0;x<560;++x)
  assert(!raster.card.m_state.diagnostic_pixel(x,y));
 raster.select();raster.address(0);rejects([&]{raster.read();});
 // Independent row-based scroll oracle, including both stationary service rows.
 fixture scroll;scroll.select();
 for(int displacement: {-1400,-700,-70,0,70,700,1400}) {
  scroll.address(0);
  for(unsigned a=0;a<32768;++a)scroll.write(((a/70)*37+(a%70)*11)&255,0x54);
  scroll.address(unsigned(displacement)&32767);
  scroll.cpu.l[7]=0x0400|(unsigned(displacement)&255);scroll.exec(0xfc70);
  assert(scroll.bus.name_type_r(4)==0x00f0);
  for(unsigned a=0;a<32768;++a){
   int source=int(a)-displacement;
   unsigned expected= a>=392*70 ? ((a/70)*37+(a%70)*11)&255 :
    (source<0 || source>=392*70 ? 0: ((source/70)*37+(source%70)*11)&255);
   scroll.address(a);assert(scroll.read()==expected);
  }
 }
 // Actual firmware's additional 1046-byte clear must remain guest-driven.
 scroll.address(0x675c);
 for(unsigned n=0;n<1046;++n)scroll.write(0,0x54);
 for(unsigned a=0x675c;a<0x6b72;++a){scroll.address(a);assert(scroll.read()==0);}
 fixture f;f.select();
 f.address(42);f.write(0x69);f.address(42);
 f.bus.data_w(4,0x08fe,0xffff);assert(f.read()==0x69);
 assert(f.card.m_state.access_mode==8 && !f.card.m_state.blanked);
 f.bus.data_w(4,0x1c00,0xffff);assert(f.card.m_state.inverted);
 f.bus.data_w(4,0x1caa,0xffff);assert(!f.card.m_state.inverted);
 f.bus.data_w(4,0x3800,0xffff);assert(f.read()==0x69);

 // Address/data independence and aliasing: fill in a permuted address order,
 // read in linear order using an independent address-derived oracle.
 for(unsigned n=0;n<32768;++n){unsigned a=(n*16381)&32767;f.address(a);f.write((a^(a>>8)^0xa7)&255);}
 for(unsigned a=0;a<32768;++a){f.address(a);assert(f.read()==((a^(a>>8)^0xa7)&255));assert(f.read()==((a^(a>>8)^0xa7)&255));}
 unsigned cases=0;
 for(unsigned a:{0U,1U,0xffU,0x100U,0x4000U,0x7ffeU,0x7fffU})
  for(unsigned v=0;v<256;++v){f.address(a);f.write(v);f.address(a);assert(f.read()==v);++cases;}
 // Streaming and read/OR/write: 54 writes advance; intervening EDB doesn't.
 f.address(0);
 for(unsigned a=0;a<32768;++a){assert(f.read()==((a==0 || a==1 || a==0xff || a==0x100 || a==0x4000 || a==0x7ffe || a==0x7fff)?255:((a^(a>>8)^0xa7)&255)));f.write(a&255,0x54);}
 rejects([&]{f.write(0,0x54);}); // No invented wrap from 7FFF.
 for(unsigned a=0;a<32768;++a){f.address(a);assert(f.read()==(a&255));}
 // DEA specifies only high ECD lanes; its unused low byte is not written.
 f.address(0x4567);f.cpu.l[7]=0x18aa;f.exec(0xfb78);assert(f.cpu.a(7)==0x67);
 rejects([&]{f.bus.data_w(4,0x18aa,0xffff);}); // Full-word18 wiring not established.
 f.write(0xe7);f.address(0x4567);assert(f.read()==0xe7);
 rejects([&]{f.write(0x22);f.write(0x33);}); // 58's next address is unresolved.
 f.address(0x4567);assert(f.read()==0x22);
 for(unsigned mask=0;mask<65536;++mask) if(mask!=0xffff){
  rejects([&]{f.bus.command_w(4,0x4567,mask);});
  rejects([&]{f.bus.data_w(4,0x58bb,mask);});
 }
 f.address(0x4567);assert(f.read()==0x22);
 for(unsigned a=0x8000;a<0x10000;++a) rejects([&]{f.address(a);});
 for(unsigned op=0;op<256;++op) if(op!=0 && op!=0x04 && op!=0x08 && op!=0x1c && op!=0x38 && op!=0x98 && op!=0x18 && op!=0x54 && op!=0x58 && !(op>=0x0c && op<=0x13))
  rejects([&]{f.bus.data_w(4,op<<8,0xffff);});
 assert(f.bus.name_type_r(4)==0x00f0);rejects([&]{f.card.control(4,0);});rejects([&]{f.card.strobe(4);});
 // Removal, non-selection and interrupt channels cannot corrupt local RAM.
 f.bus.select_w(0);f.address(5);f.write(0xcc);assert(f.bus.input_data_r(4)==0);
 f.select();f.address(5);assert(f.read()==5);
 f.bus.data_w(3,0x58cc,0xffff);assert(f.read()==5);
 f.card.controller_reset(true);f.select();assert(!f.card.direct_selected());
 f.card.controller_reset(false);f.select();f.address(5);rejects([&]{f.read();});
 f.write(0x3c);f.address(5);assert(f.read()==0x3c);
 f.bus.m_cards[8]=nullptr;f.address(5);f.write(0);assert(f.read()==0);
 f.bus.m_cards[8]=&f.card;f.address(5);assert(f.read()==0x3c);
 std::printf("PASS: GO011 production CPU/bus/board; all 32768 addresses, %u byte cases, 229600 diagnostic pixels, 219520 pointer positions, full streaming, RMW, lanes, selection, reset and explicit unknowns\n",cases);
}
'''
with tempfile.TemporaryDirectory(prefix='p6066-go011-') as directory:
    cpp=Path(directory)/'test.cpp';binary=Path(directory)/'test'
    cpp.write_text(source)
    subprocess.run(['c++','-std=c++17','-O2','-I',str(ROOT/'src/devices'),str(cpp),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
