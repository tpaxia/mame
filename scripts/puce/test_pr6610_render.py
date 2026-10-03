#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders: Salvatore Paxia
"""Check production PR6610 rendering against independent pixel coordinates, no ROM."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
text = (ROOT / "src/devices/bus/p6066/pr6610.cpp").read_text()


def method(signature):
    start = text.index(signature)
    opening = text.index("{", start)
    end, depth = opening + 1, 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end] + "\n"


source = r"""
#include "bus/p6066/goino_state.h"
#include <cassert>
#include <cstdio>
#include <tuple>
#include <vector>
using u8 = std::uint8_t;
#define BIT(v, n) (((v) >> (n)) & 1)
#define INPUT_CHANGED_MEMBER(name) void name(bool newval)
struct bitmap_stub {
 int m_xpos=28, m_ypos=24;
 std::vector<std::tuple<int,int,unsigned>> pixels;
 void draw_pixel(int x,int y,unsigned color){pixels.emplace_back(x,y,color);}
 void check_new_page(){}
};
struct p6066_pr6610_device {
 static constexpr int LEFT_MARGIN=28, COLUMN_PITCH=3, PAPER_WIDTH=1736, FEED_HALF=7;
 bitmap_stub bitmap, *m_bitmap=&bitmap;
 unsigned m_columns_cached=0,m_feed_events_cached=0,m_feed_rows_total=0,m_feed_half=0;
 unsigned m_columns_rendered=0,m_feed_rows=0,m_head_x=0,m_head_y=0;
 bool m_resync=true;
 void render_column(u8);
 void advance_feed();
 INPUT_CHANGED_MEMBER(manual_feed);
 void tick(p6066_goino_state &);
};
"""
for signature in (
    "void p6066_pr6610_device::render_column(",
    "void p6066_pr6610_device::advance_feed(",
    "INPUT_CHANGED_MEMBER(p6066_pr6610_device::manual_feed)",
    "void p6066_pr6610_device::tick(",
):
    source += method(signature)
source += r"""
int main(){
 p6066_pr6610_device card;
 p6066_goino_state state;
 // FEED is local to the paper: one 35-pixel line per press, not a column
 // transfer or a GOINO feed event. Releasing it does not advance again.
 p6066_pr6610_device manual;
 manual.manual_feed(true);
 assert(manual.bitmap.m_ypos==59 && manual.m_feed_rows==35 && manual.m_head_y==59);
 assert(manual.bitmap.pixels.empty() && state.printer_feed_events==0);
 manual.manual_feed(false);
 assert(manual.bitmap.m_ypos==59);
 manual.manual_feed(true);
 assert(manual.bitmap.m_ypos==94 && manual.m_feed_rows==70);
 state.printer_attached=true;
 state.printer_running=true;
 state.printer_columns_left=2;
 card.tick(state); // first tick establishes the baseline, requests a column
 assert(state.column_request && card.bitmap.pixels.empty());
 for(unsigned column:{1U,64U}){
  state.owned2=true;
  assert(state.data(column,2));
  state.end(2);
  if(column==64)state.printer_running=false;
  card.tick(state);
 }
 assert(state.printer_columns_discarded==2 && card.m_columns_rendered==2);
 assert(card.bitmap.pixels.size()==8); // two 2x2 dots
 for(unsigned dx=0;dx<2;++dx)for(unsigned dy=0;dy<2;++dy){
  assert(card.bitmap.pixels[dy*2+dx]==std::make_tuple(28+int(dx),24+int(dy),0U));
 assert(card.bitmap.pixels[4+dy*2+dx]==std::make_tuple(31+int(dx),42+int(dy),0U));
 }
 assert(card.m_head_x==28); // return to left margin at end of transfer
 state.printer_feeding=true;
 card.tick(state);
 assert(card.m_feed_rows==3 && card.m_head_y==27 && card.m_head_x==28);
 state.matrix_request=false;
 card.tick(state);
 assert(card.m_feed_rows==7 && card.m_head_y==31);
 // FISTN/end of transfer and FAINN/feed can overlap. There may be no
 // non-feeding idle tick before the next line starts.
 p6066_pr6610_device next_line;
 p6066_goino_state overlap;
 overlap.printer_attached=true;
 overlap.printer_running=true;
 next_line.tick(overlap); // establish independent counter baseline
 next_line.bitmap.m_xpos=31; // one served column on the outgoing line
 overlap.printer_running=false;
 overlap.printer_feeding=true;
 overlap.matrix_request=false;
 next_line.tick(overlap);
 assert(next_line.m_head_x==28 && next_line.m_feed_rows==3);
 overlap.printer_feeding=false;
 overlap.printer_running=true;
 overlap.matrix_request=false;
 overlap.owned2=true;
 assert(overlap.data(1,2));
 overlap.end(2);
 next_line.tick(overlap);
 assert(std::get<0>(next_line.bitmap.pixels.front())==28);
 std::puts("PASS: production PR6610 dot orientation, carriage return and half-pixel feeds");
}
"""
with tempfile.TemporaryDirectory(prefix="p6066-pr6610-render-") as directory:
    cpp = Path(directory) / "test.cpp"
    binary = Path(directory) / "test"
    cpp.write_text(source)
    subprocess.run(["c++", "-std=c++17", "-O2", "-I", str(ROOT / "src/devices"),
                    str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
