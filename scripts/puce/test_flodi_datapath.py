#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders: Salvatore Paxia
"""Independent synthetic byte/flux stimuli through production FLODI callbacks.

Uses the protocol harness's device/timer boundary. These tests inspect flux
calls; they do not claim that an IMD container was written or read back.
"""
from pathlib import Path
import importlib.util
import subprocess
import tempfile
import binascii
import os
import struct
import zlib

spec=importlib.util.spec_from_file_location('protocol',Path(__file__).with_name('test_flodi_protocol.py'))
protocol=importlib.util.module_from_spec(spec)
spec.loader.exec_module(protocol)
source="#include <cstdlib>\n"+protocol.source
source=source.replace('struct machine_stub {attotime time(){return {};}};\n machine_stub machine(){return {};}',
    'attotime now;struct machine_stub {attotime now;attotime time(){return now;}};\n machine_stub machine(){return {now};}')
source=source.replace('void write_start(attotime){}void write_flux_change(attotime){}void write_end(attotime){}',
    'std::vector<long long> flux;long long start=-1,end=-1;bool writing=false;\n'
    ' void write_start(attotime t){start=t.ns;writing=true;}\n'
    ' void write_flux_change(attotime t){assert(writing);flux.push_back(t.ns);}\n'
    ' void write_end(attotime t){end=t.ns;writing=false;}')
# Include actual ID search too; only flux acquisition and host clock are stubs.
source=source.replace('static const attotime never;', 'static const attotime never,zero;')
source=source.replace('friend bool operator<(attotime a,attotime b)',
    'unsigned long long as_ticks(unsigned long long rate)const{return ns*rate/1000000000;}\n'
    ' attotime &operator+=(attotime b){ns+=b.ns;return *this;}\n'
    ' attotime &operator-=(attotime b){ns-=b.ns;return *this;}\n'
    ' friend bool operator>=(attotime a,attotime b){return a.ns>=b.ns;}\n'
    ' friend bool operator<(attotime a,attotime b)')
source=source.replace('const attotime attotime::never', 'const attotime attotime::zero{0};\nconst attotime attotime::never')
rotation=protocol.method('src/devices/bus/p6066/flodi_rotation.h','inline unsigned rotational_cell(')
source=source.replace('void p6066_flodi_device::next_id(){++searches;}',
    'namespace p6066_fm {'+rotation+'}\n'+protocol.method('src/devices/bus/p6066/flodi.cpp','void p6066_flodi_device::next_id('))
# Reference CRCs come from Python's independent standard-library implementation.
crcs=[]
for length in (128,256):
    payload=bytes((i*37+11)&255 for i in range(length))
    crcs.extend(binascii.crc_hqx(bytes([mark])+payload,65535) for mark in (0xfb,0xf8))
