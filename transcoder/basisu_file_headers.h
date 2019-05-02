// basis_file_headers.h
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
#pragma once
#include "basisu_transcoder_internal.h"

namespace basist
{
	enum
	{
		cSliceDescFlagsIsAlphaData = 1,
	};

#pragma pack(push)
#pragma pack(1)
	struct basis_slice_desc
	{
		basisu::packed_uint<3> m_image_index;  // the index of the source image provided to the encoder
		basisu::packed_uint<1> m_level_index;	// the mipmap level index (mipmaps will always appear from largest to smallest)
		basisu::packed_uint<1> m_flags;

		basisu::packed_uint<2> m_orig_width;
		basisu::packed_uint<2> m_orig_height;

		basisu::packed_uint<2> m_num_blocks_x;
		basisu::packed_uint<2> m_num_blocks_y;

		basisu::packed_uint<4> m_file_ofs;
		basisu::packed_uint<4> m_file_size;

		basisu::packed_uint<2> m_slice_data_crc16;
	};

	enum basis_header_flags
	{
		cBASISHeaderFlagETC1S = 1,
		cBASISHeaderFlagYFlipped = 2,
		cBASISHeaderFlagHasAlphaSlices = 4
	};

	// The image type field attempts to describe how to interpret the image data in a Basis file.
	// The encoder library doesn't really do anything special or different with these texture types, this is mostly here for the benefit of the user.
	enum basis_texture_type
	{
		cBASISTexType2D = 0,					// An arbitrary array of 2D RGB or RGBA images with optional mipmaps, array size = # images, each image may have a different resolution and # of mipmap levels
		cBASISTexType2DArray = 1,			// An array of 2D RGB or RGBA images with optional mipmaps, array size = # images, each image has the same resolution and mipmap levels
		cBASISTexTypeCubemapArray = 2,	// an array of cubemap levels, total # of images must be divisable by 6, in X+, X-, Y+, Y-, Z+, Z- order, with optional mipmaps
		cBASISTexTypeVideoFrames = 3,		// An array of 2D video frames, with optional mipmaps, # frames = # images, each image has the same resolution and # of mipmap levels
		cBASISTexTypeVolume = 4				// A 3D texture with optional mipmaps, Z dimension = # images, each image has the same resolution and # of mipmap levels
	};

	enum
	{
		cBASISMaxUSPerFrame = 0xFFFFFF
	};

	struct basis_file_header
	{
		enum
		{
			cBASISSigValue = ('B' << 8) | 's',
			cBASISFirstVersion = 0x10
		};

		basisu::packed_uint<2>      m_sig;
		basisu::packed_uint<2>      m_ver;
		basisu::packed_uint<2>      m_header_size;
		basisu::packed_uint<2>      m_header_crc16;

		basisu::packed_uint<4>      m_data_size;
		basisu::packed_uint<2>      m_data_crc16;

		basisu::packed_uint<3>      m_total_slices;

		basisu::packed_uint<3>      m_total_images;
				
		basisu::packed_uint<1>      m_format;			// enum basist::block_format
		basisu::packed_uint<2>      m_flags;			// enum basist::header_flags
		basisu::packed_uint<1>      m_tex_type;		// enum basist::basis_texture_type
		basisu::packed_uint<3>      m_us_per_frame;	// framerate of video, in microseconds per frame

		basisu::packed_uint<4>      m_reserved;
		basisu::packed_uint<4>      m_userdata0;
		basisu::packed_uint<4>      m_userdata1;

		basisu::packed_uint<2>      m_total_endpoints;
		basisu::packed_uint<4>      m_endpoint_cb_file_ofs;
		basisu::packed_uint<3>      m_endpoint_cb_file_size;

		basisu::packed_uint<2>      m_total_selectors;
		basisu::packed_uint<4>      m_selector_cb_file_ofs;
		basisu::packed_uint<3>      m_selector_cb_file_size;

		basisu::packed_uint<4>      m_tables_file_ofs;
		basisu::packed_uint<4>      m_tables_file_size;

		basisu::packed_uint<4>      m_slice_desc_file_ofs;
		
		basisu::packed_uint<4>      m_extended_file_ofs;
		basisu::packed_uint<4>      m_extended_file_size;
	};
#pragma pack (pop)

} // namespace basist
