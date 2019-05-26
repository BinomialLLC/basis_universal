// basisu_transcoder.cpp
// Copyright (C) 2017-2019 Binomial LLC. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "basisu_transcoder.h"
#include <limits.h>
#include <vector>

// The supported .basis file header version. Keep in sync with BASIS_FILE_VERSION.
#define BASISD_SUPPORTED_BASIS_VERSION (0x13)

// Set to 1 for fuzz testing. This will disable all CRC16 checks on headers and compressed data.
#define BASISU_NO_HEADER_OR_DATA_CRC16_CHECKS 0

#ifndef BASISD_SUPPORT_DXT1
#define BASISD_SUPPORT_DXT1 1
#endif

#ifndef BASISD_SUPPORT_DXT5A
#define BASISD_SUPPORT_DXT5A 1
#endif

#ifndef BASISD_SUPPORT_BC7
#define BASISD_SUPPORT_BC7 1
#endif

#ifndef BASISD_SUPPORT_PVRTC1
#define BASISD_SUPPORT_PVRTC1 1
#endif

#ifndef BASISD_SUPPORT_ETC2_EAC_A8
#define BASISD_SUPPORT_ETC2_EAC_A8 1
#endif

#define BASISD_WRITE_NEW_BC7_TABLES				0
#define BASISD_WRITE_NEW_DXT1_TABLES			0
#define BASISD_WRITE_NEW_ETC2_EAC_A8_TABLES	0

namespace basisu
{
	static bool g_debug_printf;

	void enable_debug_printf(bool enabled)
	{
		g_debug_printf = enabled;
	}

	void debug_printf(const char *pFmt, ...)
	{
#if BASISU_DEVEL_MESSAGES	
		g_debug_printf = true;
#endif
		if (g_debug_printf)
		{
			va_list args;
			va_start(args, pFmt);
			vprintf(pFmt, args);
			va_end(args);
		}
	}
} // namespace basisu

namespace basist
{
	#include "basisu_transcoder_tables_bc7_m6.inc"
				
	uint16_t crc16(const void *r, size_t size, uint16_t crc)
	{
		crc = ~crc;

		const uint8_t *p = reinterpret_cast<const uint8_t *>(r);
		for ( ; size; --size)
		{
			const uint16_t q = *p++ ^ (crc >> 8);
			uint16_t k = (q >> 4) ^ q;
			crc = (((crc << 8) ^ k) ^ (k << 5)) ^ (k << 12);
		}

		return static_cast<uint16_t>(~crc);
	}
		
	void etc1_global_selector_codebook::init(uint32_t N, const uint32_t *pEntries)
	{
		m_palette.resize(N);
		for (uint32_t i = 0; i < N; i++)
			m_palette[i].set_uint32(pEntries[i]);
	}

	void etc1_global_selector_codebook::print_code(FILE *pFile)
	{
		fprintf(pFile, "{\n");
		for (uint32_t i = 0; i < m_palette.size(); i++)
		{
			fprintf(pFile, "0x%X,", m_palette[i].get_uint32());
			if ((i & 15) == 15)
				fprintf(pFile, "\n");
		}
		fprintf(pFile, "\n}\n");
	}

	enum etc_constants
	{
		cETC1BytesPerBlock = 8U,

		cETC1SelectorBits = 2U,
		cETC1SelectorValues = 1U << cETC1SelectorBits,
		cETC1SelectorMask = cETC1SelectorValues - 1U,

		cETC1BlockShift = 2U,
		cETC1BlockSize = 1U << cETC1BlockShift,

		cETC1LSBSelectorIndicesBitOffset = 0,
		cETC1MSBSelectorIndicesBitOffset = 16,

		cETC1FlipBitOffset = 32,
		cETC1DiffBitOffset = 33,

		cETC1IntenModifierNumBits = 3,
		cETC1IntenModifierValues = 1 << cETC1IntenModifierNumBits,
		cETC1RightIntenModifierTableBitOffset = 34,
		cETC1LeftIntenModifierTableBitOffset = 37,

		// Base+Delta encoding (5 bit bases, 3 bit delta)
		cETC1BaseColorCompNumBits = 5,
		cETC1BaseColorCompMax = 1 << cETC1BaseColorCompNumBits,

		cETC1DeltaColorCompNumBits = 3,
		cETC1DeltaColorComp = 1 << cETC1DeltaColorCompNumBits,
		cETC1DeltaColorCompMax = 1 << cETC1DeltaColorCompNumBits,

		cETC1BaseColor5RBitOffset = 59,
		cETC1BaseColor5GBitOffset = 51,
		cETC1BaseColor5BBitOffset = 43,

		cETC1DeltaColor3RBitOffset = 56,
		cETC1DeltaColor3GBitOffset = 48,
		cETC1DeltaColor3BBitOffset = 40,

		// Absolute (non-delta) encoding (two 4-bit per component bases)
		cETC1AbsColorCompNumBits = 4,
		cETC1AbsColorCompMax = 1 << cETC1AbsColorCompNumBits,

		cETC1AbsColor4R1BitOffset = 60,
		cETC1AbsColor4G1BitOffset = 52,
		cETC1AbsColor4B1BitOffset = 44,

		cETC1AbsColor4R2BitOffset = 56,
		cETC1AbsColor4G2BitOffset = 48,
		cETC1AbsColor4B2BitOffset = 40,

		cETC1ColorDeltaMin = -4,
		cETC1ColorDeltaMax = 3,

		// Delta3:
		// 0   1   2   3   4   5   6   7
		// 000 001 010 011 100 101 110 111
		// 0   1   2   3   -4  -3  -2  -1
	};

#define DECLARE_ETC1_INTEN_TABLE(name, N) \
	static const int name[cETC1IntenModifierValues][cETC1SelectorValues] = \
	{ \
		{ N * -8,  N * -2,   N * 2,   N * 8 },{ N * -17,  N * -5,  N * 5,  N * 17 },{ N * -29,  N * -9,   N * 9,  N * 29 },{ N * -42, N * -13, N * 13,  N * 42 }, \
		{ N * -60, N * -18, N * 18,  N * 60 },{ N * -80, N * -24, N * 24,  N * 80 },{ N * -106, N * -33, N * 33, N * 106 },{ N * -183, N * -47, N * 47, N * 183 } \
	};

	DECLARE_ETC1_INTEN_TABLE(g_etc1_inten_tables, 1);
	DECLARE_ETC1_INTEN_TABLE(g_etc1_inten_tables48, 3 * 16);

	static const uint8_t g_etc_5_to_8[32] = { 0, 8, 16, 24, 33, 41, 49, 57, 66, 74, 82, 90, 99, 107, 115, 123, 132, 140, 148, 156, 165, 173, 181, 189, 198, 206, 214, 222, 231, 239, 247, 255 };

	struct decoder_etc_block
	{
		// big endian uint64:
		// bit ofs:  56  48  40  32  24  16   8   0
		// byte ofs: b0, b1, b2, b3, b4, b5, b6, b7 
		union
		{
			uint64_t m_uint64;

			uint32_t m_uint32[2];

			uint8_t m_bytes[8];

			struct
			{
				signed m_dred2 : 3;
				uint32_t m_red1 : 5;

				signed m_dgreen2 : 3;
				uint32_t m_green1 : 5;

				signed m_dblue2 : 3;
				uint32_t m_blue1 : 5;

				uint32_t m_flip : 1;
				uint32_t m_diff : 1;
				uint32_t m_cw2 : 3;
				uint32_t m_cw1 : 3;

				uint32_t m_selectors;
			} m_differential;
		};

		inline void clear()
		{
			assert(sizeof(*this) == 8);
			basisu::clear_obj(*this);
		}

		inline void set_byte_bits(uint32_t ofs, uint32_t num, uint32_t bits)
		{
			assert((ofs + num) <= 64U);
			assert(num && (num < 32U));
			assert((ofs >> 3) == ((ofs + num - 1) >> 3));
			assert(bits < (1U << num));
			const uint32_t byte_ofs = 7 - (ofs >> 3);
			const uint32_t byte_bit_ofs = ofs & 7;
			const uint32_t mask = (1 << num) - 1;
			m_bytes[byte_ofs] &= ~(mask << byte_bit_ofs);
			m_bytes[byte_ofs] |= (bits << byte_bit_ofs);
		}

		inline void set_flip_bit(bool flip)
		{
			m_bytes[3] &= ~1;
			m_bytes[3] |= static_cast<uint8_t>(flip);
		}

		inline void set_diff_bit(bool diff)
		{
			m_bytes[3] &= ~2;
			m_bytes[3] |= (static_cast<uint32_t>(diff) << 1);
		}

		// Sets intensity modifier table (0-7) used by subblock subblock_id (0 or 1)
		inline void set_inten_table(uint32_t subblock_id, uint32_t t)
		{
			assert(subblock_id < 2);
			assert(t < 8);
			const uint32_t ofs = subblock_id ? 2 : 5;
			m_bytes[3] &= ~(7 << ofs);
			m_bytes[3] |= (t << ofs);
		}

		// Selector "val" ranges from 0-3 and is a direct index into g_etc1_inten_tables.
		inline void set_selector(uint32_t x, uint32_t y, uint32_t val)
		{
			assert((x | y | val) < 4);
			const uint32_t bit_index = x * 4 + y;

			uint8_t * p = &m_bytes[7 - (bit_index >> 3)];

			const uint32_t byte_bit_ofs = bit_index & 7;
			const uint32_t mask = 1 << byte_bit_ofs;

			static const uint8_t s_selector_index_to_etc1[4] = { 3, 2, 0, 1 };
			const uint32_t etc1_val = s_selector_index_to_etc1[val];

			const uint32_t lsb = etc1_val & 1;
			const uint32_t msb = etc1_val >> 1;

			p[0] &= ~mask;
			p[0] |= (lsb << byte_bit_ofs);

			p[-2] &= ~mask;
			p[-2] |= (msb << byte_bit_ofs);
		}

		// Returned encoded selector value ranges from 0-3 (this is NOT a direct index into g_etc1_inten_tables, see get_selector())
		inline uint32_t get_raw_selector(uint32_t x, uint32_t y) const
		{
			assert((x | y) < 4);

			const uint32_t bit_index = x * 4 + y;
			const uint32_t byte_bit_ofs = bit_index & 7;
			const uint8_t * p = &m_bytes[7 - (bit_index >> 3)];
			const uint32_t lsb = (p[0] >> byte_bit_ofs) & 1;
			const uint32_t msb = (p[-2] >> byte_bit_ofs) & 1;
			const uint32_t val = lsb | (msb << 1);

			return val;
		}

		// Returned selector value ranges from 0-3 and is a direct index into g_etc1_inten_tables.
		inline uint32_t get_selector(uint32_t x, uint32_t y) const
		{
			static const uint8_t s_etc1_to_selector_index[cETC1SelectorValues] = { 2, 3, 1, 0 };
			return s_etc1_to_selector_index[get_raw_selector(x, y)];
		}

		inline void set_raw_selector_bits(uint32_t bits)
		{
			m_bytes[4] = static_cast<uint8_t>(bits);
			m_bytes[5] = static_cast<uint8_t>(bits >> 8);
			m_bytes[6] = static_cast<uint8_t>(bits >> 16);
			m_bytes[7] = static_cast<uint8_t>(bits >> 24);
		}

		inline bool are_all_selectors_the_same() const
		{
			uint32_t v = *reinterpret_cast<const uint32_t*>(&m_bytes[4]);

			if ((v == 0xFFFFFFFF) || (v == 0xFFFF) || (!v) || (v == 0xFFFF0000))
				return true;

			return false;
		}

		inline void set_raw_selector_bits(uint8_t byte0, uint8_t byte1, uint8_t byte2, uint8_t byte3)
		{
			m_bytes[4] = byte0;
			m_bytes[5] = byte1;
			m_bytes[6] = byte2;
			m_bytes[7] = byte3;
		}

		inline uint32_t get_raw_selector_bits() const
		{
			return m_bytes[4] | (m_bytes[5] << 8) | (m_bytes[6] << 16) | (m_bytes[7] << 24);
		}

		inline void set_base4_color(uint32_t idx, uint16_t c)
		{
			if (idx)
			{
				set_byte_bits(cETC1AbsColor4R2BitOffset, 4, (c >> 8) & 15);
				set_byte_bits(cETC1AbsColor4G2BitOffset, 4, (c >> 4) & 15);
				set_byte_bits(cETC1AbsColor4B2BitOffset, 4, c & 15);
			}
			else
			{
				set_byte_bits(cETC1AbsColor4R1BitOffset, 4, (c >> 8) & 15);
				set_byte_bits(cETC1AbsColor4G1BitOffset, 4, (c >> 4) & 15);
				set_byte_bits(cETC1AbsColor4B1BitOffset, 4, c & 15);
			}
		}

		inline void set_base5_color(uint16_t c)
		{
			set_byte_bits(cETC1BaseColor5RBitOffset, 5, (c >> 10) & 31);
			set_byte_bits(cETC1BaseColor5GBitOffset, 5, (c >> 5) & 31);
			set_byte_bits(cETC1BaseColor5BBitOffset, 5, c & 31);
		}

		void set_delta3_color(uint16_t c)
		{
			set_byte_bits(cETC1DeltaColor3RBitOffset, 3, (c >> 6) & 7);
			set_byte_bits(cETC1DeltaColor3GBitOffset, 3, (c >> 3) & 7);
			set_byte_bits(cETC1DeltaColor3BBitOffset, 3, c & 7);
		}

		void set_block_color4(const color32 & c0_unscaled, const color32 & c1_unscaled)
		{
			set_diff_bit(false);

			set_base4_color(0, pack_color4(c0_unscaled, false));
			set_base4_color(1, pack_color4(c1_unscaled, false));
		}

		void set_block_color5(const color32 & c0_unscaled, const color32 & c1_unscaled)
		{
			set_diff_bit(true);

			set_base5_color(pack_color5(c0_unscaled, false));

			int dr = c1_unscaled.r - c0_unscaled.r;
			int dg = c1_unscaled.g - c0_unscaled.g;
			int db = c1_unscaled.b - c0_unscaled.b;

			set_delta3_color(pack_delta3(dr, dg, db));
		}

		bool set_block_color5_check(const color32 & c0_unscaled, const color32 & c1_unscaled)
		{
			set_diff_bit(true);

			set_base5_color(pack_color5(c0_unscaled, false));

			int dr = c1_unscaled.r - c0_unscaled.r;
			int dg = c1_unscaled.g - c0_unscaled.g;
			int db = c1_unscaled.b - c0_unscaled.b;

			if (((dr < cETC1ColorDeltaMin) || (dr > cETC1ColorDeltaMax)) ||
				((dg < cETC1ColorDeltaMin) || (dg > cETC1ColorDeltaMax)) ||
				((db < cETC1ColorDeltaMin) || (db > cETC1ColorDeltaMax)))
				return false;

			set_delta3_color(pack_delta3(dr, dg, db));

			return true;
		}

		inline uint32_t get_byte_bits(uint32_t ofs, uint32_t num) const
		{
			assert((ofs + num) <= 64U);
			assert(num && (num <= 8U));
			assert((ofs >> 3) == ((ofs + num - 1) >> 3));
			const uint32_t byte_ofs = 7 - (ofs >> 3);
			const uint32_t byte_bit_ofs = ofs & 7;
			return (m_bytes[byte_ofs] >> byte_bit_ofs) & ((1 << num) - 1);
		}

		inline uint16_t get_base5_color() const
		{
			const uint32_t r = get_byte_bits(cETC1BaseColor5RBitOffset, 5);
			const uint32_t g = get_byte_bits(cETC1BaseColor5GBitOffset, 5);
			const uint32_t b = get_byte_bits(cETC1BaseColor5BBitOffset, 5);
			return static_cast<uint16_t>(b | (g << 5U) | (r << 10U));
		}

		inline color32 get_base5_color_unscaled() const
		{
			return color32(m_differential.m_red1, m_differential.m_green1, m_differential.m_blue1, 255);
		}

		inline uint32_t get_inten_table(uint32_t subblock_id) const
		{
			assert(subblock_id < 2);
			const uint32_t ofs = subblock_id ? 2 : 5;
			return (m_bytes[3] >> ofs) & 7;
		}

		static uint16_t pack_color4(const color32 & color, bool scaled, uint32_t bias = 127U)
		{
			return pack_color4(color.r, color.g, color.b, scaled, bias);
		}

		static uint16_t pack_color4(uint32_t r, uint32_t g, uint32_t b, bool scaled, uint32_t bias = 127U)
		{
			if (scaled)
			{
				r = (r * 15U + bias) / 255U;
				g = (g * 15U + bias) / 255U;
				b = (b * 15U + bias) / 255U;
			}

			r = basisu::minimum(r, 15U);
			g = basisu::minimum(g, 15U);
			b = basisu::minimum(b, 15U);

			return static_cast<uint16_t>(b | (g << 4U) | (r << 8U));
		}

		static uint16_t pack_color5(const color32 & color, bool scaled, uint32_t bias = 127U)
		{
			return pack_color5(color.r, color.g, color.b, scaled, bias);
		}

		static uint16_t pack_color5(uint32_t r, uint32_t g, uint32_t b, bool scaled, uint32_t bias = 127U)
		{
			if (scaled)
			{
				r = (r * 31U + bias) / 255U;
				g = (g * 31U + bias) / 255U;
				b = (b * 31U + bias) / 255U;
			}

			r = basisu::minimum(r, 31U);
			g = basisu::minimum(g, 31U);
			b = basisu::minimum(b, 31U);

			return static_cast<uint16_t>(b | (g << 5U) | (r << 10U));
		}

		uint16_t pack_delta3(const color32 & color)
		{
			return pack_delta3(color.r, color.g, color.b);
		}

		uint16_t pack_delta3(int r, int g, int b)
		{
			assert((r >= cETC1ColorDeltaMin) && (r <= cETC1ColorDeltaMax));
			assert((g >= cETC1ColorDeltaMin) && (g <= cETC1ColorDeltaMax));
			assert((b >= cETC1ColorDeltaMin) && (b <= cETC1ColorDeltaMax));
			if (r < 0) r += 8;
			if (g < 0) g += 8;
			if (b < 0) b += 8;
			return static_cast<uint16_t>(b | (g << 3) | (r << 6));
		}

		static color32 unpack_color5(uint16_t packed_color5, bool scaled, uint32_t alpha = 255)
		{
			uint32_t b = packed_color5 & 31U;
			uint32_t g = (packed_color5 >> 5U) & 31U;
			uint32_t r = (packed_color5 >> 10U) & 31U;

			if (scaled)
			{
				b = (b << 3U) | (b >> 2U);
				g = (g << 3U) | (g >> 2U);
				r = (r << 3U) | (r >> 2U);
			}

			return color32(r, g, b, alpha);
		}

		static void unpack_color5(uint32_t & r, uint32_t & g, uint32_t & b, uint16_t packed_color5, bool scaled)
		{
			color32 c(unpack_color5(packed_color5, scaled, 0));
			r = c.r;
			g = c.g;
			b = c.b;
		}

		static void get_diff_subblock_colors(color32 * pDst, uint16_t packed_color5, uint32_t table_idx)
		{
			assert(table_idx < cETC1IntenModifierValues);
			const int* pInten_modifer_table = &g_etc1_inten_tables[table_idx][0];

			uint32_t r, g, b;
			unpack_color5(r, g, b, packed_color5, true);

			const int ir = static_cast<int>(r), ig = static_cast<int>(g), ib = static_cast<int>(b);

			const int y0 = pInten_modifer_table[0];
			pDst[0].set(clamp255(ir + y0), clamp255(ig + y0), clamp255(ib + y0), 255);

			const int y1 = pInten_modifer_table[1];
			pDst[1].set(clamp255(ir + y1), clamp255(ig + y1), clamp255(ib + y1), 255);

			const int y2 = pInten_modifer_table[2];
			pDst[2].set(clamp255(ir + y2), clamp255(ig + y2), clamp255(ib + y2), 255);

			const int y3 = pInten_modifer_table[3];
			pDst[3].set(clamp255(ir + y3), clamp255(ig + y3), clamp255(ib + y3), 255);
		}

		static int clamp255(int x)
		{
			if (x & 0xFFFFFF00)
			{
				if (x < 0)
					x = 0;
				else if (x > 255)
					x = 255;
			}

			return x;
		}

		static void get_block_colors5(color32 * pBlock_colors, const color32 & base_color5, uint32_t inten_table)
		{
			color32 b(base_color5);

			b.r = (b.r << 3) | (b.r >> 2);
			b.g = (b.g << 3) | (b.g >> 2);
			b.b = (b.b << 3) | (b.b >> 2);

			const int* pInten_table = g_etc1_inten_tables[inten_table];

			pBlock_colors[0].set(clamp255(b.r + pInten_table[0]), clamp255(b.g + pInten_table[0]), clamp255(b.b + pInten_table[0]), 255);
			pBlock_colors[1].set(clamp255(b.r + pInten_table[1]), clamp255(b.g + pInten_table[1]), clamp255(b.b + pInten_table[1]), 255);
			pBlock_colors[2].set(clamp255(b.r + pInten_table[2]), clamp255(b.g + pInten_table[2]), clamp255(b.b + pInten_table[2]), 255);
			pBlock_colors[3].set(clamp255(b.r + pInten_table[3]), clamp255(b.g + pInten_table[3]), clamp255(b.b + pInten_table[3]), 255);
		}
				
		static void get_block_colors5_bounds(color32 * pBlock_colors, const color32 & base_color5, uint32_t inten_table, uint32_t l = 0, uint32_t h = 3)
		{
			color32 b(base_color5);

			b.r = (b.r << 3) | (b.r >> 2);
			b.g = (b.g << 3) | (b.g >> 2);
			b.b = (b.b << 3) | (b.b >> 2);

			const int* pInten_table = g_etc1_inten_tables[inten_table];

			pBlock_colors[0].set(clamp255(b.r + pInten_table[l]), clamp255(b.g + pInten_table[l]), clamp255(b.b + pInten_table[l]), 255);
			pBlock_colors[1].set(clamp255(b.r + pInten_table[h]), clamp255(b.g + pInten_table[h]), clamp255(b.b + pInten_table[h]), 255);
		}
	};

	enum dxt_constants
	{
		cDXT1SelectorBits = 2U,	cDXT1SelectorValues = 1U << cDXT1SelectorBits, cDXT1SelectorMask = cDXT1SelectorValues - 1U,
		cDXT5SelectorBits = 3U,	cDXT5SelectorValues = 1U << cDXT5SelectorBits, cDXT5SelectorMask = cDXT5SelectorValues - 1U,
	};

	static const uint8_t g_etc1_x_selector_unpack[4][256] =
	{
		{
			0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3,
			0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3,
			0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3,
			0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3,
			0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3,
			0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3,
			0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3,
			0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3,
		},
		{
			0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1,
			2, 2, 3, 3, 2, 2, 3, 3, 2, 2, 3, 3, 2, 2, 3, 3, 2, 2, 3, 3, 2, 2, 3, 3, 2, 2, 3, 3, 2, 2, 3, 3,
			0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1,
			2, 2, 3, 3, 2, 2, 3, 3, 2, 2, 3, 3, 2, 2, 3, 3, 2, 2, 3, 3, 2, 2, 3, 3, 2, 2, 3, 3, 2, 2, 3, 3,
			0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1,
			2, 2, 3, 3, 2, 2, 3, 3, 2, 2, 3, 3, 2, 2, 3, 3, 2, 2, 3, 3, 2, 2, 3, 3, 2, 2, 3, 3, 2, 2, 3, 3,
			0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1,
			2, 2, 3, 3, 2, 2, 3, 3, 2, 2, 3, 3, 2, 2, 3, 3, 2, 2, 3, 3, 2, 2, 3, 3, 2, 2, 3, 3, 2, 2, 3, 3,
		},

		{
			0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1,
			0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1,
			2, 2, 2, 2, 3, 3, 3, 3, 2, 2, 2, 2, 3, 3, 3, 3, 2, 2, 2, 2, 3, 3, 3, 3, 2, 2, 2, 2, 3, 3, 3, 3,
			2, 2, 2, 2, 3, 3, 3, 3, 2, 2, 2, 2, 3, 3, 3, 3, 2, 2, 2, 2, 3, 3, 3, 3, 2, 2, 2, 2, 3, 3, 3, 3,
			0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1,
			0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1,
			2, 2, 2, 2, 3, 3, 3, 3, 2, 2, 2, 2, 3, 3, 3, 3, 2, 2, 2, 2, 3, 3, 3, 3, 2, 2, 2, 2, 3, 3, 3, 3,
			2, 2, 2, 2, 3, 3, 3, 3, 2, 2, 2, 2, 3, 3, 3, 3, 2, 2, 2, 2, 3, 3, 3, 3, 2, 2, 2, 2, 3, 3, 3, 3,
		},

		{
			0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
			0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
			0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
			0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
			2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3,
			2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3,
			2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3,
			2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3,
		}
	};

	struct dxt1_block
	{
		enum { cTotalEndpointBytes = 2, cTotalSelectorBytes = 4 };

		uint8_t m_low_color[cTotalEndpointBytes];
		uint8_t m_high_color[cTotalEndpointBytes];
		uint8_t m_selectors[cTotalSelectorBytes];

		inline void clear() { basisu::clear_obj(*this); }

		inline uint32_t get_high_color() const	{ return m_high_color[0] | (m_high_color[1] << 8U); }
		inline uint32_t get_low_color() const { return m_low_color[0] | (m_low_color[1] << 8U); }
		inline void set_low_color(uint16_t c) { m_low_color[0] = static_cast<uint8_t>(c & 0xFF); m_low_color[1] = static_cast<uint8_t>((c >> 8) & 0xFF); }
		inline void set_high_color(uint16_t c) { m_high_color[0] = static_cast<uint8_t>(c & 0xFF); m_high_color[1] = static_cast<uint8_t>((c >> 8) & 0xFF); }
		inline uint32_t get_selector(uint32_t x, uint32_t y) const { assert((x < 4U) && (y < 4U)); return (m_selectors[y] >> (x * cDXT1SelectorBits)) & cDXT1SelectorMask; }
		inline void set_selector(uint32_t x, uint32_t y, uint32_t val) { assert((x < 4U) && (y < 4U) && (val < 4U)); m_selectors[y] &= (~(cDXT1SelectorMask << (x * cDXT1SelectorBits))); m_selectors[y] |= (val << (x * cDXT1SelectorBits)); }

		static uint16_t pack_color(const color32 &color, bool scaled, uint32_t bias = 127U)
		{
			uint32_t r = color.r, g = color.g, b = color.b;
			if (scaled)
			{
				r = (r * 31U + bias) / 255U;
				g = (g * 63U + bias) / 255U;
				b = (b * 31U + bias) / 255U;
			}
			return static_cast<uint16_t>(basisu::minimum(b, 31U) | (basisu::minimum(g, 63U) << 5U) | (basisu::minimum(r, 31U) << 11U));
		}

		static uint16_t pack_unscaled_color(uint32_t r, uint32_t g, uint32_t b) { return static_cast<uint16_t>(b | (g << 5U) | (r << 11U)); }
	};

	struct dxt_selector_range
	{
		uint32_t m_low;
		uint32_t m_high;
	};

#if BASISD_SUPPORT_BC7
	static dxt_selector_range g_etc1_to_bc7_selector_ranges[] =
	{
		{ 0, 0 },
		{ 1, 1 },
		{ 2, 2 },
		{ 3, 3 },

		{ 0, 3 },

		{ 1, 3 },
		{ 0, 2 },

		{ 1, 2 },

		{ 2, 3 },
		{ 0, 1 },
	};
	const uint32_t NUM_ETC1_TO_BC7_M6_SELECTOR_RANGES = sizeof(g_etc1_to_bc7_selector_ranges) / sizeof(g_etc1_to_bc7_selector_ranges[0]);

	static uint32_t g_etc1_to_bc7_m6_selector_range_index[4][4];

	static const uint8_t g_etc1_to_bc7_selector_mappings[][4] =
	{
#if 1
		{ 5 * 0, 5 * 0, 5 * 0, 5 * 0 },
		{ 5 * 0, 5 * 0, 5 * 0, 5 * 1 },
		{ 5 * 0, 5 * 0, 5 * 0, 5 * 2 },
		{ 5 * 0, 5 * 0, 5 * 0, 5 * 3 },
		{ 5 * 0, 5 * 0, 5 * 1, 5 * 1 },
		{ 5 * 0, 5 * 0, 5 * 1, 5 * 2 },
		{ 5 * 0, 5 * 0, 5 * 1, 5 * 3 },
		{ 5 * 0, 5 * 0, 5 * 2, 5 * 2 },
		{ 5 * 0, 5 * 0, 5 * 2, 5 * 3 },
		{ 5 * 0, 5 * 0, 5 * 3, 5 * 3 },
		{ 5 * 0, 5 * 1, 5 * 1, 5 * 1 },
		{ 5 * 0, 5 * 1, 5 * 1, 5 * 2 },
		{ 5 * 0, 5 * 1, 5 * 1, 5 * 3 },
		{ 5 * 0, 5 * 1, 5 * 2, 5 * 2 },
		{ 5 * 0, 5 * 1, 5 * 2, 5 * 3 },
		{ 5 * 0, 5 * 1, 5 * 3, 5 * 3 },
		{ 5 * 0, 5 * 2, 5 * 2, 5 * 2 },
		{ 5 * 0, 5 * 2, 5 * 2, 5 * 3 },
		{ 5 * 0, 5 * 2, 5 * 3, 5 * 3 },
		{ 5 * 0, 5 * 3, 5 * 3, 5 * 3 },
		{ 5 * 1, 5 * 1, 5 * 1, 5 * 1 },
		{ 5 * 1, 5 * 1, 5 * 1, 5 * 2 },
		{ 5 * 1, 5 * 1, 5 * 1, 5 * 3 },
		{ 5 * 1, 5 * 1, 5 * 2, 5 * 2 },
		{ 5 * 1, 5 * 1, 5 * 2, 5 * 3 },
		{ 5 * 1, 5 * 1, 5 * 3, 5 * 3 },
		{ 5 * 1, 5 * 2, 5 * 2, 5 * 2 },
		{ 5 * 1, 5 * 2, 5 * 2, 5 * 3 },
		{ 5 * 1, 5 * 2, 5 * 3, 5 * 3 },
		{ 5 * 1, 5 * 3, 5 * 3, 5 * 3 },
		{ 5 * 2, 5 * 2, 5 * 2, 5 * 2 },
		{ 5 * 2, 5 * 2, 5 * 2, 5 * 3 },
		{ 5 * 2, 5 * 2, 5 * 3, 5 * 3 },
		{ 5 * 2, 5 * 3, 5 * 3, 5 * 3 },
		{ 5 * 3, 5 * 3, 5 * 3, 5 * 3 },

		{ 0, 1, 2, 3 },
		{ 0, 0, 1, 1 },
		{ 0, 0, 0, 1 },
		{ 0, 2, 4, 6 },
		{ 0, 3, 6, 9 },
		{ 0, 4, 8, 12 },

		{ 0, 4, 9, 15 },
		{ 0, 6, 11, 15 },

		{ 1, 2, 3, 4 },
		{ 1, 3, 5, 7 },

		{ 1, 8, 8, 14 },
#else
		{ 5 * 0, 5 * 0, 5 * 1, 5 * 1 },
		{ 5 * 0, 5 * 0, 5 * 1, 5 * 2 },
		{ 5 * 0, 5 * 0, 5 * 1, 5 * 3 },
		{ 5 * 0, 5 * 0, 5 * 2, 5 * 3 },
		{ 5 * 0, 5 * 1, 5 * 1, 5 * 1 },
		{ 5 * 0, 5 * 1, 5 * 2, 5 * 2 },
		{ 5 * 0, 5 * 1, 5 * 2, 5 * 3 },
		{ 5 * 0, 5 * 2, 5 * 3, 5 * 3 },
		{ 5 * 1, 5 * 2, 5 * 2, 5 * 2 },
#endif
		{ 5 * 1, 5 * 2, 5 * 3, 5 * 3 },
		{ 8, 8, 8, 8 },
	};
	const uint32_t NUM_ETC1_TO_BC7_M6_SELECTOR_MAPPINGS = sizeof(g_etc1_to_bc7_selector_mappings) / sizeof(g_etc1_to_bc7_selector_mappings[0]);

