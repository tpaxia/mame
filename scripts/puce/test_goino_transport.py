#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders: Salvatore Paxia
"""Exercise actual CPU, bus and GOINO transport bodies without guest software.

Only MAME plumbing is stubbed. Missing command_word overrides fail extraction;
production callback bodies, CPU semantics and GOINO state are compiled verbatim.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

def method(path, signature, replacement=None):
    source = (ROOT/path).read_text()
    start = source.index(signature)
    opening = source.index('{', start)
    end, depth = opening+1, 1
    while depth:
        depth += (source[end]=='{')-(source[end]=='}')
        end += 1
    result = source[start:end]
    if replacement:
        result = result.replace(signature,replacement,1)
    return result.replace(' override','')

header = 'src/devices/bus/p6066/goino.h'
source = r'''
#include "cpu/puce/puce_state.h"
#include "bus/p6066/goino_state.h"
#include <cassert>
#include <stdexcept>
#include <cstdio>
using u16=unsigned short; using u32=unsigned; using offs_t=unsigned;
template<typename... T> void fatalerror(const char *,T...){throw std::runtime_error("unsupported");}
template<typename... T> void logerror(const char *,T...){}
struct p6066_goino_device {
 p6066_goino_state m_state;
 struct machine_stub {const char *describe_context(){return "fixture";}};
 machine_stub machine(){return {};}
 void update_outputs(){}
 void data_w(offs_t,u16,u16=0xffff);
'''
source += method(header,'virtual void output_data_masked(')+'\n'
source += method(header,'virtual void command_word(')+'\n};\n'
source += method('src/devices/bus/p6066/goino.cpp','void p6066_goino_device::data_w(')+'\n'
source += r'''
struct p6066_bus_device {
 p6066_goino_device card;
 p6066_goino_device *channel_card(unsigned level) {
  return level==4 && card.m_state.selected ? &card : nullptr;
 }
 void command_w(offs_t,u16,u16);
 void data_w(offs_t,u16,u16);
};
'''
for name in ('command_w','data_w'):
    source += method('src/devices/bus/p6066/p6066.cpp',f'void p6066_bus_device::{name}(')+'\n'
source += r'''
struct io {
 p6066_bus_device &bus;
 void command(unsigned v,unsigned mask){bus.command_w(4,v,mask);}
 void output(unsigned v,unsigned mask){bus.data_w(4,v,mask);}
 void select(unsigned v){bus.card.m_state.select(v);}
 unsigned read_byte(unsigned){return 0;}
 unsigned read_word(unsigned){return 0;}
 void write_byte(unsigned,unsigned){} void write_word(unsigned,unsigned){}
 bool ecof(){return false;} unsigned input(){return 0;} unsigned name_type(){return 0;}
 void strobe(){} void control(unsigned){} void console_control(unsigned){}
 void console_output(unsigned){} unsigned console_input(unsigned){return 0;}
};
int main(){
 unsigned cases=0;
 for(unsigned opcode : {0xfc20U,0xfc22U}) // DAE L2 and CAE L2
  for(unsigned word=0;word<65536;++word)
   for(bool selected : {false,true}) {
    p6066_bus_device bus;
    bus.card.m_state.select(selected?0:1);
    puce_state cpu;cpu.level=4;cpu.active=16;cpu.di=0xa5;cpu.l[2]=word;
    io port{bus};
    const unsigned command=(word>>8)&15;
    // Current supported command inventory, not a claim that others are absent in HW.
    const bool supported=command==0||(command>=4&&command<=9)||command==11||command==12||command==13||command==14;
    bool failed=false;
    try {assert(cpu.execute_channel(opcode,port));}catch(const std::runtime_error &){failed=true;}
    assert(failed==(selected&&!supported));
    assert(cpu.di==0xa5 && cpu.l[2]==word && cpu.level==4);
    if(!failed){
     assert(bus.card.m_state.commands_seen==(selected ? 1U<<command : 0));
     assert(bus.card.m_state.lamp_strobes==(selected && ((word>>8)==0x40) ? 1 : 0));
     assert(bus.card.m_state.display_position==(selected && ((word>>8)==0x20) ? 1 : 0));
     if(selected&&((word>>8)==0x40))assert(bus.card.m_state.lamp_shift==(word&1));
     if(selected&&((word>>8)==0x20))assert(bus.card.m_state.display[0]==(word&255));
    }
    ++cases;
   }
 // The documented 16-CAE lamp transaction must survive all callbacks.
 p6066_bus_device bus;bus.card.m_state.select(0);io port{bus};puce_state cpu;
 for(int bit=15;bit>=0;--bit){cpu.l[2]=0x4000|((0xa55a>>bit)&1);assert(cpu.execute_channel(0xfc22,port));}
 assert(bus.card.m_state.lamps==0xa55a);
 // Actual startup reset sequence must not prefix nine bytes to the message.
 const unsigned startup[]={0xf400,0xf500,0xf600,0xf700,0xf800,0xf900,0xfb00,0xfd00,0xfe00};
 const unsigned lamps_before=bus.card.m_state.lamp_strobes;
 for(unsigned command:startup){cpu.l[2]=command;assert(cpu.execute_channel(0xfc20,port));}
 assert(bus.card.m_state.display_strobes==0);
 assert(bus.card.m_state.display_position==0);
 assert(bus.card.m_state.lamp_strobes==lamps_before);
 // Display framing also travels through CPU CAE and both production callbacks.
 for(unsigned i=0;i<224;++i){
  cpu.l[2]=0x2000|(i&255);assert(cpu.execute_channel(0xfc22,port));
  assert(bus.card.m_state.display_ready==(i==223));
 }
 assert(bus.card.m_state.display_position==0);
 for(unsigned i=0;i<224;++i)assert(bus.card.m_state.display[i]==i);
 assert(bus.card.m_state.display_strobes==224);
 for(unsigned command:startup){cpu.l[2]=command;assert(cpu.execute_channel(0xfc20,port));}
 assert(bus.card.m_state.display_ready && bus.card.m_state.display_position==0);
 assert(bus.card.m_state.display_strobes==224);
 for(unsigned i=0;i<224;++i)assert(bus.card.m_state.display[i]==i);
 // Actual board callback retains validation of unspecified ECD lanes.
 bool rejected=false;
 try{bus.card.command_word(4,0x4001,0x00ff);}catch(const std::runtime_error &){rejected=true;}
 assert(rejected);
 std::printf("PASS: %u CPU/bus/GOINO transport cases; CAE lamp/display frames and lane rejection\n",cases);
}
'''
with tempfile.TemporaryDirectory(prefix='p6066-goino-') as tmp:
    cpp=Path(tmp)/'test.cpp';cpp.write_text(source)
    binary=Path(tmp)/'test'
    subprocess.run(['c++','-std=c++17','-O2','-I',str(ROOT/'src/devices'),str(cpp),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
