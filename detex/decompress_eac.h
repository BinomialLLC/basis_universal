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
#pragma once

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "detex_common.h"

#ifdef __cplusplus
extern "C"
{
#endif
		 
#ifndef DETEX_RESTRICT		 
#define DETEX_RESTRICT __restrict
#endif

extern const int8_t eac_modifier_table[16][8];

bool detexDecompressBlockETC2_EAC(const uint8_t * DETEX_RESTRICT bitstring, uint8_t * DETEX_RESTRICT pixel_buffer, int bytes_per_pixel);

#ifdef __cplusplus
}
#endif