	static uint8_t g_etc1_to_bc7_selector_mappings_from_raw_etc1[NUM_ETC1_TO_BC7_M6_SELECTOR_MAPPINGS][4];
	static uint8_t g_etc1_to_bc7_selector_mappings_from_raw_etc1_inv[NUM_ETC1_TO_BC7_M6_SELECTOR_MAPPINGS][4];

	// encoding from LSB to MSB: low8, high8, error16, size is [32*8][NUM_ETC1_TO_BC7_M6_SELECTOR_RANGES][NUM_ETC1_TO_BC7_M6_SELECTOR_MAPPINGS]
	extern const uint32_t* g_etc1_to_bc7_m6_table[];

	const uint16_t s_bptc_table_aWeight4[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };

#if BASISD_WRITE_NEW_BC7_TABLES
	static void create_etc1_to_bc7_m6_conversion_table()
	{
		FILE* pFile = NULL;

		pFile = fopen("basisu_decoder_tables_bc7_m6.inc", "w");

		for (int inten = 0; inten < 8; inten++)
		{
			for (uint32_t g = 0; g < 32; g++)
			{
				color32 block_colors[4];
				decoder_etc_block::get_diff_subblock_colors(block_colors, decoder_etc_block::pack_color5(color32(g, g, g, 255), false), inten);

				fprintf(pFile, "static const uint32_t g_etc1_to_bc7_m6_table%u[] = {\n", g + inten * 32);
				uint32_t n = 0;

				for (uint32_t sr = 0; sr < NUM_ETC1_TO_BC7_M6_SELECTOR_RANGES; sr++)
				{
					const uint32_t low_selector = g_etc1_to_bc7_selector_ranges[sr].m_low;
					const uint32_t high_selector = g_etc1_to_bc7_selector_ranges[sr].m_high;

					for (uint32_t m = 0; m < NUM_ETC1_TO_BC7_M6_SELECTOR_MAPPINGS; m++)
					{
						uint32_t best_lo = 0;
						uint32_t best_hi = 0;
						uint64_t best_err = UINT64_MAX;

						for (uint32_t hi = 0; hi <= 127; hi++)
						{
							for (uint32_t lo = 0; lo <= 127; lo++)
							{
								uint32_t bc7_block_colors[16];

								bc7_block_colors[0] = lo << 1;
								bc7_block_colors[15] = (hi << 1) | 1;

								for (uint32_t i = 1; i < 15; i++)
									bc7_block_colors[i] = (bc7_block_colors[0] * (64 - s_bptc_table_aWeight4[i]) + bc7_block_colors[15] * s_bptc_table_aWeight4[i] + 32) >> 6;

								uint64_t total_err = 0;

								for (uint32_t s = low_selector; s <= high_selector; s++)
								{
									int err = (int)block_colors[s].g - (int)bc7_block_colors[g_etc1_to_bc7_selector_mappings[m][s]];

									total_err += err * err;
								}

								if (total_err < best_err)
								{
									best_err = total_err;
									best_lo = lo;
									best_hi = hi;
								}
							} // lo

						} // hi

						best_err = basisu::minimum<uint32_t>(best_err, 0xFFFF);

						const uint32_t index = (g + inten * 32) * (NUM_ETC1_TO_BC7_M6_SELECTOR_RANGES * NUM_ETC1_TO_BC7_M6_SELECTOR_MAPPINGS) + (sr * NUM_ETC1_TO_BC7_M6_SELECTOR_MAPPINGS) + m;

						uint32_t v = best_err | (best_lo << 18) | (best_hi << 25);

						fprintf(pFile, "0x%X,", v);
						n++;
						if ((n & 31) == 31)
							fprintf(pFile, "\n");

					} // m
				} // sr

				fprintf(pFile, "};\n");

			} // g
		} // inten

		fprintf(pFile, "const uint32_t *g_etc1_to_bc7_m6_table[] = {\n");

		for (uint32_t i = 0; i < 32 * 8; i++)
		{
			fprintf(pFile, "g_etc1_to_bc7_m6_table%u, ", i);
			if ((i & 15) == 15)
				fprintf(pFile, "\n");
		}

		fprintf(pFile, "};\n");
		fclose(pFile);
	}
#endif
#endif

	struct etc1_to_dxt1_56_solution
	{
		uint8_t m_lo;
		uint8_t m_hi;
		uint16_t m_err;
	};

#if BASISD_SUPPORT_DXT1
	static dxt_selector_range g_etc1_to_dxt1_selector_ranges[] =
	{
		{ 0, 3 },

		{ 1, 3 },
		{ 0, 2 },

		{ 1, 2 },

		{ 2, 3 },
		{ 0, 1 },
	};

	const uint32_t NUM_ETC1_TO_DXT1_SELECTOR_RANGES = sizeof(g_etc1_to_dxt1_selector_ranges) / sizeof(g_etc1_to_dxt1_selector_ranges[0]);

	static uint32_t g_etc1_to_dxt1_selector_range_index[4][4];

	const uint32_t NUM_ETC1_TO_DXT1_SELECTOR_MAPPINGS = 10;
	static const uint8_t g_etc1_to_dxt1_selector_mappings[NUM_ETC1_TO_DXT1_SELECTOR_MAPPINGS][4] =
	{
		{ 0, 0, 1, 1 },
		{ 0, 0, 1, 2 },
		{ 0, 0, 1, 3 },
		{ 0, 0, 2, 3 },
		{ 0, 1, 1, 1 },
		{ 0, 1, 2, 2 },
		{ 0, 1, 2, 3 },
		{ 0, 2, 3, 3 },
		{ 1, 2, 2, 2 },
		{ 1, 2, 3, 3 },
	};

	static uint8_t g_etc1_to_dxt1_selector_mappings1[NUM_ETC1_TO_DXT1_SELECTOR_MAPPINGS][4];
	static uint8_t g_etc1_to_dxt1_selector_mappings2[NUM_ETC1_TO_DXT1_SELECTOR_MAPPINGS][4];

	static const etc1_to_dxt1_56_solution g_etc1_to_dxt_6[32 * 8 * NUM_ETC1_TO_DXT1_SELECTOR_MAPPINGS * NUM_ETC1_TO_DXT1_SELECTOR_RANGES] = {
#include "basisu_transcoder_tables_dxt1_6.inc"
	};

	static const etc1_to_dxt1_56_solution g_etc1_to_dxt_5[32 * 8 * NUM_ETC1_TO_DXT1_SELECTOR_MAPPINGS * NUM_ETC1_TO_DXT1_SELECTOR_RANGES] = {
#include "basisu_transcoder_tables_dxt1_5.inc"
	};

	// First saw the idea for optimal BC1 single-color block encoding using lookup tables in ryg_dxt.
	struct bc1_match_entry
	{
		uint8_t m_hi;
		uint8_t m_lo;
	};
	static bc1_match_entry g_bc1_match5_equals_1[256], g_bc1_match6_equals_1[256]; // selector 1, allow equals hi/lo
	static bc1_match_entry g_bc1_match5_equals_0[256], g_bc1_match6_equals_0[256]; // selector 0, allow equals hi/lo
	
	static void prepare_bc1_single_color_table(bc1_match_entry *pTable, const uint8_t *pExpand, int size, int sel)
	{
		int total_e = 0;

		for (int i = 0; i < 256; i++)
		{
			int lowest_e = 256;
			for (int lo = 0; lo < size; lo++)
			{
				for (int hi = 0; hi < size; hi++)
				{
					const int lo_e = pExpand[lo], hi_e = pExpand[hi];
					int e;
										
					if (sel == 1)
					{
						// Selector 1
						e = abs(((hi_e * 2 + lo_e) / 3) - i) + ((abs(hi_e - lo_e) >> 5));
					}
					else
					{
						assert(sel == 0);

						// Selector 0
						e = abs(hi_e - i);
					}

					if (e < lowest_e)
					{
						pTable[i].m_hi = static_cast<uint8_t>(hi);
						pTable[i].m_lo = static_cast<uint8_t>(lo);
						
						lowest_e = e;
					}

				} // hi
			} // lo

			total_e += lowest_e;
		}
	}
#endif // BASISD_SUPPORT_DXT1

	#if BASISD_WRITE_NEW_DXT1_TABLES
	static void create_etc1_to_dxt1_5_conversion_table()
	{
		FILE* pFile = nullptr;
		fopen_s(&pFile, "basisu_transcoder_tables_dxt1_5.inc", "w");

		uint32_t n = 0;

		for (int inten = 0; inten < 8; inten++)
		{
			for (uint32_t g = 0; g < 32; g++)
			{
				color32 block_colors[4];
				decoder_etc_block::get_diff_subblock_colors(block_colors, decoder_etc_block::pack_color5(color32(g, g, g, 255), false), inten);

				for (uint32_t sr = 0; sr < NUM_ETC1_TO_DXT1_SELECTOR_RANGES; sr++)
				{
					const uint32_t low_selector = g_etc1_to_dxt1_selector_ranges[sr].m_low;
					const uint32_t high_selector = g_etc1_to_dxt1_selector_ranges[sr].m_high;

					for (uint32_t m = 0; m < NUM_ETC1_TO_DXT1_SELECTOR_MAPPINGS; m++)
					{
						uint32_t best_lo = 0;
						uint32_t best_hi = 0;
						uint64_t best_err = UINT64_MAX;

						for (uint32_t hi = 0; hi <= 31; hi++)
						{
							for (uint32_t lo = 0; lo <= 31; lo++)
							{
								//if (lo == hi) continue;

								uint32_t colors[4];

								colors[0] = (lo << 3) | (lo >> 2);
								colors[3] = (hi << 3) | (hi >> 2);

								colors[1] = (colors[0] * 2 + colors[3]) / 3;
								colors[2] = (colors[3] * 2 + colors[0]) / 3;

								uint64_t total_err = 0;

								for (uint32_t s = low_selector; s <= high_selector; s++)
								{
									int err = block_colors[s].g - colors[g_etc1_to_dxt1_selector_mappings[m][s]];

									total_err += err * err;
								}

								if (total_err < best_err)
								{
									best_err = total_err;
									best_lo = lo;
									best_hi = hi;
								}
							}
						}

						assert(best_err <= 0xFFFF);

						//table[g + inten * 32].m_solutions[sr][m].m_lo = static_cast<uint8_t>(best_lo);
						//table[g + inten * 32].m_solutions[sr][m].m_hi = static_cast<uint8_t>(best_hi);
						//table[g + inten * 32].m_solutions[sr][m].m_err = static_cast<uint16_t>(best_err);

						//assert(best_lo != best_hi);
						fprintf(pFile, "{%u,%u,%u},", best_lo, best_hi, (uint32_t)best_err);
						n++;
						if ((n & 31) == 31)
							fprintf(pFile, "\n");
					} // m
				} // sr
			} // g
		} // inten

		fclose(pFile);
	}

	static void create_etc1_to_dxt1_6_conversion_table()
	{
		FILE* pFile = nullptr;
		fopen_s(&pFile, "basisu_transcoder_tables_dxt1_6.inc", "w");

		uint32_t n = 0;

		for (int inten = 0; inten < 8; inten++)
		{
			for (uint32_t g = 0; g < 32; g++)
			{
				color32 block_colors[4];
				decoder_etc_block::get_diff_subblock_colors(block_colors, decoder_etc_block::pack_color5(color32(g, g, g, 255), false), inten);

				for (uint32_t sr = 0; sr < NUM_ETC1_TO_DXT1_SELECTOR_RANGES; sr++)
				{
					const uint32_t low_selector = g_etc1_to_dxt1_selector_ranges[sr].m_low;
					const uint32_t high_selector = g_etc1_to_dxt1_selector_ranges[sr].m_high;

					for (uint32_t m = 0; m < NUM_ETC1_TO_DXT1_SELECTOR_MAPPINGS; m++)
					{
						uint32_t best_lo = 0;
						uint32_t best_hi = 0;
						uint64_t best_err = UINT64_MAX;

						for (uint32_t hi = 0; hi <= 63; hi++)
						{
							for (uint32_t lo = 0; lo <= 63; lo++)
							{
								//if (lo == hi) continue;

								uint32_t colors[4];

								colors[0] = (lo << 2) | (lo >> 4);
								colors[3] = (hi << 2) | (hi >> 4);

								colors[1] = (colors[0] * 2 + colors[3]) / 3;
								colors[2] = (colors[3] * 2 + colors[0]) / 3;

								uint64_t total_err = 0;

								for (uint32_t s = low_selector; s <= high_selector; s++)
								{
									int err = block_colors[s].g - colors[g_etc1_to_dxt1_selector_mappings[m][s]];

									total_err += err * err;
								}

								if (total_err < best_err)
								{
									best_err = total_err;
									best_lo = lo;
									best_hi = hi;
								}
							}
						}

						assert(best_err <= 0xFFFF);

						//table[g + inten * 32].m_solutions[sr][m].m_lo = static_cast<uint8_t>(best_lo);
						//table[g + inten * 32].m_solutions[sr][m].m_hi = static_cast<uint8_t>(best_hi);
						//table[g + inten * 32].m_solutions[sr][m].m_err = static_cast<uint16_t>(best_err);

						//assert(best_lo != best_hi);
						fprintf(pFile, "{%u,%u,%u},", best_lo, best_hi, (uint32_t)best_err);
						n++;
						if ((n & 31) == 31)
							fprintf(pFile, "\n");

					} // m
				} // sr
			} // g
		} // inten

		fclose(pFile);
	}
#endif

#if BASISD_SUPPORT_ETC2_EAC_A8
	enum
	{
		cEAC_A8_BYTES_PER_BLOCK = 8,
		cEAC_A8_SELECTOR_BYTES = 6,
		cEAC_A8_SELECTOR_BITS = 3,
		cEAC_RGBA8_BYTES_PER_BLOCK = 16,
		cEAC_RGBA8_RGB_OFFSET = 8,
		cEAC_RGBA8_A_OFFSET = 0,
		cEAC_A8_MIN_VALUE_SELECTOR = 3,
		cEAC_A8_MAX_VALUE_SELECTOR = 7
	};

	static const int8_t g_eac_a8_modifier_table[16][8] =
	{
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
		{ -1, -2, -3, -10, 0, 1, 2, 9 }, // entry 13
		{ -4, -6, -8, -9, 3, 5, 7, 8 },
		{ -3, -5, -7, -9, 2, 4, 6, 8 }
	};

	struct eac_a8_block
	{
		uint16_t m_base : 8;

		uint16_t m_table : 4;
		uint16_t m_multiplier : 4;

		uint8_t m_selectors[6];

		uint32_t get_selector(uint32_t x, uint32_t y) const
		{
			assert((x < 4) && (y < 4));

			const uint32_t ofs = 45 - (y + x * 4) * 3;

			const uint64_t pixels = get_selector_bits();

			return (pixels >> ofs) & 7;
		}

		void set_selector(uint32_t x, uint32_t y, uint32_t s)
		{
			assert((x < 4) && (y < 4) && (s < 8));

			const uint32_t ofs = 45 - (y + x * 4) * 3;

			uint64_t pixels = get_selector_bits();

			pixels &= ~(7ULL << ofs);
			pixels |= (static_cast<uint64_t>(s) << ofs);

			set_selector_bits(pixels);
		}

		uint64_t get_selector_bits() const
		{
			uint64_t pixels = ((uint64_t)m_selectors[0] << 40) | ((uint64_t)m_selectors[1] << 32) |
				((uint64_t)m_selectors[2] << 24) |
				((uint64_t)m_selectors[3] << 16) | ((uint64_t)m_selectors[4] << 8) | m_selectors[5];
			return pixels;
		}

		void set_selector_bits(uint64_t pixels)
		{
			m_selectors[0] = (uint8_t)(pixels >> 40);
			m_selectors[1] = (uint8_t)(pixels >> 32);
			m_selectors[2] = (uint8_t)(pixels >> 24);
			m_selectors[3] = (uint8_t)(pixels >> 16);
			m_selectors[4] = (uint8_t)(pixels >> 8);
			m_selectors[5] = (uint8_t)(pixels);
		}
	};

#if BASISD_WRITE_NEW_ETC2_EAC_A8_TABLES
	struct pack_eac_a8_results
	{
		uint32_t m_base;
		uint32_t m_table;
		uint32_t m_multiplier;
		std::vector<uint8_t> m_selectors;
		std::vector<uint8_t> m_selectors_temp;
	};

	static uint64_t pack_eac_a8_exhaustive(pack_eac_a8_results & results, const uint8_t * pPixels, uint32_t num_pixels)
	{
		results.m_selectors.resize(num_pixels);
		results.m_selectors_temp.resize(num_pixels);

		uint64_t best_err = UINT64_MAX;

		for (uint32_t base_color = 0; base_color < 256; base_color++)
		{
			for (uint32_t multiplier = 1; multiplier < 16; multiplier++)
			{
				for (uint32_t table = 0; table < 16; table++)
				{
					uint64_t total_err = 0;

					for (uint32_t i = 0; i < num_pixels; i++)
					{
						const int a = pPixels[i];

						uint32_t best_s_err = UINT32_MAX;
						uint32_t best_s = 0;
						for (uint32_t s = 0; s < 8; s++)
						{
							int v = (int)multiplier * g_eac_a8_modifier_table[table][s] + (int)base_color;
							if (v < 0)
								v = 0;
							else if (v > 255)
								v = 255;

							uint32_t err = abs(a - v);
							if (err < best_s_err)
							{
								best_s_err = err;
								best_s = s;
							}
						}

						results.m_selectors_temp[i] = static_cast<uint8_t>(best_s);

						total_err += best_s_err * best_s_err;
						if (total_err >= best_err)
							break;
					}

					if (total_err < best_err)
					{
						best_err = total_err;
						results.m_base = base_color;
						results.m_multiplier = multiplier;
						results.m_table = table;
						results.m_selectors.swap(results.m_selectors_temp);
					}

				} // table

			} // multiplier

		} // base_color

		return best_err;
	}
#endif

	static const dxt_selector_range s_etc2_eac_a8_selector_ranges[] =
	{
		{ 0, 3 },

		{ 1, 3 },
		{ 0, 2 },

		{ 1, 2 },
	};

	const uint32_t NUM_ETC2_EAC_A8_SELECTOR_RANGES = sizeof(s_etc2_eac_a8_selector_ranges) / sizeof(s_etc2_eac_a8_selector_ranges[0]);

	struct etc1_g_to_etc2_a8_conversion
	{
		uint8_t m_base;
		uint8_t m_table_mul; // mul*16+table
		uint16_t m_trans; // translates ETC1 selectors to ETC2_EAC_A8
	};

