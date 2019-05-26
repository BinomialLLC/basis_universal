// basisu_gpu_texture.cpp
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
#include "basisu_gpu_texture.h"
#include "basisu_enc.h"
#include "basisu_pvrtc1_4.h"

namespace basisu
{
	const int8_t g_etc2_eac_tables[16][8] = 
	{
		{ -3, -6, -9, -15, 2, 5, 8, 14 }, { -3, -7, -10, -13, 2, 6, 9, 12 }, { -2, -5, -8, -13, 1, 4, 7, 12 }, { -2, -4, -6, -13, 1, 3, 5, 12 },
		{ -3, -6, -8, -12, 2, 5, 7, 11 }, { -3, -7, -9, -11, 2, 6, 8, 10 }, { -4, -7, -8, -11, 3, 6, 7, 10 }, { -3, -5, -8, -11, 2, 4, 7, 10 },
		{ -2, -6, -8, -10, 1, 5, 7, 9 }, { -2, -5, -8, -10, 1, 4, 7, 9 }, { -2, -4, -8, -10, 1, 3, 7, 9 }, { -2, -5, -7, -10, 1, 4, 6, 9 },
		{ -3, -4, -7, -10, 2, 3, 6, 9 }, { -1, -2, -3, -10, 0, 1, 2, 9 }, { -4, -6, -8, -9, 3, 5, 7, 8 }, { -3, -5, -7, -9, 2, 4, 6, 8 }
	};

	struct eac_a8_block
	{
		uint16_t m_base : 8;
		uint16_t m_table : 4;
		uint16_t m_multiplier : 4;

		uint8_t m_selectors[6];

		inline uint32_t get_selector(uint32_t x, uint32_t y, uint64_t selector_bits) const
		{
			assert((x < 4) && (y < 4));
			return static_cast<uint32_t>((selector_bits >> (45 - (y + x * 4) * 3)) & 7);
		}
				
		inline uint64_t get_selector_bits() const
		{
			uint64_t pixels = ((uint64_t)m_selectors[0] << 40) | ((uint64_t)m_selectors[1] << 32) | ((uint64_t)m_selectors[2] << 24) |	((uint64_t)m_selectors[3] << 16) | ((uint64_t)m_selectors[4] << 8) | m_selectors[5];
			return pixels;
		}
	};
		
	void unpack_etc2_eac(const void *pBlock_bits, color_rgba *pPixels)
	{
		static_assert(sizeof(eac_a8_block) == 8, "sizeof(eac_a8_block) == 8");

		const eac_a8_block *pBlock = static_cast<const eac_a8_block *>(pBlock_bits);

		const int8_t *pTable = g_etc2_eac_tables[pBlock->m_table];
		
		const uint64_t selector_bits = pBlock->get_selector_bits();
		
		const int32_t base = pBlock->m_base;
		const int32_t mul = pBlock->m_multiplier;

		pPixels[0].a = clamp255(base + pTable[pBlock->get_selector(0, 0, selector_bits)] * mul);
		pPixels[1].a = clamp255(base + pTable[pBlock->get_selector(1, 0, selector_bits)] * mul);
		pPixels[2].a = clamp255(base + pTable[pBlock->get_selector(2, 0, selector_bits)] * mul);
		pPixels[3].a = clamp255(base + pTable[pBlock->get_selector(3, 0, selector_bits)] * mul);

		pPixels[4].a = clamp255(base + pTable[pBlock->get_selector(0, 1, selector_bits)] * mul);
		pPixels[5].a = clamp255(base + pTable[pBlock->get_selector(1, 1, selector_bits)] * mul);
		pPixels[6].a = clamp255(base + pTable[pBlock->get_selector(2, 1, selector_bits)] * mul);
		pPixels[7].a = clamp255(base + pTable[pBlock->get_selector(3, 1, selector_bits)] * mul);

		pPixels[8].a = clamp255(base + pTable[pBlock->get_selector(0, 2, selector_bits)] * mul);
		pPixels[9].a = clamp255(base + pTable[pBlock->get_selector(1, 2, selector_bits)] * mul);
		pPixels[10].a = clamp255(base + pTable[pBlock->get_selector(2, 2, selector_bits)] * mul);
		pPixels[11].a = clamp255(base + pTable[pBlock->get_selector(3, 2, selector_bits)] * mul);

		pPixels[12].a = clamp255(base + pTable[pBlock->get_selector(0, 3, selector_bits)] * mul);
		pPixels[13].a = clamp255(base + pTable[pBlock->get_selector(1, 3, selector_bits)] * mul);
		pPixels[14].a = clamp255(base + pTable[pBlock->get_selector(2, 3, selector_bits)] * mul);
		pPixels[15].a = clamp255(base + pTable[pBlock->get_selector(3, 3, selector_bits)] * mul);
	}