fixtures=r'''
static const unsigned reference_crc[]={CRC_VALUES};
void put(p6066_flodi_device &f,unsigned byte,unsigned value,unsigned clock=255){
 for(unsigned bit=0;bit<8;++bit){f.m_bits[byte*16+bit*2]=(clock>>(7-bit))&1;f.m_bits[byte*16+bit*2+1]=(value>>(7-bit))&1;}
}
void tick(p6066_flodi_device &f){f.now=f.m_byte_time;if(f.m_write_close_pending&&!(f.now<f.m_write_end_time))f.write_gate_tick();f.byte_tick();}
void service(p6066_flodi_device &f){f.interrupt_sync(2);f.irq_ack(0);}
void end(p6066_flodi_device &f){f.interrupt_sync(2);f.irq_end(1);}
void datapath_tests(){
 unsigned checks=0;
 // Exhaustive one-byte truth table, including FF wildcard and >=/<= equality.
 for(unsigned op=4;op<=6;++op)for(unsigned d=0;d<256;++d)for(unsigned k=0;k<256;++k){
  p6066_flodi_scan s;u8 num=0;s.begin(255);s.compare(d,k,op,255,num);
  bool match=k==255||d==k||(op==5&&d<k)||(op==6&&d>k);
  assert(s.found==match);assert(num==!match);
  if(match) assert(s.different==(k!=255&&d!=k));++checks;
 }
 // A later opposite inequality cannot replace the first significant difference.
 for(unsigned op=4;op<=6;++op){
  p6066_flodi_scan s;u8 n=0;s.begin(254);
  s.compare(0x10,0x20,op,254,n);s.compare(0xf0,0x01,op,254,n);
  assert(s.found==(op==5));assert(n==(op==5?0:1));++checks;
 }
 // Full-length mask, wildcard at an unequal byte, failed fields then a match,
 // frozen result, modulo-256 counter, and per-sector reset.
 for(unsigned length: {1U,6U,256U}){
  p6066_flodi_scan s;u8 n=0,mas=-length;s.begin(mas);
  for(unsigned field=0;field<3;++field)for(unsigned i=0;i<length;++i)
   s.compare(i?0x22:0x44,i?0xff:0x11,4,mas,n);
  assert(n==3&&!s.found);
  for(unsigned i=0;i<length;++i)s.compare(0x22,0xff,4,mas,n);
  assert(s.found&&!s.different&&n==3);
  for(unsigned i=0;i<300;++i)s.compare(0,1,4,mas,n);
  assert(n==3&&s.found);s.begin(mas);assert(!s.found&&!s.different);++checks;
 }
 {p6066_flodi_scan s;u8 n=0;s.begin(255);for(unsigned i=0;i<256;++i)s.compare(0,1,4,255,n);assert(!n&&!s.found);++checks;}
 // Actual read/verify callbacks, ECOF assert/clear and final IRQ. Identical
 // ID size byte for 128 and 256-byte physical records; good and corrupt CRC.
 for(unsigned len: {128U,256U})for(unsigned mark: {0xfbU,0xf8U})
 for(unsigned bad=0;bad<2;++bad)for(unsigned op: {0x80U,0x82U}){
  p6066_flodi_device f;f.m_latches.command=op;f.m_latches.cote=false;
  f.m_reading=true;f.m_last_sector=true;f.m_bit_count=83333;f.m_data_pos=160;
  f.m_data_mark=mark;f.m_header[4]=0;
  for(unsigned i=0;i<len;++i)put(f,11+i,(i*37+11)&255);
  unsigned crc=reference_crc[(len==256?2:0)+(mark==0xf8)];
  put(f,11+len,crc>>8);put(f,12+len,(crc&255)^bad);
  unsigned assert_at=len-1+(op==0x82);
  for(unsigned i=0;i<=assert_at+3;++i){
   tick(f);service(f);
   if(op==0x80&&i<len)assert(f.input_data(1)==((i*37+11)&255));
   else f.output_data(1,i^0x5a); // verify data has no comparison meaning
   if(i==assert_at)f.control(1,7);
   if(i==assert_at+2)f.control(1,8);
   assert(!f.m_irq.fine);end(f);
   if(i<assert_at+3)assert(f.m_reading&&!f.m_irq.fine);
  }
  assert(f.m_length==len&&!f.m_reading&&f.m_irq.fine);
  assert(bool(f.m_end_status&64)==bool(bad));assert(f.m_data_crc==!bad);++checks;
 }
 // Sector writing must emit encoded transition times, mark, payload and two
 // *generated* CRC bytes. Decode captured transitions without production FM.
 for(unsigned len: {128U,256U})for(unsigned mark: {0xfbU,0xf8U}){
  p6066_flodi_device f;f.m_latches.command=0x81;f.m_reading=true;f.m_last_sector=true;
  f.m_bit_count=83333;f.m_expected_mark=mark;f.m_expected_clock=0xc7;
  f.m_write_preamble=7;f.m_payload_start=true;
  for(unsigned i=0;i<7;++i){tick(f);assert(!f.m_irq.rili);
   if(i==0)assert(f.erase.next.ns==128000&&!f.m_erase_active);
   if(i==4){f.erase_tick();assert(f.m_erase_active&&f.m_erase_output);}
  }
  for(unsigned i=0;i<=len+3;++i){
   tick(f);service(f);f.output_data(1,(i*37+11)&255);
   if(i==len)f.control(1,7);if(i==len+2)f.control(1,8);end(f);
  }
  assert(f.m_write_close_pending&&f.m_writing);f.now=f.m_write_end_time;f.write_gate_tick();
  assert(f.erase.next.ns==464000&&f.m_erase_active);f.erase_tick();assert(!f.m_erase_active);
  auto &d=f.m_drives[0]->media;
  assert(d.start==0&&d.end==static_cast<long long>(7+1+len+2)*32000+8000);
  std::vector<unsigned> cells((7+1+len+2)*16,0);
  for(auto t:d.flux){assert(t%2000==1000);unsigned p=t/2000;if(p<cells.size())cells[p]=1;else assert(t==d.end-7000||t==d.end-3000);}
  std::vector<unsigned> bytes,clocks;
  for(unsigned p=0;p<cells.size();p+=16){unsigned v=0,c=0;for(unsigned bit=0;bit<8;++bit){v=v*2+cells[p+bit*2+1];c=c*2+cells[p+bit*2];}bytes.push_back(v);clocks.push_back(c);}
  for(unsigned i=0;i<7;++i)assert(bytes[i]==0&&clocks[i]==255);
  assert(bytes[7]==mark&&clocks[7]==0xc7);
  for(unsigned i=0;i<len;++i)assert(bytes[8+i]==((i*37+11)&255)&&clocks[8+i]==255);
  unsigned crc=reference_crc[(len==256?2:0)+(mark==0xf8)];
  assert(bytes[8+len]==(crc>>8)&&bytes[9+len]==(crc&255));
  assert(f.m_irq.fine&&!f.m_reading&&!f.m_writing);
  {
   std::string path=std::string(std::getenv("FLODI_TEST_FLUX"))+"-"+std::to_string(len)+"-"+std::to_string(mark);
   FILE *out=fopen(path.c_str(),"w");assert(out);
   fprintf(out,"%lld\n",d.end);for(auto t:d.flux)fprintf(out,"%lld\n",t);fclose(out);
  }
  ++checks;
 }
 // Actual scan callback pipeline and status -> NUM after completion/clear.
 for(unsigned op: {0x84U,0x85U,0x86U}){
  p6066_flodi_device f;f.m_latches.command=op;f.m_latches.mas=250;f.m_latches.cote=false;
  f.m_scan.begin(250);f.m_reading=true;f.m_bit_count=83333;f.m_data_mark=0xfb;f.m_data_pos=160;
  for(unsigned i=0;i<128;++i)put(f,11+i,(i*37+11)&255);
  put(f,139,reference_crc[0]>>8);put(f,140,reference_crc[0]&255);
  for(unsigned i=0;i<=132;++i){
   tick(f);service(f);f.output_data(1,255);
   if(i==129)f.control(1,7);if(i==131)f.control(1,8);
   assert(!f.m_irq.fine);end(f);
  }
  assert(f.m_length==128&&f.m_scan.found&&!f.m_scan.different&&f.m_latches.num==0);
  assert(f.m_irq.fine&&!f.m_reading&&!f.m_end_status);
  f.interrupt_sync(8);f.irq_ack(2);assert(f.input_data(3)==8);
  f.command(3,0);assert(f.input_data(3)==0);++checks;
 }



 // All low-bit decoder combinations start through real PRICO/MAS strobes.
 // 87 has both comparison predicates wired; it is not a documented opcode.
 for(unsigned op=0x80;op<=0x87;++op){
  p6066_flodi_device f;f.request3(8);f.interrupt_sync(8);f.irq_ack(2);
  f.command(3,op);f.command(3,250);
  assert(f.m_reading&&!f.m_latches.cote&&f.m_latches.mas==250);
  assert(f.m_format_wait==(op==0x83));assert(f.m_irq.rili==(op==0x83));
  f.interrupt_sync(8);f.irq_end(3);f.request3(8);f.interrupt_sync(8);f.irq_ack(2);
  f.command(3,op);f.command(3,250);assert(f.m_last_sector&&!f.m_irq.fine);
  f.controller_reset(true);assert(!f.m_reading&&!f.m_format_wait&&!f.m_irq.fine);++checks;
 }
 // Changer hardware handshake is external: no automatic success/time delay.
 {p6066_flodi_device f;f.select(0xe0);f.irq_ack(3);f.command(3,0x62);
  assert(!f.changer_request()&&!f.m_latches.busy(f.m_changer_busy));
  f.changer_door_w(true);assert(f.changer_request());
  f.changer_busy_w(true);f.select(0x60);assert(f.m_selected==1);
  f.changer_ack_w(true);assert(!f.changer_request()&&!f.m_latches.command&&f.m_inop);
  assert(f.m_latches.busy(f.m_changer_busy));f.changer_busy_w(false);
  assert(!f.m_latches.busy(f.m_changer_busy));
  f.m_active_type=3;assert((f.input_data(3)&8)!=0);f.strobe(3);assert(!f.m_inop);
  f.m_local[0]=f.m_local[1]=true;f.media_loaded(f.m_drives[0]->get_device());
  assert(!f.m_local[0]&&!f.m_local[1]&&f.m_inop);f.controller_reset(true);assert(!f.m_local[1]);++checks;
 }
 // ID search: arbitrary fifth identifier byte, no predecoded length. These
 // CRC values are generated separately using Python binascii, below.
 for(unsigned mark: {0xfbU,0xf8U,0xfaU,0xf9U})for(unsigned size: {0U,1U,255U})for(unsigned op: {0x80U,0x81U,0x82U,0x84U,0x85U,0x86U}){
  p6066_flodi_device f;f.m_latches.command=op;f.m_reading=true;f.m_bit_count=83333;
  unsigned idcrc=size==0?ID_CRC_0:(size==1?ID_CRC_1:ID_CRC_FF);
  unsigned header[]={0xfe,2,0,3,size,idcrc>>8,idcrc&255};
  for(unsigned i=0;i<400;++i)put(f,i,255);
  for(unsigned i=0;i<7;++i)put(f,10+i,header[i],i?255:0xc7);
  // Writes must create their own mark even when there is none to read.
  if(op!=0x81)put(f,34,mark,0xc7);
  f.next_id();assert(f.m_id_pos==160&&f.m_id_crc&&f.m_length==0);
  for(unsigned i=0;i<8;++i){tick(f);service(f);
   f.output_data(1,i<5?header[i]:(i==5?0xc7:(i==7?0xfb:0)));
   if(i==4)f.control(1,7);if(i==6)f.control(1,8);end(f);
  }
  assert(!f.m_id_phase&&!f.m_mismatch&&f.m_length==0);
  const unsigned mark_status=mark==0xfb?0:mark==0xf8?128:mark==0xfa?64:192;
  assert(f.m_end_status==(op==0x81?0:mark_status));
  long long expected=(op==0x81?27:op==0x80?35:34)*32000LL;
  assert(f.m_byte_time.ns==expected);++checks;
 }
 // Reset aborts recording at the current time; it must not write a whole
 // cached sector after reset, or manufacture a successful completion.
 {p6066_flodi_device f;f.m_latches.command=0x81;f.m_reading=true;f.m_bit_count=83333;f.m_write_preamble=7;
  tick(f);assert(f.m_writing);f.now={12000};f.controller_reset(true);
  assert(f.drive()->end==12000&&!f.m_writing&&!f.m_irq.fine&&!f.m_reading);++checks;
 }
 // Write-track runs on index, independent of any existing sector map. The
 // CPU provides clocks and ID CRC verbatim. Exercise both fill conventions.
 for(unsigned fill: {0U,255U}){
  p6066_flodi_device f;f.m_latches.command=0x83;f.m_latches.cote=false;
  f.start_transfer();assert(f.m_format_wait&&f.m_irq.rili&&!f.m_writing);
  service(f);assert(f.name_type(1)==0x60);f.output_data(1,fill);end(f);
  assert(f.m_format_wait&&!f.m_writing);
  f.index_changed(f.drive(),1);assert(f.m_writing&&f.m_format_active);
  // Clock precedes mark in figures 25/26. Two keys demonstrate that COCI
  // re-arms key detection; ID CRC deliberately bad, and must stay bad.
  std::vector<unsigned> stream{fill,fill,0,0,0xd7,0xfc,fill,fill,fill,fill,fill,
      0,0,0xc7,0xfe,0x23,0,0x07,0,0x12,0x34,fill,fill,fill,fill};
  for(unsigned i=0;i<stream.size();++i){
   tick(f);service(f);f.output_data(1,stream[i]);
   if(i==6||i==21)f.control(1,7);
   if(i==8||i==23)f.control(1,8);
   if(i==20)f.start_transfer(); // INCO -> USETO during current field.
   end(f);
  }
  assert(!f.m_format_stream&&f.m_format_active&&f.m_writing&&!f.m_irq.fine);
  for(unsigned i=0;i<8;++i){tick(f);assert(!f.m_irq.rili&&!f.m_irq.fine);}
  f.now=f.m_byte_time;f.index_changed(f.drive(),1);
  assert(!f.m_writing&&!f.m_reading&&f.m_irq.fine);
  auto &d=f.m_drives[0]->media;std::vector<unsigned> cells(d.end/2000,0);
  for(auto t:d.flux)if(t<d.end)cells[t/2000]=1;
  std::vector<unsigned> values,clocks;
  for(unsigned p=0;p+16<=cells.size();p+=16){unsigned v=0,c=0;for(unsigned bit=0;bit<8;++bit){v=v*2+cells[p+bit*2+1];c=c*2+cells[p+bit*2];}values.push_back(v);clocks.push_back(c);}
  assert(values[0]==255&&clocks[0]==255&&values[1]==fill);
  unsigned keys=0;
  for(unsigned i=0;i<values.size();++i)if(clocks[i]!=255){
   if(!keys){assert(clocks[i]==0xd7&&values[i]==0xfc);}
   else {assert(keys==1&&clocks[i]==0xc7&&values[i]==0xfe);
    assert(values[i+1]==0x23&&values[i+2]==0&&values[i+3]==7&&values[i+4]==0);
    assert(values[i+5]==0x12&&values[i+6]==0x34);}
   ++keys;
  }
  if(keys!=2){fprintf(stderr,"fill=%u keys=%u\n",fill,keys);for(unsigned j=0;j<values.size();++j)fprintf(stderr,"%u:%02x/%02x ",j,clocks[j],values[j]);}assert(keys==2);for(unsigned i=values.size()-8;i<values.size();++i)assert(values[i]==fill&&clocks[i]==255);
  ++checks;
 }
 printf("PASS: %u independent comparator/CRC/flux/callback cases (synthetic drive boundary)\n",checks);
}
'''.replace('CRC_VALUES',','.join(map(str,crcs)))
for size,label in ((0,'ID_CRC_0'),(1,'ID_CRC_1'),(255,'ID_CRC_FF')):
    fixtures=fixtures.replace(label,str(binascii.crc_hqx(bytes([254,2,0,3,size]),65535)))
