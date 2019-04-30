/*
Copyright (c) 2015 Harm Hanemaaijer <fgenfb@yahoo.com>
Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.
THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#include "decompress_eac.h"

#define DETEX_PIXEL32_ALPHA_BYTE_OFFSET 0

static DETEX_INLINE_ONLY uint8_t detexClamp0To255(int x) {
	int i = x;
	if (i < 0)
		i = 0;
	else if (i > 255)
		i = 255;
	return (uint8_t)i;
}

const int8_t eac_modifier_table[16][8] = {
	{ -3, -6, -9, -15, 2, 5, 8, 14 },
	{ -3, -7, -10, -13, 2, 6, 9, 12 },
	{ -2, -5, -8, -13, 1, 4, 7, 12 },
	{ -2, -4, -6, -13, 1, 3, 5, 12 },
	{ -3, -6, -8, -12, 2, 5, 7, 11 },
	{ -3, -7, -9, -11, 2, 6, 8, 10 },
	{ -4, -7, -8, -11, 3, 6, 7, 10 },
	{ -3, -5, -8, -11, 2, 4, 7, 10 },
	{ -2, -6, -8, -10, 1, 5, 7, 9 },
	{ -2, -5, -8, -10, 1, 4, 7, 9 },
	{ -2, -4, -8, -10, 1, 3, 7, 9 },
	{ -2, -5, -7, -10, 1, 4, 6, 9 },
	{ -3, -4, -7, -10, 2, 3, 6, 9 },
	{ -1, -2, -3, -10, 0, 1, 2, 9 },
	{ -4, -6, -8, -9, 3, 5, 7, 8 },
	{ -3, -5, -7, -9, 2, 4, 6, 8 }
};

static DETEX_INLINE_ONLY int modifier_times_multiplier(int modifier, int multiplier) 
{
	return modifier * multiplier;
}

static DETEX_INLINE_ONLY void ProcessPixelEAC(uint8_t i, uint64_t pixels,
	const int8_t * DETEX_RESTRICT modifier_table, int base_codeword, int multiplier,
	uint8_t * DETEX_RESTRICT pixel_buffer, int bytes_per_pixel) 
{
	int modifier = modifier_table[(pixels >> (45 - i * 3)) & 7];
	
	pixel_buffer[((i & 3) * 4 + ((i & 12) >> 2)) * bytes_per_pixel + DETEX_PIXEL32_ALPHA_BYTE_OFFSET] =
		detexClamp0To255(base_codeword + modifier_times_multiplier(modifier, multiplier));
}

/* Decompress a 128-bit 4x4 pixel texture block compressed using the ETC2_EAC */
/* format. */
bool detexDecompressBlockETC2_EAC(const uint8_t * DETEX_RESTRICT bitstring, uint8_t * DETEX_RESTRICT pixel_buffer, int bytes_per_pixel)
{
	//bool r = detexDecompressBlockETC2(&bitstring[8], mode_mask, flags, pixel_buffer);
	//if (!r)
	//	return false;

	// Decode the alpha part.
	int base_codeword = bitstring[0];
	const int8_t *modifier_table = eac_modifier_table[(bitstring[1] & 0x0F)];
	int multiplier = (bitstring[1] & 0xF0) >> 4;
	
	uint64_t pixels = ((uint64_t)bitstring[2] << 40) | ((uint64_t)bitstring[3] << 32) |
		((uint64_t)bitstring[4] << 24)
		| ((uint64_t)bitstring[5] << 16) | ((uint64_t)bitstring[6] << 8) | bitstring[7];
	ProcessPixelEAC(0, pixels, modifier_table, base_codeword, multiplier, pixel_buffer, bytes_per_pixel);
	ProcessPixelEAC(1, pixels, modifier_table, base_codeword, multiplier, pixel_buffer, bytes_per_pixel);
	ProcessPixelEAC(2, pixels, modifier_table, base_codeword, multiplier, pixel_buffer, bytes_per_pixel);
	ProcessPixelEAC(3, pixels, modifier_table, base_codeword, multiplier, pixel_buffer, bytes_per_pixel);
	ProcessPixelEAC(4, pixels, modifier_table, base_codeword, multiplier, pixel_buffer, bytes_per_pixel);
	ProcessPixelEAC(5, pixels, modifier_table, base_codeword, multiplier, pixel_buffer, bytes_per_pixel);
	ProcessPixelEAC(6, pixels, modifier_table, base_codeword, multiplier, pixel_buffer, bytes_per_pixel);
	ProcessPixelEAC(7, pixels, modifier_table, base_codeword, multiplier, pixel_buffer, bytes_per_pixel);
	ProcessPixelEAC(8, pixels, modifier_table, base_codeword, multiplier, pixel_buffer, bytes_per_pixel);
	ProcessPixelEAC(9, pixels, modifier_table, base_codeword, multiplier, pixel_buffer, bytes_per_pixel);
	ProcessPixelEAC(10, pixels, modifier_table, base_codeword, multiplier, pixel_buffer, bytes_per_pixel);
	ProcessPixelEAC(11, pixels, modifier_table, base_codeword, multiplier, pixel_buffer, bytes_per_pixel);
	ProcessPixelEAC(12, pixels, modifier_table, base_codeword, multiplier, pixel_buffer, bytes_per_pixel);
	ProcessPixelEAC(13, pixels, modifier_table, base_codeword, multiplier, pixel_buffer, bytes_per_pixel);
	ProcessPixelEAC(14, pixels, modifier_table, base_codeword, multiplier, pixel_buffer, bytes_per_pixel);
	ProcessPixelEAC(15, pixels, modifier_table, base_codeword, multiplier, pixel_buffer, bytes_per_pixel);
	return true;
}