	struct bc1_block
	{
		enum { cTotalEndpointBytes = 2, cTotalSelectorBytes = 4 };

		uint8_t m_low_color[cTotalEndpointBytes];
		uint8_t m_high_color[cTotalEndpointBytes];
		uint8_t m_selectors[cTotalSelectorBytes];
				
		inline uint32_t get_high_color() const	{ return m_high_color[0] | (m_high_color[1] << 8U); }
		inline uint32_t get_low_color() const { return m_low_color[0] | (m_low_color[1] << 8U); }

		static void unpack_color(uint32_t c, uint32_t &r, uint32_t &g, uint32_t &b) 
		{
			r = (c >> 11) & 31;
			g = (c >> 5) & 63;
			b = c & 31;
			
			r = (r << 3) | (r >> 2);
			g = (g << 2) | (g >> 4);
			b = (b << 3) | (b >> 2);
		}

		inline uint32_t get_selector(uint32_t x, uint32_t y) const { assert((x < 4U) && (y < 4U)); return (m_selectors[y] >> (x * 2)) & 3; }
	};

	// Returns true if the block uses 3 color punchthrough alpha mode.
	bool unpack_bc1(const void *pBlock_bits, color_rgba *pPixels, bool set_alpha)
	{
		static_assert(sizeof(bc1_block) == 8, "sizeof(bc1_block) == 8");

		const bc1_block *pBlock = static_cast<const bc1_block *>(pBlock_bits);

		const uint32_t l = pBlock->get_low_color();
		const uint32_t h = pBlock->get_high_color();

		color_rgba c[4];

		uint32_t r0, g0, b0, r1, g1, b1;
		bc1_block::unpack_color(l, r0, g0, b0);
		bc1_block::unpack_color(h, r1, g1, b1);

		bool used_punchthrough = false;

		if (l > h)
		{
			c[0].set_noclamp_rgba(r0, g0, b0, 255);
			c[1].set_noclamp_rgba(r1, g1, b1, 255);
			c[2].set_noclamp_rgba((r0 * 2 + r1) / 3, (g0 * 2 + g1) / 3, (b0 * 2 + b1) / 3, 255);
			c[3].set_noclamp_rgba((r1 * 2 + r0) / 3, (g1 * 2 + g0) / 3, (b1 * 2 + b0) / 3, 255);
		}
		else
		{
			c[0].set_noclamp_rgba(r0, g0, b0, 255);
			c[1].set_noclamp_rgba(r1, g1, b1, 255);
			c[2].set_noclamp_rgba((r0 + r1) / 2, (g0 + g1) / 2, (b0 + b1) / 2, 255);
			c[3].set_noclamp_rgba(0, 0, 0, 0);
			used_punchthrough = true;
		}

		if (set_alpha)
		{
			for (uint32_t y = 0; y < 4; y++, pPixels += 4)
			{
				pPixels[0] = c[pBlock->get_selector(0, y)]; 
				pPixels[1] = c[pBlock->get_selector(1, y)]; 
				pPixels[2] = c[pBlock->get_selector(2, y)]; 
				pPixels[3] = c[pBlock->get_selector(3, y)];
			}
		}
		else
		{
			for (uint32_t y = 0; y < 4; y++, pPixels += 4)
			{
				pPixels[0].set_rgb(c[pBlock->get_selector(0, y)]); 
				pPixels[1].set_rgb(c[pBlock->get_selector(1, y)]); 
				pPixels[2].set_rgb(c[pBlock->get_selector(2, y)]); 
				pPixels[3].set_rgb(c[pBlock->get_selector(3, y)]);
			}
		}

		return used_punchthrough;
	}

