#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders: Salvatore Paxia
"""Check live PUCE channel semantics against CPU19 V2 and CPU19M tables.

This tests the CPU's transaction boundary, not peripheral circuit timing.
"""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
TEST=r'''
#include "cpu/puce/puce_state.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
struct io_fixture {
    puce_state &cpu;
    std::vector<std::string> events;
    unsigned address=0,value=0,mask=0,selector=0;
    bool flag=false;
    unsigned observed_index=0,index=0,observed_l=0;
    unsigned data=0x39,type=0x75,name=0xa6;
    void record(const char *event) {events.emplace_back(event);observed_index=cpu.indirect(index);observed_l=cpu.l[index];}
    unsigned read_byte(unsigned a){record("read-byte");address=a;return 0x96;}
    unsigned read_word(unsigned a){record("read-word");address=a;return 0x6996;}
    void write_byte(unsigned a,unsigned v){record("write-byte");address=a;value=v;}
    void write_word(unsigned a,unsigned v){record("write-word");address=a;value=v;}
    unsigned input(){record("input");return data;}
    unsigned name_type(){record("name-type");return type*256+name;}
    void output(unsigned v,unsigned m){record("output-ECOT");value=v;mask=m;data=0x8b;type=0xe2;}
    void command(unsigned v,unsigned m){record("command-ECOC-ECOT");value=v;mask=m;}
    void select(unsigned v){record("select-ECOS-ECOT");value=v;}
    void strobe(){record("ECOT");data=0xee;}
    void control(unsigned v){record("control");value=v;}
    bool ecof(){record("ECOFO");return flag;}
    void console_output(unsigned v){record("CO10");value=v;}
    unsigned console_input(unsigned s){record("CON");selector=s;return 0x80+s;}
    void console_control(unsigned v){record("CE-SUCE");value=v;}
};
int main(){
 unsigned cases=0;
 // V2 p.5 EDA/EDB: all RO0..2 encodings, all registers/data, both CPUs,
 // all interrupt levels. Distinct name/data catches accidental mux selection.
 for(bool variant : {false,true}) for(unsigned level=1;level<=4;++level)
 for(unsigned opcode : {0xb800U,0xa900U}) for(unsigned x=0;x<16;++x)
 for(unsigned low=8;low<16;++low) for(unsigned data=0;data<256;++data) {
  puce_state c;c.cpu19m=variant;c.level=level;c.di=data^0xa5;
  for(unsigned i=0;i<16;++i)c.l[i]=(i*0x1123+data*0x101)&0xffff;
  const auto before=c.l; const auto flags=c.di;
  io_fixture io{c,{}};io.data=data;io.name=data^0xff;
  assert(c.execute_channel(opcode|(x<<4)|low,io));
  for(unsigned i=0;i<16;++i) {
   unsigned want=before[i];
   if(i==x)want=opcode==0xb800 ? (want&0xff00)|data : (want&0xff)|(data<<8);
   assert(c.l[i]==want);
  }
  assert(c.di==flags && c.level==level);
  assert(io.events==std::vector<std::string>{"input"}); // one read; no strobe
  ++cases;
 }

 struct transfer{unsigned op;bool input,word;int step;};
 const transfer transfers[]={
  {0x8d08,true,false,0},{0xa108,true,false,-1},{0xa208,true,false,1},
  {0xe008,true,true,0},{0xec08,true,true,-1},{0xeb08,true,true,1},
  {0x9000,false,false,0},{0x9d00,false,false,-1},{0x9400,false,false,1},
  {0xf100,false,true,0},{0xfd00,false,true,-1},{0xf700,false,true,1}};
 for(auto t:transfers)for(unsigned r=0;r<16;++r)
 for(unsigned a:{0U,1U,255U,256U,0x7fffU,0x8000U,0xffffU})
 for(unsigned di:{0U,1U,0x55U,0xaaU,0xffU}){
  puce_state c;c.l.fill(0x5678);c.l[r]=a;c.di=di;
  auto expected=c.l;
  unsigned addr=r<12?a:a%256;
  expected[r]=r<12?(a+t.step)&65535:(a&0xff00)|((a+t.step)&255);
  io_fixture io{c,{}};io.index=r;
  assert(c.execute_channel(t.op|(r<<4),io));
  assert(c.l==expected && c.di==di && io.address==addr);
  assert(io.observed_index==(r<12?expected[r]:expected[r]%256));
  std::vector<std::string> events;
  if(t.input){
   events={"input"};if(t.word)events.push_back("name-type");
   events.push_back(t.word?"write-word":"write-byte");events.push_back("ECOT");
   assert(io.value==(t.word?0x7539U:0x39U));
  }else{
   events={t.word?"read-word":"read-byte","output-ECOT"};
   assert(io.value==(t.word?0x6996U:0x96U) && io.mask==(t.word?65535U:255U));
  }
  assert(io.events==events);++cases;
 }
 for(unsigned r=0;r<16;++r)for(unsigned flags=0;flags<256;++flags){
  for(unsigned op:{0xfb08U,0xcd08U,0xf408U,0xf208U,0xaa08U}){
   puce_state c;c.cpu19m=true;c.l.fill(0x5aa5);c.l[r]=0x1234;c.di=flags;
   auto expected=c.l;io_fixture io{c,{}};io.index=r;
   assert(c.execute_channel(op|(r<<4),io));
   std::vector<std::string> events;
   if(op==0xfb08 || op==0xcd08){
    events={"output-ECOT","input"};
    assert(io.value==0x1200 && io.mask==0xff00);
    expected[r]=0x128b;
    if(op==0xcd08){events.push_back("name-type");expected[r]=0xe28b;}
   }else if(op==0xaa08){events={"input","name-type"};expected[r]=0x7539;}
   else{
    events={"input","ECOT"};expected[r]=op==0xf408?0x1239:0x3934;
    assert(io.observed_l==expected[r]); // register latched before ECOT
   }
   assert(io.events==events && c.l==expected && c.di==flags);++cases;
  }
  for(unsigned op:{0xfc00U,0xfc02U,0xb402U,0xb104U}){
   puce_state c;c.l.fill(0x369c);c.di=flags;
   auto before=c.l;io_fixture io{c,{}};
   assert(c.execute_channel(op|(r<<4),io));
   if(op==0xfc00 || op==0xfc02){
    assert(io.value==0x369c && io.mask==0xffff);
    assert(io.events==std::vector<std::string>{op==0xfc00?"output-ECOT":"command-ECOC-ECOT"});
   }else{
    assert(io.address==(r<12?0x369cU:0x9cU) && io.value==0x96);
    assert(io.events==(std::vector<std::string>{"read-byte",op==0xb402?"command-ECOC-ECOT":"select-ECOS-ECOT"}));
    if(op==0xb402)assert(io.mask==0xff);
   }
   assert(c.l==before && c.di==flags);++cases;
  }
  for(unsigned mode=0;mode<3;++mode){
   const unsigned codes[]={0xca00,0xda01,0xea02};
   puce_state c;c.l.fill(0x12ff);c.di=flags;auto expected=c.l;
   io_fixture io{c,{}};
   assert(c.execute_channel(codes[mode]|(r<<4),io));expected[r]=0x1280+mode;
   assert(c.l==expected && c.di==flags && io.selector==mode && io.events==std::vector<std::string>{"CON"});++cases;
  }
 }
 // EDC preserves the upper nibble and DI, including the 0 -> FFF wrap.
 for(unsigned r=0;r<16;++r)for(unsigned value=0;value<65536;++value){
  puce_state c;c.l.fill(0x1357);c.l[r]=value;c.di=value&255;auto expected=c.l;
  expected[r]=(value&0xf000)|((value-1)&4095);io_fixture io{c,{}};
  assert(c.execute_channel(0xad0f|(r<<4),io));
  assert(c.l==expected && c.di==(value&255));
  if((value&4095)==1)assert(io.events==std::vector<std::string>{"control"} && io.value==0);
  else assert(io.events.empty());++cases;
 }
 for(unsigned level=1;level<=4;++level)for(unsigned low=0;low<256;++low)
 for(unsigned nibble=0;nibble<16;++nibble)for(bool take:{false,true}){
  puce_state c;c.level=level;c.set_pc(0x9aff);c.advance();c.di=0xa5;
  unsigned pc=c.pc();auto expected=c.l;io_fixture io{c,{}};io.flag=take;
  assert(c.execute_channel(0x4000|(nibble<<8)|low,io));
  unsigned target=take?(pc&0xff00)|low:pc;
  expected[c.pc_register()]=level>=3?target:(expected[c.pc_register()]&0xff00)|(target&255);
  assert(c.pc()==target && c.l==expected && c.di==0xa5);++cases;
 }
 for(unsigned n=4;n<=15;++n){
  puce_state c;c.di=0xd7;c.l.fill(0x5aa5);auto before=c.l;io_fixture io{c,{}};
  assert(c.execute_channel(0xbd00|(n<<4),io));
  assert(io.events==std::vector<std::string>{n==4?"ECOT":n<11?"control":"CE-SUCE"});
  if(n!=4)assert(io.value==n);assert(c.l==before && c.di==0xd7);++cases;
 }
 for(unsigned x=0;x<16;++x)for(unsigned y=0;y<16;++y){
  puce_state c;for(unsigned i=0;i<16;++i)c.l[i]=(i*257)^0xa55a;
  c.di=0x53;auto before=c.l;io_fixture io{c,{}};
  assert(c.execute_channel(0xfa00|(x<<4)|y,io));
  assert(io.value==((before[y]&0xff00)|(before[x]&255)) && io.events==std::vector<std::string>{"CO10"});
  assert(c.l==before && c.di==0x53);++cases;
 }
 for(unsigned op:{0xcd08U,0xf408U,0xf208U,0xaa08U}){
  puce_state c;io_fixture io{c,{}};assert(!c.execute_channel(op,io) && io.events.empty());
 }
 std::cout<<"PASS: "<<cases<<" channel semantics cases (CPU transaction boundary, not board timing)\n";
}
'''
if __name__=='__main__':
 with tempfile.TemporaryDirectory(prefix='puce-channel-') as temp:
  source=Path(temp)/'test.cpp';binary=Path(temp)/'test';source.write_text(TEST)
  subprocess.run(['c++','-std=c++20','-O2','-Wall','-Wextra','-Werror','-I',str(ROOT/'src/devices'),str(source),'-o',str(binary)],check=True)
  subprocess.run([str(binary)],check=True)
