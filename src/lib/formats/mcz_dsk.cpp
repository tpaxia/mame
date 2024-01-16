// license: BSD-3-Clause
// copyright-holders: Salvatore Paxia
/***************************************************************************

    Zilog MCZ  

    Disk image formats

***************************************************************************/

#include "mcz_dsk.h"
#include "imageutl.h"
#include "ioprocs.h"

#include <iostream>
#include <istream>
#include <ostream>


static const int track_size = 100'000;
static const int half_bitcell_size = 2000;


const floppy_image_format_t::desc_e mcz_format::mcz_desc[] = {
	/* 01 */ { SECTOR_LOOP_START, 0, 31 },  // 32 sectors, 7th bit = 1
	/* 02 */ {   FM, 0x00, 16 }, // 16 00 bytes
	/* 03 */// {   SECTOR_ID_FM },
	/* 04 */// {   TRACK_ID_FM },
	/* 05 */ {   SECTOR_DATA_FM, -1 },
	/* 06 */ {   FM, 0x00, 1 }, // 16 00 bytes
	/* 07 */ { SECTOR_LOOP_END },
	/* 08 */ { END }
};

mcz_format::mcz_format()
{

}

const char *mcz_format::name() const noexcept
{
	return "MCZ";
}


const char *mcz_format::description() const noexcept
{
	return "ZILOG MCZ disk image";
}


const char *mcz_format::extensions() const noexcept
{
	return "MCZ";
}


int mcz_format::identify(util::random_read &io, uint32_t form_factor, const std::vector<uint32_t> &variants) const
{
	uint64_t size;
	if (io.length(size))
		return 0;

	if (size == 335104)
		return FIFID_SIZE;

	return 0;
}


bool mcz_format::load(util::random_read &io, uint32_t form_factor, const std::vector<uint32_t> &variants, floppy_image *image) const
{
	std::cout << "Before set variant " << std::endl;
	image->set_variant(floppy_image::SSSD32);

	const int tracks = 77;
	const int sector_count = 32;
	
	uint8_t sectdata[sector_count*136];
	int track_size = sector_count*136;

	desc_s sectors[sector_count];
	for(int i=0; i<sector_count; i++) {
		sectors[i].data = sectdata + 136*i;
		sectors[i].size = 136;
		sectors[i].sector_id = 128+i;
	}

	for(int track=0; track < tracks; track++) {
		size_t actual;
		io.read_at(track*track_size, sectdata, track_size, actual);
		generate_track(mcz_desc, track, 0, sectors, sector_count, 78336, image);  // 98480
	}
 
 
	std::cout << "Loaded Disk\n" << std::endl;

	return true;
}

bool mcz_format::supports_save() const noexcept
{
	return false;
}

bool mcz_format::save(util::random_read_write &io, const std::vector<uint32_t> &variants, floppy_image *image) const
{
	return false;
}


const mcz_format FLOPPY_MCZ_FORMAT;