	struct bc4_block
	{
		enum { cBC4SelectorBits = 3, cTotalSelectorBytes = 6, cMaxSelectorValues = 8 };
		uint8_t m_endpoints[2];

		uint8_t m_selectors[cTotalSelectorBytes];

		inline uint32_t get_low_alpha() const { return m_endpoints[0]; }
		inline uint32_t get_high_alpha() const { return m_endpoints[1]; }
		inline bool is_alpha6_block() const { return get_low_alpha() <= get_high_alpha(); }

		inline uint64_t get_selector_bits() const
		{ 
			return ((uint64_t)((uint32_t)m_selectors[0] | ((uint32_t)m_selectors[1] << 8U) | ((uint32_t)m_selectors[2] << 16U) | ((uint32_t)m_selectors[3] << 24U))) |
				(((uint64_t)m_selectors[4]) << 32U) |
				(((uint64_t)m_selectors[5]) << 40U);
		}

		inline uint32_t get_selector(uint32_t x, uint32_t y, uint64_t selector_bits) const
		{
			assert((x < 4U) && (y < 4U));
			return (selector_bits >> (((y * 4) + x) * cBC4SelectorBits)) & (cMaxSelectorValues - 1);
		}
				
		static inline uint32_t get_block_values6(uint8_t *pDst, uint32_t l, uint32_t h)
		{
			pDst[0] = static_cast<uint8_t>(l);
			pDst[1] = static_cast<uint8_t>(h);
			pDst[2] = static_cast<uint8_t>((l * 4 + h) / 5);
			pDst[3] = static_cast<uint8_t>((l * 3 + h * 2) / 5);
			pDst[4] = static_cast<uint8_t>((l * 2 + h * 3) / 5);
			pDst[5] = static_cast<uint8_t>((l + h * 4) / 5);
			pDst[6] = 0;
			pDst[7] = 255;
			return 6;
		}

		static inline uint32_t get_block_values8(uint8_t *pDst, uint32_t l, uint32_t h)
		{
			pDst[0] = static_cast<uint8_t>(l);
			pDst[1] = static_cast<uint8_t>(h);
			pDst[2] = static_cast<uint8_t>((l * 6 + h) / 7);
			pDst[3] = static_cast<uint8_t>((l * 5 + h * 2) / 7);
			pDst[4] = static_cast<uint8_t>((l * 4 + h * 3) / 7);
			pDst[5] = static_cast<uint8_t>((l * 3 + h * 4) / 7);
			pDst[6] = static_cast<uint8_t>((l * 2 + h * 5) / 7);
			pDst[7] = static_cast<uint8_t>((l + h * 6) / 7);
			return 8;
		}

		static inline uint32_t get_block_values(uint8_t *pDst, uint32_t l, uint32_t h)
		{
			if (l > h)
				return get_block_values8(pDst, l, h);
			else
				return get_block_values6(pDst, l, h);
		}
	};

	void unpack_bc4(const void *pBlock_bits, uint8_t *pPixels, uint32_t stride)
	{
		static_assert(sizeof(bc4_block) == 8, "sizeof(bc4_block) == 8");

		const bc4_block *pBlock = static_cast<const bc4_block *>(pBlock_bits);

		uint8_t sel_values[8];
		bc4_block::get_block_values(sel_values, pBlock->get_low_alpha(), pBlock->get_high_alpha());

		const uint64_t selector_bits = pBlock->get_selector_bits();

		for (uint32_t y = 0; y < 4; y++, pPixels += (stride * 4U))
		{
			pPixels[0] = sel_values[pBlock->get_selector(0, y, selector_bits)];
			pPixels[stride * 1] = sel_values[pBlock->get_selector(1, y, selector_bits)];
			pPixels[stride * 2] = sel_values[pBlock->get_selector(2, y, selector_bits)];
			pPixels[stride * 3] = sel_values[pBlock->get_selector(3, y, selector_bits)];
		}
	}
	