	static
#if !BASISD_WRITE_NEW_ETC2_EAC_A8_TABLES		
		const
#endif
		etc1_g_to_etc2_a8_conversion s_etc1_g_to_etc2_a8[32 * 8][NUM_ETC2_EAC_A8_SELECTOR_RANGES] =
	{
		{ { 0,1,3328 },{ 0,1,3328 },{ 0,1,256 },{ 0,1,256 } },
		{ { 0,226,3936 },{ 0,226,3936 },{ 0,81,488 },{ 0,81,488 } },
		{ { 6,178,4012 },{ 6,178,4008 },{ 0,146,501 },{ 0,130,496 } },
		{ { 14,178,4012 },{ 14,178,4008 },{ 8,146,501 },{ 6,82,496 } },
		{ { 23,178,4012 },{ 23,178,4008 },{ 17,146,501 },{ 3,228,496 } },
		{ { 31,178,4012 },{ 31,178,4008 },{ 25,146,501 },{ 11,228,496 } },
		{ { 39,178,4012 },{ 39,178,4008 },{ 33,146,501 },{ 19,228,496 } },
		{ { 47,178,4012 },{ 47,178,4008 },{ 41,146,501 },{ 27,228,496 } },
		{ { 56,178,4012 },{ 56,178,4008 },{ 50,146,501 },{ 36,228,496 } },
		{ { 64,178,4012 },{ 64,178,4008 },{ 58,146,501 },{ 44,228,496 } },
		{ { 72,178,4012 },{ 72,178,4008 },{ 66,146,501 },{ 52,228,496 } },
		{ { 80,178,4012 },{ 80,178,4008 },{ 74,146,501 },{ 60,228,496 } },
		{ { 89,178,4012 },{ 89,178,4008 },{ 83,146,501 },{ 69,228,496 } },
		{ { 97,178,4012 },{ 97,178,4008 },{ 91,146,501 },{ 77,228,496 } },
		{ { 105,178,4012 },{ 105,178,4008 },{ 99,146,501 },{ 85,228,496 } },
		{ { 113,178,4012 },{ 113,178,4008 },{ 107,146,501 },{ 93,228,496 } },
		{ { 122,178,4012 },{ 122,178,4008 },{ 116,146,501 },{ 102,228,496 } },
		{ { 130,178,4012 },{ 130,178,4008 },{ 124,146,501 },{ 110,228,496 } },
		{ { 138,178,4012 },{ 138,178,4008 },{ 132,146,501 },{ 118,228,496 } },
		{ { 146,178,4012 },{ 146,178,4008 },{ 140,146,501 },{ 126,228,496 } },
		{ { 155,178,4012 },{ 155,178,4008 },{ 149,146,501 },{ 135,228,496 } },
		{ { 163,178,4012 },{ 163,178,4008 },{ 157,146,501 },{ 143,228,496 } },
		{ { 171,178,4012 },{ 171,178,4008 },{ 165,146,501 },{ 151,228,496 } },
		{ { 179,178,4012 },{ 179,178,4008 },{ 173,146,501 },{ 159,228,496 } },
		{ { 188,178,4012 },{ 188,178,4008 },{ 182,146,501 },{ 168,228,496 } },
		{ { 196,178,4012 },{ 196,178,4008 },{ 190,146,501 },{ 176,228,496 } },
		{ { 204,178,4012 },{ 204,178,4008 },{ 198,146,501 },{ 184,228,496 } },
		{ { 212,178,4012 },{ 212,178,4008 },{ 206,146,501 },{ 192,228,496 } },
		{ { 221,178,4012 },{ 221,178,4008 },{ 215,146,501 },{ 201,228,496 } },
		{ { 229,178,4012 },{ 229,178,4008 },{ 223,146,501 },{ 209,228,496 } },
		{ { 235,66,4012 },{ 221,100,4008 },{ 231,146,501 },{ 217,228,496 } },
		{ { 211,102,4085 },{ 118,31,4080 },{ 211,102,501 },{ 118,31,496 } },
		{ { 1,2,3328 },{ 1,2,3328 },{ 0,1,320 },{ 0,1,320 } },
		{ { 7,162,3905 },{ 7,162,3904 },{ 1,17,480 },{ 1,17,480 } },
		{ { 15,162,3906 },{ 15,162,3904 },{ 1,117,352 },{ 1,117,352 } },
		{ { 23,162,3906 },{ 23,162,3904 },{ 5,34,500 },{ 4,53,424 } },
		{ { 32,162,3906 },{ 32,162,3904 },{ 14,34,500 },{ 3,69,424 } },
		{ { 40,162,3906 },{ 40,162,3904 },{ 22,34,500 },{ 1,133,496 } },
		{ { 48,162,3906 },{ 48,162,3904 },{ 30,34,500 },{ 4,85,496 } },
		{ { 56,162,3906 },{ 56,162,3904 },{ 38,34,500 },{ 12,85,496 } },
		{ { 65,162,3906 },{ 65,162,3904 },{ 47,34,500 },{ 1,106,424 } },
		{ { 73,162,3906 },{ 73,162,3904 },{ 55,34,500 },{ 9,106,424 } },
		{ { 81,162,3906 },{ 81,162,3904 },{ 63,34,500 },{ 7,234,496 } },
		{ { 89,162,3906 },{ 89,162,3904 },{ 71,34,500 },{ 15,234,496 } },
		{ { 98,162,3906 },{ 98,162,3904 },{ 80,34,500 },{ 24,234,496 } },
		{ { 106,162,3906 },{ 106,162,3904 },{ 88,34,500 },{ 32,234,496 } },
		{ { 114,162,3906 },{ 114,162,3904 },{ 96,34,500 },{ 40,234,496 } },
		{ { 122,162,3906 },{ 122,162,3904 },{ 104,34,500 },{ 48,234,496 } },
		{ { 131,162,3906 },{ 131,162,3904 },{ 113,34,500 },{ 57,234,496 } },
		{ { 139,162,3906 },{ 139,162,3904 },{ 121,34,500 },{ 65,234,496 } },
		{ { 147,162,3906 },{ 147,162,3904 },{ 129,34,500 },{ 73,234,496 } },
		{ { 155,162,3906 },{ 155,162,3904 },{ 137,34,500 },{ 81,234,496 } },
		{ { 164,162,3906 },{ 164,162,3904 },{ 146,34,500 },{ 90,234,496 } },
		{ { 172,162,3906 },{ 172,162,3904 },{ 154,34,500 },{ 98,234,496 } },
		{ { 180,162,3906 },{ 180,162,3904 },{ 162,34,500 },{ 106,234,496 } },
		{ { 188,162,3906 },{ 188,162,3904 },{ 170,34,500 },{ 114,234,496 } },
		{ { 197,162,3906 },{ 197,162,3904 },{ 179,34,500 },{ 123,234,496 } },
		{ { 205,162,3906 },{ 205,162,3904 },{ 187,34,500 },{ 131,234,496 } },
		{ { 213,162,3906 },{ 213,162,3904 },{ 195,34,500 },{ 139,234,496 } },
		{ { 221,162,3906 },{ 221,162,3904 },{ 203,34,500 },{ 147,234,496 } },
		{ { 230,162,3906 },{ 230,162,3904 },{ 212,34,500 },{ 156,234,496 } },
		{ { 238,162,3906 },{ 174,106,4008 },{ 220,34,500 },{ 164,234,496 } },
		{ { 240,178,4001 },{ 182,106,4008 },{ 228,34,500 },{ 172,234,496 } },
		{ { 166,108,4085 },{ 115,31,4080 },{ 166,108,501 },{ 115,31,496 } },
		{ { 1,68,3328 },{ 1,68,3328 },{ 0,17,384 },{ 0,17,384 } },
		{ { 1,148,3904 },{ 1,148,3904 },{ 1,2,384 },{ 1,2,384 } },
		{ { 21,18,3851 },{ 21,18,3848 },{ 1,50,488 },{ 1,50,488 } },
		{ { 27,195,3851 },{ 29,18,3848 },{ 0,67,488 },{ 0,67,488 } },
		{ { 34,195,3907 },{ 38,18,3848 },{ 20,66,482 },{ 0,3,496 } },
		{ { 42,195,3907 },{ 46,18,3848 },{ 28,66,482 },{ 2,6,424 } },
		{ { 50,195,3907 },{ 54,18,3848 },{ 36,66,482 },{ 4,22,424 } },
		{ { 58,195,3907 },{ 62,18,3848 },{ 44,66,482 },{ 3,73,424 } },
		{ { 67,195,3907 },{ 71,18,3848 },{ 53,66,482 },{ 3,22,496 } },
		{ { 75,195,3907 },{ 79,18,3848 },{ 61,66,482 },{ 2,137,496 } },
		{ { 83,195,3907 },{ 87,18,3848 },{ 69,66,482 },{ 1,89,496 } },
		{ { 91,195,3907 },{ 95,18,3848 },{ 77,66,482 },{ 9,89,496 } },
		{ { 100,195,3907 },{ 104,18,3848 },{ 86,66,482 },{ 18,89,496 } },
		{ { 108,195,3907 },{ 112,18,3848 },{ 94,66,482 },{ 26,89,496 } },
		{ { 116,195,3907 },{ 120,18,3848 },{ 102,66,482 },{ 34,89,496 } },
		{ { 124,195,3907 },{ 128,18,3848 },{ 110,66,482 },{ 42,89,496 } },
		{ { 133,195,3907 },{ 137,18,3848 },{ 119,66,482 },{ 51,89,496 } },
		{ { 141,195,3907 },{ 145,18,3848 },{ 127,66,482 },{ 59,89,496 } },
		{ { 149,195,3907 },{ 153,18,3848 },{ 135,66,482 },{ 67,89,496 } },
		{ { 157,195,3907 },{ 161,18,3848 },{ 143,66,482 },{ 75,89,496 } },
		{ { 166,195,3907 },{ 170,18,3848 },{ 152,66,482 },{ 84,89,496 } },
		{ { 174,195,3907 },{ 178,18,3848 },{ 160,66,482 },{ 92,89,496 } },
		{ { 182,195,3907 },{ 186,18,3848 },{ 168,66,482 },{ 100,89,496 } },
		{ { 190,195,3907 },{ 194,18,3848 },{ 176,66,482 },{ 108,89,496 } },
		{ { 199,195,3907 },{ 203,18,3848 },{ 185,66,482 },{ 117,89,496 } },
		{ { 207,195,3907 },{ 211,18,3848 },{ 193,66,482 },{ 125,89,496 } },
		{ { 215,195,3907 },{ 219,18,3848 },{ 201,66,482 },{ 133,89,496 } },
		{ { 223,195,3907 },{ 227,18,3848 },{ 209,66,482 },{ 141,89,496 } },
		{ { 231,195,3907 },{ 168,89,4008 },{ 218,66,482 },{ 150,89,496 } },
		{ { 236,18,3907 },{ 176,89,4008 },{ 226,66,482 },{ 158,89,496 } },
		{ { 158,90,4085 },{ 103,31,4080 },{ 158,90,501 },{ 103,31,496 } },
		{ { 166,90,4085 },{ 111,31,4080 },{ 166,90,501 },{ 111,31,496 } },
		{ { 0,70,3328 },{ 0,70,3328 },{ 0,45,256 },{ 0,45,256 } },
		{ { 0,117,3904 },{ 0,117,3904 },{ 0,35,384 },{ 0,35,384 } },
		{ { 13,165,3905 },{ 13,165,3904 },{ 3,221,416 },{ 3,221,416 } },
		{ { 21,165,3906 },{ 21,165,3904 },{ 11,221,416 },{ 11,221,416 } },
		{ { 30,165,3906 },{ 30,165,3904 },{ 7,61,352 },{ 7,61,352 } },
		{ { 38,165,3906 },{ 38,165,3904 },{ 2,125,352 },{ 2,125,352 } },
		{ { 46,165,3906 },{ 46,165,3904 },{ 2,37,500 },{ 10,125,352 } },
		{ { 54,165,3906 },{ 54,165,3904 },{ 10,37,500 },{ 5,61,424 } },
		{ { 63,165,3906 },{ 63,165,3904 },{ 19,37,500 },{ 1,189,424 } },
		{ { 4,254,4012 },{ 71,165,3904 },{ 27,37,500 },{ 9,189,424 } },
		{ { 12,254,4012 },{ 79,165,3904 },{ 35,37,500 },{ 4,77,424 } },
		{ { 20,254,4012 },{ 87,165,3904 },{ 43,37,500 },{ 12,77,424 } },
		{ { 29,254,4012 },{ 96,165,3904 },{ 52,37,500 },{ 8,93,424 } },
		{ { 37,254,4012 },{ 104,165,3904 },{ 60,37,500 },{ 3,141,496 } },
		{ { 45,254,4012 },{ 112,165,3904 },{ 68,37,500 },{ 11,141,496 } },
		{ { 53,254,4012 },{ 120,165,3904 },{ 76,37,500 },{ 6,93,496 } },
		{ { 62,254,4012 },{ 129,165,3904 },{ 85,37,500 },{ 15,93,496 } },
		{ { 70,254,4012 },{ 137,165,3904 },{ 93,37,500 },{ 23,93,496 } },
		{ { 78,254,4012 },{ 145,165,3904 },{ 101,37,500 },{ 31,93,496 } },
		{ { 86,254,4012 },{ 153,165,3904 },{ 109,37,500 },{ 39,93,496 } },
		{ { 95,254,4012 },{ 162,165,3904 },{ 118,37,500 },{ 48,93,496 } },
		{ { 103,254,4012 },{ 170,165,3904 },{ 126,37,500 },{ 56,93,496 } },
		{ { 111,254,4012 },{ 178,165,3904 },{ 134,37,500 },{ 64,93,496 } },
		{ { 119,254,4012 },{ 186,165,3904 },{ 142,37,500 },{ 72,93,496 } },
		{ { 128,254,4012 },{ 195,165,3904 },{ 151,37,500 },{ 81,93,496 } },
		{ { 136,254,4012 },{ 203,165,3904 },{ 159,37,500 },{ 89,93,496 } },
		{ { 212,165,3906 },{ 136,77,4008 },{ 167,37,500 },{ 97,93,496 } },
		{ { 220,165,3394 },{ 131,93,4008 },{ 175,37,500 },{ 105,93,496 } },
		{ { 214,181,4001 },{ 140,93,4008 },{ 184,37,500 },{ 114,93,496 } },
		{ { 222,181,4001 },{ 148,93,4008 },{ 192,37,500 },{ 122,93,496 } },
		{ { 114,95,4085 },{ 99,31,4080 },{ 114,95,501 },{ 99,31,496 } },
		{ { 122,95,4085 },{ 107,31,4080 },{ 122,95,501 },{ 107,31,496 } },
		{ { 0,102,3840 },{ 0,102,3840 },{ 0,18,384 },{ 0,18,384 } },
		{ { 5,167,3904 },{ 5,167,3904 },{ 0,13,256 },{ 0,13,256 } },
		{ { 4,54,3968 },{ 4,54,3968 },{ 1,67,448 },{ 1,67,448 } },
		{ { 30,198,3850 },{ 30,198,3848 },{ 0,3,480 },{ 0,3,480 } },
		{ { 39,198,3850 },{ 39,198,3848 },{ 3,52,488 },{ 3,52,488 } },
		{ { 47,198,3851 },{ 47,198,3848 },{ 3,4,488 },{ 3,4,488 } },
		{ { 55,198,3851 },{ 55,198,3848 },{ 1,70,488 },{ 1,70,488 } },
		{ { 54,167,3906 },{ 63,198,3848 },{ 3,22,488 },{ 3,22,488 } },
		{ { 62,167,3906 },{ 72,198,3848 },{ 24,118,488 },{ 0,6,496 } },
		{ { 70,167,3906 },{ 80,198,3848 },{ 32,118,488 },{ 2,89,488 } },
		{ { 78,167,3906 },{ 88,198,3848 },{ 40,118,488 },{ 1,73,496 } },
		{ { 86,167,3906 },{ 96,198,3848 },{ 48,118,488 },{ 0,28,424 } },
		{ { 95,167,3906 },{ 105,198,3848 },{ 57,118,488 },{ 9,28,424 } },
		{ { 103,167,3906 },{ 113,198,3848 },{ 65,118,488 },{ 5,108,496 } },
		{ { 111,167,3906 },{ 121,198,3848 },{ 73,118,488 },{ 13,108,496 } },
		{ { 119,167,3906 },{ 129,198,3848 },{ 81,118,488 },{ 21,108,496 } },
		{ { 128,167,3906 },{ 138,198,3848 },{ 90,118,488 },{ 6,28,496 } },
		{ { 136,167,3906 },{ 146,198,3848 },{ 98,118,488 },{ 14,28,496 } },
		{ { 144,167,3906 },{ 154,198,3848 },{ 106,118,488 },{ 22,28,496 } },
		{ { 152,167,3906 },{ 162,198,3848 },{ 114,118,488 },{ 30,28,496 } },
		{ { 161,167,3906 },{ 171,198,3848 },{ 123,118,488 },{ 39,28,496 } },
		{ { 169,167,3906 },{ 179,198,3848 },{ 131,118,488 },{ 47,28,496 } },
		{ { 177,167,3906 },{ 187,198,3848 },{ 139,118,488 },{ 55,28,496 } },
		{ { 185,167,3906 },{ 195,198,3848 },{ 147,118,488 },{ 63,28,496 } },
		{ { 194,167,3906 },{ 120,12,4008 },{ 156,118,488 },{ 72,28,496 } },
		{ { 206,198,3907 },{ 116,28,4008 },{ 164,118,488 },{ 80,28,496 } },
		{ { 214,198,3907 },{ 124,28,4008 },{ 172,118,488 },{ 88,28,496 } },
		{ { 222,198,3395 },{ 132,28,4008 },{ 180,118,488 },{ 96,28,496 } },
		{ { 207,134,4001 },{ 141,28,4008 },{ 189,118,488 },{ 105,28,496 } },
		{ { 95,30,4085 },{ 86,31,4080 },{ 95,30,501 },{ 86,31,496 } },
		{ { 103,30,4085 },{ 94,31,4080 },{ 103,30,501 },{ 94,31,496 } },
		{ { 111,30,4085 },{ 102,31,4080 },{ 111,30,501 },{ 102,31,496 } },
		{ { 0,104,3840 },{ 0,104,3840 },{ 0,18,448 },{ 0,18,448 } },
		{ { 4,39,3904 },{ 4,39,3904 },{ 0,4,384 },{ 0,4,384 } },
		{ { 0,56,3968 },{ 0,56,3968 },{ 0,84,448 },{ 0,84,448 } },
		{ { 6,110,3328 },{ 6,110,3328 },{ 0,20,448 },{ 0,20,448 } },
		{ { 41,200,3850 },{ 41,200,3848 },{ 1,4,480 },{ 1,4,480 } },
		{ { 49,200,3850 },{ 49,200,3848 },{ 1,8,416 },{ 1,8,416 } },
		{ { 57,200,3851 },{ 57,200,3848 },{ 1,38,488 },{ 1,38,488 } },
		{ { 65,200,3851 },{ 65,200,3848 },{ 1,120,488 },{ 1,120,488 } },
		{ { 74,200,3851 },{ 74,200,3848 },{ 2,72,488 },{ 2,72,488 } },
		{ { 69,6,3907 },{ 82,200,3848 },{ 2,24,488 },{ 2,24,488 } },
		{ { 77,6,3907 },{ 90,200,3848 },{ 26,120,488 },{ 10,24,488 } },
		{ { 97,63,3330 },{ 98,200,3848 },{ 34,120,488 },{ 2,8,496 } },
		{ { 106,63,3330 },{ 107,200,3848 },{ 43,120,488 },{ 3,92,488 } },
		{ { 114,63,3330 },{ 115,200,3848 },{ 51,120,488 },{ 11,92,488 } },
		{ { 122,63,3330 },{ 123,200,3848 },{ 59,120,488 },{ 7,76,496 } },
		{ { 130,63,3330 },{ 131,200,3848 },{ 67,120,488 },{ 15,76,496 } },
		{ { 139,63,3330 },{ 140,200,3848 },{ 76,120,488 },{ 24,76,496 } },
		{ { 147,63,3330 },{ 148,200,3848 },{ 84,120,488 },{ 32,76,496 } },
		{ { 155,63,3330 },{ 156,200,3848 },{ 92,120,488 },{ 40,76,496 } },
		{ { 163,63,3330 },{ 164,200,3848 },{ 100,120,488 },{ 48,76,496 } },
		{ { 172,63,3330 },{ 173,200,3848 },{ 109,120,488 },{ 57,76,496 } },
		{ { 184,6,3851 },{ 181,200,3848 },{ 117,120,488 },{ 65,76,496 } },
		{ { 192,6,3851 },{ 133,28,3936 },{ 125,120,488 },{ 73,76,496 } },
		{ { 189,200,3907 },{ 141,28,3936 },{ 133,120,488 },{ 81,76,496 } },
		{ { 198,200,3907 },{ 138,108,4000 },{ 142,120,488 },{ 90,76,496 } },
		{ { 206,200,3907 },{ 146,108,4000 },{ 150,120,488 },{ 98,76,496 } },
		{ { 214,200,3395 },{ 154,108,4000 },{ 158,120,488 },{ 106,76,496 } },
		{ { 190,136,4001 },{ 162,108,4000 },{ 166,120,488 },{ 114,76,496 } },
		{ { 123,30,4076 },{ 87,15,4080 },{ 123,30,492 },{ 87,15,496 } },
		{ { 117,110,4084 },{ 80,31,4080 },{ 117,110,500 },{ 80,31,496 } },
		{ { 125,110,4084 },{ 88,31,4080 },{ 125,110,500 },{ 88,31,496 } },
		{ { 133,110,4084 },{ 96,31,4080 },{ 133,110,500 },{ 96,31,496 } },
		{ { 9,56,3904 },{ 9,56,3904 },{ 0,67,448 },{ 0,67,448 } },
		{ { 1,8,3904 },{ 1,8,3904 },{ 1,84,448 },{ 1,84,448 } },
		{ { 1,124,3904 },{ 1,124,3904 },{ 0,39,384 },{ 0,39,384 } },
		{ { 9,124,3904 },{ 9,124,3904 },{ 1,4,448 },{ 1,4,448 } },
		{ { 6,76,3904 },{ 6,76,3904 },{ 0,70,448 },{ 0,70,448 } },
		{ { 62,6,3859 },{ 62,6,3856 },{ 2,38,480 },{ 2,38,480 } },
		{ { 70,6,3859 },{ 70,6,3856 },{ 5,43,416 },{ 5,43,416 } },
		{ { 78,6,3859 },{ 78,6,3856 },{ 2,11,416 },{ 2,11,416 } },
		{ { 87,6,3859 },{ 87,6,3856 },{ 0,171,488 },{ 0,171,488 } },
		{ { 67,8,3906 },{ 95,6,3856 },{ 8,171,488 },{ 8,171,488 } },
		{ { 75,8,3907 },{ 103,6,3856 },{ 5,123,488 },{ 5,123,488 } },
		{ { 83,8,3907 },{ 111,6,3856 },{ 2,75,488 },{ 2,75,488 } },
		{ { 92,8,3907 },{ 120,6,3856 },{ 0,27,488 },{ 0,27,488 } },
		{ { 100,8,3907 },{ 128,6,3856 },{ 8,27,488 },{ 8,27,488 } },
		{ { 120,106,3843 },{ 136,6,3856 },{ 100,6,387 },{ 16,27,488 } },
		{ { 128,106,3843 },{ 144,6,3856 },{ 108,6,387 },{ 2,11,496 } },
		{ { 137,106,3843 },{ 153,6,3856 },{ 117,6,387 },{ 11,11,496 } },
		{ { 145,106,3843 },{ 161,6,3856 },{ 125,6,387 },{ 19,11,496 } },
		{ { 163,8,3851 },{ 137,43,3904 },{ 133,6,387 },{ 27,11,496 } },
		{ { 171,8,3851 },{ 101,11,4000 },{ 141,6,387 },{ 35,11,496 } },
		{ { 180,8,3851 },{ 110,11,4000 },{ 150,6,387 },{ 44,11,496 } },
		{ { 188,8,3851 },{ 118,11,4000 },{ 158,6,387 },{ 52,11,496 } },
		{ { 172,72,3907 },{ 126,11,4000 },{ 166,6,387 },{ 60,11,496 } },
		{ { 174,6,3971 },{ 134,11,4000 },{ 174,6,387 },{ 68,11,496 } },
		{ { 183,6,3971 },{ 143,11,4000 },{ 183,6,387 },{ 77,11,496 } },
		{ { 191,6,3971 },{ 151,11,4000 },{ 191,6,387 },{ 85,11,496 } },
		{ { 199,6,3971 },{ 159,11,4000 },{ 199,6,387 },{ 93,11,496 } },
		{ { 92,12,4084 },{ 69,15,4080 },{ 92,12,500 },{ 69,15,496 } },
		{ { 101,12,4084 },{ 78,15,4080 },{ 101,12,500 },{ 78,15,496 } },
		{ { 109,12,4084 },{ 86,15,4080 },{ 109,12,500 },{ 86,15,496 } },
		{ { 117,12,4084 },{ 79,31,4080 },{ 117,12,500 },{ 79,31,496 } },
		{ { 125,12,4084 },{ 87,31,4080 },{ 125,12,500 },{ 87,31,496 } },
		{ { 71,8,3602 },{ 71,8,3600 },{ 2,21,384 },{ 2,21,384 } },
		{ { 79,8,3611 },{ 79,8,3608 },{ 0,69,448 },{ 0,69,448 } },
		{ { 87,8,3611 },{ 87,8,3608 },{ 0,23,384 },{ 0,23,384 } },
		{ { 95,8,3611 },{ 95,8,3608 },{ 1,5,448 },{ 1,5,448 } },
		{ { 104,8,3611 },{ 104,8,3608 },{ 0,88,448 },{ 0,88,448 } },
		{ { 112,8,3611 },{ 112,8,3608 },{ 0,72,448 },{ 0,72,448 } },
		{ { 120,8,3611 },{ 121,8,3608 },{ 36,21,458 },{ 36,21,456 } },
		{ { 133,47,3091 },{ 129,8,3608 },{ 44,21,458 },{ 44,21,456 } },
		{ { 142,47,3091 },{ 138,8,3608 },{ 53,21,459 },{ 53,21,456 } },
		{ { 98,12,3850 },{ 98,12,3848 },{ 61,21,459 },{ 61,21,456 } },
		{ { 106,12,3850 },{ 106,12,3848 },{ 10,92,480 },{ 69,21,456 } },
		{ { 114,12,3851 },{ 114,12,3848 },{ 18,92,480 },{ 77,21,456 } },
		{ { 87,12,3906 },{ 87,12,3904 },{ 3,44,488 },{ 86,21,456 } },
		{ { 95,12,3906 },{ 95,12,3904 },{ 11,44,488 },{ 94,21,456 } },
		{ { 103,12,3906 },{ 103,12,3904 },{ 19,44,488 },{ 102,21,456 } },
		{ { 111,12,3907 },{ 111,12,3904 },{ 27,44,489 },{ 110,21,456 } },
		{ { 120,12,3907 },{ 120,12,3904 },{ 36,44,489 },{ 119,21,456 } },
		{ { 128,12,3907 },{ 128,12,3904 },{ 44,44,489 },{ 127,21,456 } },
		{ { 136,12,3907 },{ 136,12,3904 },{ 52,44,489 },{ 135,21,456 } },
		{ { 144,12,3907 },{ 144,12,3904 },{ 60,44,489 },{ 143,21,456 } },
		{ { 153,12,3907 },{ 153,12,3904 },{ 69,44,490 },{ 152,21,456 } },
		{ { 161,12,3395 },{ 149,188,3968 },{ 77,44,490 },{ 160,21,456 } },
		{ { 169,12,3395 },{ 198,21,3928 },{ 85,44,490 },{ 168,21,456 } },
		{ { 113,95,4001 },{ 201,69,3992 },{ 125,8,483 },{ 176,21,456 } },
		{ { 122,95,4001 },{ 200,21,3984 },{ 134,8,483 },{ 185,21,456 } },
		{ { 142,8,4067 },{ 208,21,3984 },{ 142,8,483 },{ 193,21,456 } },
		{ { 151,8,4067 },{ 47,15,4080 },{ 151,8,483 },{ 47,15,496 } },
		{ { 159,8,4067 },{ 55,15,4080 },{ 159,8,483 },{ 55,15,496 } },
		{ { 168,8,4067 },{ 64,15,4080 },{ 168,8,483 },{ 64,15,496 } },
		{ { 160,40,4075 },{ 72,15,4080 },{ 160,40,491 },{ 72,15,496 } },
		{ { 168,40,4075 },{ 80,15,4080 },{ 168,40,491 },{ 80,15,496 } },
		{ { 144,8,4082 },{ 88,15,4080 },{ 144,8,498 },{ 88,15,496 } }
	};
#endif

#if BASISD_WRITE_NEW_ETC2_EAC_A8_TABLES
	static void create_etc2_eac_a8_conversion_table()
	{
		FILE* pFile = fopen("basisu_decoder_tables_etc2_eac_a8.inc", "w");

		for (uint32_t inten = 0; inten < 8; inten++)
		{
			for (uint32_t base = 0; base < 32; base++)
			{
				color32 block_colors[4];
				decoder_etc_block::get_diff_subblock_colors(block_colors, decoder_etc_block::pack_color5(color32(base, base, base, 255), false), inten);

				fprintf(pFile, "{");

				for (uint32_t sel_range = 0; sel_range < NUM_ETC2_EAC_A8_SELECTOR_RANGES; sel_range++)
				{
					const uint32_t low_selector = s_etc2_eac_a8_selector_ranges[sel_range].m_low;
					const uint32_t high_selector = s_etc2_eac_a8_selector_ranges[sel_range].m_high;

					// We have a ETC1 base color and intensity, and a used selector range from low_selector-high_selector.
					// Now find the best ETC2 EAC A8 base/table/multiplier that fits these colors.

					uint8_t pixels[4];
					uint32_t num_pixels = 0;
					for (uint32_t s = low_selector; s <= high_selector; s++)
						pixels[num_pixels++] = block_colors[s].g;

					pack_eac_a8_results pack_results;
					pack_eac_a8_exhaustive(pack_results, pixels, num_pixels);

					etc1_g_to_etc2_a8_conversion & c = s_etc1_g_to_etc2_a8[base + inten * 32][sel_range];

					c.m_base = pack_results.m_base;
					c.m_table_mul = pack_results.m_table * 16 + pack_results.m_multiplier;
					c.m_trans = 0;

					for (uint32_t s = 0; s < 4; s++)
					{
						if ((s < low_selector) || (s > high_selector))
							continue;

						uint32_t etc2_selector = pack_results.m_selectors[s - low_selector];

						c.m_trans |= (etc2_selector << (s * 3));
					}

					fprintf(pFile, "{%u,%u,%u}", c.m_base, c.m_table_mul, c.m_trans);
					if (sel_range < (NUM_ETC2_EAC_A8_SELECTOR_RANGES - 1))
						fprintf(pFile, ",");
				}

				fprintf(pFile, "},\n");
			}
		}

		fclose(pFile);
	}
#endif

	void basisu_transcoder_init()
	{
		static bool s_initialized;
		if (s_initialized)
			return;

#if BASISD_WRITE_NEW_BC7_TABLES
		create_etc1_to_bc7_m6_conversion_table();
		exit(0);
#endif

#if BASISD_WRITE_NEW_DXT1_TABLES
		create_etc1_to_dxt1_5_conversion_table();
		create_etc1_to_dxt1_6_conversion_table();
		exit(0);
#endif

#if BASISD_WRITE_NEW_ETC2_EAC_A8_TABLES
		create_etc2_eac_a8_conversion_table();
		exit(0);
#endif

#if BASISD_SUPPORT_DXT1
		uint8_t bc1_expand5[32];
		for (int i = 0; i < 32; i++)
			bc1_expand5[i] = static_cast<uint8_t>((i << 3) | (i >> 2));
		prepare_bc1_single_color_table(g_bc1_match5_equals_1, bc1_expand5, 32, 1);
		prepare_bc1_single_color_table(g_bc1_match5_equals_0, bc1_expand5, 32, 0);
			
		uint8_t bc1_expand6[64];
		for (int i = 0; i < 64; i++)
			bc1_expand6[i] = static_cast<uint8_t>((i << 2) | (i >> 4));
		prepare_bc1_single_color_table(g_bc1_match6_equals_1, bc1_expand6, 64, 1);
		prepare_bc1_single_color_table(g_bc1_match6_equals_0, bc1_expand6, 64, 0);
						
		for (uint32_t i = 0; i < NUM_ETC1_TO_DXT1_SELECTOR_RANGES; i++)
		{
			uint32_t l = g_etc1_to_dxt1_selector_ranges[i].m_low;
			uint32_t h = g_etc1_to_dxt1_selector_ranges[i].m_high;
			g_etc1_to_dxt1_selector_range_index[l][h] = i;
		}

		for (uint32_t sm = 0; sm < NUM_ETC1_TO_DXT1_SELECTOR_MAPPINGS; sm++)
		{
			// iterate over the raw ETC1 selectors (which aren't linear)
			for (uint32_t j = 0; j < 4; j++)
			{
				static const uint8_t s_etc1_to_selector_index[cETC1SelectorValues] = { 2, 3, 1, 0 };
				static const uint8_t s_etc1_to_dxt1_xlat[4] = { 0, 2, 3, 1 };
				static const uint8_t s_etc1_to_dxt1_inverted_xlat[4] = { 1, 3, 2, 0 };

				uint32_t etc1_selector = s_etc1_to_selector_index[j];

				uint32_t dxt1_selector = g_etc1_to_dxt1_selector_mappings[sm][etc1_selector];

				uint32_t raw_dxt1_selector = s_etc1_to_dxt1_xlat[dxt1_selector];
				uint32_t raw_dxt1_selector_inv = s_etc1_to_dxt1_inverted_xlat[dxt1_selector];

				g_etc1_to_dxt1_selector_mappings1[sm][j] = (uint8_t)raw_dxt1_selector;
				g_etc1_to_dxt1_selector_mappings2[sm][j] = (uint8_t)raw_dxt1_selector_inv;
			}
		}
#endif

#if BASISD_SUPPORT_BC7
		for (uint32_t i = 0; i < NUM_ETC1_TO_BC7_M6_SELECTOR_RANGES; i++)
		{
			uint32_t l = g_etc1_to_bc7_selector_ranges[i].m_low;
			uint32_t h = g_etc1_to_bc7_selector_ranges[i].m_high;
			g_etc1_to_bc7_m6_selector_range_index[l][h] = i;
		}

		for (uint32_t sm = 0; sm < NUM_ETC1_TO_BC7_M6_SELECTOR_MAPPINGS; sm++)
		{
			// iterate over the raw ETC1 selectors (which aren't linearize)
			for (uint32_t j = 0; j < 4; j++)
			{
				static const uint8_t s_etc1_to_selector_index[cETC1SelectorValues] = { 2, 3, 1, 0 };

				uint32_t etc1_selector = s_etc1_to_selector_index[j];

				uint32_t bc7_m6_selector = g_etc1_to_bc7_selector_mappings[sm][etc1_selector];
				uint32_t bc7_m6_selector_inv = 15 - bc7_m6_selector;

				g_etc1_to_bc7_selector_mappings_from_raw_etc1[sm][j] = (uint8_t)bc7_m6_selector;
				g_etc1_to_bc7_selector_mappings_from_raw_etc1_inv[sm][j] = (uint8_t)bc7_m6_selector_inv;
			}
		}
#endif

		s_initialized = true;
	}

#if BASISD_SUPPORT_DXT1
	static void convert_etc1s_to_dxt1(dxt1_block * pDst_block, const decoder_etc_block *pSrc_block, const selector * pSelector, bool use_threecolor_blocks)
	{
#if !BASISD_WRITE_NEW_DXT1_TABLES
		const uint32_t low_selector = pSelector->m_lo_selector;
		const uint32_t high_selector = pSelector->m_hi_selector;

		const color32 base_color(pSrc_block->get_base5_color_unscaled());
		const uint32_t inten_table = pSrc_block->get_inten_table(0);

		if (low_selector == high_selector)
		{
			color32 block_colors[4];

			decoder_etc_block::get_block_colors5(block_colors, base_color, inten_table);

			const uint32_t r = block_colors[low_selector].r;
			const uint32_t g = block_colors[low_selector].g;
			const uint32_t b = block_colors[low_selector].b;

			uint32_t mask = 0xAA;
			uint32_t max16 = (g_bc1_match5_equals_1[r].m_hi << 11) | (g_bc1_match6_equals_1[g].m_hi << 5) | g_bc1_match5_equals_1[b].m_hi;
			uint32_t min16 = (g_bc1_match5_equals_1[r].m_lo << 11) | (g_bc1_match6_equals_1[g].m_lo << 5) | g_bc1_match5_equals_1[b].m_lo;

			if ((!use_threecolor_blocks) && (min16 == max16))
			{
				// This is an annoying edge case that impacts BC3.
				// This is to guarantee that BC3 blocks never use punchthrough alpha (3 color) mode, which isn't supported on some (all?) GPU's.
				mask = 0;

				// Make l > h
				if (min16 > 0)
					min16--;
				else 
				{
					// l = h = 0
					assert(min16 == max16 && max16 == 0);

					max16 = 1;
					min16 = 0;
					mask = 0x55;
				}
			
				assert(max16 > min16);
			}

			if (max16 < min16)
			{
				std::swap(max16, min16);
				mask ^= 0x55;
			}
						
			pDst_block->set_low_color(static_cast<uint16_t>(max16));
			pDst_block->set_high_color(static_cast<uint16_t>(min16));
			pDst_block->m_selectors[0] = static_cast<uint8_t>(mask);
			pDst_block->m_selectors[1] = static_cast<uint8_t>(mask);
			pDst_block->m_selectors[2] = static_cast<uint8_t>(mask);
			pDst_block->m_selectors[3] = static_cast<uint8_t>(mask);

			return;
		}
		else if ((inten_table >= 7) && (pSelector->m_num_unique_selectors == 2) && (pSelector->m_lo_selector == 0) && (pSelector->m_hi_selector == 3))
		{
			color32 block_colors[4];

			decoder_etc_block::get_block_colors5(block_colors, base_color, inten_table);

			const uint32_t r0 = block_colors[0].r;
			const uint32_t g0 = block_colors[0].g;
			const uint32_t b0 = block_colors[0].b;

			const uint32_t r1 = block_colors[3].r;
			const uint32_t g1 = block_colors[3].g;
			const uint32_t b1 = block_colors[3].b;

			uint32_t max16 = (g_bc1_match5_equals_0[r0].m_hi << 11) | (g_bc1_match6_equals_0[g0].m_hi << 5) | g_bc1_match5_equals_0[b0].m_hi;
			uint32_t min16 = (g_bc1_match5_equals_0[r1].m_hi << 11) | (g_bc1_match6_equals_0[g1].m_hi << 5) | g_bc1_match5_equals_0[b1].m_hi;

			uint32_t l = 0, h = 1;

			if (min16 == max16)
			{
				// Make l > h
				if (min16 > 0)
				{
					min16--;

					l = 0; 
					h = 0;
				}
				else 
				{
					// l = h = 0
					assert(min16 == max16 && max16 == 0);

					max16 = 1;
					min16 = 0;
					
					l = 1;
					h = 1;
				}
			
				assert(max16 > min16);
			}

			if (max16 < min16)
			{
				std::swap(max16, min16);
				l = 1; 
				h = 0;
			}

			pDst_block->set_low_color((uint16_t)max16);
			pDst_block->set_high_color((uint16_t)min16);

			// TODO: This can be further optimized (read of ETC1 selector bits)
			for (uint32_t y = 0; y < 4; y++)
			{
				for (uint32_t x = 0; x < 4; x++)
				{
					uint32_t s = pSrc_block->get_selector(x, y);
					pDst_block->set_selector(x, y, (s == 3) ? h : l);
				}
			}

			return;
		}

		const uint32_t selector_range_table = g_etc1_to_dxt1_selector_range_index[low_selector][high_selector];

		//[32][8][RANGES][MAPPING]
		const etc1_to_dxt1_56_solution *pTable_r = &g_etc1_to_dxt_5[(inten_table * 32 + base_color.r) * (NUM_ETC1_TO_DXT1_SELECTOR_RANGES * NUM_ETC1_TO_DXT1_SELECTOR_MAPPINGS) + selector_range_table * NUM_ETC1_TO_DXT1_SELECTOR_MAPPINGS];
		const etc1_to_dxt1_56_solution *pTable_g = &g_etc1_to_dxt_6[(inten_table * 32 + base_color.g) * (NUM_ETC1_TO_DXT1_SELECTOR_RANGES * NUM_ETC1_TO_DXT1_SELECTOR_MAPPINGS) + selector_range_table * NUM_ETC1_TO_DXT1_SELECTOR_MAPPINGS];
		const etc1_to_dxt1_56_solution *pTable_b = &g_etc1_to_dxt_5[(inten_table * 32 + base_color.b) * (NUM_ETC1_TO_DXT1_SELECTOR_RANGES * NUM_ETC1_TO_DXT1_SELECTOR_MAPPINGS) + selector_range_table * NUM_ETC1_TO_DXT1_SELECTOR_MAPPINGS];

		uint32_t best_err = UINT_MAX;
		uint32_t best_mapping = 0;

		assert(NUM_ETC1_TO_DXT1_SELECTOR_MAPPINGS == 10);
#define DO_ITER(m) { uint32_t total_err = pTable_r[m].m_err + pTable_g[m].m_err + pTable_b[m].m_err; if (total_err < best_err) { best_err = total_err; best_mapping = m; } }
		DO_ITER(0); DO_ITER(1); DO_ITER(2); DO_ITER(3); DO_ITER(4);
		DO_ITER(5); DO_ITER(6); DO_ITER(7); DO_ITER(8); DO_ITER(9);
#undef DO_ITER

		uint32_t l = dxt1_block::pack_unscaled_color(pTable_r[best_mapping].m_lo, pTable_g[best_mapping].m_lo, pTable_b[best_mapping].m_lo);
		uint32_t h = dxt1_block::pack_unscaled_color(pTable_r[best_mapping].m_hi, pTable_g[best_mapping].m_hi, pTable_b[best_mapping].m_hi);

		const uint8_t *pSelectors_xlat = &g_etc1_to_dxt1_selector_mappings1[best_mapping][0];

		if (l < h)
		{
			std::swap(l, h);
			pSelectors_xlat = &g_etc1_to_dxt1_selector_mappings2[best_mapping][0];
		}

		pDst_block->set_low_color(static_cast<uint16_t>(l));
		pDst_block->set_high_color(static_cast<uint16_t>(h));

		if (l == h)
		{
			uint8_t mask = 0;

			if (!use_threecolor_blocks)
			{
				// This is an annoying edge case that impacts BC3.

				// Make l > h
				if (h > 0)
					h--;
				else 
				{
					// l = h = 0
					assert(l == h && h == 0);

					h = 0;
					l = 1;
					mask = 0x55;
				}

				assert(l > h);
				pDst_block->set_low_color(static_cast<uint16_t>(l));
				pDst_block->set_high_color(static_cast<uint16_t>(h));
			}
			
			pDst_block->m_selectors[0] = mask;
			pDst_block->m_selectors[1] = mask;
			pDst_block->m_selectors[2] = mask;
			pDst_block->m_selectors[3] = mask;

			return;
		}

		const uint32_t sel_bits0 = pSrc_block->m_bytes[7];
		const uint32_t sel_bits1 = pSrc_block->m_bytes[6];
		const uint32_t sel_bits2 = pSrc_block->m_bytes[5];
		const uint32_t sel_bits3 = pSrc_block->m_bytes[4];

		// y coords
		// 4 3210 3210 MSB
		// 5 3210 3210 MSB
		// 6 3210 3210 LSB
		// 7 3210 3210 LSB

		// x coords
		// 4 3333 2222 MSB
		// 5 1111 0000 MSB
		// 6 3333 2222 LSB
		// 7 1111 0000 LSB

		uint32_t dxt1_sels0 = 0, dxt1_sels1 = 0, dxt1_sels2 = 0, dxt1_sels3 = 0;

		// TODO: This function accepts ETC1S selectors, which are packed in some silly way. We could modify the transcoder so it unmunges the ETC1S selector data when it decodes the codebooks.
#if 0

#define DO_X(x) { \
		const uint32_t byte_ofs = 7 - (((x) * 4) >> 3); \
		uint32_t lsb_bits = pSrc_block->m_bytes[byte_ofs] >> (((x) & 1) * 4); \
		uint32_t msb_bits = pSrc_block->m_bytes[byte_ofs - 2] >> (((x) & 1) * 4); \
		uint32_t x_shift = (x) * 2; \
		dxt1_sels0 |= (pSelectors_xlat[(lsb_bits & 1) | ((msb_bits & 1) << 1)] << x_shift); \
		dxt1_sels1 |= (pSelectors_xlat[((lsb_bits >> 1) & 1) | (((msb_bits >> 1) & 1) << 1)] << x_shift); \
		dxt1_sels2 |= (pSelectors_xlat[((lsb_bits >> 2) & 1) | (((msb_bits >> 2) & 1) << 1)] << x_shift); \
		dxt1_sels3 |= (pSelectors_xlat[((lsb_bits >> 3) & 1) | (((msb_bits >> 3) & 1) << 1)] << x_shift); }

