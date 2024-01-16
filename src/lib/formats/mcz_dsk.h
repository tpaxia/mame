// license:BSD-3-Clause
// copyright-holders:Salvatore Paxia
/*********************************************************************

    formats/mcz.h

    ZILOG MCZ disk images

*********************************************************************/
#ifndef MAME_FORMATS_MCZ_DSK_H
#define MAME_FORMATS_MCZ_DSK_H

#pragma once

/**************************************************************************/

#include "flopimg.h"

/**************************************************************************/

class mcz_format : public floppy_image_format_t
{
public:
	mcz_format();
	virtual int identify(util::random_read &io, uint32_t form_factor, const std::vector<uint32_t> &variants) const override;
	virtual bool load(util::random_read &io, uint32_t form_factor, const std::vector<uint32_t> &variants, floppy_image *image) const override;
	virtual bool save(util::random_read_write &io, const std::vector<uint32_t> &variants, floppy_image *image) const override;

	virtual const char *name() const noexcept override;
	virtual const char *description() const noexcept override;
	virtual const char *extensions() const noexcept override;
	virtual bool supports_save() const noexcept override;

	static const desc_e mcz_desc[];
};

extern const mcz_format FLOPPY_MCZ_FORMAT;

#endif // MAME_FORMATS_MCZ_DSK_H