	// Returns false if the block uses 3-color punchthrough alpha mode, which isn't supported on some GPU's for BC3.
	bool unpack_bc3(const void *pBlock_bits, color_rgba *pPixels)
	{
		bool success = true;

		if (unpack_bc1((const uint8_t *)pBlock_bits + sizeof(bc4_block), pPixels, true))
			success = false;

		unpack_bc4(pBlock_bits, &pPixels[0].a, sizeof(color_rgba));
		
		return success;
	}

	// writes RG
	void unpack_bc5(const void *pBlock_bits, color_rgba *pPixels)
	{
		unpack_bc4(pBlock_bits, &pPixels[0].r, sizeof(color_rgba));
		unpack_bc4((const uint8_t *)pBlock_bits + sizeof(bc4_block), &pPixels[0].g, sizeof(color_rgba));
	}

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

	static const uint32_t g_bc7_weights4[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };

	// The transcoder only outputs mode 6 at the moment, so this is easy.
	bool unpack_bc7_mode6(const void *pBlock_bits, color_rgba *pPixels)
	{
		static_assert(sizeof(bc7_mode_6) == 16, "sizeof(bc7_mode_6) == 16");

		const bc7_mode_6 &block = *static_cast<const bc7_mode_6 *>(pBlock_bits);

		if (block.m_lo.m_mode != (1 << 6))
			return false;

		const uint32_t r0 = (uint32_t)((block.m_lo.m_r0 << 1) | block.m_lo.m_p0);
		const uint32_t g0 = (uint32_t)((block.m_lo.m_g0 << 1) | block.m_lo.m_p0);
		const uint32_t b0 = (uint32_t)((block.m_lo.m_b0 << 1) | block.m_lo.m_p0);
		const uint32_t a0 = (uint32_t)((block.m_lo.m_a0 << 1) | block.m_lo.m_p0);
		const uint32_t r1 = (uint32_t)((block.m_lo.m_r1 << 1) | block.m_hi.m_p1);
		const uint32_t g1 = (uint32_t)((block.m_lo.m_g1 << 1) | block.m_hi.m_p1);
		const uint32_t b1 = (uint32_t)((block.m_lo.m_b1 << 1) | block.m_hi.m_p1);
		const uint32_t a1 = (uint32_t)((block.m_lo.m_a1 << 1) | block.m_hi.m_p1);

		color_rgba vals[16];
		for (uint32_t i = 0; i < 16; i++)
		{
			const uint32_t w = g_bc7_weights4[i];
			const uint32_t iw = 64 - w;
			vals[i].set_noclamp_rgba( 
				(r0 * iw + r1 * w + 32) >> 6, 
				(g0 * iw + g1 * w + 32) >> 6, 
				(b0 * iw + b1 * w + 32) >> 6, 
				(a0 * iw + a1 * w + 32) >> 6);
		}

		pPixels[0] = vals[block.m_hi.m_s00];
		pPixels[1] = vals[block.m_hi.m_s10];
		pPixels[2] = vals[block.m_hi.m_s20];
		pPixels[3] = vals[block.m_hi.m_s30];

		pPixels[4] = vals[block.m_hi.m_s01];
		pPixels[5] = vals[block.m_hi.m_s11];
		pPixels[6] = vals[block.m_hi.m_s21];
		pPixels[7] = vals[block.m_hi.m_s31];
		
		pPixels[8] = vals[block.m_hi.m_s02];
		pPixels[9] = vals[block.m_hi.m_s12];
		pPixels[10] = vals[block.m_hi.m_s22];
		pPixels[11] = vals[block.m_hi.m_s32];

		pPixels[12] = vals[block.m_hi.m_s03];
		pPixels[13] = vals[block.m_hi.m_s13];
		pPixels[14] = vals[block.m_hi.m_s23];
		pPixels[15] = vals[block.m_hi.m_s33];

		return true;
	}
	