		DO_X(0);
		DO_X(1);
		DO_X(2);
		DO_X(3);
#undef DO_X

#else

#define DO_X(x) { \
		const uint32_t byte_ofs = 7 - (((x) * 4) >> 3); \
		const uint32_t lsb_bits = pSrc_block->m_bytes[byte_ofs] >> (((x) & 1) * 4); \
		const uint32_t msb_bits = pSrc_block->m_bytes[byte_ofs - 2] >> (((x) & 1) * 4); \
		const uint32_t lookup = (lsb_bits & 0xF)| ((msb_bits & 0xF) << 4); \
		const uint32_t x_shift = (x) * 2; \
		dxt1_sels0 |= (pSelectors_xlat[g_etc1_x_selector_unpack[0][lookup]] << x_shift); \
		dxt1_sels1 |= (pSelectors_xlat[g_etc1_x_selector_unpack[1][lookup]] << x_shift); \
		dxt1_sels2 |= (pSelectors_xlat[g_etc1_x_selector_unpack[2][lookup]] << x_shift); \
		dxt1_sels3 |= (pSelectors_xlat[g_etc1_x_selector_unpack[3][lookup]] << x_shift); }

		DO_X(0);
		DO_X(1);
		DO_X(2);
		DO_X(3);
#undef DO_X

#endif

		pDst_block->m_selectors[0] = (uint8_t)dxt1_sels0;
		pDst_block->m_selectors[1] = (uint8_t)dxt1_sels1;
		pDst_block->m_selectors[2] = (uint8_t)dxt1_sels2;
		pDst_block->m_selectors[3] = (uint8_t)dxt1_sels3;
#endif
	}
#endif

	static dxt_selector_range s_dxt5a_selector_ranges[] =
	{
		{ 0, 3 },

		{ 1, 3 },
		{ 0, 2 },

		{ 1, 2 },
	};

	const uint32_t NUM_DXT5A_SELECTOR_RANGES = sizeof(s_dxt5a_selector_ranges) / sizeof(s_dxt5a_selector_ranges[0]);

	struct etc1_g_to_dxt5a_conversion
	{
		uint8_t m_lo, m_hi;
		uint16_t m_trans;
	};

	static etc1_g_to_dxt5a_conversion g_etc1_g_to_dxt5a[32 * 8][NUM_DXT5A_SELECTOR_RANGES] =
	{
		{ { 8, 0, 393 },{ 8, 0, 392 },{ 2, 0, 9 },{ 2, 0, 8 }, },
		{ { 6, 16, 710 },{ 16, 6, 328 },{ 0, 10, 96 },{ 10, 6, 8 }, },
		{ { 28, 5, 1327 },{ 24, 14, 328 },{ 8, 18, 96 },{ 18, 14, 8 }, },
		{ { 36, 13, 1327 },{ 32, 22, 328 },{ 16, 26, 96 },{ 26, 22, 8 }, },
		{ { 45, 22, 1327 },{ 41, 31, 328 },{ 25, 35, 96 },{ 35, 31, 8 }, },
		{ { 53, 30, 1327 },{ 49, 39, 328 },{ 33, 43, 96 },{ 43, 39, 8 }, },
		{ { 61, 38, 1327 },{ 57, 47, 328 },{ 41, 51, 96 },{ 51, 47, 8 }, },
		{ { 69, 46, 1327 },{ 65, 55, 328 },{ 49, 59, 96 },{ 59, 55, 8 }, },
		{ { 78, 55, 1327 },{ 74, 64, 328 },{ 58, 68, 96 },{ 68, 64, 8 }, },
		{ { 86, 63, 1327 },{ 82, 72, 328 },{ 66, 76, 96 },{ 76, 72, 8 }, },
		{ { 94, 71, 1327 },{ 90, 80, 328 },{ 74, 84, 96 },{ 84, 80, 8 }, },
		{ { 102, 79, 1327 },{ 98, 88, 328 },{ 82, 92, 96 },{ 92, 88, 8 }, },
		{ { 111, 88, 1327 },{ 107, 97, 328 },{ 91, 101, 96 },{ 101, 97, 8 }, },
		{ { 119, 96, 1327 },{ 115, 105, 328 },{ 99, 109, 96 },{ 109, 105, 8 }, },
		{ { 127, 104, 1327 },{ 123, 113, 328 },{ 107, 117, 96 },{ 117, 113, 8 }, },
		{ { 135, 112, 1327 },{ 131, 121, 328 },{ 115, 125, 96 },{ 125, 121, 8 }, },
		{ { 144, 121, 1327 },{ 140, 130, 328 },{ 124, 134, 96 },{ 134, 130, 8 }, },
		{ { 152, 129, 1327 },{ 148, 138, 328 },{ 132, 142, 96 },{ 142, 138, 8 }, },
		{ { 160, 137, 1327 },{ 156, 146, 328 },{ 140, 150, 96 },{ 150, 146, 8 }, },
		{ { 168, 145, 1327 },{ 164, 154, 328 },{ 148, 158, 96 },{ 158, 154, 8 }, },
		{ { 177, 154, 1327 },{ 173, 163, 328 },{ 157, 167, 96 },{ 167, 163, 8 }, },
		{ { 185, 162, 1327 },{ 181, 171, 328 },{ 165, 175, 96 },{ 175, 171, 8 }, },
		{ { 193, 170, 1327 },{ 189, 179, 328 },{ 173, 183, 96 },{ 183, 179, 8 }, },
		{ { 201, 178, 1327 },{ 197, 187, 328 },{ 181, 191, 96 },{ 191, 187, 8 }, },
		{ { 210, 187, 1327 },{ 206, 196, 328 },{ 190, 200, 96 },{ 200, 196, 8 }, },
		{ { 218, 195, 1327 },{ 214, 204, 328 },{ 198, 208, 96 },{ 208, 204, 8 }, },
		{ { 226, 203, 1327 },{ 222, 212, 328 },{ 206, 216, 96 },{ 216, 212, 8 }, },
		{ { 234, 211, 1327 },{ 230, 220, 328 },{ 214, 224, 96 },{ 224, 220, 8 }, },
		{ { 243, 220, 1327 },{ 239, 229, 328 },{ 223, 233, 96 },{ 233, 229, 8 }, },
		{ { 251, 228, 1327 },{ 247, 237, 328 },{ 231, 241, 96 },{ 241, 237, 8 }, },
		{ { 239, 249, 3680 },{ 245, 249, 3648 },{ 239, 249, 96 },{ 249, 245, 8 }, },
		{ { 247, 253, 4040 },{ 255, 253, 8 },{ 247, 253, 456 },{ 255, 253, 8 }, },
		{ { 5, 17, 566 },{ 5, 17, 560 },{ 5, 0, 9 },{ 5, 0, 8 }, },
		{ { 25, 0, 313 },{ 25, 3, 328 },{ 13, 0, 49 },{ 13, 3, 8 }, },
		{ { 39, 0, 1329 },{ 33, 11, 328 },{ 11, 21, 70 },{ 21, 11, 8 }, },
		{ { 47, 7, 1329 },{ 41, 19, 328 },{ 29, 7, 33 },{ 29, 19, 8 }, },
		{ { 50, 11, 239 },{ 50, 28, 328 },{ 38, 16, 33 },{ 38, 28, 8 }, },
		{ { 92, 13, 2423 },{ 58, 36, 328 },{ 46, 24, 33 },{ 46, 36, 8 }, },
		{ { 100, 21, 2423 },{ 66, 44, 328 },{ 54, 32, 33 },{ 54, 44, 8 }, },
		{ { 86, 7, 1253 },{ 74, 52, 328 },{ 62, 40, 33 },{ 62, 52, 8 }, },
		{ { 95, 16, 1253 },{ 83, 61, 328 },{ 71, 49, 33 },{ 71, 61, 8 }, },
		{ { 103, 24, 1253 },{ 91, 69, 328 },{ 79, 57, 33 },{ 79, 69, 8 }, },
		{ { 111, 32, 1253 },{ 99, 77, 328 },{ 87, 65, 33 },{ 87, 77, 8 }, },
		{ { 119, 40, 1253 },{ 107, 85, 328 },{ 95, 73, 33 },{ 95, 85, 8 }, },
		{ { 128, 49, 1253 },{ 116, 94, 328 },{ 104, 82, 33 },{ 104, 94, 8 }, },
		{ { 136, 57, 1253 },{ 124, 102, 328 },{ 112, 90, 33 },{ 112, 102, 8 }, },
		{ { 144, 65, 1253 },{ 132, 110, 328 },{ 120, 98, 33 },{ 120, 110, 8 }, },
		{ { 152, 73, 1253 },{ 140, 118, 328 },{ 128, 106, 33 },{ 128, 118, 8 }, },
		{ { 161, 82, 1253 },{ 149, 127, 328 },{ 137, 115, 33 },{ 137, 127, 8 }, },
		{ { 169, 90, 1253 },{ 157, 135, 328 },{ 145, 123, 33 },{ 145, 135, 8 }, },
		{ { 177, 98, 1253 },{ 165, 143, 328 },{ 153, 131, 33 },{ 153, 143, 8 }, },
		{ { 185, 106, 1253 },{ 173, 151, 328 },{ 161, 139, 33 },{ 161, 151, 8 }, },
		{ { 194, 115, 1253 },{ 182, 160, 328 },{ 170, 148, 33 },{ 170, 160, 8 }, },
		{ { 202, 123, 1253 },{ 190, 168, 328 },{ 178, 156, 33 },{ 178, 168, 8 }, },
		{ { 210, 131, 1253 },{ 198, 176, 328 },{ 186, 164, 33 },{ 186, 176, 8 }, },
		{ { 218, 139, 1253 },{ 206, 184, 328 },{ 194, 172, 33 },{ 194, 184, 8 }, },
		{ { 227, 148, 1253 },{ 215, 193, 328 },{ 203, 181, 33 },{ 203, 193, 8 }, },
		{ { 235, 156, 1253 },{ 223, 201, 328 },{ 211, 189, 33 },{ 211, 201, 8 }, },
		{ { 243, 164, 1253 },{ 231, 209, 328 },{ 219, 197, 33 },{ 219, 209, 8 }, },
		{ { 183, 239, 867 },{ 239, 217, 328 },{ 227, 205, 33 },{ 227, 217, 8 }, },
		{ { 254, 214, 1329 },{ 248, 226, 328 },{ 236, 214, 33 },{ 236, 226, 8 }, },
		{ { 222, 244, 3680 },{ 234, 244, 3648 },{ 244, 222, 33 },{ 244, 234, 8 }, },
		{ { 230, 252, 3680 },{ 242, 252, 3648 },{ 252, 230, 33 },{ 252, 242, 8 }, },
		{ { 238, 250, 4040 },{ 255, 250, 8 },{ 238, 250, 456 },{ 255, 250, 8 }, },
		{ { 9, 29, 566 },{ 9, 29, 560 },{ 9, 0, 9 },{ 9, 0, 8 }, },
		{ { 17, 37, 566 },{ 17, 37, 560 },{ 17, 0, 9 },{ 17, 0, 8 }, },
		{ { 45, 0, 313 },{ 45, 0, 312 },{ 25, 0, 49 },{ 25, 7, 8 }, },
		{ { 14, 63, 2758 },{ 5, 53, 784 },{ 15, 33, 70 },{ 33, 15, 8 }, },
		{ { 71, 6, 1329 },{ 72, 4, 1328 },{ 42, 4, 33 },{ 42, 24, 8 }, },
		{ { 70, 3, 239 },{ 70, 2, 232 },{ 50, 12, 33 },{ 50, 32, 8 }, },
		{ { 0, 98, 2842 },{ 78, 10, 232 },{ 58, 20, 33 },{ 58, 40, 8 }, },
		{ { 97, 27, 1329 },{ 86, 18, 232 },{ 66, 28, 33 },{ 66, 48, 8 }, },
		{ { 0, 94, 867 },{ 95, 27, 232 },{ 75, 37, 33 },{ 75, 57, 8 }, },
		{ { 8, 102, 867 },{ 103, 35, 232 },{ 83, 45, 33 },{ 83, 65, 8 }, },
		{ { 12, 112, 867 },{ 111, 43, 232 },{ 91, 53, 33 },{ 91, 73, 8 }, },
		{ { 139, 2, 1253 },{ 119, 51, 232 },{ 99, 61, 33 },{ 99, 81, 8 }, },
		{ { 148, 13, 1253 },{ 128, 60, 232 },{ 108, 70, 33 },{ 108, 90, 8 }, },
		{ { 156, 21, 1253 },{ 136, 68, 232 },{ 116, 78, 33 },{ 116, 98, 8 }, },
		{ { 164, 29, 1253 },{ 144, 76, 232 },{ 124, 86, 33 },{ 124, 106, 8 }, },
		{ { 172, 37, 1253 },{ 152, 84, 232 },{ 132, 94, 33 },{ 132, 114, 8 }, },
		{ { 181, 46, 1253 },{ 161, 93, 232 },{ 141, 103, 33 },{ 141, 123, 8 }, },
		{ { 189, 54, 1253 },{ 169, 101, 232 },{ 149, 111, 33 },{ 149, 131, 8 }, },
		{ { 197, 62, 1253 },{ 177, 109, 232 },{ 157, 119, 33 },{ 157, 139, 8 }, },
		{ { 205, 70, 1253 },{ 185, 117, 232 },{ 165, 127, 33 },{ 165, 147, 8 }, },
		{ { 214, 79, 1253 },{ 194, 126, 232 },{ 174, 136, 33 },{ 174, 156, 8 }, },
		{ { 222, 87, 1253 },{ 202, 134, 232 },{ 182, 144, 33 },{ 182, 164, 8 }, },
		{ { 230, 95, 1253 },{ 210, 142, 232 },{ 190, 152, 33 },{ 190, 172, 8 }, },
		{ { 238, 103, 1253 },{ 218, 150, 232 },{ 198, 160, 33 },{ 198, 180, 8 }, },
		{ { 247, 112, 1253 },{ 227, 159, 232 },{ 207, 169, 33 },{ 207, 189, 8 }, },
		{ { 255, 120, 1253 },{ 235, 167, 232 },{ 215, 177, 33 },{ 215, 197, 8 }, },
		{ { 146, 243, 867 },{ 243, 175, 232 },{ 223, 185, 33 },{ 223, 205, 8 }, },
		{ { 184, 231, 3682 },{ 203, 251, 784 },{ 231, 193, 33 },{ 231, 213, 8 }, },
		{ { 193, 240, 3682 },{ 222, 240, 3648 },{ 240, 202, 33 },{ 240, 222, 8 }, },
		{ { 255, 210, 169 },{ 230, 248, 3648 },{ 248, 210, 33 },{ 248, 230, 8 }, },
		{ { 218, 238, 4040 },{ 255, 238, 8 },{ 218, 238, 456 },{ 255, 238, 8 }, },
		{ { 226, 246, 4040 },{ 255, 246, 8 },{ 226, 246, 456 },{ 255, 246, 8 }, },
		{ { 13, 42, 566 },{ 13, 42, 560 },{ 13, 0, 9 },{ 13, 0, 8 }, },
		{ { 50, 0, 329 },{ 50, 0, 328 },{ 21, 0, 9 },{ 21, 0, 8 }, },
		{ { 29, 58, 566 },{ 67, 2, 1352 },{ 3, 29, 70 },{ 29, 3, 8 }, },
		{ { 10, 79, 2758 },{ 76, 11, 1352 },{ 11, 37, 70 },{ 37, 11, 8 }, },
		{ { 7, 75, 790 },{ 7, 75, 784 },{ 20, 46, 70 },{ 46, 20, 8 }, },
		{ { 15, 83, 790 },{ 97, 1, 1328 },{ 28, 54, 70 },{ 54, 28, 8 }, },
		{ { 101, 7, 1329 },{ 105, 9, 1328 },{ 62, 0, 39 },{ 62, 36, 8 }, },
		{ { 99, 1, 239 },{ 99, 3, 232 },{ 1, 71, 98 },{ 70, 44, 8 }, },
		{ { 107, 11, 239 },{ 108, 12, 232 },{ 10, 80, 98 },{ 79, 53, 8 }, },
		{ { 115, 19, 239 },{ 116, 20, 232 },{ 18, 88, 98 },{ 87, 61, 8 }, },
		{ { 123, 27, 239 },{ 124, 28, 232 },{ 26, 96, 98 },{ 95, 69, 8 }, },
		{ { 131, 35, 239 },{ 132, 36, 232 },{ 34, 104, 98 },{ 103, 77, 8 }, },
		{ { 140, 44, 239 },{ 141, 45, 232 },{ 43, 113, 98 },{ 112, 86, 8 }, },
		{ { 148, 52, 239 },{ 149, 53, 232 },{ 51, 121, 98 },{ 120, 94, 8 }, },
		{ { 156, 60, 239 },{ 157, 61, 232 },{ 59, 129, 98 },{ 128, 102, 8 }, },
		{ { 164, 68, 239 },{ 165, 69, 232 },{ 67, 137, 98 },{ 136, 110, 8 }, },
		{ { 173, 77, 239 },{ 174, 78, 232 },{ 76, 146, 98 },{ 145, 119, 8 }, },
		{ { 181, 85, 239 },{ 182, 86, 232 },{ 84, 154, 98 },{ 153, 127, 8 }, },
		{ { 189, 93, 239 },{ 190, 94, 232 },{ 92, 162, 98 },{ 161, 135, 8 }, },
		{ { 197, 101, 239 },{ 198, 102, 232 },{ 100, 170, 98 },{ 169, 143, 8 }, },
		{ { 206, 110, 239 },{ 207, 111, 232 },{ 109, 179, 98 },{ 178, 152, 8 }, },
		{ { 214, 118, 239 },{ 215, 119, 232 },{ 117, 187, 98 },{ 186, 160, 8 }, },
		{ { 222, 126, 239 },{ 223, 127, 232 },{ 125, 195, 98 },{ 194, 168, 8 }, },
		{ { 230, 134, 239 },{ 231, 135, 232 },{ 133, 203, 98 },{ 202, 176, 8 }, },
		{ { 239, 143, 239 },{ 240, 144, 232 },{ 142, 212, 98 },{ 211, 185, 8 }, },
		{ { 247, 151, 239 },{ 180, 248, 784 },{ 150, 220, 98 },{ 219, 193, 8 }, },
		{ { 159, 228, 3682 },{ 201, 227, 3648 },{ 158, 228, 98 },{ 227, 201, 8 }, },
		{ { 181, 249, 3928 },{ 209, 235, 3648 },{ 166, 236, 98 },{ 235, 209, 8 }, },
		{ { 255, 189, 169 },{ 218, 244, 3648 },{ 175, 245, 98 },{ 244, 218, 8 }, },
		{ { 197, 226, 4040 },{ 226, 252, 3648 },{ 183, 253, 98 },{ 252, 226, 8 }, },
		{ { 205, 234, 4040 },{ 255, 234, 8 },{ 205, 234, 456 },{ 255, 234, 8 }, },
		{ { 213, 242, 4040 },{ 255, 242, 8 },{ 213, 242, 456 },{ 255, 242, 8 }, },
		{ { 18, 60, 566 },{ 18, 60, 560 },{ 18, 0, 9 },{ 18, 0, 8 }, },
		{ { 26, 68, 566 },{ 26, 68, 560 },{ 26, 0, 9 },{ 26, 0, 8 }, },
		{ { 34, 76, 566 },{ 34, 76, 560 },{ 34, 0, 9 },{ 34, 0, 8 }, },
		{ { 5, 104, 2758 },{ 98, 5, 1352 },{ 42, 0, 57 },{ 42, 6, 8 }, },
		{ { 92, 0, 313 },{ 93, 1, 312 },{ 15, 51, 70 },{ 51, 15, 8 }, },
		{ { 3, 101, 790 },{ 3, 101, 784 },{ 0, 59, 88 },{ 59, 23, 8 }, },
		{ { 14, 107, 790 },{ 11, 109, 784 },{ 31, 67, 70 },{ 67, 31, 8 }, },
		{ { 19, 117, 790 },{ 19, 117, 784 },{ 39, 75, 70 },{ 75, 39, 8 }, },
		{ { 28, 126, 790 },{ 28, 126, 784 },{ 83, 5, 33 },{ 84, 48, 8 }, },
		{ { 132, 0, 239 },{ 36, 134, 784 },{ 91, 13, 33 },{ 92, 56, 8 }, },
		{ { 142, 4, 239 },{ 44, 142, 784 },{ 99, 21, 33 },{ 100, 64, 8 }, },
		{ { 150, 12, 239 },{ 52, 150, 784 },{ 107, 29, 33 },{ 108, 72, 8 }, },
		{ { 159, 21, 239 },{ 61, 159, 784 },{ 116, 38, 33 },{ 117, 81, 8 }, },
		{ { 167, 29, 239 },{ 69, 167, 784 },{ 124, 46, 33 },{ 125, 89, 8 }, },
		{ { 175, 37, 239 },{ 77, 175, 784 },{ 132, 54, 33 },{ 133, 97, 8 }, },
		{ { 183, 45, 239 },{ 85, 183, 784 },{ 140, 62, 33 },{ 141, 105, 8 }, },
		{ { 192, 54, 239 },{ 94, 192, 784 },{ 149, 71, 33 },{ 150, 114, 8 }, },
		{ { 200, 62, 239 },{ 102, 200, 784 },{ 157, 79, 33 },{ 158, 122, 8 }, },
		{ { 208, 70, 239 },{ 110, 208, 784 },{ 165, 87, 33 },{ 166, 130, 8 }, },
		{ { 216, 78, 239 },{ 118, 216, 784 },{ 173, 95, 33 },{ 174, 138, 8 }, },
		{ { 225, 87, 239 },{ 127, 225, 784 },{ 182, 104, 33 },{ 183, 147, 8 }, },
		{ { 233, 95, 239 },{ 135, 233, 784 },{ 190, 112, 33 },{ 191, 155, 8 }, },
		{ { 241, 103, 239 },{ 143, 241, 784 },{ 198, 120, 33 },{ 199, 163, 8 }, },
		{ { 111, 208, 3682 },{ 151, 249, 784 },{ 206, 128, 33 },{ 207, 171, 8 }, },
		{ { 120, 217, 3682 },{ 180, 216, 3648 },{ 215, 137, 33 },{ 216, 180, 8 }, },
		{ { 128, 225, 3682 },{ 188, 224, 3648 },{ 223, 145, 33 },{ 224, 188, 8 }, },
		{ { 155, 253, 3928 },{ 196, 232, 3648 },{ 231, 153, 33 },{ 232, 196, 8 }, },
		{ { 144, 241, 3682 },{ 204, 240, 3648 },{ 239, 161, 33 },{ 240, 204, 8 }, },
		{ { 153, 250, 3682 },{ 213, 249, 3648 },{ 248, 170, 33 },{ 249, 213, 8 }, },
		{ { 179, 221, 4040 },{ 255, 221, 8 },{ 179, 221, 456 },{ 255, 221, 8 }, },
		{ { 187, 229, 4040 },{ 255, 229, 8 },{ 187, 229, 456 },{ 255, 229, 8 }, },
		{ { 195, 237, 4040 },{ 255, 237, 8 },{ 195, 237, 456 },{ 255, 237, 8 }, },
		{ { 24, 80, 566 },{ 24, 80, 560 },{ 24, 0, 9 },{ 24, 0, 8 }, },
		{ { 32, 88, 566 },{ 32, 88, 560 },{ 32, 0, 9 },{ 32, 0, 8 }, },
		{ { 40, 96, 566 },{ 40, 96, 560 },{ 40, 0, 9 },{ 40, 0, 8 }, },
		{ { 48, 104, 566 },{ 48, 104, 560 },{ 48, 0, 9 },{ 48, 0, 8 }, },
		{ { 9, 138, 2758 },{ 130, 7, 1352 },{ 9, 57, 70 },{ 57, 9, 8 }, },
		{ { 119, 0, 313 },{ 120, 0, 312 },{ 17, 65, 70 },{ 65, 17, 8 }, },
		{ { 0, 128, 784 },{ 128, 6, 312 },{ 25, 73, 70 },{ 73, 25, 8 }, },
		{ { 6, 137, 790 },{ 5, 136, 784 },{ 33, 81, 70 },{ 81, 33, 8 }, },
		{ { 42, 171, 2758 },{ 14, 145, 784 },{ 42, 90, 70 },{ 90, 42, 8 }, },
		{ { 50, 179, 2758 },{ 22, 153, 784 },{ 50, 98, 70 },{ 98, 50, 8 }, },
		{ { 58, 187, 2758 },{ 30, 161, 784 },{ 58, 106, 70 },{ 106, 58, 8 }, },
		{ { 191, 18, 1329 },{ 38, 169, 784 },{ 112, 9, 33 },{ 114, 66, 8 }, },
		{ { 176, 0, 239 },{ 47, 178, 784 },{ 121, 18, 33 },{ 123, 75, 8 }, },
		{ { 187, 1, 239 },{ 55, 186, 784 },{ 129, 26, 33 },{ 131, 83, 8 }, },
		{ { 195, 10, 239 },{ 63, 194, 784 },{ 137, 34, 33 },{ 139, 91, 8 }, },
		{ { 203, 18, 239 },{ 71, 202, 784 },{ 145, 42, 33 },{ 147, 99, 8 }, },
		{ { 212, 27, 239 },{ 80, 211, 784 },{ 154, 51, 33 },{ 156, 108, 8 }, },
		{ { 220, 35, 239 },{ 88, 219, 784 },{ 162, 59, 33 },{ 164, 116, 8 }, },
		{ { 228, 43, 239 },{ 96, 227, 784 },{ 170, 67, 33 },{ 172, 124, 8 }, },
		{ { 236, 51, 239 },{ 104, 235, 784 },{ 178, 75, 33 },{ 180, 132, 8 }, },
		{ { 245, 60, 239 },{ 113, 244, 784 },{ 187, 84, 33 },{ 189, 141, 8 }, },
		{ { 91, 194, 3680 },{ 149, 197, 3648 },{ 195, 92, 33 },{ 197, 149, 8 }, },
		{ { 99, 202, 3680 },{ 157, 205, 3648 },{ 203, 100, 33 },{ 205, 157, 8 }, },
		{ { 107, 210, 3680 },{ 165, 213, 3648 },{ 211, 108, 33 },{ 213, 165, 8 }, },
		{ { 119, 249, 3928 },{ 174, 222, 3648 },{ 220, 117, 33 },{ 222, 174, 8 }, },
		{ { 127, 255, 856 },{ 182, 230, 3648 },{ 228, 125, 33 },{ 230, 182, 8 }, },
		{ { 255, 135, 169 },{ 190, 238, 3648 },{ 236, 133, 33 },{ 238, 190, 8 }, },
		{ { 140, 243, 3680 },{ 198, 246, 3648 },{ 244, 141, 33 },{ 246, 198, 8 }, },
		{ { 151, 207, 4040 },{ 255, 207, 8 },{ 151, 207, 456 },{ 255, 207, 8 }, },
		{ { 159, 215, 4040 },{ 255, 215, 8 },{ 159, 215, 456 },{ 255, 215, 8 }, },
		{ { 167, 223, 4040 },{ 255, 223, 8 },{ 167, 223, 456 },{ 255, 223, 8 }, },
		{ { 175, 231, 4040 },{ 255, 231, 8 },{ 175, 231, 456 },{ 255, 231, 8 }, },
		{ { 33, 106, 566 },{ 33, 106, 560 },{ 33, 0, 9 },{ 33, 0, 8 }, },
		{ { 41, 114, 566 },{ 41, 114, 560 },{ 41, 0, 9 },{ 41, 0, 8 }, },
		{ { 49, 122, 566 },{ 49, 122, 560 },{ 49, 0, 9 },{ 49, 0, 8 }, },
		{ { 57, 130, 566 },{ 57, 130, 560 },{ 57, 0, 9 },{ 57, 0, 8 }, },
		{ { 66, 139, 566 },{ 66, 139, 560 },{ 66, 0, 9 },{ 66, 0, 8 }, },
		{ { 74, 147, 566 },{ 170, 7, 1352 },{ 8, 74, 70 },{ 74, 8, 8 }, },
		{ { 152, 0, 313 },{ 178, 15, 1352 },{ 0, 82, 80 },{ 82, 16, 8 }, },
		{ { 162, 0, 313 },{ 186, 23, 1352 },{ 24, 90, 70 },{ 90, 24, 8 }, },
		{ { 0, 171, 784 },{ 195, 32, 1352 },{ 33, 99, 70 },{ 99, 33, 8 }, },
		{ { 6, 179, 790 },{ 203, 40, 1352 },{ 41, 107, 70 },{ 107, 41, 8 }, },
		{ { 15, 187, 790 },{ 211, 48, 1352 },{ 115, 0, 41 },{ 115, 49, 8 }, },
		{ { 61, 199, 710 },{ 219, 56, 1352 },{ 57, 123, 70 },{ 123, 57, 8 }, },
		{ { 70, 208, 710 },{ 228, 65, 1352 },{ 66, 132, 70 },{ 132, 66, 8 }, },
		{ { 78, 216, 710 },{ 236, 73, 1352 },{ 74, 140, 70 },{ 140, 74, 8 }, },
		{ { 86, 224, 710 },{ 244, 81, 1352 },{ 145, 7, 33 },{ 148, 82, 8 }, },
		{ { 222, 8, 233 },{ 252, 89, 1352 },{ 153, 15, 33 },{ 156, 90, 8 }, },
		{ { 235, 0, 239 },{ 241, 101, 328 },{ 166, 6, 39 },{ 165, 99, 8 }, },
		{ { 32, 170, 3680 },{ 249, 109, 328 },{ 0, 175, 98 },{ 173, 107, 8 }, },
		{ { 40, 178, 3680 },{ 115, 181, 3648 },{ 8, 183, 98 },{ 181, 115, 8 }, },
		{ { 48, 186, 3680 },{ 123, 189, 3648 },{ 16, 191, 98 },{ 189, 123, 8 }, },
		{ { 57, 195, 3680 },{ 132, 198, 3648 },{ 25, 200, 98 },{ 198, 132, 8 }, },
		{ { 67, 243, 3928 },{ 140, 206, 3648 },{ 33, 208, 98 },{ 206, 140, 8 }, },
		{ { 76, 251, 3928 },{ 148, 214, 3648 },{ 41, 216, 98 },{ 214, 148, 8 }, },
		{ { 86, 255, 856 },{ 156, 222, 3648 },{ 49, 224, 98 },{ 222, 156, 8 }, },
		{ { 255, 93, 169 },{ 165, 231, 3648 },{ 58, 233, 98 },{ 231, 165, 8 }, },
		{ { 98, 236, 3680 },{ 173, 239, 3648 },{ 66, 241, 98 },{ 239, 173, 8 }, },
		{ { 108, 181, 4040 },{ 181, 247, 3648 },{ 74, 249, 98 },{ 247, 181, 8 }, },
		{ { 116, 189, 4040 },{ 255, 189, 8 },{ 116, 189, 456 },{ 255, 189, 8 }, },
		{ { 125, 198, 4040 },{ 255, 198, 8 },{ 125, 198, 456 },{ 255, 198, 8 }, },
		{ { 133, 206, 4040 },{ 255, 206, 8 },{ 133, 206, 456 },{ 255, 206, 8 }, },
		{ { 141, 214, 4040 },{ 255, 214, 8 },{ 141, 214, 456 },{ 255, 214, 8 }, },
		{ { 149, 222, 4040 },{ 255, 222, 8 },{ 149, 222, 456 },{ 255, 222, 8 }, },
		{ { 47, 183, 566 },{ 47, 183, 560 },{ 47, 0, 9 },{ 47, 0, 8 }, },
		{ { 55, 191, 566 },{ 55, 191, 560 },{ 55, 0, 9 },{ 55, 0, 8 }, },
		{ { 63, 199, 566 },{ 63, 199, 560 },{ 63, 0, 9 },{ 63, 0, 8 }, },
		{ { 71, 207, 566 },{ 71, 207, 560 },{ 71, 0, 9 },{ 71, 0, 8 }, },
		{ { 80, 216, 566 },{ 80, 216, 560 },{ 80, 0, 9 },{ 80, 0, 8 }, },
		{ { 88, 224, 566 },{ 88, 224, 560 },{ 88, 0, 9 },{ 88, 0, 8 }, },
		{ { 3, 233, 710 },{ 3, 233, 704 },{ 2, 96, 70 },{ 96, 2, 8 }, },
		{ { 11, 241, 710 },{ 11, 241, 704 },{ 10, 104, 70 },{ 104, 10, 8 }, },
		{ { 20, 250, 710 },{ 20, 250, 704 },{ 19, 113, 70 },{ 113, 19, 8 }, },
		{ { 27, 121, 3654 },{ 27, 121, 3648 },{ 27, 121, 70 },{ 121, 27, 8 }, },
		{ { 35, 129, 3654 },{ 35, 129, 3648 },{ 35, 129, 70 },{ 129, 35, 8 }, },
		{ { 43, 137, 3654 },{ 43, 137, 3648 },{ 43, 137, 70 },{ 137, 43, 8 }, },
		{ { 52, 146, 3654 },{ 52, 146, 3648 },{ 52, 146, 70 },{ 146, 52, 8 }, },
		{ { 60, 154, 3654 },{ 60, 154, 3648 },{ 60, 154, 70 },{ 154, 60, 8 }, },
		{ { 68, 162, 3654 },{ 68, 162, 3648 },{ 68, 162, 70 },{ 162, 68, 8 }, },
		{ { 76, 170, 3654 },{ 76, 170, 3648 },{ 76, 170, 70 },{ 170, 76, 8 }, },
		{ { 85, 179, 3654 },{ 85, 179, 3648 },{ 85, 179, 70 },{ 179, 85, 8 }, },
		{ { 93, 187, 3654 },{ 93, 187, 3648 },{ 93, 187, 70 },{ 187, 93, 8 }, },
		{ { 101, 195, 3654 },{ 101, 195, 3648 },{ 101, 195, 70 },{ 195, 101, 8 }, },
		{ { 109, 203, 3654 },{ 109, 203, 3648 },{ 109, 203, 70 },{ 203, 109, 8 }, },
		{ { 118, 212, 3654 },{ 118, 212, 3648 },{ 118, 212, 70 },{ 212, 118, 8 }, },
		{ { 126, 220, 3654 },{ 126, 220, 3648 },{ 126, 220, 70 },{ 220, 126, 8 }, },
		{ { 134, 228, 3654 },{ 134, 228, 3648 },{ 134, 228, 70 },{ 228, 134, 8 }, },
		{ { 5, 236, 3680 },{ 142, 236, 3648 },{ 5, 236, 96 },{ 236, 142, 8 }, },
		{ { 14, 245, 3680 },{ 151, 245, 3648 },{ 14, 245, 96 },{ 245, 151, 8 }, },
		{ { 23, 159, 4040 },{ 159, 253, 3648 },{ 23, 159, 456 },{ 253, 159, 8 }, },
		{ { 31, 167, 4040 },{ 255, 167, 8 },{ 31, 167, 456 },{ 255, 167, 8 }, },
		{ { 39, 175, 4040 },{ 255, 175, 8 },{ 39, 175, 456 },{ 255, 175, 8 }, },
		{ { 48, 184, 4040 },{ 255, 184, 8 },{ 48, 184, 456 },{ 255, 184, 8 }, },
		{ { 56, 192, 4040 },{ 255, 192, 8 },{ 56, 192, 456 },{ 255, 192, 8 }, },
		{ { 64, 200, 4040 },{ 255, 200, 8 },{ 64, 200, 456 },{ 255, 200, 8 }, },
		{ { 72, 208, 4040 },{ 255, 208, 8 },{ 72, 208, 456 },{ 255, 208, 8 }, },

	};

	struct dxt5a_block
	{
		uint8_t m_endpoints[2];

		enum { cTotalSelectorBytes = 6 };
		uint8_t m_selectors[cTotalSelectorBytes];

		inline void clear()
		{
			basisu::clear_obj(*this);
		}

		inline uint32_t get_low_alpha() const
		{
			return m_endpoints[0];
		}

		inline uint32_t get_high_alpha() const
		{
			return m_endpoints[1];
		}

		inline void set_low_alpha(uint32_t i)
		{
			assert(i <= UINT8_MAX);
			m_endpoints[0] = static_cast<uint8_t>(i);
		}

		inline void set_high_alpha(uint32_t i)
		{
			assert(i <= UINT8_MAX);
			m_endpoints[1] = static_cast<uint8_t>(i);
		}

		inline bool is_alpha6_block() const { return get_low_alpha() <= get_high_alpha(); }

		uint32_t get_endpoints_as_word() const { return m_endpoints[0] | (m_endpoints[1] << 8); }
		uint32_t get_selectors_as_word(uint32_t index) { assert(index < 3); return m_selectors[index * 2] | (m_selectors[index * 2 + 1] << 8); }

		inline uint32_t get_selector(uint32_t x, uint32_t y) const
		{
			assert((x < 4U) && (y < 4U));

			uint32_t selector_index = (y * 4) + x;
			uint32_t bit_index = selector_index * cDXT5SelectorBits;

			uint32_t byte_index = bit_index >> 3;
			uint32_t bit_ofs = bit_index & 7;

			uint32_t v = m_selectors[byte_index];
			if (byte_index < (cTotalSelectorBytes - 1))
				v |= (m_selectors[byte_index + 1] << 8);

			return (v >> bit_ofs) & 7;
		}

		inline void set_selector(uint32_t x, uint32_t y, uint32_t val)
		{
			assert((x < 4U) && (y < 4U) && (val < 8U));

			uint32_t selector_index = (y * 4) + x;
			uint32_t bit_index = selector_index * cDXT5SelectorBits;

			uint32_t byte_index = bit_index >> 3;
			uint32_t bit_ofs = bit_index & 7;

			uint32_t v = m_selectors[byte_index];
			if (byte_index < (cTotalSelectorBytes - 1))
				v |= (m_selectors[byte_index + 1] << 8);

			v &= (~(7 << bit_ofs));
			v |= (val << bit_ofs);

			m_selectors[byte_index] = static_cast<uint8_t>(v);
			if (byte_index < (cTotalSelectorBytes - 1))
				m_selectors[byte_index + 1] = static_cast<uint8_t>(v >> 8);
		}

		enum { cMaxSelectorValues = 8 };

		static uint32_t get_block_values6(color32* pDst, uint32_t l, uint32_t h)
		{
			pDst[0].a = static_cast<uint8_t>(l);
			pDst[1].a = static_cast<uint8_t>(h);
			pDst[2].a = static_cast<uint8_t>((l * 4 + h) / 5);
			pDst[3].a = static_cast<uint8_t>((l * 3 + h * 2) / 5);
			pDst[4].a = static_cast<uint8_t>((l * 2 + h * 3) / 5);
			pDst[5].a = static_cast<uint8_t>((l + h * 4) / 5);
			pDst[6].a = 0;
			pDst[7].a = 255;
			return 6;
		}

		static uint32_t get_block_values8(color32 * pDst, uint32_t l, uint32_t h)
		{
			pDst[0].a = static_cast<uint8_t>(l);
			pDst[1].a = static_cast<uint8_t>(h);
			pDst[2].a = static_cast<uint8_t>((l * 6 + h) / 7);
			pDst[3].a = static_cast<uint8_t>((l * 5 + h * 2) / 7);
			pDst[4].a = static_cast<uint8_t>((l * 4 + h * 3) / 7);
			pDst[5].a = static_cast<uint8_t>((l * 3 + h * 4) / 7);
			pDst[6].a = static_cast<uint8_t>((l * 2 + h * 5) / 7);
			pDst[7].a = static_cast<uint8_t>((l + h * 6) / 7);
			return 8;
		}

		static uint32_t get_block_values(color32 * pDst, uint32_t l, uint32_t h)
		{
			if (l > h)
				return get_block_values8(pDst, l, h);
			else
				return get_block_values6(pDst, l, h);
		}
	};

	static void convert_etc1s_to_dxt5a(dxt5a_block *pDst_block, const decoder_etc_block *pSrc_block, const selector *pSelector)
	{
		const uint32_t low_selector = pSelector->m_lo_selector;
		const uint32_t high_selector = pSelector->m_hi_selector;

		const color32 base_color(decoder_etc_block::unpack_color5(pSrc_block->get_base5_color(), false));
		const uint32_t inten_table = pSrc_block->get_inten_table(0);

		if (low_selector == high_selector)
		{
			color32 block_colors[4];

			decoder_etc_block::get_block_colors5(block_colors, base_color, inten_table);

			const uint32_t r = block_colors[low_selector].r;

			pDst_block->set_low_alpha(r);
			pDst_block->set_high_alpha(r);
			pDst_block->m_selectors[0] = 0;
			pDst_block->m_selectors[1] = 0;
			pDst_block->m_selectors[2] = 0;
			pDst_block->m_selectors[3] = 0;
			pDst_block->m_selectors[4] = 0;
			pDst_block->m_selectors[5] = 0;
			return;
		}
		else if (pSelector->m_num_unique_selectors == 2)
		{
			color32 block_colors[4];

			decoder_etc_block::get_block_colors5(block_colors, base_color, inten_table);

			const uint32_t r0 = block_colors[low_selector].r;
			const uint32_t r1 = block_colors[high_selector].r;

			pDst_block->set_low_alpha(r0);
			pDst_block->set_high_alpha(r1);

			// TODO: Optimize this
			for (uint32_t y = 0; y < 4; y++)
			{
				for (uint32_t x = 0; x < 4; x++)
				{
					uint32_t s = pSrc_block->get_selector(x, y);
					pDst_block->set_selector(x, y, (s == high_selector) ? 1 : 0);
				}
			}

			return;
		}

		uint32_t selector_range_table = 0;
		for (selector_range_table = 0; selector_range_table < NUM_DXT5A_SELECTOR_RANGES; selector_range_table++)
			if ((low_selector == s_dxt5a_selector_ranges[selector_range_table].m_low) && (high_selector == s_dxt5a_selector_ranges[selector_range_table].m_high))
				break;
		if (selector_range_table >= NUM_DXT5A_SELECTOR_RANGES)
			selector_range_table = 0;

		const etc1_g_to_dxt5a_conversion *pTable_entry = &g_etc1_g_to_dxt5a[base_color.r + inten_table * 32][selector_range_table];

		pDst_block->set_low_alpha(pTable_entry->m_lo);
		pDst_block->set_high_alpha(pTable_entry->m_hi);

		// TODO: Optimize this (like ETC1->BC1)
		for (uint32_t y = 0; y < 4; y++)
		{
			for (uint32_t x = 0; x < 4; x++)
			{
				uint32_t s = pSrc_block->get_selector(x, y);

				uint32_t ds = (pTable_entry->m_trans >> (s * 3)) & 7;

				pDst_block->set_selector(x, y, ds);
			}
		}
	}

	// PVRTC