source=source.replace('int main(){',fixtures+'\nint main(){datapath_tests();')
with tempfile.TemporaryDirectory(prefix='p6066-flodi-datapath-') as tmp:
    path=Path(tmp)/'test.cpp';path.write_text(source)
    binary=Path(tmp)/'test'
    subprocess.run(['c++','-std=c++17','-O2','-I',str(protocol.ROOT/'src/devices'),str(path),'-o',str(binary)],check=True)
    flux=Path(tmp)/'write-flux.txt'
    subprocess.run([str(binary)],check=True,env={**os.environ,'FLODI_TEST_FLUX':str(flux)})
    # Feed actual controller-generated transitions into MAME's independent
    # MFI loader/FM decoder/IMD writer. Surround the new record with a fixed
    # valid ID and untouched FM gap. This is a format round-trip, not a test
    # of floppy_image_device's write-buffer/save-state internals.
    for length,mark in ((128,251),(128,248),(256,251),(256,248)):
        times=list(map(int,Path(str(flux)+f"-{length}-{mark}").read_text().splitlines()))
        cells=[1]*83333
        def emit(byte,value,clock=255):
            for bit in range(8):
                cells[byte*16+2*bit]=(clock>>(7-bit))&1
                cells[byte*16+2*bit+1]=(value>>(7-bit))&1
        header=bytes([254,0,0,1,0 if length==128 else 1])
        crc=binascii.crc_hqx(header,65535)
        for i,v in enumerate(header+crc.to_bytes(2,'big')):
            emit(80+i,v,0xc7 if i==0 else 255)
        start=(80+17)*32000
        for cell in range(start//2000,(start+times[0])//2000):cells[cell]=0
        for t in times[1:]:cells[(start+t)//2000]=1
        # MFI stores flux deltas in 1/200,000,000 revolution units.
        transitions=[i*2400+1200 for i,v in enumerate(cells) if v]
        deltas=[];previous=0
        for t in transitions:deltas.append(t-previous);previous=t
        raw=struct.pack('<'+str(len(deltas))+'I',*deltas);compressed=zlib.compress(raw)
        mfi=Path(tmp)/f'written-{length}-{mark}.mfi';imd=Path(tmp)/f'written-{length}-{mark}.imd'
        mfi.write_bytes(b'MAMEFLOPPYIMAGE'.ljust(16,b'\0')+struct.pack('<IIII',1,1,0x20202038,0x44535353)
            +struct.pack('<IIII',48,len(compressed),len(raw),start*6//5)+compressed)
        subprocess.run([str(protocol.ROOT/'floptool'),'flopconvert','mfi','imd',str(mfi),str(imd)],check=True)
        data=imd.read_bytes();pos=data.index(b'\x1a')+1
        mode,cyl,head,count,size=data[pos:pos+5];pos+=5
        assert (cyl,head&1,count,size)==(0,0,1,0 if length==128 else 1),(mode,cyl,head,count,size)
        assert data[pos]==1;pos+=count
        if head&128:pos+=count
        if head&64:pos+=count
        assert data[pos]==(1 if mark==251 else 3), 'normal/deleted record with good CRC'
        assert data[pos+1:pos+1+length]==bytes((i*37+11)&255 for i in range(length))
    print('PASS: controller write flux -> MAME MFI/FM decoder -> IMD payload and CRC')