	// Unpacks to RGBA, R, RG, or A
	bool unpack_block(texture_format fmt, const void* pBlock, color_rgba* pPixels)
	{
		switch (fmt)
		{
		case cBC1:
		{
			unpack_bc1(pBlock, pPixels, true);
			break;
		}
		case cBC3:
		{
			return unpack_bc3(pBlock, pPixels);
		}
		case cBC4:
		{
			// Unpack to R
			unpack_bc4(pBlock, &pPixels[0].r, sizeof(color_rgba));
			break;
		}
		case cBC5:
		{
			unpack_bc5(pBlock, pPixels);
			break;
		}
		case cBC7:
		{
			return unpack_bc7_mode6(pBlock, pPixels);
		}
		// Full ETC2 color blocks (planar/T/H modes) is currently unsupported in basisu, but we do support ETC2 with alpha (using ETC1 for color)
		case cETC2_RGB:
		case cETC1:
		case cETC1S:
		{
			return unpack_etc1(*static_cast<const etc_block*>(pBlock), pPixels);
			break;
		}
		case cETC2_RGBA:
		{
			if (!unpack_etc1(static_cast<const etc_block*>(pBlock)[1], pPixels))
				return false;
			unpack_etc2_eac(pBlock, pPixels);
			break;
		}
		case cETC2_ALPHA:
		{
			// Unpack to A
			unpack_etc2_eac(pBlock, pPixels);
			break;
		}
		default:
		{
			assert(0);
			// TODO
			return false;
		}
		}
		return true;
	}

	bool gpu_image::unpack(image& img, bool pvrtc_wrap_addressing) const
	{
		img.resize(get_width(), get_height());
		img.set_all(g_black_color);

		if (!img.get_width() || !img.get_height())
			return true;

		if ((m_fmt == cPVRTC1_4_RGB) || (m_fmt == cPVRTC1_4_RGBA))
		{
			pvrtc4_image pi(m_width, m_height, pvrtc_wrap_addressing);
			
			if (get_total_blocks() != pi.get_total_blocks())
				return false;
			
			memcpy(&pi.get_blocks()[0], get_ptr(), get_size_in_bytes());

			pi.deswizzle();

			pi.unpack_all_pixels(img);

			return true;
		}

		color_rgba pixels[cMaxBlockSize * cMaxBlockSize];
		for (uint32_t i = 0; i < cMaxBlockSize * cMaxBlockSize; i++)
			pixels[i] = g_black_color;

		bool success = true;

		for (uint32_t by = 0; by < m_blocks_y; by++)
		{
			for (uint32_t bx = 0; bx < m_blocks_x; bx++)
			{
				const void* pBlock = get_block_ptr(bx, by);

				if (!unpack_block(m_fmt, pBlock, pixels))
					success = false;

				img.set_block_clipped(pixels, bx * m_block_width, by * m_block_height, m_block_width, m_block_height);
			} // bx
		} // by

		return success;
	}
		
	static const uint8_t g_ktx_file_id[12] = { 0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A };

	// KTX/GL enums
	enum
	{
		KTX_ENDIAN = 0x04030201, 
		KTX_OPPOSITE_ENDIAN = 0x01020304,
		KTX_ETC1_RGB8_OES = 0x8D64,
		KTX_RED = 0x1903,
		KTX_RG = 0x8227,
		KTX_RGB = 0x1907,
		KTX_RGBA = 0x1908,
		KTX_COMPRESSED_RGB_S3TC_DXT1_EXT = 0x83F0,
		KTX_COMPRESSED_RGBA_S3TC_DXT5_EXT = 0x83F3,
		KTX_COMPRESSED_RED_RGTC1_EXT = 0x8DBB,
		KTX_COMPRESSED_RED_GREEN_RGTC2_EXT = 0x8DBD,
		KTX_COMPRESSED_RGB8_ETC2 = 0x9274,
		KTX_COMPRESSED_RGBA8_ETC2_EAC = 0x9278,
		KTX_COMPRESSED_RGBA_BPTC_UNORM_ARB,
		KTX_COMPRESSED_RGB_PVRTC_4BPPV1_IMG = 0x8C00,
		KTX_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG,
	};
		