#if BASISD_SUPPORT_PVRTC1
	const  uint16_t g_pvrtc_swizzle_table[256] =
	{
		0x0000, 0x0001, 0x0004, 0x0005, 0x0010, 0x0011, 0x0014, 0x0015, 0x0040, 0x0041, 0x0044, 0x0045, 0x0050, 0x0051, 0x0054, 0x0055, 0x0100, 0x0101, 0x0104, 0x0105, 0x0110, 0x0111, 0x0114, 0x0115, 0x0140, 0x0141, 0x0144, 0x0145, 0x0150, 0x0151, 0x0154, 0x0155,
		0x0400, 0x0401, 0x0404, 0x0405, 0x0410, 0x0411, 0x0414, 0x0415, 0x0440, 0x0441, 0x0444, 0x0445, 0x0450, 0x0451, 0x0454, 0x0455, 0x0500, 0x0501, 0x0504, 0x0505, 0x0510, 0x0511, 0x0514, 0x0515, 0x0540, 0x0541, 0x0544, 0x0545, 0x0550, 0x0551, 0x0554, 0x0555,
		0x1000, 0x1001, 0x1004, 0x1005, 0x1010, 0x1011, 0x1014, 0x1015, 0x1040, 0x1041, 0x1044, 0x1045, 0x1050, 0x1051, 0x1054, 0x1055, 0x1100, 0x1101, 0x1104, 0x1105, 0x1110, 0x1111, 0x1114, 0x1115, 0x1140, 0x1141, 0x1144, 0x1145, 0x1150, 0x1151, 0x1154, 0x1155,
		0x1400, 0x1401, 0x1404, 0x1405, 0x1410, 0x1411, 0x1414, 0x1415, 0x1440, 0x1441, 0x1444, 0x1445, 0x1450, 0x1451, 0x1454, 0x1455, 0x1500, 0x1501, 0x1504, 0x1505, 0x1510, 0x1511, 0x1514, 0x1515, 0x1540, 0x1541, 0x1544, 0x1545, 0x1550, 0x1551, 0x1554, 0x1555,
		0x4000, 0x4001, 0x4004, 0x4005, 0x4010, 0x4011, 0x4014, 0x4015, 0x4040, 0x4041, 0x4044, 0x4045, 0x4050, 0x4051, 0x4054, 0x4055, 0x4100, 0x4101, 0x4104, 0x4105, 0x4110, 0x4111, 0x4114, 0x4115, 0x4140, 0x4141, 0x4144, 0x4145, 0x4150, 0x4151, 0x4154, 0x4155,
		0x4400, 0x4401, 0x4404, 0x4405, 0x4410, 0x4411, 0x4414, 0x4415, 0x4440, 0x4441, 0x4444, 0x4445, 0x4450, 0x4451, 0x4454, 0x4455, 0x4500, 0x4501, 0x4504, 0x4505, 0x4510, 0x4511, 0x4514, 0x4515, 0x4540, 0x4541, 0x4544, 0x4545, 0x4550, 0x4551, 0x4554, 0x4555,
		0x5000, 0x5001, 0x5004, 0x5005, 0x5010, 0x5011, 0x5014, 0x5015, 0x5040, 0x5041, 0x5044, 0x5045, 0x5050, 0x5051, 0x5054, 0x5055, 0x5100, 0x5101, 0x5104, 0x5105, 0x5110, 0x5111, 0x5114, 0x5115, 0x5140, 0x5141, 0x5144, 0x5145, 0x5150, 0x5151, 0x5154, 0x5155,
		0x5400, 0x5401, 0x5404, 0x5405, 0x5410, 0x5411, 0x5414, 0x5415, 0x5440, 0x5441, 0x5444, 0x5445, 0x5450, 0x5451, 0x5454, 0x5455, 0x5500, 0x5501, 0x5504, 0x5505, 0x5510, 0x5511, 0x5514, 0x5515, 0x5540, 0x5541, 0x5544, 0x5545, 0x5550, 0x5551, 0x5554, 0x5555
	};


	struct pvrtc4_block
	{
		uint32_t m_modulation;
		uint32_t m_endpoints;

		pvrtc4_block() : m_modulation(0), m_endpoints(0) { }

		inline bool operator== (const pvrtc4_block& rhs) const
		{
			return (m_modulation == rhs.m_modulation) && (m_endpoints == rhs.m_endpoints);
		}

		inline void clear()
		{
			m_modulation = 0;
			m_endpoints = 0;
		}

		inline bool get_block_uses_transparent_modulation() const
		{
			return (m_endpoints & 1) != 0;
		}

		inline void set_block_uses_transparent_modulation(bool m)
		{
			m_endpoints = (m_endpoints & ~1U) | static_cast<uint32_t>(m);
		}

		inline bool is_endpoint_opaque(uint32_t endpoint_index) const
		{
			static const uint32_t s_bitmasks[2] = { 0x8000U, 0x80000000U };
			return (m_endpoints & s_bitmasks[basisu::open_range_check(endpoint_index, 2U)]) != 0;
		}

		inline void set_endpoint_opaque(uint32_t endpoint_index, bool opaque)
		{
			assert(endpoint_index < 2);
			static const uint32_t s_bitmasks[2] = { 0x8000U, 0x80000000U };
			if (opaque)
				m_endpoints |= s_bitmasks[endpoint_index];
			else
				m_endpoints &= ~s_bitmasks[endpoint_index];
		}

		// Returns 5554 or 8888
		inline color32 get_endpoint(uint32_t endpoint_index, bool unpack = false) const
		{
			assert(endpoint_index < 2);
			static const uint32_t s_endpoint_mask[2] = { 0xFFFE, 0xFFFF };
			uint32_t packed = (m_endpoints >> (basisu::open_range_check(endpoint_index, 2U) ? 16 : 0)) & s_endpoint_mask[endpoint_index];

			uint32_t r, g, b, a;
			if (packed & 0x8000)
			{
				// opaque 554 or 555
				r = (packed >> 10) & 31;
				g = (packed >> 5) & 31;
				b = packed & 31;

				if (!endpoint_index)
					b |= (b >> 4);

				a = 0xF;
			}
			else
			{
				// translucent 4433 or 4443
				r = (packed >> 7) & 0x1E;
				g = (packed >> 3) & 0x1E;
				b = (packed & 0xF) << 1;

				r |= (r >> 4);
				g |= (g >> 4);

				if (!endpoint_index)
					b |= (b >> 3);
				else
					b |= (b >> 4);

				a = (packed >> 11) & 0xE;
			}

			assert((r < 32) && (g < 32) && (b < 32) && (a < 16));

			if (unpack)
			{
				r = (r << 3) | (r >> 2);
				g = (g << 3) | (g >> 2);
				b = (b << 3) | (b >> 2);
				a = (a << 4) | a;
				assert((r < 256) && (g < 256) && (b < 256) && (a < 256));
			}

			return color32(r, g, b, a);
		}

		inline color32 get_opaque_endpoint_rgb888(uint32_t endpoint_index) const
		{
			assert(endpoint_index < 2);
			static const uint32_t s_endpoint_mask[2] = { 0xFFFE, 0xFFFF };
			uint32_t packed = (m_endpoints >> (basisu::open_range_check(endpoint_index, 2U) ? 16 : 0)) & s_endpoint_mask[endpoint_index];

			uint32_t r, g, b;
			assert(packed & 0x8000);

			// opaque 554 or 555
			r = (packed >> 10) & 31;
			g = (packed >> 5) & 31;
			b = packed & 31;

			if (!endpoint_index)
				b |= (b >> 4);

			assert((r < 32) && (g < 32) && (b < 32));

			r = (r << 3) | (r >> 2);
			g = (g << 3) | (g >> 2);
			b = (b << 3) | (b >> 2);

			return color32(r, g, b, 255);
		}

		inline uint32_t get_opaque_endpoint_l0() const
		{
			uint32_t packed = m_endpoints & 0xFFFE;

			uint32_t r, g, b;
			assert(packed & 0x8000);

			// opaque 554 or 555
			r = (packed >> 10) & 31;
			g = (packed >> 5) & 31;
			b = packed & 31;
			b |= (b >> 4);

			return r + g + b;
		}

		inline uint32_t get_opaque_endpoint_l1() const
		{
			uint32_t packed = m_endpoints >> 16;

			uint32_t r, g, b;
			assert(packed & 0x8000);

			// opaque 554 or 555
			r = (packed >> 10) & 31;
			g = (packed >> 5) & 31;
			b = packed & 31;

			return r + g + b;
		}

		static inline uint32_t c3_to_4(uint32_t x) { return (basisu::open_range_check(x, 8U) << 1) | (x >> 2); }
		static inline uint32_t c3_to_5(uint32_t x) { return (basisu::open_range_check(x, 8U) << 2) | (x >> 1); }
		static inline uint32_t c4_to_5(uint32_t x) { return (basisu::open_range_check(x, 16U) << 1) | (x >> 3); }

		static uint32_t get_component_precision_in_bits(uint32_t c, uint32_t endpoint_index, bool opaque_endpoint)
		{
			static const uint32_t s_comp_prec[4][4] =
			{
				// R0 G0 B0 A0      R1 G1 B1 A1
				{ 4, 4, 3, 3 },{ 4, 4, 4, 3 }, // transparent endpoint

				{ 5, 5, 4, 0 },{ 5, 5, 5, 0 }  // opaque endpoint
			};
			return s_comp_prec[basisu::open_range_check(endpoint_index, 2U) + (opaque_endpoint * 2)][basisu::open_range_check(c, 4U)];
		}

		static color32 get_color_precision_in_bits(uint32_t endpoint_index, bool opaque_endpoint)
		{
			static const color32 s_color_prec[4] =
			{
				color32(4, 4, 3, 3), color32(4, 4, 4, 3), // transparent endpoint
				color32(5, 5, 4, 0), color32(5, 5, 5, 0)  // opaque endpoint
			};
			return s_color_prec[basisu::open_range_check(endpoint_index, 2U) + (opaque_endpoint * 2)];
		}

		// accepts 5554 or 8888
		inline void set_endpoint(uint32_t endpoint_index, const color32 & c, bool opaque_endpoint, bool pack = false, uint32_t pack_round = 128)
		{
			assert(endpoint_index < 2);
			const uint32_t m = m_endpoints & 1;
			uint32_t r = c[0], g = c[1], b = c[2], a = c[3];

			// TODO: Use lookup tables.
			if (pack)
			{
				// ceil for endpoint_index 1, otherwise floor
				const uint32_t k = pack_round;// endpoint_index ? 254 : 0;

				// TODO: Improve rounding for transparent endpoints.
				r = (r * 31 + k) / 255;
				g = (g * 31 + k) / 255;

				if (!endpoint_index)
					b = ((b * 15 + k) / 255) << 1;
				else
					b = (b * 31 + k) / 255;

				a = (a * 15 + k) / 255;
			}

			// rgba=5554 here
			assert((r < 32) && (g < 32) && (b < 32) && (a < 16));

			uint32_t packed;

			if (opaque_endpoint)
			{
				// 1RRRRRGGGGGBBBBM
				// 1RRRRRGGGGGBBBBB

				// opaque 554 or 555
				packed = 0x8000 | (r << 10) | (g << 5) | b;
				if (!endpoint_index)
					packed = (packed & ~1) | m;
			}
			else
			{
				// 0AAA RRRR GGGG BBBM
				// 0AAA RRRR GGGG BBBB

				// translucent 4433 or 4443
				packed = ((a << 11) & 0x7000) | ((r << 7) & 0xF00) | ((g << 3) & 0xF0) | (b >> 1);
				if (!endpoint_index)
					packed = (packed & ~1) | m;
			}

			assert(packed <= 0xFFFF);

			if (endpoint_index)
				m_endpoints = (m_endpoints & 0xFFFFU) | (packed << 16);
			else
				m_endpoints = (m_endpoints & 0xFFFF0000U) | packed;

#ifdef BASISD_BUILD_DEBUG
			assert((m != 0) == get_block_uses_transparent_modulation());

			color32 v(get_endpoint(endpoint_index, false));
			// v is 5554

			if (opaque_endpoint)
			{
				assert(v[0] == r);
				assert(v[1] == g);
				if (!endpoint_index)
					assert(v[2] == c4_to_5(v[2] >> 1));
				else
					assert(v[2] == b);
			}
			else
			{
				assert(v[0] == c4_to_5(r >> 1));
				assert(v[1] == c4_to_5(g >> 1));
				if (!endpoint_index)
					assert(v[2] == c3_to_5(b >> 2));
				else
					assert(v[2] == c4_to_5(b >> 1));
				assert(v[3] == (c3_to_4(a >> 1) & ~1));
			}
#endif
		}

		inline uint32_t get_modulation(uint32_t x, uint32_t y) const
		{
			assert((x < 4) && (y < 4));
			return (m_modulation >> ((y * 4 + x) * 2)) & 3;
		}

		// Scaled by 8
		inline const uint32_t* get_scaled_modulation_values(bool block_uses_transparent_modulation) const
		{
			static const uint32_t s_block_scales[2][4] = { { 0, 3, 5, 8 },{ 0, 4, 4, 8 } };
			return s_block_scales[block_uses_transparent_modulation];
		}

		// Scaled by 8
		inline uint32_t get_scaled_modulation(uint32_t x, uint32_t y) const
		{
			return get_scaled_modulation_values(get_block_uses_transparent_modulation())[get_modulation(x, y)];
		}

		inline void set_modulation(uint32_t x, uint32_t y, uint32_t s)
		{
			assert((x < 4) && (y < 4) && (s < 4));
			uint32_t n = (y * 4 + x) * 2;
			m_modulation = (m_modulation & (~(3 << n))) | (s << n);
			assert(get_modulation(x, y) == s);
		}

		// Assumes modulation was initialized to 0
		inline void set_modulation_fast(uint32_t x, uint32_t y, uint32_t s)
		{
			assert((x < 4) && (y < 4) && (s < 4));
			uint32_t n = (y * 4 + x) * 2;
			m_modulation |= (s << n);
			assert(get_modulation(x, y) == s);
		}
	};

	static const uint8_t g_pvrtc_bilinear_weights[16][4] =
	{
		{ 4, 4, 4, 4 }, { 2, 6, 2, 6 }, { 8, 0, 8, 0 }, { 6, 2, 6, 2 },
		{ 2, 2, 6, 6 }, { 1, 3, 3, 9 }, { 4, 0, 12, 0 }, { 3, 1, 9, 3 },
		{ 8, 8, 0, 0 }, { 4, 12, 0, 0 }, { 16, 0, 0, 0 }, { 12, 4, 0, 0 },
		{ 6, 6, 2, 2 }, { 3, 9, 1, 3 }, { 12, 0, 4, 0 }, { 9, 3, 3, 1 },
	};

	struct pvrtc1_temp_block
	{
		decoder_etc_block m_etc1_block;
		uint32_t m_pvrtc_endpoints;
	};

	static inline uint32_t get_opaque_endpoint_l0(uint32_t endpoints)
	{
		uint32_t packed = endpoints;

		uint32_t r, g, b;
		assert(packed & 0x8000);

		// opaque 554 or 555
		r = (packed >> 10) & 31;
		g = (packed >> 5) & 31;
		b = packed & 30;
		b |= (b >> 4);

		return r + g + b;
	}

	static inline uint32_t get_opaque_endpoint_l1(uint32_t endpoints)
	{
		uint32_t packed = endpoints >> 16;

		uint32_t r, g, b;
		assert(packed & 0x8000);

		// opaque 554 or 555
		r = (packed >> 10) & 31;
		g = (packed >> 5) & 31;
		b = packed & 31;

		return r + g + b;
	}

	// TODO: Support decoding a non-pow2 ETC1S texture into the next larger pow2 PVRTC texture.
	static void fixup_pvrtc1_4_modulation(const decoder_etc_block *pETC_Blocks, const uint32_t *pPVRTC_endpoints, void *pDst_blocks, uint32_t num_blocks_x, uint32_t num_blocks_y, bool pvrtc_wrap_addressing)
	{
		const uint32_t x_mask = num_blocks_x - 1;
		const uint32_t y_mask = num_blocks_y - 1;
		const uint32_t x_bits = basisu::total_bits(x_mask);
		const uint32_t y_bits = basisu::total_bits(y_mask);
		const uint32_t min_bits = basisu::minimum(x_bits, y_bits);
		const uint32_t max_bits = basisu::maximum(x_bits, y_bits);
		const uint32_t swizzle_mask = (1 << (min_bits * 2)) - 1;

		uint32_t block_index = 0;

		// really 3x3
		int e0[4][4], e1[4][4];

		for (int y = 0; y < static_cast<int>(num_blocks_y); y++)
		{
			const uint32_t* pE_rows[3];

			for (int ey = 0; ey < 3; ey++)
			{
				int by = y + ey - 1; if (!pvrtc_wrap_addressing) by = basisu::clamp<int>(by, 0, y_mask);

				const uint32_t* pE = &pPVRTC_endpoints[(by & y_mask) * num_blocks_x];

				pE_rows[ey] = pE;

				for (int ex = 0; ex < 3; ex++)
				{
					int bx = 0 + ex - 1; if (!pvrtc_wrap_addressing) bx = basisu::clamp<int>(bx, 0, x_mask);

					const uint32_t e = pE[bx & x_mask];

					e0[ex][ey] = (get_opaque_endpoint_l0(e) * 255) / 31;
					e1[ex][ey] = (get_opaque_endpoint_l1(e) * 255) / 31;
				}
			}

			const uint32_t y_swizzle = (g_pvrtc_swizzle_table[y >> 8] << 16) | g_pvrtc_swizzle_table[y & 0xFF];

			for (int x = 0; x < static_cast<int>(num_blocks_x); x++, block_index++)
			{
				const decoder_etc_block& src_block = pETC_Blocks[block_index];

				const uint32_t x_swizzle = (g_pvrtc_swizzle_table[x >> 8] << 17) | (g_pvrtc_swizzle_table[x & 0xFF] << 1);

				uint32_t swizzled = x_swizzle | y_swizzle;
				if (num_blocks_x != num_blocks_y)
				{
					swizzled &= swizzle_mask;

					if (num_blocks_x > num_blocks_y)
						swizzled |= ((x >> min_bits) << (min_bits * 2));
					else
						swizzled |= ((y >> min_bits) << (min_bits * 2));
				}


				pvrtc4_block *pDst_block = static_cast<pvrtc4_block*>(pDst_blocks) + swizzled;
				pDst_block->m_endpoints = pPVRTC_endpoints[block_index];

				uint32_t base_r = g_etc_5_to_8[src_block.m_differential.m_red1];
				uint32_t base_g = g_etc_5_to_8[src_block.m_differential.m_green1];
				uint32_t base_b = g_etc_5_to_8[src_block.m_differential.m_blue1];

				const int *pInten_table48 = g_etc1_inten_tables48[src_block.m_differential.m_cw1];
				int by = (base_r + base_g + base_b) * 16;
				int block_colors_y_x16[4];
				block_colors_y_x16[0] = by + pInten_table48[2];
				block_colors_y_x16[1] = by + pInten_table48[3];
				block_colors_y_x16[2] = by + pInten_table48[1];
				block_colors_y_x16[3] = by + pInten_table48[0];

				{
					const uint32_t ex = 2;
					int bx = x + ex - 1;
					if (!pvrtc_wrap_addressing)
						bx = basisu::clamp<int>(bx, 0, x_mask);
					bx &= x_mask;

#define DO_ROW(ey) \
					{ \
						const uint32_t e = pE_rows[ey][bx]; \
						e0[ex][ey] = (get_opaque_endpoint_l0(e) * 255) / 31; \
						e1[ex][ey] = (get_opaque_endpoint_l1(e) * 255) / 31; \
					}

					DO_ROW(0);
					DO_ROW(1);
					DO_ROW(2);
				}

				uint32_t mod = 0;

				uint32_t lookup_x[4];

#define DO_LOOKUP(lx) { \
					const uint32_t byte_ofs = 7 - (((lx) * 4) >> 3); \
					const uint32_t lsb_bits = src_block.m_bytes[byte_ofs] >> (((lx) & 1) * 4); \
					const uint32_t msb_bits = src_block.m_bytes[byte_ofs - 2] >> (((lx) & 1) * 4); \
					lookup_x[lx] = (lsb_bits & 0xF) | ((msb_bits & 0xF) << 4); }

				DO_LOOKUP(0);
				DO_LOOKUP(1);
				DO_LOOKUP(2);
				DO_LOOKUP(3);
#undef DO_LOOKUP

#define DO_PIX(lx, ly, w0, w1, w2, w3) \
				{ \
					int ca_l = a0 * w0 + a1 * w1 + a2 * w2 + a3 * w3; \
					int cb_l = b0 * w0 + b1 * w1 + b2 * w2 + b3 * w3; \
					int cl = block_colors_y_x16[g_etc1_x_selector_unpack[ly][lookup_x[lx]]]; \
					int dl = cb_l - ca_l; \
					int vl = cl - ca_l; \
					int p = vl * 16; \
					if (ca_l > cb_l) { p = -p; dl = -dl; } \
					uint32_t m = 0; \
					if (p > 3 * dl) m = (uint32_t)(1 << ((ly) * 8 + (lx) * 2)); \
					if (p > 8 * dl) m = (uint32_t)(2 << ((ly) * 8 + (lx) * 2)); \
					if (p > 13 * dl) m = (uint32_t)(3 << ((ly) * 8 + (lx) * 2)); \
					mod |= m; \
				}

				{
					const uint32_t ex = 0, ey = 0;
					const int a0 = e0[ex][ey], a1 = e0[ex + 1][ey], a2 = e0[ex][ey + 1], a3 = e0[ex + 1][ey + 1];
					const int b0 = e1[ex][ey], b1 = e1[ex + 1][ey], b2 = e1[ex][ey + 1], b3 = e1[ex + 1][ey + 1];
					DO_PIX(0, 0, 4, 4, 4, 4);
					DO_PIX(1, 0, 2, 6, 2, 6);
					DO_PIX(0, 1, 2, 2, 6, 6);
					DO_PIX(1, 1, 1, 3, 3, 9);
				}

				{
					const uint32_t ex = 1, ey = 0;
					const int a0 = e0[ex][ey], a1 = e0[ex + 1][ey], a2 = e0[ex][ey + 1], a3 = e0[ex + 1][ey + 1];
					const int b0 = e1[ex][ey], b1 = e1[ex + 1][ey], b2 = e1[ex][ey + 1], b3 = e1[ex + 1][ey + 1];
					DO_PIX(2, 0, 8, 0, 8, 0);
					DO_PIX(3, 0, 6, 2, 6, 2);
					DO_PIX(2, 1, 4, 0, 12, 0);
					DO_PIX(3, 1, 3, 1, 9, 3);
				}

				{
					const uint32_t ex = 0, ey = 1;
					const int a0 = e0[ex][ey], a1 = e0[ex + 1][ey], a2 = e0[ex][ey + 1], a3 = e0[ex + 1][ey + 1];
					const int b0 = e1[ex][ey], b1 = e1[ex + 1][ey], b2 = e1[ex][ey + 1], b3 = e1[ex + 1][ey + 1];
					DO_PIX(0, 2, 8, 8, 0, 0);
					DO_PIX(1, 2, 4, 12, 0, 0);
					DO_PIX(0, 3, 6, 6, 2, 2);
					DO_PIX(1, 3, 3, 9, 1, 3);
				}

				{
					const uint32_t ex = 1, ey = 1;
					const int a0 = e0[ex][ey], a1 = e0[ex + 1][ey], a2 = e0[ex][ey + 1], a3 = e0[ex + 1][ey + 1];
					const int b0 = e1[ex][ey], b1 = e1[ex + 1][ey], b2 = e1[ex][ey + 1], b3 = e1[ex + 1][ey + 1];
					DO_PIX(2, 2, 16, 0, 0, 0);
					DO_PIX(3, 2, 12, 4, 0, 0);
					DO_PIX(2, 3, 12, 0, 4, 0);
					DO_PIX(3, 3, 9, 3, 3, 1);
				}
#undef DO_PIX

				pDst_block->m_modulation = mod;

				e0[0][0] = e0[1][0]; e0[1][0] = e0[2][0];
				e0[0][1] = e0[1][1]; e0[1][1] = e0[2][1];
				e0[0][2] = e0[1][2]; e0[1][2] = e0[2][2];

				e1[0][0] = e1[1][0]; e1[1][0] = e1[2][0];
				e1[0][1] = e1[1][1]; e1[1][1] = e1[2][1];
				e1[0][2] = e1[1][2]; e1[1][2] = e1[2][2];

			} // x
		} // y
	}
