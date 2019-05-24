// basisu_transcoder.h
// Copyright (C) 2017-2019 Binomial LLC. All Rights Reserved.
// Important: If compiling with gcc, be sure strict aliasing is disabled: -fno-strict-aliasing
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
#pragma once

// Set BASISU_DEVEL_MESSAGES to 1 to enable debug printf()'s whenever an error occurs, for easier debugging during development.
//#define BASISU_DEVEL_MESSAGES 1

#include "basisu_transcoder_internal.h"
#include "basisu_global_selector_palette.h"
#include "basisu_file_headers.h"

namespace basist
{
	// Low-level formats directly supported by the transcoder (other supported texture formats are combinations of these low-level block formats)
	enum block_format
	{
		cETC1,								// ETC1S RGB 
		cBC1,									// DXT1 RGB 
		cBC4,									// DXT5A (alpha block only)
		cPVRTC1_4_OPAQUE_ONLY,			// opaque only PVRTC1 4bpp
		cBC7_M6_OPAQUE_ONLY,				// RGB BC7 mode 6
		cETC2_EAC_A8,						// alpha block of ETC2 EAC (first 8 bytes of the 16-bit ETC2 EAC RGBA format)
		
		cTotalBlockFormats
	};

	// High-level composite texture formats supported by the transcoder
	enum transcoder_texture_format
	{
		cTFETC1,
		cTFBC1,
		cTFBC4,
		cTFPVRTC1_4_OPAQUE_ONLY,
		cTFBC7_M6_OPAQUE_ONLY,
		cTFETC2,								// ETC2_EAC_A8 block followed by a ETC1 block
		cTFBC3,								// BC4 followed by a BC1 block
		cTFBC5,								// two BC4 blocks

		cTFTotalTextureFormats
	};

	uint32_t basis_get_bytes_per_block(transcoder_texture_format fmt);
	const char *basis_get_format_name(transcoder_texture_format fmt);
	bool basis_transcoder_format_has_alpha(transcoder_texture_format fmt);
	basisu::texture_format basis_get_basisu_texture_format(transcoder_texture_format fmt);
	const char *basis_get_texture_type_name(basis_texture_type tex_type);
	
	class basisu_transcoder;
	
	class basisu_lowlevel_transcoder
	{
		friend class basisu_transcoder;
	
	public:
		basisu_lowlevel_transcoder(const basist::etc1_global_selector_codebook *pGlobal_sel_codebook);

		bool decode_palettes(
			uint32_t num_endpoints, const uint8_t *pEndpoints_data, uint32_t endpoints_data_size,
			uint32_t num_selectors, const uint8_t *pSelectors_data, uint32_t selectors_data_size);

		bool decode_tables(const uint8_t *pTable_data, uint32_t table_data_size);

		bool transcode_slice(void *pDst_blocks, uint32_t num_blocks_x, uint32_t num_blocks_y, const uint8_t *pImage_data, uint32_t image_data_size, block_format fmt, uint32_t output_stride, bool wrap_addressing, bool bc1_allow_threecolor_blocks);

	private:
		struct endpoint
		{
			color32 m_color5;
			uint8_t m_inten5;
		};

		typedef std::vector<endpoint> endpoint_vec;
		endpoint_vec m_endpoints;

		typedef std::vector<selector> selector_vec;
		selector_vec m_selectors;

		const etc1_global_selector_codebook *m_pGlobal_sel_codebook;

		huffman_decoding_table m_endpoint_pred_model, m_delta_endpoint_model, m_selector_model, m_selector_history_buf_rle_model;

		uint32_t m_selector_history_buf_size;

		struct block_preds
		{
			uint16_t m_endpoint_index;
			uint8_t m_pred_bits;
		};

		std::vector<block_preds> m_block_endpoint_preds[2];
	};

	struct basisu_slice_info
	{
		uint32_t m_orig_width;
		uint32_t m_orig_height;

		uint32_t m_width;
		uint32_t m_height;

		uint32_t m_num_blocks_x;
		uint32_t m_num_blocks_y;
		uint32_t m_total_blocks;

		uint32_t m_compressed_size;

