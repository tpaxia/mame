#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders: Salvatore Paxia
"""Test the live backplane interrupt arbiter: priority, nesting and ownership."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
TEST=r'''
#include "bus/p6066/arbiter.h"
#include <cassert>
#include <iostream>
int main()
{
    p6066_irq_arbiter a;
    for(unsigned mask=0;mask<16;++mask) for(unsigned current=1;current<=4;++current)
    {
        a.reset(); a.requests[9]=mask;
        unsigned expected=0;
        if(current>1 && (mask&1)) expected=1;
        else if(current>2 && (mask&2)) expected=2;
        else if(current>3 && (mask&4)) expected=3;
        else if(current>3 && (mask&8)) expected=4;
        assert(a.next(current)==expected);
    }
    a.reset(); a.requests[12]=4; a.requests[1]=8;
    assert(a.next(4)==3 && a.acknowledge(3)==12); // 3A outranks physically earlier 3B
    a.requests[12]=0; assert(a.owners[3]==12 && a.next(3)==0);
    a.requests[8]=2; a.requests[2]=2;
    assert(a.next(3)==2 && a.acknowledge(2)==2); // physical slot breaks tie
    a.requests[2]=0; a.requests[7]=1;
    assert(a.next(2)==1 && a.acknowledge(1)==7);
    assert(a.owners[3]==12 && a.owners[2]==2 && a.owners[1]==7);
    assert(a.acknowledge(1)==-1 && a.next(1)==0);
    assert(a.release(1)==7 && a.release(2)==2 && a.release(3)==12);
    a.requests[7]=0; a.requests[8]=0;
    assert(a.next(4)==4 && a.acknowledge(4)==1);
    assert(a.release(4)==-1 && a.release(0)==-1 && a.acknowledge(0)==-1 && a.acknowledge(5)==-1);
    a.reset(); assert(a.next(4)==0 && a.owners[3]==-1);
    std::cout<<"PASS: all request masks/levels, 3A over 3B, physical priority, nested ownership and reset\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
    source=Path(tmp)/'test.cpp'; binary=Path(tmp)/'test'; source.write_text(TEST)
    subprocess.run(['c++','-std=c++17','-O2','-I',str(ROOT/'src/devices'),str(source),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