#endif

#if BASISD_SUPPORT_BC7
	struct bc7_mode_6
	{
		struct
		{
			uint64_t m_mode : 7;
			uint64_t m_r0 : 7;
			uint64_t m_r1 : 7;
			uint64_t m_g0 : 7;
			uint64_t m_g1 : 7;
			uint64_t m_b0 : 7;
			uint64_t m_b1 : 7;
			uint64_t m_a0 : 7;
			uint64_t m_a1 : 7;
			uint64_t m_p0 : 1;
		} m_lo;

		union
		{
			struct
			{
				uint64_t m_p1 : 1;
				uint64_t m_s00 : 3;
				uint64_t m_s10 : 4;
				uint64_t m_s20 : 4;
				uint64_t m_s30 : 4;

				uint64_t m_s01 : 4;
				uint64_t m_s11 : 4;
				uint64_t m_s21 : 4;
				uint64_t m_s31 : 4;

				uint64_t m_s02 : 4;
				uint64_t m_s12 : 4;
				uint64_t m_s22 : 4;
				uint64_t m_s32 : 4;

				uint64_t m_s03 : 4;
				uint64_t m_s13 : 4;
				uint64_t m_s23 : 4;
				uint64_t m_s33 : 4;

			} m_hi;

			uint64_t m_hi_bits;
		};
	};

	static void convert_etc1s_to_bc7_m6(bc7_mode_6 * pDst_block, const decoder_etc_block * pSrc_block, const selector * pSelector)
	{
#if !BASISD_WRITE_NEW_BC7_TABLES
		const uint32_t low_selector = pSelector->m_lo_selector;
		const uint32_t high_selector = pSelector->m_hi_selector;
		
		const uint32_t inten_table = pSrc_block->m_differential.m_cw1;
		const uint32_t base_color_r = pSrc_block->m_differential.m_red1;
		const uint32_t base_color_g = pSrc_block->m_differential.m_green1;
		const uint32_t base_color_b = pSrc_block->m_differential.m_blue1;

		if (pSelector->m_num_unique_selectors <= 2)
		{
			// Only two unique selectors so just switch to block truncation coding (BTC) to avoid quality issues on extreme blocks.
			pDst_block->m_lo.m_mode = 64;

			pDst_block->m_lo.m_a0 = 127;
			pDst_block->m_lo.m_a1 = 127;

			color32 block_colors[4];

			decoder_etc_block::get_block_colors5(block_colors, color32(base_color_r, base_color_g, base_color_b, 255), inten_table);

			const uint32_t r0 = block_colors[low_selector].r;
			const uint32_t g0 = block_colors[low_selector].g;
			const uint32_t b0 = block_colors[low_selector].b;
			const uint32_t low_bits0 = (r0 & 1) + (g0 & 1) + (b0 & 1);
			uint32_t p0 = low_bits0 >= 2;

			const uint32_t r1 = block_colors[high_selector].r;
			const uint32_t g1 = block_colors[high_selector].g;
			const uint32_t b1 = block_colors[high_selector].b;
			const uint32_t low_bits1 = (r1 & 1) + (g1 & 1) + (b1 & 1);
			uint32_t p1 = low_bits1 >= 2;
															
			pDst_block->m_lo.m_r0 = r0 >> 1;
			pDst_block->m_lo.m_g0 = g0 >> 1;
			pDst_block->m_lo.m_b0 = b0 >> 1;
			pDst_block->m_lo.m_p0 = p0;

			pDst_block->m_lo.m_r1 = r1 >> 1;
			pDst_block->m_lo.m_g1 = g1 >> 1;
			pDst_block->m_lo.m_b1 = b1 >> 1;
						
			uint32_t output_low_selector = 0;
			uint32_t output_bit_offset = 1;
			uint64_t output_hi_bits = p1;

			for (uint32_t y = 0; y < 4; y++)
			{
				for (uint32_t x = 0; x < 4; x++)
				{
					uint32_t s = pSrc_block->get_selector(x, y);
					uint32_t os = (s == low_selector) ? output_low_selector : (15 ^ output_low_selector);
					
					uint32_t num_bits = 4;

					if ((x | y) == 0)
					{
						if (os & 8)
						{
							pDst_block->m_lo.m_r0 = r1 >> 1;
							pDst_block->m_lo.m_g0 = g1 >> 1;
							pDst_block->m_lo.m_b0 = b1 >> 1;
							pDst_block->m_lo.m_p0 = p1;

							pDst_block->m_lo.m_r1 = r0 >> 1;
							pDst_block->m_lo.m_g1 = g0 >> 1;
							pDst_block->m_lo.m_b1 = b0 >> 1;

							output_hi_bits &= ~1ULL;
							output_hi_bits |= p0;
							std::swap(p0, p1);
												
							output_low_selector = 15;
							os = 0;
						}

						num_bits = 3;
					}

					output_hi_bits |= (static_cast<uint64_t>(os) << output_bit_offset);
					output_bit_offset += num_bits;
				}
			}

			pDst_block->m_hi_bits = output_hi_bits;
			
			assert(pDst_block->m_hi.m_p1 == p1);
									
			return;
		}
				
		uint32_t selector_range_table = g_etc1_to_bc7_m6_selector_range_index[low_selector][high_selector];

		const uint32_t* pTable_r = g_etc1_to_bc7_m6_table[base_color_r + inten_table * 32] + (selector_range_table * NUM_ETC1_TO_BC7_M6_SELECTOR_MAPPINGS);
		const uint32_t* pTable_g = g_etc1_to_bc7_m6_table[base_color_g + inten_table * 32] + (selector_range_table * NUM_ETC1_TO_BC7_M6_SELECTOR_MAPPINGS);
		const uint32_t* pTable_b = g_etc1_to_bc7_m6_table[base_color_b + inten_table * 32] + (selector_range_table * NUM_ETC1_TO_BC7_M6_SELECTOR_MAPPINGS);

#if 1
		assert(NUM_ETC1_TO_BC7_M6_SELECTOR_MAPPINGS == 48);

		uint32_t best_err0 = UINT_MAX, best_err1 = UINT_MAX;

#define DO_ITER2(idx) \
		{  \
			uint32_t v0 = ((pTable_r[(idx)+0] + pTable_g[(idx)+0] + pTable_b[(idx)+0]) << 14) | ((idx) + 0); if (v0 < best_err0) best_err0 = v0; \
			uint32_t v1 = ((pTable_r[(idx)+1] + pTable_g[(idx)+1] + pTable_b[(idx)+1]) << 14) | ((idx) + 1); if (v1 < best_err1) best_err1 = v1; \
		}
#define DO_ITER4(idx) DO_ITER2(idx); DO_ITER2((idx) + 2);
#define DO_ITER8(idx) DO_ITER4(idx); DO_ITER4((idx) + 4);
#define DO_ITER16(idx) DO_ITER8(idx); DO_ITER8((idx) + 8);

		DO_ITER16(0);
		DO_ITER16(16);
		DO_ITER16(32);
#undef DO_ITER2
#undef DO_ITER4
#undef DO_ITER8
#undef DO_ITER16

		uint32_t best_err = basisu::minimum(best_err0, best_err1);
		uint32_t best_mapping = best_err & 0xFF;
		//best_err >>= 14;
#else
		uint32_t best_err = UINT_MAX;
		uint32_t best_mapping = 0;
		assert((NUM_ETC1_TO_BC7_M6_SELECTOR_MAPPINGS % 2) == 0);
		for (uint32_t m = 0; m < NUM_ETC1_TO_BC7_M6_SELECTOR_MAPPINGS; m += 2)
		{
#define DO_ITER(idx)	{ uint32_t total_err = (pTable_r[idx] + pTable_g[idx] + pTable_b[idx]) & 0x3FFFF; if (total_err < best_err) { best_err = total_err; best_mapping = idx; } }
			DO_ITER(m);
			DO_ITER(m + 1);
#undef DO_ITER
		}
#endif		

		pDst_block->m_lo.m_mode = 64;

		pDst_block->m_lo.m_a0 = 127;
		pDst_block->m_lo.m_a1 = 127;

		uint64_t v = 0;
		const uint8_t* pSelectors_xlat;

		if (g_etc1_to_bc7_selector_mappings[best_mapping][pSrc_block->get_selector(0, 0)] & 8)
		{
			pDst_block->m_lo.m_r1 = (pTable_r[best_mapping] >> 18) & 0x7F;
			pDst_block->m_lo.m_g1 = (pTable_g[best_mapping] >> 18) & 0x7F;
			pDst_block->m_lo.m_b1 = (pTable_b[best_mapping] >> 18) & 0x7F;

			pDst_block->m_lo.m_r0 = (pTable_r[best_mapping] >> 25) & 0x7F;
			pDst_block->m_lo.m_g0 = (pTable_g[best_mapping] >> 25) & 0x7F;
			pDst_block->m_lo.m_b0 = (pTable_b[best_mapping] >> 25) & 0x7F;

			pDst_block->m_lo.m_p0 = 1;
			pDst_block->m_hi.m_p1 = 0;

			v = 0;
			pSelectors_xlat = &g_etc1_to_bc7_selector_mappings_from_raw_etc1_inv[best_mapping][0];
		}
		else
		{
			pDst_block->m_lo.m_r0 = (pTable_r[best_mapping] >> 18) & 0x7F;
			pDst_block->m_lo.m_g0 = (pTable_g[best_mapping] >> 18) & 0x7F;
			pDst_block->m_lo.m_b0 = (pTable_b[best_mapping] >> 18) & 0x7F;

			pDst_block->m_lo.m_r1 = (pTable_r[best_mapping] >> 25) & 0x7F;
			pDst_block->m_lo.m_g1 = (pTable_g[best_mapping] >> 25) & 0x7F;
			pDst_block->m_lo.m_b1 = (pTable_b[best_mapping] >> 25) & 0x7F;

			pDst_block->m_lo.m_p0 = 0;
			pDst_block->m_hi.m_p1 = 1;

			v = 1;
			pSelectors_xlat = &g_etc1_to_bc7_selector_mappings_from_raw_etc1[best_mapping][0];
		}

		uint64_t v1 = 0, v2 = 0, v3 = 0;

#define DO_X(x, s0, s1, s2, s3) { \
		const uint32_t byte_ofs = 7 - (((x) * 4) >> 3); \
		const uint32_t lsb_bits = pSrc_block->m_bytes[byte_ofs] >> (((x) & 1) * 4); \
		const uint32_t msb_bits = pSrc_block->m_bytes[byte_ofs - 2] >> (((x) & 1) * 4); \
		const uint32_t lookup = (lsb_bits & 0xF)| ((msb_bits & 0xF) << 4); \
		v |= ((uint64_t)pSelectors_xlat[g_etc1_x_selector_unpack[0][lookup]] << (s0)); \
		v1 |= ((uint64_t)pSelectors_xlat[g_etc1_x_selector_unpack[1][lookup]] << (s1)); \
		v2 |= ((uint64_t)pSelectors_xlat[g_etc1_x_selector_unpack[2][lookup]] << (s2)); \
		v3 |= ((uint64_t)pSelectors_xlat[g_etc1_x_selector_unpack[3][lookup]] << (s3)); }

		// 1  4  8  12
		// 16 20 24 28
		// 32 36 40 44
		// 48 52 56 60

		DO_X(0, 1, 16, 32, 48);
		DO_X(1, 4, 20, 36, 52);
		DO_X(2, 8, 24, 40, 56);
		DO_X(3, 12, 28, 44, 60);
#undef DO_X

		pDst_block->m_hi_bits = v | v1 | v2 | v3;
#endif

	}
#endif

#if BASISD_SUPPORT_ETC2_EAC_A8
	static void convert_etc1s_to_etc2_eac_a8(eac_a8_block * pDst_block, const decoder_etc_block * pSrc_block, const selector * pSelector)
	{
		const uint32_t low_selector = pSelector->m_lo_selector;
		const uint32_t high_selector = pSelector->m_hi_selector;

		const color32 base_color(decoder_etc_block::unpack_color5(pSrc_block->get_base5_color(), false));
		const uint32_t inten_table = pSrc_block->get_inten_table(0);

		if (low_selector == high_selector)
		{
			color32 block_colors[4];

			decoder_etc_block::get_block_colors5(block_colors, base_color, inten_table);

			const uint32_t r = block_colors[low_selector].r;

			// Constant color block
			// Select table 13, use selector 4 (0), set multiplier to 1 and base color g
			pDst_block->m_base = r;
			pDst_block->m_table = 13;
			pDst_block->m_multiplier = 1;

			// selectors are all 4's
			static const uint8_t s_etc2_eac_a8_sel4[6] = { 0x92, 0x49, 0x24, 0x92, 0x49, 0x24 };
			memcpy(pDst_block->m_selectors, s_etc2_eac_a8_sel4, sizeof(s_etc2_eac_a8_sel4));

			return;
		}

		uint32_t selector_range_table = 0;
		for (selector_range_table = 0; selector_range_table < NUM_ETC2_EAC_A8_SELECTOR_RANGES; selector_range_table++)
			if ((low_selector == s_etc2_eac_a8_selector_ranges[selector_range_table].m_low) && (high_selector == s_etc2_eac_a8_selector_ranges[selector_range_table].m_high))
				break;
		if (selector_range_table >= NUM_ETC2_EAC_A8_SELECTOR_RANGES)
			selector_range_table = 0;

		const etc1_g_to_etc2_a8_conversion *pTable_entry = &s_etc1_g_to_etc2_a8[base_color.r + inten_table * 32][selector_range_table];

		pDst_block->m_base = pTable_entry->m_base;
		pDst_block->m_table = pTable_entry->m_table_mul >> 4;
		pDst_block->m_multiplier = pTable_entry->m_table_mul & 15;

		uint64_t selector_bits = 0;

		// TODO: This can be further optimized (read of ETC1 selector bits)
		for (uint32_t y = 0; y < 4; y++)
		{
			for (uint32_t x = 0; x < 4; x++)
			{
				uint32_t s = pSrc_block->get_selector(x, y);

				uint32_t ds = (pTable_entry->m_trans >> (s * 3)) & 7;

				const uint32_t dst_ofs = 45 - (y + x * 4) * 3;
				selector_bits |= (static_cast<uint64_t>(ds) << dst_ofs);
			}
		}

		pDst_block->set_selector_bits(selector_bits);
	}