	struct ktx_header
	{
		uint8_t m_identifier[12];
		packed_uint<4> m_endianness;
		packed_uint<4> m_glType;
		packed_uint<4> m_glTypeSize;
		packed_uint<4> m_glFormat;
		packed_uint<4> m_glInternalFormat;
		packed_uint<4> m_glBaseInternalFormat;
		packed_uint<4> m_pixelWidth;
		packed_uint<4> m_pixelHeight;
		packed_uint<4> m_pixelDepth;
		packed_uint<4> m_numberOfArrayElements;
		packed_uint<4> m_numberOfFaces;
		packed_uint<4> m_numberOfMipmapLevels;
		packed_uint<4> m_bytesOfKeyValueData;

		void clear() { clear_obj(*this);	}
	};

	// Input is a texture array of mipmapped gpu_image's: gpu_images[array_index][level_index]
	bool create_ktx_texture_file(uint8_vec &ktx_data, const std::vector<gpu_image_vec>& gpu_images, bool cubemap_flag)
	{
		if (!gpu_images.size())
		{
			assert(0);
			return false;
		}

		uint32_t width = 0, height = 0, total_levels = 0;
		basisu::texture_format fmt = cInvalidTextureFormat;

		if (cubemap_flag)
		{
			if ((gpu_images.size() % 6) != 0)
			{
				assert(0);
				return false;
			}
		}

		for (uint32_t array_index = 0; array_index < gpu_images.size(); array_index++)
		{
			const gpu_image_vec &levels = gpu_images[array_index];

			if (!levels.size())
			{
				// Empty mip chain
				assert(0);
				return false;
			}

			if (!array_index)
			{
				width = levels[0].get_width();
				height = levels[0].get_height();
				total_levels = (uint32_t)levels.size();
				fmt = levels[0].get_format();
			}
			else
			{
				if ((width != levels[0].get_width()) ||
				    (height != levels[0].get_height()) ||
				    (total_levels != levels.size()))
				{
					// All cubemap/texture array faces must be the same dimension
					assert(0);
					return false;
				}
			}

			for (uint32_t level_index = 0; level_index < levels.size(); level_index++)
			{
				if (level_index)
				{
					if ( (levels[level_index].get_width() != maximum<uint32_t>(1, levels[0].get_width() >> level_index)) ||
							(levels[level_index].get_height() != maximum<uint32_t>(1, levels[0].get_height() >> level_index)) )
					{
						// Malformed mipmap chain
						assert(0);
						return false;
					}
				}

				if (fmt != levels[level_index].get_format())
				{
					// All input textures must use the same GPU format
					assert(0);
					return false;
				}
			}
		}

		uint32_t internal_fmt = KTX_ETC1_RGB8_OES, base_internal_fmt = KTX_RGB;

		switch (fmt)
		{
		case cBC1:
		{
			internal_fmt = KTX_COMPRESSED_RGB_S3TC_DXT1_EXT;
			break;
		}
		case cBC3:
		{
			internal_fmt = KTX_COMPRESSED_RGBA_S3TC_DXT5_EXT;
			base_internal_fmt = KTX_RGBA;
			break;
		}
		case cBC4:
		{
			internal_fmt = KTX_COMPRESSED_RED_RGTC1_EXT;// KTX_COMPRESSED_LUMINANCE_LATC1_EXT;
			base_internal_fmt = KTX_RED;
			break;
		}
		case cBC5:
		{
			internal_fmt = KTX_COMPRESSED_RED_GREEN_RGTC2_EXT;
			base_internal_fmt = KTX_RG;
			break;
		}
		case cETC1:
		case cETC1S:
		{
			internal_fmt = KTX_ETC1_RGB8_OES;
			break;
		}
		case cETC2_RGB:
		{
			internal_fmt = KTX_COMPRESSED_RGB8_ETC2;
			break;
		}
		case cETC2_RGBA:
		{
			internal_fmt = KTX_COMPRESSED_RGBA8_ETC2_EAC;
			base_internal_fmt = KTX_RGBA;
			break;
		}
		case cBC7:
		{
			internal_fmt = KTX_COMPRESSED_RGBA_BPTC_UNORM_ARB;
			base_internal_fmt = KTX_RGBA;
			break;
		}
		case cPVRTC1_4_RGB:
		{
			internal_fmt = KTX_COMPRESSED_RGB_PVRTC_4BPPV1_IMG;
			break;
		}
		case cPVRTC1_4_RGBA:
		{
			internal_fmt = KTX_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG;
			base_internal_fmt = KTX_RGBA;
			break;
		}
		default:
		{
			// TODO
			assert(0);
			return false;
		}
		}
		
		ktx_header header;
		header.clear();
		memcpy(&header.m_identifier, g_ktx_file_id, sizeof(g_ktx_file_id));
		header.m_endianness = KTX_ENDIAN;
		
		header.m_pixelWidth = width;
		header.m_pixelHeight = height;
		
		header.m_glInternalFormat = internal_fmt;
		header.m_glBaseInternalFormat = base_internal_fmt;

		header.m_numberOfArrayElements = (uint32_t)(cubemap_flag ? (gpu_images.size() / 6) : gpu_images.size());
		if (header.m_numberOfArrayElements == 1)
			header.m_numberOfArrayElements = 0;

		header.m_numberOfMipmapLevels = total_levels;
		header.m_numberOfFaces = cubemap_flag ? 6 : 1;

		append_vector(ktx_data, (uint8_t *)&header, sizeof(header));

		for (uint32_t level_index = 0; level_index < total_levels; level_index++)
		{
			uint32_t img_size = gpu_images[0][level_index].get_size_in_bytes();
			
			img_size = img_size * header.m_numberOfFaces * maximum<uint32_t>(1, header.m_numberOfArrayElements);
			
			assert(img_size && ((img_size & 3) == 0));

			packed_uint<4> packed_img_size(img_size);
			append_vector(ktx_data, (uint8_t *)&packed_img_size, sizeof(packed_img_size));

			uint32_t bytes_written = 0;

			for (uint32_t array_index = 0; array_index < maximum<uint32_t>(1, header.m_numberOfArrayElements); array_index++)
			{
				for (uint32_t face_index = 0; face_index < header.m_numberOfFaces; face_index++)
				{
					const gpu_image& img = gpu_images[cubemap_flag ? (array_index * 6 + face_index) : array_index][level_index];

					append_vector(ktx_data, (uint8_t *)img.get_ptr(), img.get_size_in_bytes());
					
					bytes_written += img.get_size_in_bytes();
				}
			
			} // array_index

			assert(bytes_written == img_size);
			
		} // level_index

		return true;
	}

	bool write_compressed_texture_file(const char* pFilename, const std::vector<gpu_image_vec>& g, bool cubemap_flag)
	{
		std::string extension(string_tolower(string_get_extension(pFilename)));

		uint8_vec filedata;
		if (extension == "ktx")
		{
			if (!create_ktx_texture_file(filedata, g, cubemap_flag))
				return false;
		}
		else if (extension == "pvr")
		{
			// TODO
			return false;
		}
		else if (extension == "dds")
		{
			// TODO
			return false;
		}
		else
		{
			// unsupported texture format
			assert(0);
			return false;
		}

		return basisu::write_vec_to_file(pFilename, filedata);
	}

	bool write_compressed_texture_file(const char* pFilename, const gpu_image& g)
	{
		std::vector<gpu_image_vec> v;
		enlarge_vector(v, 1)->push_back(g);
		return write_compressed_texture_file(pFilename, v, false);
	}

} // basisu

