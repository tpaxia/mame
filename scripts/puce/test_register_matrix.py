#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders: Salvatore Paxia
"""CPU19 register instruction matrix, independently specified expected effects."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
TEST=r'''
#include "cpu/puce/puce_state.h"
#include <cassert>
#include <iostream>
int main(){
 unsigned long count=0;
 // Unary instructions: every 16-bit register value at every destination.
 const unsigned unary[]={0xab0f,0xbb0f,0xcb0f,0xdb0f,0x850f,0x950f,0xa50f,
  0xae0f,0xbe0f,0xe50f,0x8e0f,0x9e0f,0xf50f,0x8b0f,0x9b0f,
  0x830f,0x930f,0xc50f,0xd50f,0xa30f,0xb30f,
  0xc300,0xd300,0xc400,0xd400,0xc301,0xd301,0xc401,0xd401};
 for(unsigned r=0;r<16;++r)for(unsigned v=0;v<65536;++v)
 for(unsigned op:unary){
  puce_state c;c.l.fill(0x5aa5);c.l[r]=v;c.di=(v^(v>>8))&255;
  auto regs=c.l;unsigned flags=c.di,a=v%256,b=v/256,result=v;
  auto set_zero=[&](bool z){flags=(flags&253)|(z?2:0);};
  switch(op){
   case 0xab0f: result=v&0xfff0;break;case 0xbb0f:result=v&0xff0f;break;
   case 0xcb0f: result=v&0xf0ff;break;case 0xdb0f:result=v&0x0fff;break;
   case 0x850f: result=(v&0xff00)|((a+1)%256);break;
   case 0x950f: result=a+((b+1)%256)*256;break;
   case 0xa50f: result=(v+1)%65536;break;
   case 0xae0f: result=(v&0xff00)|((a+255)%256);set_zero(a==1);break;
   case 0xbe0f: result=a+((b+255)%256)*256;set_zero(b==1);break;
   case 0xe50f: result=(v+65535)%65536;set_zero(v==1);break;
   case 0x8e0f: set_zero(a==0);break;case 0x9e0f:set_zero(b==0);break;
   case 0xf50f: set_zero(v==0);break;
   case 0x8b0f: result=(v&0xff00)+a/16+(a%16)*16;break;
   case 0x9b0f: result=a+(b/16+(b%16)*16)*256;break;
   case 0x830f:flags=a;break;case 0x930f:flags=b;break;
   case 0xc50f:result=(v&0xff00)+flags;break;case 0xd50f:result=a+flags*256;break;
   case 0xa30f:result=(v&0xff00)+flags;flags=a;break;
   case 0xb30f:result=a+flags*256;flags=b;break;
   default:{
    bool upper=(op>>12)==13,left=((op>>8)&15)==4,long_shift=(op&15)==1;
    unsigned source=upper?b:a,in=long_shift?flags%2:0;
    unsigned outgoing=left?source/128:source%2;
    unsigned shifted=left?(source*2+in)%256:source/2+in*128;
    result=upper?a+shifted*256:(v&0xff00)+shifted;
    flags=(flags&254)+outgoing;
   }
  }
  regs[r]=result;assert(c.execute_register(op|(r<<4)));
  assert(c.l==regs && c.di==flags);++count;
 }
 // Logical operations: exhaustive byte operands with aliased/separate halves.
 for(unsigned alias=0;alias<2;++alias)for(unsigned a=0;a<256;++a)for(unsigned b=0;b<256;++b)
 for(unsigned family=0;family<3;++family)for(unsigned dest=0;dest<3;++dest){
  puce_state c;unsigned x=3,y=alias?3:4;c.l.fill(0x56a9);c.set_a(x,a);c.set_b(y,b);
  c.di=(a^b);auto regs=c.l;
  unsigned value=family==0?(a&b):family==1?(a|b):(a^b);
  const unsigned codes[3][3]={{0x9700,0xa700,0xb700},{0xe600,0xf600,0x8700},{0xc700,0xd700,0xe700}};
  unsigned flags=(c.di&253)|(value==0?2:0);
  if(dest==1)regs[x]=(regs[x]&0xff00)|value;
  if(dest==2)regs[y]=(regs[y]&255)|(value<<8);
  assert(c.execute_register(codes[family][dest]|(x<<4)|y));
  assert(c.l==regs && c.di==flags);++count;
 }
 // Half-register transfers and exchanges: every selector pair and byte value.
 for(unsigned x=0;x<16;++x)for(unsigned y=0;y<16;++y)for(unsigned v=0;v<256;++v)
 for(unsigned hi:{0xd8U,0xe9U,0xe8U,0xd9U,0xf9U,0xf8U,0xbaU,0xbcU}){
  puce_state c;for(unsigned r=0;r<16;++r)c.l[r]=((v+r*17)*257)^0x5aa5;
  c.di=v;auto regs=c.l;
  unsigned ax=regs[x]&255,by=regs[y]>>8;
  switch(hi){
   case 0xd8:regs[y]=(regs[y]&255)|(ax<<8);break;
   case 0xe9:regs[x]=(regs[x]&0xff00)|by;break;
   case 0xe8:regs[y]=(regs[y]&0xf0ff)|((ax&15)<<8);break;
   case 0xd9:regs[y]=(regs[y]&0x0fff)|((ax&240)<<8);break;
   case 0xf9:regs[x]=(regs[x]&0xfff0)|(by&15);break;
   case 0xf8:regs[x]=(regs[x]&0xff0f)|(by&240);break;
   case 0xba:regs[x]=(regs[x]&0xff00)|by;regs[y]=(regs[y]&255)|(ax<<8);break;
   case 0xbc:if(x!=y){unsigned oldx=regs[x];regs[x]=(regs[y]>>8)|((regs[y]&255)<<8);regs[y]=(oldx>>8)|((oldx&255)<<8);}break;
  }
  assert(c.execute_register((hi<<8)|(x<<4)|y));assert(c.l==regs && c.di==v);++count;
 }
 for(unsigned mask=0;mask<256;++mask)for(unsigned flags=0;flags<256;++flags){
  for(bool set:{false,true}){puce_state c;c.di=flags;auto regs=c.l;
   assert(c.execute_register((set?0xc900:0xc800)|mask));
   assert(c.di==(set?(flags|mask):(flags&(~mask&255))) && c.l==regs);++count;}
 }
 for(unsigned r=0;r<16;++r)for(unsigned value=0;value<256;++value)for(bool upper:{false,true}){
  puce_state c;c.l.fill(0xa55a);c.di=value;auto regs=c.l;
  regs[r]=upper?0x5a+value*256:0xa500+value;
  assert(c.execute_register((upper?0x5000:0x7000)|(r<<8)|value));assert(c.l==regs && c.di==value);++count;
 }
 std::cout<<"PASS: "<<count<<" CPU19 register matrix cases\n";
}
'''
if __name__=='__main__':
 with tempfile.TemporaryDirectory(prefix='puce-register-') as temp:
  source=Path(temp)/'test.cpp';binary=Path(temp)/'test';source.write_text(TEST)
  subprocess.run(['c++','-std=c++20','-O2','-Wall','-Wextra','-Werror','-I',str(ROOT/'src/devices'),str(source),'-o',str(binary)],check=True)
  subprocess.run([str(binary)],check=True)