#endif // BASISD_SUPPORT_ETC2_EAC_A8

	basisu_lowlevel_transcoder::basisu_lowlevel_transcoder(const etc1_global_selector_codebook * pGlobal_sel_codebook) :
		m_pGlobal_sel_codebook(pGlobal_sel_codebook),
		m_selector_history_buf_size(0)
	{
	}

	bool basisu_lowlevel_transcoder::decode_palettes(
		uint32_t num_endpoints, const uint8_t * pEndpoints_data, uint32_t endpoints_data_size,
		uint32_t num_selectors, const uint8_t * pSelectors_data, uint32_t selectors_data_size)
	{
		bitwise_decoder sym_codec;

		huffman_decoding_table color5_delta_model0, color5_delta_model1, color5_delta_model2, inten_delta_model;

		if (!sym_codec.init(pEndpoints_data, endpoints_data_size))
		{
			BASISU_DEVEL_ERROR("basisu_lowlevel_transcoder::decode_palettes: fail 0\n");		
			return false;
		}
				
		if (!sym_codec.read_huffman_table(color5_delta_model0))
		{
			BASISU_DEVEL_ERROR("basisu_lowlevel_transcoder::decode_palettes: fail 1\n");		
			return false;
		}

		if (!sym_codec.read_huffman_table(color5_delta_model1))
		{
			BASISU_DEVEL_ERROR("basisu_lowlevel_transcoder::decode_palettes: fail 1a\n");		
			return false;
		}

		if (!sym_codec.read_huffman_table(color5_delta_model2))
		{
			BASISU_DEVEL_ERROR("basisu_lowlevel_transcoder::decode_palettes: fail 2a\n");		
			return false;
		}
				
		if (!sym_codec.read_huffman_table(inten_delta_model))
		{
			BASISU_DEVEL_ERROR("basisu_lowlevel_transcoder::decode_palettes: fail 2b\n");		
			return false;
		}

		if (!color5_delta_model0.is_valid() || !color5_delta_model1.is_valid() || !color5_delta_model2.is_valid() || !inten_delta_model.is_valid())
		{
			BASISU_DEVEL_ERROR("basisu_lowlevel_transcoder::decode_palettes: fail 2b\n");		
			return false;
		}

		const bool endpoints_are_grayscale = sym_codec.get_bits(1) != 0;
		
		m_endpoints.resize(num_endpoints);

		color32 prev_color5(16, 16, 16, 0);
		uint32_t prev_inten = 0;
		
		for (uint32_t i = 0; i < num_endpoints; i++)
		{
			uint32_t inten_delta = sym_codec.decode_huffman(inten_delta_model);
			m_endpoints[i].m_inten5 = static_cast<uint8_t>((inten_delta + prev_inten) & 7);
			prev_inten = m_endpoints[i].m_inten5;
			
			for (uint32_t c = 0; c < (endpoints_are_grayscale ? 1U : 3U); c++)
			{
				int delta;
				if (prev_color5[c] <= basist::COLOR5_PAL0_PREV_HI)
					delta = sym_codec.decode_huffman(color5_delta_model0);
				else if (prev_color5[c] <= basist::COLOR5_PAL1_PREV_HI)
					delta = sym_codec.decode_huffman(color5_delta_model1);
				else
					delta = sym_codec.decode_huffman(color5_delta_model2);

				int v = (prev_color5[c] + delta) & 31;
				
				m_endpoints[i].m_color5[c] = static_cast<uint8_t>(v);

				prev_color5[c] = static_cast<uint8_t>(v);
			}

			if (endpoints_are_grayscale)
			{
				m_endpoints[i].m_color5[1] = m_endpoints[i].m_color5[0];
				m_endpoints[i].m_color5[2] = m_endpoints[i].m_color5[0];
			}
		}
				
		sym_codec.stop();

		m_selectors.resize(num_selectors);

		if (!sym_codec.init(pSelectors_data, selectors_data_size))
		{
			BASISU_DEVEL_ERROR("basisu_lowlevel_transcoder::decode_palettes: fail 5\n");		
			return false;
		}

		basist::huffman_decoding_table delta_selector_pal_model;

		const bool used_global_selector_cb = (sym_codec.get_bits(1) == 1);

		if (used_global_selector_cb)
		{
			// global selector palette
			uint32_t pal_bits = sym_codec.get_bits(4);
			uint32_t mod_bits = sym_codec.get_bits(4);

			basist::huffman_decoding_table mod_model;
			if (mod_bits)
			{
				if (!sym_codec.read_huffman_table(mod_model))
				{
					BASISU_DEVEL_ERROR("basisu_lowlevel_transcoder::decode_palettes: fail 6\n");		
					return false;
				}
				if (!mod_model.is_valid())
				{
					BASISU_DEVEL_ERROR("basisu_lowlevel_transcoder::decode_palettes: fail 6a\n");		
					return false;
				}
			}
						
			for (uint32_t i = 0; i < num_selectors; i++)
			{
				uint32_t pal_index = 0;
				if (pal_bits)
					pal_index = sym_codec.get_bits(pal_bits);

				uint32_t mod_index = 0;
				if (mod_bits)
					mod_index = sym_codec.decode_huffman(mod_model);
					
				if (pal_index >= m_pGlobal_sel_codebook->size())
				{
					BASISU_DEVEL_ERROR("basisu_lowlevel_transcoder::decode_palettes: fail 7z\n");		
					return false;
				}

				const etc1_selector_palette_entry e(m_pGlobal_sel_codebook->get_entry(pal_index, etc1_global_palette_entry_modifier(mod_index)));

				// TODO: Optimize this
				for (uint32_t y = 0; y < 4; y++)
					for (uint32_t x = 0; x < 4; x++)
						m_selectors[i].set_selector(x, y, e[x + y * 4]);
								
				m_selectors[i].init_flags();
			}
		}
		else
		{
			const bool used_hybrid_selector_cb = (sym_codec.get_bits(1) == 1);

			if (used_hybrid_selector_cb)
			{
				const uint32_t pal_bits = sym_codec.get_bits(4);
				const uint32_t mod_bits = sym_codec.get_bits(4);

				basist::huffman_decoding_table uses_global_cb_bitflags_model;
				if (!sym_codec.read_huffman_table(uses_global_cb_bitflags_model))
				{
					BASISU_DEVEL_ERROR("basisu_lowlevel_transcoder::decode_palettes: fail 7\n");		
					return false;
				}
				if (!uses_global_cb_bitflags_model.is_valid())
				{
					BASISU_DEVEL_ERROR("basisu_lowlevel_transcoder::decode_palettes: fail 7a\n");		
					return false;
				}

				basist::huffman_decoding_table global_mod_indices_model;
				if (mod_bits)
				{
					if (!sym_codec.read_huffman_table(global_mod_indices_model))
					{
						BASISU_DEVEL_ERROR("basisu_lowlevel_transcoder::decode_palettes: fail 8\n");		
						return false;
					}
					if (!global_mod_indices_model.is_valid())
					{
						BASISU_DEVEL_ERROR("basisu_lowlevel_transcoder::decode_palettes: fail 8a\n");		
						return false;
					}
				}

				uint32_t cur_uses_global_cb_bitflags = 0;
				uint32_t uses_global_cb_bitflags_remaining = 0;
								
				for (uint32_t q = 0; q < num_selectors; q++)
				{
					if (!uses_global_cb_bitflags_remaining)
					{
						cur_uses_global_cb_bitflags = sym_codec.decode_huffman(uses_global_cb_bitflags_model);

						uses_global_cb_bitflags_remaining = 8;
					}
					uses_global_cb_bitflags_remaining--;

					const bool used_global_cb_flag = (cur_uses_global_cb_bitflags & 1) != 0;
					cur_uses_global_cb_bitflags >>= 1;

					if (used_global_cb_flag)
					{
						const uint32_t pal_index = pal_bits ? sym_codec.get_bits(pal_bits) : 0;
						const uint32_t mod_index = mod_bits ? sym_codec.decode_huffman(global_mod_indices_model) : 0;

						if (pal_index >= m_pGlobal_sel_codebook->size())
						{
							BASISU_DEVEL_ERROR("basisu_lowlevel_transcoder::decode_palettes: fail 8b\n");		
							return false;
						}

						const etc1_selector_palette_entry e(m_pGlobal_sel_codebook->get_entry(pal_index, etc1_global_palette_entry_modifier(mod_index)));

						for (uint32_t y = 0; y < 4; y++)
							for (uint32_t x = 0; x < 4; x++)
								m_selectors[q].set_selector(x, y, e[x + y * 4]);
					}
					else
					{
						for (uint32_t j = 0; j < 4; j++)
						{
							uint32_t cur_byte = sym_codec.get_bits(8);

							for (uint32_t k = 0; k < 4; k++)
								m_selectors[q].set_selector(k, j, (cur_byte >> (k * 2)) & 3);
						}
					}

					m_selectors[q].init_flags();
				}
			}
			else
			{
				const bool used_raw_encoding = (sym_codec.get_bits(1) == 1);

				if (used_raw_encoding)
				{
					for (uint32_t i = 0; i < num_selectors; i++)
					{
						for (uint32_t j = 0; j < 4; j++)
						{
							uint32_t cur_byte = sym_codec.get_bits(8);

							for (uint32_t k = 0; k < 4; k++)
								m_selectors[i].set_selector(k, j, (cur_byte >> (k * 2)) & 3);
						}

						m_selectors[i].init_flags();
					}
				}
				else
				{
					if (!sym_codec.read_huffman_table(delta_selector_pal_model))
					{
						BASISU_DEVEL_ERROR("basisu_lowlevel_transcoder::decode_palettes: fail 10\n");		
						return false;
					}

					if ((num_selectors > 1) && (!delta_selector_pal_model.is_valid()))
					{
						BASISU_DEVEL_ERROR("basisu_lowlevel_transcoder::decode_palettes: fail 10a\n");		
						return false;
					}

					uint8_t prev_bytes[4] = { 0, 0, 0, 0 };
					
					for (uint32_t i = 0; i < num_selectors; i++)
					{
						if (!i)
						{
							for (uint32_t j = 0; j < 4; j++)
							{
								uint32_t cur_byte = sym_codec.get_bits(8);
								prev_bytes[j] = static_cast<uint8_t>(cur_byte);
								
								for (uint32_t k = 0; k < 4; k++)
									m_selectors[i].set_selector(k, j, (cur_byte >> (k * 2)) & 3);
							}
							m_selectors[i].init_flags();
							continue;
						}

						for (uint32_t j = 0; j < 4; j++)
						{
							int delta_byte = sym_codec.decode_huffman(delta_selector_pal_model);

							uint32_t cur_byte = delta_byte ^ prev_bytes[j];
							prev_bytes[j] = static_cast<uint8_t>(cur_byte);

							for (uint32_t k = 0; k < 4; k++)
								m_selectors[i].set_selector(k, j, (cur_byte >> (k * 2)) & 3);
						}
						m_selectors[i].init_flags();
					}
				}
			}
		}

		sym_codec.stop();

		return true;
	}

	bool basisu_lowlevel_transcoder::decode_tables(const uint8_t * pTable_data, uint32_t table_data_size)
	{
		basist::bitwise_decoder sym_codec;
		if (!sym_codec.init(pTable_data, table_data_size))
		{
			BASISU_DEVEL_ERROR("basisu_lowlevel_transcoder::decode_tables: fail 0\n");		
			return false;
		}

		if (!sym_codec.read_huffman_table(m_endpoint_pred_model))
		{
			BASISU_DEVEL_ERROR("basisu_lowlevel_transcoder::decode_tables: fail 1\n");		
			return false;
		}
		
		if (m_endpoint_pred_model.get_code_sizes().size() == 0)
		{
			BASISU_DEVEL_ERROR("basisu_lowlevel_transcoder::decode_tables: fail 1a\n");		
			return false;
		}

		if (!sym_codec.read_huffman_table(m_delta_endpoint_model))
		{
			BASISU_DEVEL_ERROR("basisu_lowlevel_transcoder::decode_tables: fail 2\n");		
			return false;
		}

		if (m_delta_endpoint_model.get_code_sizes().size() == 0)
		{
			BASISU_DEVEL_ERROR("basisu_lowlevel_transcoder::decode_tables: fail 2a\n");		
			return false;
		}

		if (!sym_codec.read_huffman_table(m_selector_model))
		{
			BASISU_DEVEL_ERROR("basisu_lowlevel_transcoder::decode_tables: fail 3\n");		
			return false;
		}

		if (m_selector_model.get_code_sizes().size() == 0)
		{
			BASISU_DEVEL_ERROR("basisu_lowlevel_transcoder::decode_tables: fail 3a\n");		
			return false;
		}

		if (!sym_codec.read_huffman_table(m_selector_history_buf_rle_model))
		{
			BASISU_DEVEL_ERROR("basisu_lowlevel_transcoder::decode_tables: fail 4\n");		
			return false;
		}

		if (m_selector_history_buf_rle_model.get_code_sizes().size() == 0)
		{
			BASISU_DEVEL_ERROR("basisu_lowlevel_transcoder::decode_tables: fail 4a\n");		
			return false;
		}

		m_selector_history_buf_size = sym_codec.get_bits(13);
				
		sym_codec.stop();

		return true;
	}
			
	bool basisu_lowlevel_transcoder::transcode_slice(void *pDst_blocks, uint32_t num_blocks_x, uint32_t num_blocks_y, const uint8_t *pImage_data, uint32_t image_data_size, block_format fmt, 
		uint32_t output_stride, bool pvrtc_wrap_addressing, bool bc1_allow_threecolor_blocks)
	{
		const uint32_t total_blocks = num_blocks_x * num_blocks_y;

		basist::bitwise_decoder sym_codec;
				
		if (!sym_codec.init(pImage_data, image_data_size))
		{
			BASISU_DEVEL_ERROR("basisu_lowlevel_transcoder::transcode_slice: sym_codec.init failed\n");		
			return false;
		}
		
		approx_move_to_front selector_history_buf(m_selector_history_buf_size);
				
		int prev_selector_index = 0;

		const uint32_t SELECTOR_HISTORY_BUF_FIRST_SYMBOL_INDEX = (uint32_t)m_selectors.size();
		const uint32_t SELECTOR_HISTORY_BUF_RLE_SYMBOL_INDEX = m_selector_history_buf_size + SELECTOR_HISTORY_BUF_FIRST_SYMBOL_INDEX;
		uint32_t cur_selector_rle_count = 0;
								
		decoder_etc_block block;
		memset(&block, 0, sizeof(block));

		block.set_flip_bit(true);
		block.set_diff_bit(true);

		void *pPVRTC_work_mem = nullptr;
		uint32_t *pPVRTC_endpoints = nullptr;
		if (fmt == cPVRTC1_4_OPAQUE_ONLY)
		{
			pPVRTC_work_mem = malloc(num_blocks_x * num_blocks_y * (sizeof(decoder_etc_block) + sizeof(uint32_t)));
			if (!pPVRTC_work_mem)
			{
				BASISU_DEVEL_ERROR("basisu_lowlevel_transcoder::transcode_slice: malloc failed\n");		
				return false;
			}
			pPVRTC_endpoints = (uint32_t *)&((decoder_etc_block*)pPVRTC_work_mem)[num_blocks_x * num_blocks_y];
		}
		
		if (m_block_endpoint_preds[0].size() < num_blocks_x)
		{
			m_block_endpoint_preds[0].resize(num_blocks_x);
			m_block_endpoint_preds[1].resize(num_blocks_x);
		}

		uint32_t cur_pred_bits = 0;
		int prev_endpoint_pred_sym = 0;
		int endpoint_pred_repeat_count = 0;
		uint32_t prev_endpoint_index = 0;
			
		for (uint32_t block_y = 0; block_y < num_blocks_y; block_y++)
		{
			const uint32_t cur_block_endpoint_pred_array = block_y & 1;

			for (uint32_t block_x = 0; block_x < num_blocks_x; block_x++)
			{
				// Decode endpoint index predictor symbols
				if ((block_x & 1) == 0)
				{
					if ((block_y & 1) == 0)
					{
						if (endpoint_pred_repeat_count)
						{
							endpoint_pred_repeat_count--;
							cur_pred_bits = prev_endpoint_pred_sym;
						}
						else
						{
							cur_pred_bits = sym_codec.decode_huffman(m_endpoint_pred_model);
							if (cur_pred_bits == ENDPOINT_PRED_REPEAT_LAST_SYMBOL)
							{
								endpoint_pred_repeat_count = sym_codec.decode_vlc(ENDPOINT_PRED_COUNT_VLC_BITS) + ENDPOINT_PRED_MIN_REPEAT_COUNT - 1;
								  
								cur_pred_bits = prev_endpoint_pred_sym;
							}
							else
							{
								prev_endpoint_pred_sym = cur_pred_bits;
							}
						}

						m_block_endpoint_preds[cur_block_endpoint_pred_array ^ 1][block_x].m_pred_bits = (uint8_t)(cur_pred_bits >> 4);
					 }
					 else
					 {
						 cur_pred_bits = m_block_endpoint_preds[cur_block_endpoint_pred_array][block_x].m_pred_bits;
					 }
				}

				// Decode endpoint index
				uint32_t endpoint_index;

				const uint32_t pred = cur_pred_bits & 3;
				cur_pred_bits >>= 2;

				if (pred == 0)
				{
					// Left
					if (!block_x)
					{
						BASISU_DEVEL_ERROR("basisu_lowlevel_transcoder::transcode_slice: invalid datastream (0)\n");		
						if (pPVRTC_work_mem)
							free(pPVRTC_work_mem);
						return false;
					}

					endpoint_index = prev_endpoint_index;
				}
				else if (pred == 1)
				{
					// Upper
					if (!block_y)
					{
						BASISU_DEVEL_ERROR("basisu_lowlevel_transcoder::transcode_slice: invalid datastream (1)\n");		
						if (pPVRTC_work_mem)
							free(pPVRTC_work_mem);
						return false;
					}

					endpoint_index = m_block_endpoint_preds[cur_block_endpoint_pred_array ^ 1][block_x].m_endpoint_index;
				}
				else if (pred == 2)
				{
					// Upper left
					if ((!block_x) || (!block_y))
					{
						BASISU_DEVEL_ERROR("basisu_lowlevel_transcoder::transcode_slice: invalid datastream (2)\n");		
						if (pPVRTC_work_mem)
							free(pPVRTC_work_mem);
						return false;
					}

					endpoint_index = m_block_endpoint_preds[cur_block_endpoint_pred_array ^ 1][block_x - 1].m_endpoint_index;
				}
				else
				{
					// Decode and apply delta
					const uint32_t delta_sym = sym_codec.decode_huffman(m_delta_endpoint_model);

					endpoint_index = delta_sym + prev_endpoint_index;
					if (endpoint_index >= m_endpoints.size())
						endpoint_index -= (int)m_endpoints.size();
				}

				m_block_endpoint_preds[cur_block_endpoint_pred_array][block_x].m_endpoint_index = (uint16_t)endpoint_index;

				prev_endpoint_index = endpoint_index;
				
				// Decode selector index
				uint32_t selector_index;
				int selector_sym;
				if (cur_selector_rle_count > 0)
				{
					cur_selector_rle_count--;

					selector_sym = (int)m_selectors.size();
				}
				else
				{
					selector_sym = sym_codec.decode_huffman(m_selector_model);

					if (selector_sym == static_cast<int>(SELECTOR_HISTORY_BUF_RLE_SYMBOL_INDEX))
					{
						int run_sym = sym_codec.decode_huffman(m_selector_history_buf_rle_model);

						if (run_sym == (SELECTOR_HISTORY_BUF_RLE_COUNT_TOTAL - 1))
							cur_selector_rle_count = sym_codec.decode_vlc(7) + SELECTOR_HISTORY_BUF_RLE_COUNT_THRESH;
						else
							cur_selector_rle_count = run_sym + SELECTOR_HISTORY_BUF_RLE_COUNT_THRESH;

						if (cur_selector_rle_count > total_blocks)
						{
							// The file is corrupted or we've got a bug.
							BASISU_DEVEL_ERROR("basisu_lowlevel_transcoder::transcode_slice: invalid datastream (3)\n");		
							if (pPVRTC_work_mem)
								free(pPVRTC_work_mem);
							return false;
						}

						selector_sym = (int)m_selectors.size();

						cur_selector_rle_count--;
					}
				}

				if (selector_sym >= (int)m_selectors.size())
				{
					assert(m_selector_history_buf_size > 0);

					int history_buf_index = selector_sym - (int)m_selectors.size();

					if (history_buf_index >= (int)selector_history_buf.size())
					{
						// The file is corrupted or we've got a bug.
						BASISU_DEVEL_ERROR("basisu_lowlevel_transcoder::transcode_slice: invalid datastream (4)\n");		
						if (pPVRTC_work_mem)
							free(pPVRTC_work_mem);
						return false;
					}

					selector_index = selector_history_buf[history_buf_index];

					if (history_buf_index != 0)
						selector_history_buf.use(history_buf_index);
				}
				else
				{
					selector_index = selector_sym;

					if (m_selector_history_buf_size)
						selector_history_buf.add(selector_index);
				}

				prev_selector_index = selector_index;

				if ((endpoint_index >= m_endpoints.size()) || (selector_index >= m_selectors.size()))
				{
					// The file is corrupted or we've got a bug.
					BASISU_DEVEL_ERROR("basisu_lowlevel_transcoder::transcode_slice: invalid datastream (5)\n");		
					if (pPVRTC_work_mem)
						free(pPVRTC_work_mem);
					return false;
				}

				const endpoint *pEndpoint0 = &m_endpoints[endpoint_index];
												
				block.set_base5_color(decoder_etc_block::pack_color5(pEndpoint0->m_color5, false));

				block.set_inten_table(0, pEndpoint0->m_inten5);
				block.set_inten_table(1, pEndpoint0->m_inten5);

				const selector *pSelector = &m_selectors[selector_index];

				switch (fmt)
				{
				case cETC1:
				{
					//block.set_raw_selector_bits(pSelector->m_bytes[0], pSelector->m_bytes[1], pSelector->m_bytes[2], pSelector->m_bytes[3]);
					//memcpy(pDst_block, &block, sizeof(block));

					decoder_etc_block* pDst_block = reinterpret_cast<decoder_etc_block*>(static_cast<uint8_t*>(pDst_blocks) + (block_x + block_y * num_blocks_x) * output_stride);
					pDst_block->m_uint32[0] = block.m_uint32[0];
					pDst_block->set_raw_selector_bits(pSelector->m_bytes[0], pSelector->m_bytes[1], pSelector->m_bytes[2], pSelector->m_bytes[3]);

					break;
				}
				case cBC1:
				{
					block.set_raw_selector_bits(pSelector->m_bytes[0], pSelector->m_bytes[1], pSelector->m_bytes[2], pSelector->m_bytes[3]);

					void* pDst_block = static_cast<uint8_t*>(pDst_blocks) + (block_x + block_y * num_blocks_x) * output_stride;
#if BASISD_SUPPORT_DXT1
					convert_etc1s_to_dxt1(static_cast<dxt1_block*>(pDst_block), &block, pSelector, bc1_allow_threecolor_blocks);
#else
					assert(0);
#endif
					break;
				}
				case cBC4:
				{
					block.set_raw_selector_bits(pSelector->m_bytes[0], pSelector->m_bytes[1], pSelector->m_bytes[2], pSelector->m_bytes[3]);

					void* pDst_block = static_cast<uint8_t*>(pDst_blocks) + (block_x + block_y * num_blocks_x) * output_stride;
#if BASISD_SUPPORT_DXT5A
					convert_etc1s_to_dxt5a(static_cast<dxt5a_block*>(pDst_block), &block, pSelector);
#else
					assert(0);
#endif
					break;
				}
				case cPVRTC1_4_OPAQUE_ONLY:
				{
#if BASISD_SUPPORT_PVRTC1
					block.set_raw_selector_bits(pSelector->m_bytes[0], pSelector->m_bytes[1], pSelector->m_bytes[2], pSelector->m_bytes[3]);

					((decoder_etc_block*)pPVRTC_work_mem)[block_x + block_y * num_blocks_x] = block;

					const color32 base_color(block.get_base5_color_unscaled());
					const uint32_t inten_table = block.get_inten_table(0);

					const uint32_t low_selector = pSelector->m_lo_selector;
					const uint32_t high_selector = pSelector->m_hi_selector;

					color32 block_colors[2];
					decoder_etc_block::get_block_colors5_bounds(block_colors, base_color, inten_table, low_selector, high_selector);

					assert(block_colors[0][0] <= block_colors[1][0]);
					assert(block_colors[0][1] <= block_colors[1][1]);
					assert(block_colors[0][2] <= block_colors[1][2]);

					pvrtc4_block temp;
					temp.set_endpoint(0, block_colors[0], true, true, 0);
					temp.set_endpoint(1, block_colors[1], true, true, 254);

					//temp.set_endpoint(0, block_colors[1], true, true, 254);
					//temp.set_endpoint(1, block_colors[0], true, true, 0);

					pPVRTC_endpoints[block_x + block_y * num_blocks_x] = temp.m_endpoints;
#else
					assert(0);
#endif	

					break;
				}
				case cBC7_M6_OPAQUE_ONLY:
				{
#if BASISD_SUPPORT_BC7
					block.set_raw_selector_bits(pSelector->m_bytes[0], pSelector->m_bytes[1], pSelector->m_bytes[2], pSelector->m_bytes[3]);
					
					void* pDst_block = static_cast<uint8_t*>(pDst_blocks) + (block_x + block_y * num_blocks_x) * output_stride;
					convert_etc1s_to_bc7_m6(static_cast<bc7_mode_6*>(pDst_block), &block, pSelector);
#else	
					assert(0);
#endif
					break;
				}
				case cETC2_EAC_A8:
				{
					block.set_raw_selector_bits(pSelector->m_bytes[0], pSelector->m_bytes[1], pSelector->m_bytes[2], pSelector->m_bytes[3]);
					void* pDst_block = static_cast<uint8_t*>(pDst_blocks) + (block_x + block_y * num_blocks_x) * output_stride;
#if BASISD_SUPPORT_ETC2_EAC_A8
					convert_etc1s_to_etc2_eac_a8(static_cast<eac_a8_block*>(pDst_block), &block, pSelector);
#else
					assert(0);
#endif
					break;
				}
				default:
				{
					assert(0);
					break;
				}
				}

			} // block_x

		} // block-y

		if (endpoint_pred_repeat_count != 0)
		{
			BASISU_DEVEL_ERROR("basisu_lowlevel_transcoder::transcode_slice: endpoint_pred_repeat_count != 0. The file is corrupted or this is a bug\n");		
			return false;
		}

		//assert(endpoint_pred_repeat_count == 0);
		
		if (fmt == cPVRTC1_4_OPAQUE_ONLY)
		{
			// PVRTC post process - create per-pixel modulation values.
#if BASISD_SUPPORT_PVRTC1
			fixup_pvrtc1_4_modulation((decoder_etc_block*)pPVRTC_work_mem, pPVRTC_endpoints, pDst_blocks, num_blocks_x, num_blocks_y, pvrtc_wrap_addressing);
#endif
		}

		if (pPVRTC_work_mem)
			free(pPVRTC_work_mem);

		return true;
	}

	basisu_transcoder::basisu_transcoder(const etc1_global_selector_codebook * pGlobal_sel_codebook) :
		m_pFile_data(NULL),
		m_file_data_size(0),
		m_lowlevel_decoder(pGlobal_sel_codebook)
	{
	}

	bool basisu_transcoder::validate_file_checksums(const void* pData, uint32_t data_size, bool full_validation) const
	{
		if (!validate_header(pData, data_size))
			return false;

		const basis_file_header *pHeader = reinterpret_cast<const basis_file_header*>(pData);

#if !BASISU_NO_HEADER_OR_DATA_CRC16_CHECKS
		if (crc16(&pHeader->m_data_size, sizeof(basis_file_header) - BASISU_OFFSETOF(basis_file_header, m_data_size), 0) != pHeader->m_header_crc16)
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::get_total_images: header CRC check failed\n");		
			return false;
		}

		if (full_validation)
		{
			if (crc16(reinterpret_cast<const uint8_t*>(pData) + sizeof(basis_file_header), pHeader->m_data_size, 0) != pHeader->m_data_crc16)
			{
				BASISU_DEVEL_ERROR("basisu_transcoder::get_total_images: data CRC check failed\n");		
				return false;
			}
		}
