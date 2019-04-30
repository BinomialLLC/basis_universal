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
#include "detex/decompress_bc.h"
#include "detex/decompress_bc7.h"
#include "detex/decompress_eac.h"
#include "basisu_enc.h"
#include "basisu_pvrtc1_4.h"

namespace basisu
{
	// Unpacks to RGBA, R, RG, or A
	void unpack_block(texture_format fmt, const void* pBlock, color_rgba* pPixels)
	{
		switch (fmt)
		{
		case cBC1:
		{
			if (detexGetModeBC1((uint8_t*)pBlock))
				detexDecompressBlockBC1A((uint8_t*)pBlock, 0, (uint8_t*)pPixels);
			else
				detexDecompressBlockBC1((uint8_t*)pBlock, 0, (uint8_t*)pPixels);
			break;
		}
		case cBC3:
		{
			detexDecompressBlockBC3((uint8_t*)pBlock, 0, (uint8_t*)pPixels);
			break;
		}
		case cBC4:
		{
			// Unpack to R
			detexDecompressBlockBC4((uint8_t*)pBlock, 0, (uint8_t*)pPixels, sizeof(color_rgba));
			break;
		}
		case cBC5:
		{
			// Unpack to RG
			detexDecompressBlockBC4((uint8_t*)pBlock, 0, (uint8_t*)pPixels, sizeof(color_rgba));
			detexDecompressBlockBC4((uint8_t*)pBlock + sizeof(uint64_t), 0, (uint8_t*)pPixels + 1, sizeof(color_rgba));
			break;
		}
		case cBC7:
		{
			detexDecompressBlockBPTC((const uint8_t*)pBlock, UINT32_MAX, 0, (uint8_t*)pPixels);
			break;
		}
		// Full ETC2 color blocks (planar/T/H modes) is currently unsupported in basisu, but we do support ETC2 with alpha (using ETC1 for color)
		case cETC2_RGB:
		case cETC1:
		case cETC1S:
		{
			unpack_etc1(*static_cast<const etc_block*>(pBlock), pPixels);
			break;
		}
		case cETC2_RGBA:
		{
			unpack_etc1(static_cast<const etc_block*>(pBlock)[1], pPixels);
			detexDecompressBlockETC2_EAC((const uint8_t*)pBlock, (uint8_t*)pPixels + 3, sizeof(color_rgba));
			break;
		}
		case cETC2_ALPHA:
		{
			// Unpack to A
			detexDecompressBlockETC2_EAC((const uint8_t*)pBlock, (uint8_t*)pPixels + 3, sizeof(color_rgba));
			break;
		}
		default:
		{
			assert(0);
			// TODO
			break;
		}
		}
	}

	bool gpu_image::unpack(image& img, bool pvrtc_wrap_addressing) const
	{
		img.resize(get_width(), get_height());
		img.set_all(g_black_color);

		if (!img.get_width() || !img.get_height())
			return true;

		if ((m_fmt == cPVRTC1_4_RGB) || (m_fmt == cPVRTC1_4_RGBA))
		{
			if (!is_pow2(m_width) || !is_pow2(m_height))
			{
				// PVRTC1 images must use power of 2 dimensions
				return false;
			}

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

		for (uint32_t by = 0; by < m_blocks_y; by++)
		{
			for (uint32_t bx = 0; bx < m_blocks_x; bx++)
			{
				const void* pBlock = get_block_ptr(bx, by);

				unpack_block(m_fmt, pBlock, pixels);

				img.set_block_clipped(pixels, bx * m_block_width, by * m_block_height, m_block_width, m_block_height);
			} // bx
		} // by

		return true;
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

	bool create_ktx_texture_file(uint8_vec &ktx_data, const gpu_image_vec& g)
	{
		if (!g.size())
		{
			assert(0);
			return false;
		}

		uint32_t internal_fmt = KTX_ETC1_RGB8_OES, base_internal_fmt = KTX_RGB;

		switch (g[0].get_format())
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
		header.m_pixelWidth = g[0].get_width();
		header.m_pixelHeight = g[0].get_height();
		header.m_glInternalFormat = internal_fmt;
		header.m_glBaseInternalFormat = base_internal_fmt;
		header.m_numberOfMipmapLevels = (uint32_t)g.size();
		header.m_numberOfFaces = 1;

		append_vector(ktx_data, (uint8_t *)&header, sizeof(header));
		
		for (uint32_t level = 0; level < g.size(); level++)
		{
			const gpu_image& img = g[level];

			if (level)
			{
				if ( (img.get_format() != g[0].get_format()) ||
					  (img.get_width() != maximum<uint32_t>(1, g[0].get_width() >> level)) ||
					  (img.get_height() != maximum<uint32_t>(1, g[0].get_height() >> level)) )
				{
					// Bad input
					assert(0);
					return false;
				}
			}

			packed_uint<4> img_size = (uint32_t)img.get_size_in_bytes();

			assert(img_size && ((img_size & 3) == 0));
			
			append_vector(ktx_data, (uint8_t *)&img_size, sizeof(img_size));

			append_vector(ktx_data, (uint8_t *)img.get_ptr(), img.get_size_in_bytes());
		}

		return true;
	}

	bool write_compressed_texture_file(const char* pFilename, const gpu_image_vec& g)
	{
		std::string extension(string_tolower(string_get_extension(pFilename)));

		uint8_vec filedata;
		if (extension == "ktx")
		{
			if (!create_ktx_texture_file(filedata, g))
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
		gpu_image_vec v;
		v.push_back(g);
		return write_compressed_texture_file(pFilename, v);
	}

} // basisu