		uint32_t m_slice_index;	// the slice index in the .basis file
		uint32_t m_image_index;	// the source image index originally provided to the encoder
		uint32_t m_level_index;	// the mipmap level within this image
		
		uint32_t m_unpacked_slice_crc16;
		
		bool m_alpha_flag;		// true if the slice has alpha data
	};

	typedef std::vector<basisu_slice_info> basisu_slice_info_vec;

	struct basisu_image_info
	{
		uint32_t m_image_index;
		uint32_t m_total_levels;	

		uint32_t m_orig_width;
		uint32_t m_orig_height;

		uint32_t m_width;
		uint32_t m_height;

		uint32_t m_num_blocks_x;
		uint32_t m_num_blocks_y;
		uint32_t m_total_blocks;

		uint32_t m_first_slice_index;	
								
		bool m_alpha_flag;		// true if the image has alpha data
	};

	struct basisu_image_level_info
	{
		uint32_t m_image_index;
		uint32_t m_level_index;

		uint32_t m_orig_width;
		uint32_t m_orig_height;

		uint32_t m_width;
		uint32_t m_height;

		uint32_t m_num_blocks_x;
		uint32_t m_num_blocks_y;
		uint32_t m_total_blocks;

		uint32_t m_first_slice_index;	
								
		bool m_alpha_flag;		// true if the image has alpha data
	};

	struct basisu_file_info
	{
		uint32_t m_version;
		uint32_t m_total_header_size;

		uint32_t m_total_selectors;
		uint32_t m_selector_codebook_size;

		uint32_t m_total_endpoints;
		uint32_t m_endpoint_codebook_size;

		uint32_t m_tables_size;
		uint32_t m_slices_size;	

		basis_texture_type m_tex_type;
		uint32_t m_us_per_frame;

		// Low-level slice information (1 slice per image for color-only basis files, 2 for alpha basis files)
		basisu_slice_info_vec m_slice_info;

		uint32_t m_total_images;	 // total # of images
		std::vector<uint32_t> m_image_mipmap_levels; // the # of mipmap levels for each image

		uint32_t m_userdata0;
		uint32_t m_userdata1;
		
		bool m_etc1s;					// always true for basis universal
		bool m_y_flipped;				// true if the image was Y flipped
		bool m_has_alpha_slices;	// true if the texture has alpha slices (even slices RGB, odd slices alpha)
	};

	class basisu_transcoder
	{
		basisu_transcoder(basisu_transcoder&);
		basisu_transcoder& operator= (const basisu_transcoder&);

	public:
		basisu_transcoder(const etc1_global_selector_codebook *pGlobal_sel_codebook);

		// Validates the .basis file. This computes a crc16 over the entire file, so it's slow.
		bool validate_file_checksums(const void *pData, uint32_t data_size, bool full_validation) const;

		// Quick header validation - no crc16 checks.
		bool validate_header(const void *pData, uint32_t data_size) const;

		basis_texture_type get_texture_type(const void *pData, uint32_t data_size) const;
		bool get_userdata(const void *pData, uint32_t data_size, uint32_t &userdata0, uint32_t &userdata1) const;
		
		// Returns the total number of images in the basis file (always 1 or more).
		// Note that the number of mipmap levels for each image may differ, and that images may have different resolutions.
		uint32_t get_total_images(const void *pData, uint32_t data_size) const;

		// Returns the number of mipmap levels in an image.
		uint32_t get_total_image_levels(const void *pData, uint32_t data_size, uint32_t image_index) const;
		
		// Returns basic information about an image. Note that orig_width/orig_height may not be a multiple of 4.
		bool get_image_level_desc(const void *pData, uint32_t data_size, uint32_t image_index, uint32_t level_index, uint32_t &orig_width, uint32_t &orig_height, uint32_t &total_blocks) const;

		// Returns information about the specified image.
		bool get_image_info(const void *pData, uint32_t data_size, basisu_image_info &image_info, uint32_t image_index) const;

		// Returns information about the specified image's mipmap level.
		bool get_image_level_info(const void *pData, uint32_t data_size, basisu_image_level_info &level_info, uint32_t image_index, uint32_t level_index) const;
				