#endif		

		return true;
	}
	
	bool basisu_transcoder::validate_header_quick(const void* pData, uint32_t data_size) const
	{
		if (data_size <= sizeof(basis_file_header))
			return false;

		const basis_file_header *pHeader = reinterpret_cast<const basis_file_header*>(pData);

		if ((pHeader->m_sig != basis_file_header::cBASISSigValue) || (pHeader->m_ver != BASISD_SUPPORTED_BASIS_VERSION) || (pHeader->m_header_size != sizeof(basis_file_header)))
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::get_total_images: header has an invalid signature, or file version is unsupported\n");		
			return false;
		}

		uint32_t expected_file_size = sizeof(basis_file_header) + pHeader->m_data_size;
		if (data_size < expected_file_size)
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::get_total_images: source buffer is too small\n");		
			return false;
		}

		if ((!pHeader->m_total_slices) || (!pHeader->m_total_images))
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::validate_header_quick: header is invalid\n");
			return false;
		}

		if ( (pHeader->m_slice_desc_file_ofs >= data_size) ||
			  ((data_size - pHeader->m_slice_desc_file_ofs) < (sizeof(basis_slice_desc) * pHeader->m_total_slices))
			)
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::validate_header_quick: passed in buffer is too small or data is corrupted\n");
			return false;
		}
							
		return true;
	}

	bool basisu_transcoder::validate_header(const void* pData, uint32_t data_size) const
	{
		if (data_size <= sizeof(basis_file_header))
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::get_total_images: input source buffer is too small\n");		
			return false;
		}

		const basis_file_header *pHeader = reinterpret_cast<const basis_file_header*>(pData);

		if ((pHeader->m_sig != basis_file_header::cBASISSigValue) || (pHeader->m_ver != BASISD_SUPPORTED_BASIS_VERSION) || (pHeader->m_header_size != sizeof(basis_file_header)))
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::get_total_images: header has an invalid signature, or file version is unsupported\n");		
			return false;
		}

		uint32_t expected_file_size = sizeof(basis_file_header) + pHeader->m_data_size;
		if (data_size < expected_file_size)
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::get_total_images: input source buffer is too small, or header is corrupted\n");		
			return false;
		}

		if ((!pHeader->m_total_images) || (!pHeader->m_total_slices))
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::get_total_images: invalid basis file (total images or slices are 0)\n");		
			return false;
		}

		if (pHeader->m_total_images > pHeader->m_total_slices)
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::get_total_images: invalid basis file (too many images)\n");		
			return false;
		}

		if (pHeader->m_flags & cBASISHeaderFlagHasAlphaSlices)
		{
			if (pHeader->m_total_slices & 1)
			{
				BASISU_DEVEL_ERROR("basisu_transcoder::get_total_images: invalid alpha basis file\n");		
				return false;
			}
		}

		if ((pHeader->m_flags & cBASISHeaderFlagETC1S) == 0)
		{
			// We only support ETC1S in basis universal
			BASISU_DEVEL_ERROR("basisu_transcoder::get_total_images: invalid basis file (ETC1S flag check)\n");		
			return false;
		}

		if ( (pHeader->m_slice_desc_file_ofs >= data_size) ||
			  ((data_size - pHeader->m_slice_desc_file_ofs) < (sizeof(basis_slice_desc) * pHeader->m_total_slices))
			)
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::validate_header_quick: passed in buffer is too small or data is corrupted\n");
			return false;
		}

		return true;
	}

	basis_texture_type basisu_transcoder::get_texture_type(const void *pData, uint32_t data_size) const
	{
		if (!validate_header_quick(pData, data_size))
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::get_texture_type: header validation failed\n");		
			return cBASISTexType2DArray;
		}

		const basis_file_header *pHeader = static_cast<const basis_file_header *>(pData);

		basis_texture_type btt = static_cast<basis_texture_type>(static_cast<uint8_t>(pHeader->m_tex_type));
		
		if (btt >= cBASISTexTypeTotal)
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::validate_header_quick: header's texture type field is invalid\n");
			return cBASISTexType2DArray;
		}

		return btt;
	}

	bool basisu_transcoder::get_userdata(const void *pData, uint32_t data_size, uint32_t &userdata0, uint32_t &userdata1) const
	{
		if (!validate_header_quick(pData, data_size))
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::get_userdata: header validation failed\n");		
			return false;
		}

		const basis_file_header *pHeader = static_cast<const basis_file_header *>(pData);

		userdata0 = pHeader->m_userdata0;
		userdata1 = pHeader->m_userdata1;
		return true;
	}
	
	uint32_t basisu_transcoder::get_total_images(const void *pData, uint32_t data_size) const
	{
		if (!validate_header_quick(pData, data_size))
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::get_total_images: header validation failed\n");		
			return 0;
		}

		const basis_file_header *pHeader = static_cast<const basis_file_header *>(pData);

		return pHeader->m_total_images;
	}

	bool basisu_transcoder::get_image_info(const void *pData, uint32_t data_size, basisu_image_info &image_info, uint32_t image_index) const
	{
		if (!validate_header_quick(pData, data_size))
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::get_image_info: header validation failed\n");		
			return false;
		}
				
		int slice_index = find_first_slice_index(pData, data_size, image_index, 0);
		if (slice_index < 0)
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::get_image_info: invalid slice index\n");		
			return false;
		}

		const basis_file_header *pHeader = static_cast<const basis_file_header *>(pData);

		if (image_index >= pHeader->m_total_images)
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::get_image_info: invalid image_index\n");		
			return false;
		}

		const basis_slice_desc *pSlice_descs = reinterpret_cast<const basis_slice_desc *>(static_cast<const uint8_t *>(pData) + pHeader->m_slice_desc_file_ofs);

		uint32_t total_levels = 1;
		for (uint32_t i = slice_index + 1; i < pHeader->m_total_slices; i++)
			if (pSlice_descs[i].m_image_index == image_index)
				total_levels = basisu::maximum<uint32_t>(total_levels, pSlice_descs[i].m_level_index + 1);
			else 
				break;

		if (total_levels > 16)
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::get_image_info: invalid image_index\n");		
			return false;
		}

		const basis_slice_desc &slice_desc = pSlice_descs[slice_index];

		image_info.m_image_index = image_index;
		image_info.m_total_levels = total_levels;
		image_info.m_alpha_flag = (pHeader->m_flags & cBASISHeaderFlagHasAlphaSlices) != 0;
		image_info.m_width = slice_desc.m_num_blocks_x * 4;
		image_info.m_height = slice_desc.m_num_blocks_y * 4;
		image_info.m_orig_width = slice_desc.m_orig_width;
		image_info.m_orig_height = slice_desc.m_orig_height;
		image_info.m_num_blocks_x = slice_desc.m_num_blocks_x;
		image_info.m_num_blocks_y = slice_desc.m_num_blocks_y;
		image_info.m_total_blocks = image_info.m_num_blocks_x * image_info.m_num_blocks_y;
		image_info.m_first_slice_index = slice_index;

		return true;
	}
	
	uint32_t basisu_transcoder::get_total_image_levels(const void *pData, uint32_t data_size, uint32_t image_index) const
	{
		if (!validate_header_quick(pData, data_size))
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::get_total_image_levels: header validation failed\n");		
			return false;
		}

		int slice_index = find_first_slice_index(pData, data_size, image_index, 0);
		if (slice_index < 0)
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::get_total_image_levels: failed finding slice\n");		
			return false;
		}

		const basis_file_header *pHeader = static_cast<const basis_file_header *>(pData);

		if (image_index >= pHeader->m_total_images)
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::get_total_image_levels: invalid image_index\n");		
			return false;
		}

		const basis_slice_desc *pSlice_descs = reinterpret_cast<const basis_slice_desc *>(static_cast<const uint8_t *>(pData) + pHeader->m_slice_desc_file_ofs);

		uint32_t total_levels = 1;
		for (uint32_t i = slice_index + 1; i < pHeader->m_total_slices; i++)
			if (pSlice_descs[i].m_image_index == image_index)
				total_levels = basisu::maximum<uint32_t>(total_levels, pSlice_descs[i].m_level_index + 1);
			else 
				break;

		if (total_levels > 16)
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::get_total_image_levels: invalid image levels!\n");		
			return false;
		}
				
		return total_levels;
	}
		
	bool basisu_transcoder::get_image_level_desc(const void *pData, uint32_t data_size, uint32_t image_index, uint32_t level_index, uint32_t &orig_width, uint32_t &orig_height, uint32_t &total_blocks) const
	{
		if (!validate_header_quick(pData, data_size))
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::get_image_level_desc: header validation failed\n");		
			return false;
		}
				
		int slice_index = find_first_slice_index(pData, data_size, image_index, level_index);
		if (slice_index < 0)
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::get_image_level_desc: failed finding slice\n");		
			return false;
		}

		const basis_file_header *pHeader = static_cast<const basis_file_header *>(pData);

		if (image_index >= pHeader->m_total_images)
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::get_image_level_desc: invalid image_index\n");		
			return false;
		}

		const basis_slice_desc *pSlice_descs = reinterpret_cast<const basis_slice_desc *>(static_cast<const uint8_t *>(pData) + pHeader->m_slice_desc_file_ofs);
		
		const basis_slice_desc &slice_desc = pSlice_descs[slice_index];

		orig_width = slice_desc.m_orig_width;
		orig_height = slice_desc.m_orig_height;
		total_blocks = slice_desc.m_num_blocks_x * slice_desc.m_num_blocks_y;
				
		return true;
	}

	bool basisu_transcoder::get_image_level_info(const void *pData, uint32_t data_size, basisu_image_level_info &image_info, uint32_t image_index, uint32_t level_index) const
	{
		if (!validate_header_quick(pData, data_size))
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::get_image_level_info: validate_file_checksums failed\n");		
			return false;
		}
				
		int slice_index = find_first_slice_index(pData, data_size, image_index, level_index);
		if (slice_index < 0)
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::get_image_level_info: failed finding slice\n");		
			return false;
		}

		const basis_file_header *pHeader = static_cast<const basis_file_header *>(pData);

		if (image_index >= pHeader->m_total_images)
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::get_image_level_info: invalid image_index\n");		
			return false;
		}

		const basis_slice_desc *pSlice_descs = reinterpret_cast<const basis_slice_desc *>(static_cast<const uint8_t *>(pData) + pHeader->m_slice_desc_file_ofs);

		const basis_slice_desc &slice_desc = pSlice_descs[slice_index];

		image_info.m_image_index = image_index;
		image_info.m_level_index = level_index;
		image_info.m_alpha_flag = (pHeader->m_flags & cBASISHeaderFlagHasAlphaSlices) != 0;
		image_info.m_width = slice_desc.m_num_blocks_x * 4;
		image_info.m_height = slice_desc.m_num_blocks_y * 4;
		image_info.m_orig_width = slice_desc.m_orig_width;
		image_info.m_orig_height = slice_desc.m_orig_height;
		image_info.m_num_blocks_x = slice_desc.m_num_blocks_x;
		image_info.m_num_blocks_y = slice_desc.m_num_blocks_y;
		image_info.m_total_blocks = image_info.m_num_blocks_x * image_info.m_num_blocks_y;
		image_info.m_first_slice_index = slice_index;

		return true;
	}

	bool basisu_transcoder::get_file_info(const void* pData, uint32_t data_size, basisu_file_info & file_info) const
	{
		if (!validate_file_checksums(pData, data_size, false))
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::get_file_info: validate_file_checksums failed\n");
			return false;
		}
				
		const basis_file_header* pHeader = static_cast<const basis_file_header*>(pData);
		const basis_slice_desc* pSlice_descs = reinterpret_cast<const basis_slice_desc*>(static_cast<const uint8_t*>(pData) + pHeader->m_slice_desc_file_ofs);
				
		file_info.m_version = pHeader->m_ver;

		file_info.m_total_header_size = sizeof(basis_file_header) + pHeader->m_total_slices * sizeof(basis_slice_desc);

		file_info.m_total_selectors = pHeader->m_total_selectors;
		file_info.m_selector_codebook_size = pHeader->m_selector_cb_file_size;

		file_info.m_total_endpoints = pHeader->m_total_endpoints;
		file_info.m_endpoint_codebook_size = pHeader->m_endpoint_cb_file_size;

		file_info.m_tables_size = pHeader->m_tables_file_size;

		file_info.m_etc1s = (pHeader->m_flags & cBASISHeaderFlagETC1S) != 0;
		file_info.m_y_flipped = (pHeader->m_flags & cBASISHeaderFlagYFlipped) != 0;
		file_info.m_has_alpha_slices = (pHeader->m_flags & cBASISHeaderFlagHasAlphaSlices) != 0;

		const uint32_t total_slices = pHeader->m_total_slices;

		file_info.m_slice_info.resize(total_slices);

		file_info.m_slices_size = 0;

		file_info.m_tex_type = static_cast<basis_texture_type>(static_cast<uint8_t>(pHeader->m_tex_type));

		if (file_info.m_tex_type > cBASISTexTypeTotal)
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::get_file_info: invalid texture type, file is corrupted\n");
			return false;
		}

		file_info.m_us_per_frame = pHeader->m_us_per_frame;
		file_info.m_userdata0 = pHeader->m_userdata0;
		file_info.m_userdata1 = pHeader->m_userdata1;
				
		file_info.m_image_mipmap_levels.resize(0);
		file_info.m_image_mipmap_levels.resize(pHeader->m_total_images);

		file_info.m_total_images = pHeader->m_total_images;

		for (uint32_t i = 0; i < total_slices; i++)
		{
			file_info.m_slices_size += pSlice_descs[i].m_file_size;

			basisu_slice_info& slice_info = file_info.m_slice_info[i];
						
			slice_info.m_orig_width = pSlice_descs[i].m_orig_width;
			slice_info.m_orig_height = pSlice_descs[i].m_orig_height;
			slice_info.m_width = pSlice_descs[i].m_num_blocks_x * 4;
			slice_info.m_height = pSlice_descs[i].m_num_blocks_y * 4;
			slice_info.m_num_blocks_x = pSlice_descs[i].m_num_blocks_x;
			slice_info.m_num_blocks_y = pSlice_descs[i].m_num_blocks_y;
			slice_info.m_total_blocks = slice_info.m_num_blocks_x * slice_info.m_num_blocks_y;
			slice_info.m_compressed_size = pSlice_descs[i].m_file_size;
			slice_info.m_slice_index = i;
			slice_info.m_image_index = pSlice_descs[i].m_image_index;
			slice_info.m_level_index = pSlice_descs[i].m_level_index;
			slice_info.m_unpacked_slice_crc16 = pSlice_descs[i].m_slice_data_crc16;
			slice_info.m_alpha_flag = (pSlice_descs[i].m_flags & cSliceDescFlagsIsAlphaData) != 0;

			if (pSlice_descs[i].m_image_index >= pHeader->m_total_images)
			{
				BASISU_DEVEL_ERROR("basisu_transcoder::get_file_info: slice desc's image index is invalid\n");
				return false;
			}

			file_info.m_image_mipmap_levels[pSlice_descs[i].m_image_index] = basisu::maximum<uint32_t>(file_info.m_image_mipmap_levels[pSlice_descs[i].m_image_index], pSlice_descs[i].m_level_index + 1);

			if (file_info.m_image_mipmap_levels[pSlice_descs[i].m_image_index] > 16)
			{
				BASISU_DEVEL_ERROR("basisu_transcoder::get_file_info: slice mipmap level is invalid\n");
				return false;
			}
		}

		return true;
	}

	bool basisu_transcoder::start_transcoding(const void *pData, uint32_t data_size) const
	{
		if (m_lowlevel_decoder.m_endpoints.size())
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::transcode_slice: already called start_transcoding\n");
			return true;
		}
	
		if (!validate_header_quick(pData, data_size))
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::transcode_slice: header validation failed\n");
			return false;
		}

		const basis_file_header *pHeader = reinterpret_cast<const basis_file_header*>(pData);

		const uint8_t *pDataU8 = static_cast<const uint8_t *>(pData);

		if (!pHeader->m_endpoint_cb_file_size || !pHeader->m_selector_cb_file_size || !pHeader->m_tables_file_size)
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::transcode_slice: file is corrupted (0)\n");
		}

		if ((pHeader->m_endpoint_cb_file_ofs > data_size) || (pHeader->m_selector_cb_file_ofs > data_size) || (pHeader->m_tables_file_ofs > data_size))
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::transcode_slice: file is corrupted or passed in buffer too small (1)\n");
			return false;
		}
		
		if (pHeader->m_endpoint_cb_file_size > (data_size - pHeader->m_endpoint_cb_file_ofs))
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::transcode_slice: file is corrupted or passed in buffer too small (2)\n");
			return false;
		}

		if (pHeader->m_selector_cb_file_size > (data_size - pHeader->m_selector_cb_file_ofs))
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::transcode_slice: file is corrupted or passed in buffer too small (3)\n");
			return false;
		}

		if (pHeader->m_tables_file_size > (data_size - pHeader->m_tables_file_ofs))
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::transcode_slice: file is corrupted or passed in buffer too small (3)\n");
			return false;
		}

		if (!m_lowlevel_decoder.decode_palettes(
			pHeader->m_total_endpoints, pDataU8 + pHeader->m_endpoint_cb_file_ofs, pHeader->m_endpoint_cb_file_size,
			pHeader->m_total_selectors, pDataU8 + pHeader->m_selector_cb_file_ofs, pHeader->m_selector_cb_file_size))
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::transcode_slice: decode_palettes failed\n");
			return false;
		}

		if (!m_lowlevel_decoder.decode_tables(pDataU8 + pHeader->m_tables_file_ofs, pHeader->m_tables_file_size))
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::transcode_slice: decode_tables failed\n");
			return false;
		}

		return true;
	}

	bool basisu_transcoder::transcode_slice(const void *pData, uint32_t data_size, uint32_t slice_index, void *pOutput_blocks, uint32_t output_blocks_buf_size_in_blocks, block_format fmt, 
		uint32_t output_stride, uint32_t decode_flags) const
	{
		if (!m_lowlevel_decoder.m_endpoints.size())
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::transcode_slice: must call start_transcoding first\n");
			return false;
		}
			
		if (decode_flags & cDecodeFlagsPVRTCDecodeToNextPow2)
		{
			// TODO: Not yet supported
			BASISU_DEVEL_ERROR("basisu_transcoder::transcode_slice: cDecodeFlagsPVRTCDecodeToNextPow2 currently unsupported\n");
			return false;
		}
		
		if (!validate_header_quick(pData, data_size))
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::transcode_slice: header validation failed\n");
			return false;
		}
			
		const basis_file_header *pHeader = reinterpret_cast<const basis_file_header *>(pData);

		const uint8_t *pDataU8 = static_cast<const uint8_t* >(pData);

		if (slice_index >= pHeader->m_total_slices)
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::transcode_slice: slice_index >= pHeader->m_total_slices\n");
			return false;
		}

		const basis_slice_desc &slice_desc = reinterpret_cast<const basis_slice_desc *>(pDataU8 + pHeader->m_slice_desc_file_ofs)[slice_index];

		uint32_t total_blocks = slice_desc.m_num_blocks_x * slice_desc.m_num_blocks_y;
		if (output_blocks_buf_size_in_blocks < total_blocks)
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::transcode_slice: output_blocks_buf_size_in_blocks < total_blocks\n");
			return false;
		}

		if (fmt != cETC1)
		{
			if (fmt == cPVRTC1_4_OPAQUE_ONLY)
			{
				if ((!basisu::is_pow2(slice_desc.m_num_blocks_x * 4)) || (!basisu::is_pow2(slice_desc.m_num_blocks_y * 4)))
				{
					// PVRTC1 only supports power of 2 dimensions
					BASISU_DEVEL_ERROR("basisu_transcoder::transcode_slice: PVRTC1 only supports power of 2 dimensions\n");
					return false;
				}
			}
		}

		if (slice_desc.m_file_ofs > data_size)
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::transcode_slice: invalid slice_desc.m_file_ofs, or passed in buffer too small\n");
			return false;
		}

		const uint32_t data_size_left = data_size - slice_desc.m_file_ofs;
		if (data_size_left < slice_desc.m_file_size)
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::transcode_slice: invalid slice_desc.m_file_size, or passed in buffer too small\n");
			return false;
		}
				
		return m_lowlevel_decoder.transcode_slice(pOutput_blocks, slice_desc.m_num_blocks_x, slice_desc.m_num_blocks_y,
			pDataU8 + slice_desc.m_file_ofs, slice_desc.m_file_size,
			fmt, output_stride, (decode_flags & cDecodeFlagsPVRTCWrapAddressing) != 0, (decode_flags & cDecodeFlagsBC1ForbidThreeColorBlocks) == 0);
	}

	int basisu_transcoder::find_first_slice_index(const void *pData, uint32_t data_size, uint32_t image_index, uint32_t level_index) const
	{
		(void)data_size;

		const basis_file_header *pHeader = reinterpret_cast<const basis_file_header*>(pData);
		const uint8_t *pDataU8 = static_cast<const uint8_t*>(pData);

		// For very large basis files this search could be painful
		// TODO: Binary search this
		for (uint32_t slice_iter = 0; slice_iter < pHeader->m_total_slices; slice_iter++)
		{
			const basis_slice_desc &slice_desc = reinterpret_cast<const basis_slice_desc *>(pDataU8 + pHeader->m_slice_desc_file_ofs)[slice_iter];
			if ((slice_desc.m_image_index == image_index) && (slice_desc.m_level_index == level_index))
				return slice_iter;
		}
		
		BASISU_DEVEL_ERROR("basisu_transcoder::find_first_slice_index: didn't find slice\n");

		return -1;
	}

	int basisu_transcoder::find_slice(const void *pData, uint32_t data_size, uint32_t image_index, uint32_t level_index, bool alpha_data) const
	{
		if (!validate_header_quick(pData, data_size))
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::find_slice: header validation failed\n");
			return false;
		}

		const basis_file_header *pHeader = reinterpret_cast<const basis_file_header*>(pData);
		const uint8_t *pDataU8 = static_cast<const uint8_t*>(pData);
		const basis_slice_desc *pSlice_descs = reinterpret_cast<const basis_slice_desc*>(pDataU8 + pHeader->m_slice_desc_file_ofs);

		// For very large basis files this search could be painful
		// TODO: Binary search this
		for (uint32_t slice_iter = 0; slice_iter < pHeader->m_total_slices; slice_iter++)
		{
			const basis_slice_desc& slice_desc = pSlice_descs[slice_iter];
			if ((slice_desc.m_image_index == image_index) && (slice_desc.m_level_index == level_index))
			{
				const bool slice_alpha = (slice_desc.m_flags & cSliceDescFlagsIsAlphaData) != 0;
				if (slice_alpha == alpha_data)
					return slice_iter;
			}
		}
		
		BASISU_DEVEL_ERROR("basisu_transcoder::find_slice: didn't find slice\n");

		return -1;
	}

	static void write_opaque_alpha_blocks(uint32_t total_slice_blocks, void *pOutput_blocks, uint32_t output_blocks_buf_size_in_blocks, block_format fmt, uint32_t stride)
	{
		BASISU_NOTE_UNUSED(output_blocks_buf_size_in_blocks);
		assert(total_slice_blocks <= output_blocks_buf_size_in_blocks);

		if (fmt == cETC2_EAC_A8)
		{
#if BASISD_SUPPORT_ETC2_EAC_A8
			eac_a8_block blk;
			blk.m_base = 255;
			blk.m_multiplier = 1;
			blk.m_table = 13;
			
			// Selectors are all 4's
			static const uint8_t s_etc2_eac_a8_sel4[6] = { 0x92, 0x49, 0x24, 0x92, 0x49, 0x24 };
			memcpy(&blk.m_selectors, s_etc2_eac_a8_sel4, sizeof(s_etc2_eac_a8_sel4));

			for (uint32_t i = 0; i < total_slice_blocks; i++)
			{
				memcpy((uint8_t *)pOutput_blocks + stride * i, &blk, sizeof(blk));
			}
#endif
		}
		else if (fmt == cBC4)
		{
#if BASISD_SUPPORT_DXT5A
			dxt5a_block blk;
			blk.m_endpoints[0] = 255;
			blk.m_endpoints[1] = 255;
			memset(blk.m_selectors, 0, sizeof(blk.m_selectors));
			
			for (uint32_t i = 0; i < total_slice_blocks; i++)
			{
				memcpy((uint8_t *)pOutput_blocks + stride * i, &blk, sizeof(blk));
			}
#endif
		}
	}
		
	bool basisu_transcoder::transcode_image_level(
		const void *pData, uint32_t data_size,
		uint32_t image_index, uint32_t level_index, 
		void *pOutput_blocks, uint32_t output_blocks_buf_size_in_blocks,
		transcoder_texture_format fmt,
		uint32_t decode_flags) const
	{
		if (!m_lowlevel_decoder.m_endpoints.size())
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::transcode_image_level: must call start_transcoding() first\n");
			return false;
		}
					
		const bool transcode_alpha_data_to_opaque_formats = (decode_flags & cDecodeFlagsTranscodeAlphaDataToOpaqueFormats) != 0;

		if (decode_flags & cDecodeFlagsPVRTCDecodeToNextPow2)
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::transcode_image_level: cDecodeFlagsPVRTCDecodeToNextPow2 currently unsupported\n");
			// TODO: Not yet supported
			return false;
		}

		if (!validate_header_quick(pData, data_size))
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::transcode_image_level: header validation failed\n");
			return false;
		}

		const basis_file_header *pHeader = reinterpret_cast<const basis_file_header*>(pData);

		const uint8_t *pDataU8 = static_cast<const uint8_t*>(pData);

		const basis_slice_desc *pSlice_descs = reinterpret_cast<const basis_slice_desc*>(pDataU8 + pHeader->m_slice_desc_file_ofs);

		const bool basis_file_has_alpha_slices = (pHeader->m_flags & cBASISHeaderFlagHasAlphaSlices) != 0;
		
		int slice_index = find_first_slice_index(pData, data_size, image_index, level_index);
		if (slice_index < 0)
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::transcode_image_level: failed finding slice index\n");
			// Unable to find the requested image/level 
			return false;
		}
				
		uint32_t total_slices = 1;
		switch (fmt)
		{
		case cTFETC2:
		case cTFBC3:
		case cTFBC5:
			total_slices = 2;
			break;
		default:
			break;
		}
				
		if (pSlice_descs[slice_index].m_flags & cSliceDescFlagsIsAlphaData)
		{
			BASISU_DEVEL_ERROR("basisu_transcoder::transcode_image_level: alpha basis file has out of order alpha slice\n");
			
			// The first slice shouldn't have alpha data in a properly formed basis file
			return false;
		}

		if (basis_file_has_alpha_slices)
		{
			// The alpha data should immediately follow the color data, and have the same resolution.
			if ((slice_index + 1U) >= pHeader->m_total_slices)
			{
				BASISU_DEVEL_ERROR("basisu_transcoder::transcode_image_level: alpha basis file has missing alpha slice\n");
				// basis file is missing the alpha slice
				return false;
			}

			// Basic sanity checks
			if ((pSlice_descs[slice_index + 1].m_flags & cSliceDescFlagsIsAlphaData) == 0)
			{
				BASISU_DEVEL_ERROR("basisu_transcoder::transcode_image_level: alpha basis file has missing alpha slice (flag check)\n");
				// This slice should have alpha data
				return false;
			}
						
			if ((pSlice_descs[slice_index].m_num_blocks_x != pSlice_descs[slice_index + 1].m_num_blocks_x) || (pSlice_descs[slice_index].m_num_blocks_y != pSlice_descs[slice_index + 1].m_num_blocks_y))
			{
				BASISU_DEVEL_ERROR("basisu_transcoder::transcode_image_level: alpha basis file slice dimensions bad\n");
				// Alpha slice should have been the same res as the color slice
				return false;
			}
		}

		uint32_t bytes_per_block = 8;
		switch (fmt)
		{
		case cTFBC7_M6_OPAQUE_ONLY:
		case cTFETC2:
		case cTFBC3:
		case cTFBC5:
			bytes_per_block = 16;
			break;
		default:
			break;
		}

		bool status = false;

		const uint32_t total_slice_blocks = pSlice_descs[slice_index].m_num_blocks_x * pSlice_descs[slice_index].m_num_blocks_y;
								
		switch (fmt)
		{
		case cTFETC1:
		{
			assert(total_slices == 1);
			
			uint32_t slice_index_to_decode = slice_index;
			// If the caller wants us to transcode the mip level's alpha data, then use the next slice.
			if ((basis_file_has_alpha_slices) && (transcode_alpha_data_to_opaque_formats))
				slice_index_to_decode++;

			status = transcode_slice(pData, data_size, slice_index_to_decode, pOutput_blocks, output_blocks_buf_size_in_blocks, cETC1, bytes_per_block, decode_flags);
			if (!status)
			{
				BASISU_DEVEL_ERROR("basisu_transcoder::transcode_image_level: transcode_slice() to ETC1 failed\n");
			}
			break;
		}
		case cTFBC1:
		{
#if !BASISD_SUPPORT_DXT1
			return false;
#endif
			assert(total_slices == 1);

			uint32_t slice_index_to_decode = slice_index;
			// If the caller wants us to transcode the mip level's alpha data, then use the next slice.
			if ((basis_file_has_alpha_slices) && (transcode_alpha_data_to_opaque_formats))
				slice_index_to_decode++;

			status = transcode_slice(pData, data_size, slice_index_to_decode, pOutput_blocks, output_blocks_buf_size_in_blocks, cBC1, bytes_per_block, decode_flags);
			if (!status)
			{
				BASISU_DEVEL_ERROR("basisu_transcoder::transcode_image_level: transcode_slice() to BC1 failed\n");
			}
			break;
		}
		case cTFBC4:
		{
#if !BASISD_SUPPORT_DXT5A
			return false;
#endif
			assert(total_slices == 1);

			uint32_t slice_index_to_decode = slice_index;
			// If the caller wants us to transcode the mip level's alpha data, then use the next slice.
			if ((basis_file_has_alpha_slices) && (transcode_alpha_data_to_opaque_formats))
				slice_index_to_decode++;

			status = transcode_slice(pData, data_size, slice_index_to_decode, pOutput_blocks, output_blocks_buf_size_in_blocks, cBC4, bytes_per_block, decode_flags);
			if (!status)
			{
				BASISU_DEVEL_ERROR("basisu_transcoder::transcode_image_level: transcode_slice() to BC4 failed\n");
			}
			break;
		}
		case cTFPVRTC1_4_OPAQUE_ONLY:
		{
#if !BASISD_SUPPORT_PVRTC1
			return false;
#endif
			assert(total_slices == 1);

			uint32_t slice_index_to_decode = slice_index;
			// If the caller wants us to transcode the mip level's alpha data, then use the next slice.
			if ((basis_file_has_alpha_slices) && (transcode_alpha_data_to_opaque_formats))
				slice_index_to_decode++;

			status = transcode_slice(pData, data_size, slice_index_to_decode, pOutput_blocks, output_blocks_buf_size_in_blocks, cPVRTC1_4_OPAQUE_ONLY, bytes_per_block, decode_flags);
			if (!status)
			{
				BASISU_DEVEL_ERROR("basisu_transcoder::transcode_image_level: transcode_slice() to PVRTC1 4 opaque only failed\n");
			}
			break;
		}
		case cTFBC7_M6_OPAQUE_ONLY:
		{
#if !BASISD_SUPPORT_BC7
			return false;
#endif
			assert(total_slices == 1);
			
			uint32_t slice_index_to_decode = slice_index;
			// If the caller wants us to transcode the mip level's alpha data, then use the next slice.
			if ((basis_file_has_alpha_slices) && (transcode_alpha_data_to_opaque_formats))
				slice_index_to_decode++;

			status = transcode_slice(pData, data_size, slice_index_to_decode, pOutput_blocks, output_blocks_buf_size_in_blocks, cBC7_M6_OPAQUE_ONLY, bytes_per_block, decode_flags);
			if (!status)
			{
				BASISU_DEVEL_ERROR("basisu_transcoder::transcode_image_level: transcode_slice() to BC7 m6 opaque only failed\n");
			}
			break;
		}
		case cTFETC2:
		{
#if !BASISD_SUPPORT_ETC2_EAC_A8
			return false;
#endif
			assert(total_slices == 2);
			
			if (basis_file_has_alpha_slices)
			{
				// First decode the alpha data 
				status = transcode_slice(pData, data_size, slice_index + 1, pOutput_blocks, output_blocks_buf_size_in_blocks, cETC2_EAC_A8, 16, decode_flags);
			}
			else
			{
				write_opaque_alpha_blocks(total_slice_blocks, pOutput_blocks, output_blocks_buf_size_in_blocks, cETC2_EAC_A8, 16);
				status = true;
			}

			if (status)
			{
				// Now decode the color data
				status = transcode_slice(pData, data_size, slice_index, (uint8_t*)pOutput_blocks + 8, output_blocks_buf_size_in_blocks, cETC1, 16, decode_flags);
				if (!status)
				{
					BASISU_DEVEL_ERROR("basisu_transcoder::transcode_image_level: transcode_slice() to ETC2 RGB failed\n");
				}
			}
			else
			{
				BASISU_DEVEL_ERROR("basisu_transcoder::transcode_image_level: transcode_slice() to ETC2 A failed\n");
			}
			break;
		}
		case cTFBC3:
		{
#if !BASISD_SUPPORT_DXT1
			return false;
#endif
#if !BASISD_SUPPORT_DXT5A
			return false;
#endif
			assert(total_slices == 2);

			// First decode the alpha data 
			if (basis_file_has_alpha_slices)
			{
				status = transcode_slice(pData, data_size, slice_index + 1, pOutput_blocks, output_blocks_buf_size_in_blocks, cBC4, 16, decode_flags);
			}
			else
			{
				write_opaque_alpha_blocks(total_slice_blocks, pOutput_blocks, output_blocks_buf_size_in_blocks, cBC4, 16);
				status = true;
			}

			if (status)
			{
				// Now decode the color data. Forbid 3 color blocks, which aren't allowed in BC3.
				status = transcode_slice(pData, data_size, slice_index, (uint8_t*)pOutput_blocks + 8, output_blocks_buf_size_in_blocks, cBC1, 16, decode_flags | cDecodeFlagsBC1ForbidThreeColorBlocks);
				if (!status)
				{
					BASISU_DEVEL_ERROR("basisu_transcoder::transcode_image_level: transcode_slice() to BC3 RGB failed\n");
				}
			}
			else
			{
				BASISU_DEVEL_ERROR("basisu_transcoder::transcode_image_level: transcode_slice() to BC3 A failed\n");
			}

			break;
		}
		case cTFBC5:
		{
#if !BASISD_SUPPORT_DXT5A
			return false;
#endif
			assert(total_slices == 2);
			// Decode the R data (actually the green channel of the color data slice in the basis file)
			status = transcode_slice(pData, data_size, slice_index, pOutput_blocks, output_blocks_buf_size_in_blocks, cBC4, 16, decode_flags);
			if (status)
			{
				if (basis_file_has_alpha_slices)
				{
					// Decode the G data (actually the green channel of the alpha data slice in the basis file)
					status = transcode_slice(pData, data_size, slice_index + 1, (uint8_t*)pOutput_blocks + 8, output_blocks_buf_size_in_blocks, cBC4, 16, decode_flags);
					if (!status)
					{
						BASISU_DEVEL_ERROR("basisu_transcoder::transcode_image_level: transcode_slice() to BC5 1 failed\n");
					}
				}
				else
				{
					write_opaque_alpha_blocks(total_slice_blocks, (uint8_t*)pOutput_blocks + 8, output_blocks_buf_size_in_blocks, cBC4, 16);
					status = true;
				}
			}
			else
			{
				BASISU_DEVEL_ERROR("basisu_transcoder::transcode_image_level: transcode_slice() to BC5 channel 0 failed\n");
			}
			break;
		}
		default:
		{
			assert(0);
			BASISU_DEVEL_ERROR("basisu_transcoder::transcode_image_level: Invalid fmt\n");
			break;
		}
		}

		return status;
	}

	uint32_t basis_get_bytes_per_block(transcoder_texture_format fmt)
	{
		switch (fmt)
		{
		case cTFETC1:
		case cTFBC1:
		case cTFBC4:
		case cTFPVRTC1_4_OPAQUE_ONLY:
			return 8;
		case cTFBC7_M6_OPAQUE_ONLY:
		case cTFETC2:
		case cTFBC3:
		case cTFBC5:
			return 16;
		default:
			assert(0);
			BASISU_DEVEL_ERROR("basis_get_basisu_texture_format: Invalid fmt\n");
			break;
		}
		return 0;
	}

	const char *basis_get_format_name(transcoder_texture_format fmt)
	{
		switch (fmt)
		{
		case cTFETC1: return "ETC1";
		case cTFBC1: return "BC1";
		case cTFBC4: return "BC4";
		case cTFPVRTC1_4_OPAQUE_ONLY: return "PVRTC1_4_OPAQUE_ONLY";
		case cTFBC7_M6_OPAQUE_ONLY: return "BC7_M6_OPAQUE_ONLY";
		case cTFETC2: return "ETC2";
		case cTFBC3: return "BC3";
		case cTFBC5: return "BC5";
		default:
			assert(0);
			BASISU_DEVEL_ERROR("basis_get_basisu_texture_format: Invalid fmt\n");
			break;
		}
		return "";
	}

	const char *basis_get_texture_type_name(basis_texture_type tex_type)
	{
		switch (tex_type)
		{
			case cBASISTexType2D: return "2D";
			case cBASISTexType2DArray: return "2D array";
			case cBASISTexTypeCubemapArray: return "cubemap array";
			case cBASISTexTypeVideoFrames: return "video";
			case cBASISTexTypeVolume: return "3D";
			default: 
				assert(0);
				BASISU_DEVEL_ERROR("basis_get_texture_type_name: Invalid tex_type\n");
				break;
		}
		return "";
	}

	bool basis_transcoder_format_has_alpha(transcoder_texture_format fmt)
	{
		switch (fmt)
		{
		case cTFETC2: 
		case cTFBC3: 
			return true;
		default:
			break;
		}
		return false;
	}

	basisu::texture_format basis_get_basisu_texture_format(transcoder_texture_format fmt)
	{
		switch (fmt)
		{
			case cTFETC1: return basisu::cETC1;
			case cTFBC1: return basisu::cBC1;
			case cTFBC4: return basisu::cBC4;
			case cTFPVRTC1_4_OPAQUE_ONLY: return basisu::cPVRTC1_4_RGB;
			case cTFBC7_M6_OPAQUE_ONLY: return basisu::cBC7;
			case cTFETC2: return basisu::cETC2_RGBA;
			case cTFBC3: return basisu::cBC3;
			case cTFBC5: return basisu::cBC5;
			default:
				assert(0);
				BASISU_DEVEL_ERROR("basis_get_basisu_texture_format: Invalid fmt\n");
				break;
		}
		return basisu::cInvalidTextureFormat;
	}

} // namespace basist

