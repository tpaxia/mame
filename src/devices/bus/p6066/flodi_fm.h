// license:BSD-3-Clause
// copyright-holders: Salvatore Paxia
#ifndef MAME_BUS_P6066_FLODI_FM_H
#define MAME_BUS_P6066_FLODI_FM_H
#pragma once
#include <cstdint>

// Logical FM cells, clock followed by data, MSB first. Used with actual drive
// transitions; no dependency on an image container or a fabricated sector map.
namespace p6066_fm {
inline std::uint16_t crc_byte(std::uint16_t crc, std::uint8_t data)
{
	crc ^= std::uint16_t(data)<<8;
	for (unsigned i=0;i<8;++i) crc=(crc<<1)^((crc&0x8000)?0x1021:0);
	return crc;
}
template <typename Bits> std::uint16_t word(const Bits &bits,unsigned count,unsigned pos)
{
	std::uint16_t result=0;
	for (unsigned i=0;i<16;++i) result=(result<<1)|bits[(pos+i)%count];
	return result;
}
inline std::uint8_t data(std::uint16_t word)
{
	std::uint8_t value=0;
	for (unsigned i=0;i<8;++i) value=(value<<1)|((word>>(14-2*i))&1);
	return value;
}
inline std::uint8_t clock(std::uint16_t word) { return data(word>>1); }
template <typename Bits> std::uint8_t byte(const Bits &bits,unsigned count,unsigned pos) { return data(word(bits,count,pos)); }
}
#endif