		// Get a description of the basis file and low-level information about each slice.
		bool get_file_info(const void *pData, uint32_t data_size, basisu_file_info &file_info) const;
				
		// start_transcoding() must be called before calling transcode_slice() or transcode_image_level().
		// This decompresses the selector/endpoint codebooks, so ideally you would only call this once per .basis file (not each image/mipmap level).
		bool start_transcoding(const void *pData, uint32_t data_size) const;
		
		// Returns true if start_transcoding() has been called.
		bool get_ready_to_transcode() const { return m_lowlevel_decoder.m_endpoints.size() > 0; }

		enum 
		{
			// PVRTC1: texture will use wrap addressing vs. clamp (most PVRTC viewer tools assume wrap addressing, so we default to wrap although that can cause edge artifacts)
			cDecodeFlagsPVRTCWrapAddressing = 1,	
						
			// PVRTC1: decode non-pow2 ETC1S texture level to the next larger power of 2 (not implemented yet, but we're going to support it). Ignored if the slice's dimensions are already a power of 2.
			cDecodeFlagsPVRTCDecodeToNextPow2 = 2,	
			
			// When decoding to an opaque texture format, if the basis file has alpha, decode the alpha slice instead of the color slice to the output texture format
			cDecodeFlagsTranscodeAlphaDataToOpaqueFormats = 4,

			// Forbid usage of BC1 3 color blocks (we don't support BC1 punchthrough alpha yet).
			cDecodeFlagsBC1ForbidThreeColorBlocks = 8
		};
								
		// transcode_image_level() decodes a single mipmap level from the .basis file to any of the supported output texture formats.
		// It'll first find the slice(s) to transcode, then call transcode_slice() one or two times to decode both the color and alpha texture data (or RG texture data from two slices for BC5).
		// If the .basis file doesn't have alpha slices, the output alpha blocks will be set to fully opaque (all 255's).
		// Currently, to decode to PVRTC1 the basis texture's dimensions in pixels must be a power of 2, due to PVRTC1 format requirements. 
		// output_blocks_buf_size_in_blocks should be at least the image level's total_blocks (num_blocks_x * num_blocks_y)
		// If fmt isn't cETC1, basisu_transcoder_init() must have been called first to initialize the transcoder lookup tables.
		bool transcode_image_level(
			const void *pData, uint32_t data_size, 
			uint32_t image_index, uint32_t level_index, 
			void *pOutput_blocks, uint32_t output_blocks_buf_size_in_blocks,
			transcoder_texture_format fmt,
			uint32_t decode_flags = cDecodeFlagsPVRTCWrapAddressing) const;

		// Finds the basis slice corresponding to the specified image/level/alpha params, or -1 if the slice can't be found.
		int find_slice(const void *pData, uint32_t data_size, uint32_t image_index, uint32_t level_index, bool alpha_data) const;

		// transcode_slice() decodes a single slice from the .basis file. It's a low-level API - most likely you want to use transcode_image_level().
		// This is a low-level API, and will be needed to be called multiple times to decode some texture formats (like BC3, BC5, or ETC2).
		// output_blocks_buf_size_in_blocks is just used for verification to make sure the output buffer is large enough.
		// output_blocks_buf_size_in_blocks should be at least the slice's total_blocks (num_blocks_x * num_blocks_y)
		// If fmt isn't cETC1, basisu_transcoder_init() must have been called first to initialize the transcoder lookup tables.
		bool transcode_slice(const void *pData, uint32_t data_size, uint32_t slice_index, 
			void *pOutput_blocks, uint32_t output_blocks_buf_size_in_blocks, 
			block_format fmt, uint32_t output_stride, uint32_t decode_flags = cDecodeFlagsPVRTCWrapAddressing) const;

	private:
		const void *m_pFile_data;
		uint32_t m_file_data_size;

		mutable basisu_lowlevel_transcoder m_lowlevel_decoder;

		int find_first_slice_index(const void* pData, uint32_t data_size, uint32_t image_index, uint32_t level_index) const;
		
		bool validate_header_quick(const void* pData, uint32_t data_size) const;
	};

	// basisu_transcoder_init() must be called before a .basis file can be transcoded.
	void basisu_transcoder_init();

} // namespace basisu
