// basisu_transcoder.h
// Copyright (C) 2019-2020 Binomial LLC. All Rights Reserved.
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

// Set BASISU_FORCE_DEVEL_MESSAGES to 1 to enable debug printf()'s whenever an error occurs, for easier debugging during development.
#ifndef BASISU_FORCE_DEVEL_MESSAGES
#define BASISU_FORCE_DEVEL_MESSAGES 0
#endif

/**** start inlining basisu_transcoder_internal.h ****/
// basisu_transcoder_internal.h - Universal texture format transcoder library.
// Copyright (C) 2019-2020 Binomial LLC. All Rights Reserved.
//
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

#ifdef _MSC_VER
#pragma warning (disable: 4127) //  conditional expression is constant
#endif

#define BASISD_LIB_VERSION 112
#define BASISD_VERSION_STRING "01.12"

#ifdef _DEBUG
#define BASISD_BUILD_DEBUG
#else
#define BASISD_BUILD_RELEASE
#endif

/**** start inlining basisu.h ****/
// basisu.h
// Copyright (C) 2019-2020 Binomial LLC. All Rights Reserved.
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

#ifdef _MSC_VER

	#pragma warning (disable : 4201)
	#pragma warning (disable : 4127) // warning C4127: conditional expression is constant
	#pragma warning (disable : 4530) // C++ exception handler used, but unwind semantics are not enabled.

	#ifndef BASISU_NO_ITERATOR_DEBUG_LEVEL
		//#define _HAS_ITERATOR_DEBUGGING 0

		#if defined(_DEBUG) || defined(DEBUG)
			// This is madness, but we need to disable iterator debugging in debug builds or the encoder is unsable because MSVC's iterator debugging implementation is totally broken.
			#ifndef _ITERATOR_DEBUG_LEVEL
			#define _ITERATOR_DEBUG_LEVEL 1
			#endif
			#ifndef _SECURE_SCL
			#define _SECURE_SCL 1
			#endif
		#else // defined(_DEBUG) || defined(DEBUG)
			#ifndef _SECURE_SCL
			#define _SECURE_SCL 0
			#endif
			#ifndef _ITERATOR_DEBUG_LEVEL
			#define _ITERATOR_DEBUG_LEVEL 0
			#endif
		#endif // defined(_DEBUG) || defined(DEBUG)

		#ifndef NOMINMAX
			#define NOMINMAX
		#endif

	#endif // BASISU_NO_ITERATOR_DEBUG_LEVEL

#endif // _MSC_VER

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>
#include <memory.h>
#include <limits.h>
#include <stdint.h>

#include <algorithm>
#include <limits>
#include <functional>
#include <iterator>
#include <type_traits>
#include <vector>
#include <assert.h>
#include <random>

#ifdef max
#undef max
#endif

#ifdef min
#undef min
#endif

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

// Set to one to enable debug printf()'s when any errors occur, for development/debugging. Especially useful for WebGL development.
#ifndef BASISU_FORCE_DEVEL_MESSAGES
#define BASISU_FORCE_DEVEL_MESSAGES 0
#endif

#define BASISU_NOTE_UNUSED(x) (void)(x)
#define BASISU_ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#define BASISU_NO_EQUALS_OR_COPY_CONSTRUCT(x) x(const x &) = delete; x& operator= (const x &) = delete;
#define BASISU_ASSUME(x) static_assert(x, #x);
#define BASISU_OFFSETOF(s, m) (uint32_t)(intptr_t)(&((s *)(0))->m)
#define BASISU_STRINGIZE(x) #x
#define BASISU_STRINGIZE2(x) BASISU_STRINGIZE(x)

#if BASISU_FORCE_DEVEL_MESSAGES
	#define BASISU_DEVEL_ERROR(...) do { basisu::debug_printf(__VA_ARGS__); } while(0)
#else
	#define BASISU_DEVEL_ERROR(...)
#endif

namespace basisu
{
	// Types/utilities

#ifdef _WIN32
	const char BASISU_PATH_SEPERATOR_CHAR = '\\';
#else
	const char BASISU_PATH_SEPERATOR_CHAR = '/';
#endif

	typedef std::vector<uint8_t> uint8_vec;
	typedef std::vector<int16_t> int16_vec;
	typedef std::vector<uint16_t> uint16_vec;
	typedef std::vector<uint32_t> uint_vec;
	typedef std::vector<uint64_t> uint64_vec;
	typedef std::vector<int> int_vec;
	typedef std::vector<bool> bool_vec;

	void enable_debug_printf(bool enabled);
	void debug_printf(const char *pFmt, ...);
		
	template <typename T> inline void clear_obj(T& obj) { memset(&obj, 0, sizeof(obj)); }

	template <typename T0, typename T1> inline T0 lerp(T0 a, T0 b, T1 c) { return a + (b - a) * c; }

	template <typename S> inline S maximum(S a, S b) { return (a > b) ? a : b; }
	template <typename S> inline S maximum(S a, S b, S c) { return maximum(maximum(a, b), c); }
	template <typename S> inline S maximum(S a, S b, S c, S d) { return maximum(maximum(maximum(a, b), c), d); }
	
	template <typename S> inline S minimum(S a, S b) {	return (a < b) ? a : b; }
	template <typename S> inline S minimum(S a, S b, S c) {	return minimum(minimum(a, b), c); }
	template <typename S> inline S minimum(S a, S b, S c, S d) { return minimum(minimum(minimum(a, b), c), d); }

	inline float clampf(float value, float low, float high) { if (value < low) value = low; else if (value > high) value = high;	return value; }
	inline float saturate(float value) { return clampf(value, 0, 1.0f); }
	inline uint8_t minimumub(uint8_t a, uint8_t b) { return (a < b) ? a : b; }
	inline uint32_t minimumu(uint32_t a, uint32_t b) { return (a < b) ? a : b; }
	inline int32_t minimumi(int32_t a, int32_t b) { return (a < b) ? a : b; }
	inline float minimumf(float a, float b) { return (a < b) ? a : b; }
	inline uint8_t maximumub(uint8_t a, uint8_t b) { return (a > b) ? a : b; }
	inline uint32_t maximumu(uint32_t a, uint32_t b) { return (a > b) ? a : b; }
	inline int32_t maximumi(int32_t a, int32_t b) { return (a > b) ? a : b; }
	inline float maximumf(float a, float b) { return (a > b) ? a : b; }
	inline int squarei(int i) { return i * i; }
	inline float squaref(float i) { return i * i; }
	template<typename T> inline T square(T a) { return a * a; }

	template <typename S> inline S clamp(S value, S low, S high) { return (value < low) ? low : ((value > high) ? high : value); }

	inline uint32_t iabs(int32_t i) { return (i < 0) ? static_cast<uint32_t>(-i) : static_cast<uint32_t>(i);	}
	inline uint64_t iabs64(int64_t i) {	return (i < 0) ? static_cast<uint64_t>(-i) : static_cast<uint64_t>(i); }

	template<typename T> inline void clear_vector(T &vec) { vec.erase(vec.begin(), vec.end()); }		
	template<typename T> inline typename T::value_type *enlarge_vector(T &vec, size_t n) { size_t cs = vec.size(); vec.resize(cs + n); return &vec[cs]; }

	inline bool is_pow2(uint32_t x) { return x && ((x & (x - 1U)) == 0U); }
	inline bool is_pow2(uint64_t x) { return x && ((x & (x - 1U)) == 0U); }

	template<typename T> inline T open_range_check(T v, T minv, T maxv) { assert(v >= minv && v < maxv); BASISU_NOTE_UNUSED(minv); BASISU_NOTE_UNUSED(maxv); return v; }
	template<typename T> inline T open_range_check(T v, T maxv) { assert(v < maxv); BASISU_NOTE_UNUSED(maxv); return v; }

	inline uint32_t total_bits(uint32_t v) { uint32_t l = 0; for ( ; v > 0U; ++l) v >>= 1; return l; }

	template<typename T> inline T saturate(T val) { return clamp(val, 0.0f, 1.0f); }

	template<typename T, typename R> inline void append_vector(T &vec, const R *pObjs, size_t n) 
	{ 
		if (n)
		{
			const size_t cur_s = vec.size();
			vec.resize(cur_s + n);
			memcpy(&vec[cur_s], pObjs, sizeof(R) * n);
		}
	}

	template<typename T> inline void append_vector(T &vec, const T &other_vec)
	{
		if (other_vec.size())
			append_vector(vec, &other_vec[0], other_vec.size());
	}

	template<typename T> inline void vector_ensure_element_is_valid(T &vec, size_t idx)
	{
		if (idx >= vec.size())
			vec.resize(idx + 1);
	}

	template<typename T> inline void vector_sort(T &vec)
	{
		if (vec.size())
			std::sort(vec.begin(), vec.end());
	}

	template<typename T, typename U> inline bool unordered_set_contains(T& set, const U&obj)
	{
		return set.find(obj) != set.end();
	}

	template<typename T> int vector_find(const T &vec, const typename T::value_type &obj)
	{
		assert(vec.size() <= INT_MAX);
		for (size_t i = 0; i < vec.size(); i++)
			if (vec[i] == obj)
				return static_cast<int>(i);
		return -1;
	}

	template<typename T> void vector_set_all(T &vec, const typename T::value_type &obj)
	{
		for (size_t i = 0; i < vec.size(); i++)
			vec[i] = obj;
	}
		
	inline uint64_t read_be64(const void *p)
	{
		uint64_t val = 0;
		for (uint32_t i = 0; i < 8; i++)
			val |= (static_cast<uint64_t>(static_cast<const uint8_t *>(p)[7 - i]) << (i * 8));
		return val;
	}

	inline void write_be64(void *p, uint64_t x)
	{
		for (uint32_t i = 0; i < 8; i++)
			static_cast<uint8_t *>(p)[7 - i] = static_cast<uint8_t>(x >> (i * 8));
	}

	static inline uint16_t byteswap16(uint16_t x) { return static_cast<uint16_t>((x << 8) | (x >> 8)); }
	static inline uint32_t byteswap32(uint32_t x) { return ((x << 24) | ((x << 8) & 0x00FF0000) | ((x >> 8) & 0x0000FF00) | (x >> 24)); }

	inline uint32_t floor_log2i(uint32_t v)
	{
		uint32_t b = 0;
		for (; v > 1U; ++b)
			v >>= 1;
		return b;
	}

	inline uint32_t ceil_log2i(uint32_t v)
	{
		uint32_t b = floor_log2i(v);
		if ((b != 32) && (v > (1U << b)))
			++b;
		return b;
	}

	inline int posmod(int x, int y)
	{
		if (x >= 0)
			return (x < y) ? x : (x % y);
		int m = (-x) % y;
		return (m != 0) ? (y - m) : m;
	}

	inline bool do_excl_ranges_overlap(int la, int ha, int lb, int hb)
	{
		assert(la < ha && lb < hb);
		if ((ha <= lb) || (la >= hb)) return false;
		return true;
	}
		
	// Always little endian 2-4 byte unsigned int
	template<uint32_t NumBytes>
	struct packed_uint
	{
		uint8_t m_bytes[NumBytes];

		inline packed_uint() { static_assert(NumBytes <= 4, "NumBytes <= 4"); }
		inline packed_uint(uint32_t v) { *this = v; }
		inline packed_uint(const packed_uint& other) { *this = other; }

		inline packed_uint& operator= (uint32_t v) { for (uint32_t i = 0; i < NumBytes; i++) m_bytes[i] = static_cast<uint8_t>(v >> (i * 8)); return *this; }
		inline packed_uint& operator= (const packed_uint& rhs) { memcpy(m_bytes, rhs.m_bytes, sizeof(m_bytes)); return *this; }

		inline operator uint32_t() const
		{
			switch (NumBytes)
			{
				case 1:  return  m_bytes[0];
				case 2:  return (m_bytes[1] << 8U) | m_bytes[0];
				case 3:  return (m_bytes[2] << 16U) | (m_bytes[1] << 8U) | (m_bytes[0]);
				default: return (m_bytes[3] << 24U) | (m_bytes[2] << 16U) | (m_bytes[1] << 8U) | (m_bytes[0]);
			}
		}
	};

	enum eZero { cZero };
	enum eNoClamp { cNoClamp };
	
	// Rice/Huffman entropy coding
		
	// This is basically Deflate-style canonical Huffman, except we allow for a lot more symbols.
	enum
	{
		cHuffmanMaxSupportedCodeSize = 16, cHuffmanMaxSupportedInternalCodeSize = 31, 
		cHuffmanFastLookupBits = 10, cHuffmanFastLookupSize = 1 << cHuffmanFastLookupBits,
		cHuffmanMaxSymsLog2 = 14, cHuffmanMaxSyms = 1 << cHuffmanMaxSymsLog2,

		// Small zero runs
		cHuffmanSmallZeroRunSizeMin = 3, cHuffmanSmallZeroRunSizeMax = 10, cHuffmanSmallZeroRunExtraBits = 3,

		// Big zero run
		cHuffmanBigZeroRunSizeMin = 11, cHuffmanBigZeroRunSizeMax = 138, cHuffmanBigZeroRunExtraBits = 7,

		// Small non-zero run
		cHuffmanSmallRepeatSizeMin = 3, cHuffmanSmallRepeatSizeMax = 6, cHuffmanSmallRepeatExtraBits = 2,

		// Big non-zero run
		cHuffmanBigRepeatSizeMin = 7, cHuffmanBigRepeatSizeMax = 134, cHuffmanBigRepeatExtraBits = 7,

		cHuffmanTotalCodelengthCodes = 21, cHuffmanSmallZeroRunCode = 17, cHuffmanBigZeroRunCode = 18, cHuffmanSmallRepeatCode = 19, cHuffmanBigRepeatCode = 20
	};

	static const uint8_t g_huffman_sorted_codelength_codes[] = { cHuffmanSmallZeroRunCode, cHuffmanBigZeroRunCode,	cHuffmanSmallRepeatCode, cHuffmanBigRepeatCode, 0, 8, 7, 9, 6, 0xA, 5, 0xB, 4, 0xC, 3, 0xD, 2, 0xE, 1, 0xF, 0x10 };
	const uint32_t cHuffmanTotalSortedCodelengthCodes = sizeof(g_huffman_sorted_codelength_codes) / sizeof(g_huffman_sorted_codelength_codes[0]);

	// GPU texture formats

	enum class texture_format
	{
		cInvalidTextureFormat = -1,
		
		// Block-based formats
		cETC1,			// ETC1
		cETC1S,			// ETC1 (subset: diff colors only, no subblocks)
		cETC2_RGB,		// ETC2 color block (basisu doesn't support ETC2 planar/T/H modes - just basic ETC1)
		cETC2_RGBA,		// ETC2 EAC alpha block followed by ETC2 color block
		cETC2_ALPHA,	// ETC2 EAC alpha block 
		cBC1,				// DXT1
		cBC3,				// DXT5 (BC4/DXT5A block followed by a BC1/DXT1 block)
		cBC4,				// DXT5A
		cBC5,				// 3DC/DXN (two BC4/DXT5A blocks)
		cBC7,
		cASTC4x4,		// LDR only
		cPVRTC1_4_RGB,
		cPVRTC1_4_RGBA,
		cATC_RGB,
		cATC_RGBA_INTERPOLATED_ALPHA,
		cFXT1_RGB,
		cPVRTC2_4_RGBA,
		cETC2_R11_EAC,
		cETC2_RG11_EAC,
		cUASTC4x4,		
		
		// Uncompressed/raw pixels
		cRGBA32,
		cRGB565,
		cBGR565,
		cRGBA4444,
		cABGR4444
	};

	inline uint32_t get_bytes_per_block(texture_format fmt)
	{
		switch (fmt)
		{
		case texture_format::cETC1:
		case texture_format::cETC1S:
		case texture_format::cETC2_RGB:
		case texture_format::cETC2_ALPHA:
		case texture_format::cBC1:
		case texture_format::cBC4:
		case texture_format::cPVRTC1_4_RGB:
		case texture_format::cPVRTC1_4_RGBA:
		case texture_format::cATC_RGB:
		case texture_format::cPVRTC2_4_RGBA:
		case texture_format::cETC2_R11_EAC:
			return 8;
		case texture_format::cRGBA32:
			return sizeof(uint32_t) * 16;
		default:
			break;
		}
		return 16;
	}

	inline uint32_t get_qwords_per_block(texture_format fmt)
	{
		return get_bytes_per_block(fmt) >> 3;
	}

	inline uint32_t get_block_width(texture_format fmt)
	{
		BASISU_NOTE_UNUSED(fmt);
		switch (fmt)
		{
		case texture_format::cFXT1_RGB:
			return 8;
		default:
			break;
		}
		return 4;
	}

	inline uint32_t get_block_height(texture_format fmt)
	{
		BASISU_NOTE_UNUSED(fmt);
		return 4;
	}
							
} // namespace basisu

/**** ended inlining basisu.h ****/

#define BASISD_znew (z = 36969 * (z & 65535) + (z >> 16))

namespace basisu
{
	extern bool g_debug_printf;
}

namespace basist
{
	// Low-level formats directly supported by the transcoder (other supported texture formats are combinations of these low-level block formats).
	// You probably don't care about these enum's unless you are going pretty low-level and calling the transcoder to decode individual slices.
	enum class block_format
	{
		cETC1,								// ETC1S RGB 
		cETC2_RGBA,							// full ETC2 EAC RGBA8 block
		cBC1,									// DXT1 RGB 
		cBC3,									// BC4 block followed by a four color BC1 block
		cBC4,									// DXT5A (alpha block only)
		cBC5,									// two BC4 blocks
		cPVRTC1_4_RGB,						// opaque-only PVRTC1 4bpp
		cPVRTC1_4_RGBA,					// PVRTC1 4bpp RGBA
		cBC7,									// Full BC7 block, any mode
		cBC7_M5_COLOR,						// RGB BC7 mode 5 color (writes an opaque mode 5 block)
		cBC7_M5_ALPHA,						// alpha portion of BC7 mode 5 (cBC7_M5_COLOR output data must have been written to the output buffer first to set the mode/rot fields etc.)
		cETC2_EAC_A8,						// alpha block of ETC2 EAC (first 8 bytes of the 16-bit ETC2 EAC RGBA format)
		cASTC_4x4,							// ASTC 4x4 (either color-only or color+alpha). Note that the transcoder always currently assumes sRGB is not enabled when outputting ASTC 
												// data. If you use a sRGB ASTC format you'll get ~1 LSB of additional error, because of the different way ASTC decoders scale 8-bit endpoints to 16-bits during unpacking.
		
		cATC_RGB,
		cATC_RGBA_INTERPOLATED_ALPHA,
		cFXT1_RGB,							// Opaque-only, has oddball 8x4 pixel block size

		cPVRTC2_4_RGB,
		cPVRTC2_4_RGBA,

		cETC2_EAC_R11,
		cETC2_EAC_RG11,
												
		cIndices,							// Used internally: Write 16-bit endpoint and selector indices directly to output (output block must be at least 32-bits)

		cRGB32,								// Writes RGB components to 32bpp output pixels
		cRGBA32,								// Writes RGB255 components to 32bpp output pixels
		cA32,									// Writes alpha component to 32bpp output pixels
				
		cRGB565,
		cBGR565,
		
		cRGBA4444_COLOR,
		cRGBA4444_ALPHA,
		cRGBA4444_COLOR_OPAQUE,
		cRGBA4444,
						
		cTotalBlockFormats
	};

	const int COLOR5_PAL0_PREV_HI = 9, COLOR5_PAL0_DELTA_LO = -9, COLOR5_PAL0_DELTA_HI = 31;
	const int COLOR5_PAL1_PREV_HI = 21, COLOR5_PAL1_DELTA_LO = -21, COLOR5_PAL1_DELTA_HI = 21;
	const int COLOR5_PAL2_PREV_HI = 31, COLOR5_PAL2_DELTA_LO = -31, COLOR5_PAL2_DELTA_HI = 9;
	const int COLOR5_PAL_MIN_DELTA_B_RUNLEN = 3, COLOR5_PAL_DELTA_5_RUNLEN_VLC_BITS = 3;

	const uint32_t ENDPOINT_PRED_TOTAL_SYMBOLS = (4 * 4 * 4 * 4) + 1;
	const uint32_t ENDPOINT_PRED_REPEAT_LAST_SYMBOL = ENDPOINT_PRED_TOTAL_SYMBOLS - 1;
	const uint32_t ENDPOINT_PRED_MIN_REPEAT_COUNT = 3;
	const uint32_t ENDPOINT_PRED_COUNT_VLC_BITS = 4;

	const uint32_t NUM_ENDPOINT_PREDS = 3;// BASISU_ARRAY_SIZE(g_endpoint_preds);
	const uint32_t CR_ENDPOINT_PRED_INDEX = NUM_ENDPOINT_PREDS - 1;
	const uint32_t NO_ENDPOINT_PRED_INDEX = 3;//NUM_ENDPOINT_PREDS;
	const uint32_t MAX_SELECTOR_HISTORY_BUF_SIZE = 64;
	const uint32_t SELECTOR_HISTORY_BUF_RLE_COUNT_THRESH = 3;
	const uint32_t SELECTOR_HISTORY_BUF_RLE_COUNT_BITS = 6;
	const uint32_t SELECTOR_HISTORY_BUF_RLE_COUNT_TOTAL = (1 << SELECTOR_HISTORY_BUF_RLE_COUNT_BITS);
		
	uint16_t crc16(const void *r, size_t size, uint16_t crc);
		
	class huffman_decoding_table
	{
		friend class bitwise_decoder;

	public:
		huffman_decoding_table()
		{
		}

		void clear()
		{
			basisu::clear_vector(m_code_sizes);
			basisu::clear_vector(m_lookup);
			basisu::clear_vector(m_tree);
		}

		bool init(uint32_t total_syms, const uint8_t *pCode_sizes)
		{
			if (!total_syms)
			{
				clear();
				return true;
			}

			m_code_sizes.resize(total_syms);
			memcpy(&m_code_sizes[0], pCode_sizes, total_syms);

			m_lookup.resize(0);
			m_lookup.resize(basisu::cHuffmanFastLookupSize);

			m_tree.resize(0);
			m_tree.resize(total_syms * 2);

			uint32_t syms_using_codesize[basisu::cHuffmanMaxSupportedInternalCodeSize + 1];
			basisu::clear_obj(syms_using_codesize);
			for (uint32_t i = 0; i < total_syms; i++)
			{
				if (pCode_sizes[i] > basisu::cHuffmanMaxSupportedInternalCodeSize)
					return false;
				syms_using_codesize[pCode_sizes[i]]++;
			}

			uint32_t next_code[basisu::cHuffmanMaxSupportedInternalCodeSize + 1];
			next_code[0] = next_code[1] = 0;

			uint32_t used_syms = 0, total = 0;
			for (uint32_t i = 1; i < basisu::cHuffmanMaxSupportedInternalCodeSize; i++)
			{
				used_syms += syms_using_codesize[i];
				next_code[i + 1] = (total = ((total + syms_using_codesize[i]) << 1));
			}

			if (((1U << basisu::cHuffmanMaxSupportedInternalCodeSize) != total) && (used_syms > 1U))
				return false;

			for (int tree_next = -1, sym_index = 0; sym_index < (int)total_syms; ++sym_index)
			{
				uint32_t rev_code = 0, l, cur_code, code_size = pCode_sizes[sym_index];
				if (!code_size)
					continue;

				cur_code = next_code[code_size]++;

				for (l = code_size; l > 0; l--, cur_code >>= 1)
					rev_code = (rev_code << 1) | (cur_code & 1);

				if (code_size <= basisu::cHuffmanFastLookupBits)
				{
					uint32_t k = (code_size << 16) | sym_index;
					while (rev_code < basisu::cHuffmanFastLookupSize)
					{
						if (m_lookup[rev_code] != 0)
						{
							// Supplied codesizes can't create a valid prefix code.
							return false;
						}

						m_lookup[rev_code] = k;
						rev_code += (1 << code_size);
					}
					continue;
				}

				int tree_cur;
				if (0 == (tree_cur = m_lookup[rev_code & (basisu::cHuffmanFastLookupSize - 1)]))
				{
					const uint32_t idx = rev_code & (basisu::cHuffmanFastLookupSize - 1);
					if (m_lookup[idx] != 0)
					{
						// Supplied codesizes can't create a valid prefix code.
						return false;
					}

					m_lookup[idx] = tree_next;
					tree_cur = tree_next;
					tree_next -= 2;
				}

				if (tree_cur >= 0)
				{
					// Supplied codesizes can't create a valid prefix code.
					return false;
				}

				rev_code >>= (basisu::cHuffmanFastLookupBits - 1);

				for (int j = code_size; j > (basisu::cHuffmanFastLookupBits + 1); j--)
				{
					tree_cur -= ((rev_code >>= 1) & 1);

					const int idx = -tree_cur - 1;
					if (idx < 0)
						return false;
					else if (idx >= (int)m_tree.size())
						m_tree.resize(idx + 1);
										
					if (!m_tree[idx])
					{
						m_tree[idx] = (int16_t)tree_next;
						tree_cur = tree_next;
						tree_next -= 2;
					}
					else
					{
						tree_cur = m_tree[idx];
						if (tree_cur >= 0)
						{
							// Supplied codesizes can't create a valid prefix code.
							return false;
						}
					}
				}

				tree_cur -= ((rev_code >>= 1) & 1);

				const int idx = -tree_cur - 1;
				if (idx < 0)
					return false;
				else if (idx >= (int)m_tree.size())
					m_tree.resize(idx + 1);

				if (m_tree[idx] != 0)
				{
					// Supplied codesizes can't create a valid prefix code.
					return false;
				}

				m_tree[idx] = (int16_t)sym_index;
			}

			return true;
		}

		const basisu::uint8_vec &get_code_sizes() const { return m_code_sizes; }

		bool is_valid() const { return m_code_sizes.size() > 0; }

	private:
		basisu::uint8_vec m_code_sizes;
		basisu::int_vec m_lookup;
		basisu::int16_vec m_tree;
	};

	class bitwise_decoder
	{
	public:
		bitwise_decoder() :
			m_buf_size(0),
			m_pBuf(nullptr),
			m_pBuf_start(nullptr),
			m_pBuf_end(nullptr),
			m_bit_buf(0),
			m_bit_buf_size(0)
		{
		}

		void clear()
		{
			m_buf_size = 0;
			m_pBuf = nullptr;
			m_pBuf_start = nullptr;
			m_pBuf_end = nullptr;
			m_bit_buf = 0;
			m_bit_buf_size = 0;
		}

		bool init(const uint8_t *pBuf, uint32_t buf_size)
		{
			if ((!pBuf) && (buf_size))
				return false;

			m_buf_size = buf_size;
			m_pBuf = pBuf;
			m_pBuf_start = pBuf;
			m_pBuf_end = pBuf + buf_size;
			m_bit_buf = 0;
			m_bit_buf_size = 0;
			return true;
		}

		void stop()
		{
		}

		inline uint32_t peek_bits(uint32_t num_bits)
		{
			if (!num_bits)
				return 0;

			assert(num_bits <= 25);

			while (m_bit_buf_size < num_bits)
			{
				uint32_t c = 0;
				if (m_pBuf < m_pBuf_end)
					c = *m_pBuf++;

				m_bit_buf |= (c << m_bit_buf_size);
				m_bit_buf_size += 8;
				assert(m_bit_buf_size <= 32);
			}

			return m_bit_buf & ((1 << num_bits) - 1);
		}

		void remove_bits(uint32_t num_bits)
		{
			assert(m_bit_buf_size >= num_bits);

			m_bit_buf >>= num_bits;
			m_bit_buf_size -= num_bits;
		}

		uint32_t get_bits(uint32_t num_bits)
		{
			if (num_bits > 25)
			{
				assert(num_bits <= 32);

				const uint32_t bits0 = peek_bits(25);
				m_bit_buf >>= 25;
				m_bit_buf_size -= 25;
				num_bits -= 25;

				const uint32_t bits = peek_bits(num_bits);
				m_bit_buf >>= num_bits;
				m_bit_buf_size -= num_bits;

				return bits0 | (bits << 25);
			}

			const uint32_t bits = peek_bits(num_bits);

			m_bit_buf >>= num_bits;
			m_bit_buf_size -= num_bits;

			return bits;
		}

		uint32_t decode_truncated_binary(uint32_t n)
		{
			assert(n >= 2);

			const uint32_t k = basisu::floor_log2i(n);
			const uint32_t u = (1 << (k + 1)) - n;

			uint32_t result = get_bits(k);

			if (result >= u)
				result = ((result << 1) | get_bits(1)) - u;

			return result;
		}

		uint32_t decode_rice(uint32_t m)
		{
			assert(m);

			uint32_t q = 0;
			for (;;)
			{
				uint32_t k = peek_bits(16);
				
				uint32_t l = 0;
				while (k & 1)
				{
					l++;
					k >>= 1;
				}
				
				q += l;

				remove_bits(l);

				if (l < 16)
					break;
			}

			return (q << m) + (get_bits(m + 1) >> 1);
		}

		inline uint32_t decode_vlc(uint32_t chunk_bits)
		{
			assert(chunk_bits);

			const uint32_t chunk_size = 1 << chunk_bits;
			const uint32_t chunk_mask = chunk_size - 1;
					
			uint32_t v = 0;
			uint32_t ofs = 0;

			for ( ; ; )
			{
				uint32_t s = get_bits(chunk_bits + 1);
				v |= ((s & chunk_mask) << ofs);
				ofs += chunk_bits;

				if ((s & chunk_size) == 0)
					break;
				
				if (ofs >= 32)
				{
					assert(0);
					break;
				}
			}

			return v;
		}

		inline uint32_t decode_huffman(const huffman_decoding_table &ct)
		{
			assert(ct.m_code_sizes.size());
						
			while (m_bit_buf_size < 16)
			{
				uint32_t c = 0;
				if (m_pBuf < m_pBuf_end)
					c = *m_pBuf++;

				m_bit_buf |= (c << m_bit_buf_size);
				m_bit_buf_size += 8;
				assert(m_bit_buf_size <= 32);
			}
						
			int code_len;

			int sym;
			if ((sym = ct.m_lookup[m_bit_buf & (basisu::cHuffmanFastLookupSize - 1)]) >= 0)
			{
				code_len = sym >> 16;
				sym &= 0xFFFF;
			}
			else
			{
				code_len = basisu::cHuffmanFastLookupBits;
				do
				{
					sym = ct.m_tree[~sym + ((m_bit_buf >> code_len++) & 1)]; // ~sym = -sym - 1
				} while (sym < 0);
			}

			m_bit_buf >>= code_len;
			m_bit_buf_size -= code_len;

			return sym;
		}

		bool read_huffman_table(huffman_decoding_table &ct)
		{
			ct.clear();

			const uint32_t total_used_syms = get_bits(basisu::cHuffmanMaxSymsLog2);

			if (!total_used_syms)
				return true;
			if (total_used_syms > basisu::cHuffmanMaxSyms)
				return false;

			uint8_t code_length_code_sizes[basisu::cHuffmanTotalCodelengthCodes];
			basisu::clear_obj(code_length_code_sizes);

			const uint32_t num_codelength_codes = get_bits(5);
			if ((num_codelength_codes < 1) || (num_codelength_codes > basisu::cHuffmanTotalCodelengthCodes))
				return false;

			for (uint32_t i = 0; i < num_codelength_codes; i++)
				code_length_code_sizes[basisu::g_huffman_sorted_codelength_codes[i]] = static_cast<uint8_t>(get_bits(3));

			huffman_decoding_table code_length_table;
			if (!code_length_table.init(basisu::cHuffmanTotalCodelengthCodes, code_length_code_sizes))
				return false;

			if (!code_length_table.is_valid())
				return false;

			basisu::uint8_vec code_sizes(total_used_syms);

			uint32_t cur = 0;
			while (cur < total_used_syms)
			{
				int c = decode_huffman(code_length_table);

				if (c <= 16)
					code_sizes[cur++] = static_cast<uint8_t>(c);
				else if (c == basisu::cHuffmanSmallZeroRunCode)
					cur += get_bits(basisu::cHuffmanSmallZeroRunExtraBits) + basisu::cHuffmanSmallZeroRunSizeMin;
				else if (c == basisu::cHuffmanBigZeroRunCode)
					cur += get_bits(basisu::cHuffmanBigZeroRunExtraBits) + basisu::cHuffmanBigZeroRunSizeMin;
				else
				{
					if (!cur)
						return false;

					uint32_t l;
					if (c == basisu::cHuffmanSmallRepeatCode)
						l = get_bits(basisu::cHuffmanSmallRepeatExtraBits) + basisu::cHuffmanSmallRepeatSizeMin;
					else
						l = get_bits(basisu::cHuffmanBigRepeatExtraBits) + basisu::cHuffmanBigRepeatSizeMin;

					const uint8_t prev = code_sizes[cur - 1];
					if (prev == 0)
						return false;
					do
					{
						if (cur >= total_used_syms)
							return false;
						code_sizes[cur++] = prev;
					} while (--l > 0);
				}
			}

			if (cur != total_used_syms)
				return false;

			return ct.init(total_used_syms, &code_sizes[0]);
		}

	private:
		uint32_t m_buf_size;
		const uint8_t *m_pBuf;
		const uint8_t *m_pBuf_start;
		const uint8_t *m_pBuf_end;

		uint32_t m_bit_buf;
		uint32_t m_bit_buf_size;
	};

	inline uint32_t basisd_rand(uint32_t seed)
	{
		if (!seed)
			seed++;
		uint32_t z = seed;
		BASISD_znew;
		return z;
	}

	// Returns random number in [0,limit). Max limit is 0xFFFF.
	inline uint32_t basisd_urand(uint32_t& seed, uint32_t limit)
	{
		seed = basisd_rand(seed);
		return (((seed ^ (seed >> 16)) & 0xFFFF) * limit) >> 16;
	}

	class approx_move_to_front
	{
	public:
		approx_move_to_front(uint32_t n)
		{
			init(n);
		}

		void init(uint32_t n)
		{
			m_values.resize(n);
			m_rover = n / 2;
		}

		const basisu::int_vec& get_values() const { return m_values; }
		basisu::int_vec& get_values() { return m_values; }

		uint32_t size() const { return (uint32_t)m_values.size(); }

		const int& operator[] (uint32_t index) const { return m_values[index]; }
		int operator[] (uint32_t index) { return m_values[index]; }

		void add(int new_value)
		{
			m_values[m_rover++] = new_value;
			if (m_rover == m_values.size())
				m_rover = (uint32_t)m_values.size() / 2;
		}

		void use(uint32_t index)
		{
			if (index)
			{
				//std::swap(m_values[index / 2], m_values[index]);
				int x = m_values[index / 2];
				int y = m_values[index];
				m_values[index / 2] = y;
				m_values[index] = x;
			}
		}

		// returns -1 if not found
		int find(int value) const
		{
			for (uint32_t i = 0; i < m_values.size(); i++)
				if (m_values[i] == value)
					return i;
			return -1;
		}

		void reset()
		{
			const uint32_t n = (uint32_t)m_values.size();

			m_values.clear();

			init(n);
		}

	private:
		basisu::int_vec m_values;
		uint32_t m_rover;
	};

	struct decoder_etc_block;
	
	inline uint8_t clamp255(int32_t i)
	{
		return (uint8_t)((i & 0xFFFFFF00U) ? (~(i >> 31)) : i);
	}

	enum eNoClamp
	{
		cNoClamp = 0
	};

	struct color32
	{
		union
		{
			struct
			{
				uint8_t r;
				uint8_t g;
				uint8_t b;
				uint8_t a;
			};

			uint8_t c[4];
			
			uint32_t m;
		};

		color32() { }

		color32(uint32_t vr, uint32_t vg, uint32_t vb, uint32_t va) { set(vr, vg, vb, va); }
		color32(eNoClamp unused, uint32_t vr, uint32_t vg, uint32_t vb, uint32_t va) { (void)unused; set_noclamp_rgba(vr, vg, vb, va); }

		void set(uint32_t vr, uint32_t vg, uint32_t vb, uint32_t va) { c[0] = static_cast<uint8_t>(vr); c[1] = static_cast<uint8_t>(vg); c[2] = static_cast<uint8_t>(vb); c[3] = static_cast<uint8_t>(va); }

		void set_noclamp_rgb(uint32_t vr, uint32_t vg, uint32_t vb) { c[0] = static_cast<uint8_t>(vr); c[1] = static_cast<uint8_t>(vg); c[2] = static_cast<uint8_t>(vb); }
		void set_noclamp_rgba(uint32_t vr, uint32_t vg, uint32_t vb, uint32_t va) { set(vr, vg, vb, va); }

		void set_clamped(int vr, int vg, int vb, int va) { c[0] = clamp255(vr); c[1] = clamp255(vg);	c[2] = clamp255(vb); c[3] = clamp255(va); }

		uint8_t operator[] (uint32_t idx) const { assert(idx < 4); return c[idx]; }
		uint8_t &operator[] (uint32_t idx) { assert(idx < 4); return c[idx]; }

		bool operator== (const color32&rhs) const { return m == rhs.m; }

		static color32 comp_min(const color32& a, const color32& b) { return color32(cNoClamp, std::min(a[0], b[0]), std::min(a[1], b[1]), std::min(a[2], b[2]), std::min(a[3], b[3])); }
		static color32 comp_max(const color32& a, const color32& b) { return color32(cNoClamp, std::max(a[0], b[0]), std::max(a[1], b[1]), std::max(a[2], b[2]), std::max(a[3], b[3])); }
	};

	struct endpoint
	{
		color32 m_color5;
		uint8_t m_inten5;
	};

	struct selector
	{
		// Plain selectors (2-bits per value)
		uint8_t m_selectors[4];

		// ETC1 selectors
		uint8_t m_bytes[4];

		uint8_t m_lo_selector, m_hi_selector;
		uint8_t m_num_unique_selectors;

		void init_flags()
		{
			uint32_t hist[4] = { 0, 0, 0, 0 };
			for (uint32_t y = 0; y < 4; y++)
			{
				for (uint32_t x = 0; x < 4; x++)
				{
					uint32_t s = get_selector(x, y);
					hist[s]++;
				}
			}

			m_lo_selector = 3;
			m_hi_selector = 0;
			m_num_unique_selectors = 0;

			for (uint32_t i = 0; i < 4; i++)
			{
				if (hist[i])
				{
					m_num_unique_selectors++;
					if (i < m_lo_selector) m_lo_selector = static_cast<uint8_t>(i);
					if (i > m_hi_selector) m_hi_selector = static_cast<uint8_t>(i);
				}
			}
		}

		// Returned selector value ranges from 0-3 and is a direct index into g_etc1_inten_tables.
		inline uint32_t get_selector(uint32_t x, uint32_t y) const
		{
			assert((x < 4) && (y < 4));
			return (m_selectors[y] >> (x * 2)) & 3;
		}

		void set_selector(uint32_t x, uint32_t y, uint32_t val)
		{
			static const uint8_t s_selector_index_to_etc1[4] = { 3, 2, 0, 1 };

			assert((x | y | val) < 4);

			m_selectors[y] &= ~(3 << (x * 2));
			m_selectors[y] |= (val << (x * 2));

			const uint32_t etc1_bit_index = x * 4 + y;

			uint8_t *p = &m_bytes[3 - (etc1_bit_index >> 3)];

			const uint32_t byte_bit_ofs = etc1_bit_index & 7;
			const uint32_t mask = 1 << byte_bit_ofs;

			const uint32_t etc1_val = s_selector_index_to_etc1[val];

			const uint32_t lsb = etc1_val & 1;
			const uint32_t msb = etc1_val >> 1;

			p[0] &= ~mask;
			p[0] |= (lsb << byte_bit_ofs);

			p[-2] &= ~mask;
			p[-2] |= (msb << byte_bit_ofs);
		}
	};

	bool basis_block_format_is_uncompressed(block_format tex_type);
	
} // namespace basist



/**** ended inlining basisu_transcoder_internal.h ****/
/**** start inlining basisu_transcoder_uastc.h ****/
// basisu_transcoder_uastc.h
/**** skipping file: basisu_transcoder_internal.h ****/

namespace basist
{
	struct color_quad_u8
	{ 
		uint8_t m_c[4]; 
	};

	const uint32_t TOTAL_UASTC_MODES = 19;
	const uint32_t UASTC_MODE_INDEX_SOLID_COLOR = 8;

	const uint32_t TOTAL_ASTC_BC7_COMMON_PARTITIONS2 = 30;
	const uint32_t TOTAL_ASTC_BC7_COMMON_PARTITIONS3 = 11;
	const uint32_t TOTAL_BC7_3_ASTC2_COMMON_PARTITIONS = 19;

	extern const uint8_t g_uastc_mode_weight_bits[TOTAL_UASTC_MODES];
	extern const uint8_t g_uastc_mode_weight_ranges[TOTAL_UASTC_MODES];
	extern const uint8_t g_uastc_mode_endpoint_ranges[TOTAL_UASTC_MODES];
	extern const uint8_t g_uastc_mode_subsets[TOTAL_UASTC_MODES];
	extern const uint8_t g_uastc_mode_planes[TOTAL_UASTC_MODES];
	extern const uint8_t g_uastc_mode_comps[TOTAL_UASTC_MODES];
	extern const uint8_t g_uastc_mode_has_etc1_bias[TOTAL_UASTC_MODES];
	extern const uint8_t g_uastc_mode_has_bc1_hint0[TOTAL_UASTC_MODES];
	extern const uint8_t g_uastc_mode_has_bc1_hint1[TOTAL_UASTC_MODES];
	extern const uint8_t g_uastc_mode_has_alpha[TOTAL_UASTC_MODES];
	extern const uint8_t g_uastc_mode_is_la[TOTAL_UASTC_MODES];

	struct astc_bc7_common_partition2_desc
	{
		uint8_t m_bc7;
		uint16_t m_astc;
		bool m_invert;
	};

	extern const astc_bc7_common_partition2_desc g_astc_bc7_common_partitions2[TOTAL_ASTC_BC7_COMMON_PARTITIONS2];

	struct bc73_astc2_common_partition_desc
	{
		uint8_t m_bc73;
		uint16_t m_astc2;
		uint8_t k;		// 0-5 - how to modify the BC7 3-subset pattern to match the ASTC pattern (LSB=invert)
	};

	extern const bc73_astc2_common_partition_desc g_bc7_3_astc2_common_partitions[TOTAL_BC7_3_ASTC2_COMMON_PARTITIONS];

	struct astc_bc7_common_partition3_desc
	{
		uint8_t m_bc7;
		uint16_t m_astc;
		uint8_t m_astc_to_bc7_perm; // converts ASTC to BC7 partition using g_astc_bc7_partition_index_perm_tables[][]
	};

	extern const astc_bc7_common_partition3_desc g_astc_bc7_common_partitions3[TOTAL_ASTC_BC7_COMMON_PARTITIONS3];

	extern const uint8_t g_astc_bc7_patterns2[TOTAL_ASTC_BC7_COMMON_PARTITIONS2][16];
	extern const uint8_t g_astc_bc7_patterns3[TOTAL_ASTC_BC7_COMMON_PARTITIONS3][16];
	extern const uint8_t g_bc7_3_astc2_patterns2[TOTAL_BC7_3_ASTC2_COMMON_PARTITIONS][16];

	extern const uint8_t g_astc_bc7_pattern2_anchors[TOTAL_ASTC_BC7_COMMON_PARTITIONS2][3];
	extern const uint8_t g_astc_bc7_pattern3_anchors[TOTAL_ASTC_BC7_COMMON_PARTITIONS3][3];
	extern const uint8_t g_bc7_3_astc2_patterns2_anchors[TOTAL_BC7_3_ASTC2_COMMON_PARTITIONS][3];

	extern const uint32_t g_uastc_mode_huff_codes[TOTAL_UASTC_MODES + 1][2];

	extern const uint8_t g_astc_to_bc7_partition_index_perm_tables[6][3];
	extern const uint8_t g_bc7_to_astc_partition_index_perm_tables[6][3]; // inverse of g_astc_to_bc7_partition_index_perm_tables

	extern const uint8_t* s_uastc_to_bc1_weights[6];

	uint32_t bc7_convert_partition_index_3_to_2(uint32_t p, uint32_t k);

	inline uint32_t astc_interpolate(uint32_t l, uint32_t h, uint32_t w, bool srgb)
	{
		if (srgb)
		{
			l = (l << 8) | 0x80;
			h = (h << 8) | 0x80;
		}
		else
		{
			l = (l << 8) | l;
			h = (h << 8) | h;
		}

		uint32_t k = (l * (64 - w) + h * w + 32) >> 6;

		return k >> 8;
	}

	struct astc_block_desc
	{
		int m_weight_range;	// weight BISE range

		int m_subsets;			// number of ASTC partitions
		int m_partition_seed;	// partition pattern seed
		int m_cem;				// color endpoint mode used by all subsets

		int m_ccs;				// color component selector (dual plane only)
		bool m_dual_plane;	// true if dual plane

		// Weight and endpoint BISE values. 
		// Note these values are NOT linear, they must be BISE encoded. See Table 97 and Table 107.
		uint8_t m_endpoints[18];	// endpoint values, in RR GG BB etc. order 
		uint8_t m_weights[64];		// weight index values, raster order, in P0 P1, P0 P1, etc. or P0, P0, P0, P0, etc. order
	};

	const uint32_t BC7ENC_TOTAL_ASTC_RANGES = 21;

	// See tables 81, 93, 18.13.Endpoint Unquantization
	const uint32_t TOTAL_ASTC_RANGES = 21;
	extern const int g_astc_bise_range_table[TOTAL_ASTC_RANGES][3];

	struct astc_quant_bin
	{
		uint8_t m_unquant; // unquantized value
		uint8_t m_index; // sorted index
	};

	extern astc_quant_bin g_astc_unquant[BC7ENC_TOTAL_ASTC_RANGES][256]; // [ASTC encoded endpoint index]

	int astc_get_levels(int range);
	bool astc_is_valid_endpoint_range(uint32_t range);
	uint32_t unquant_astc_endpoint(uint32_t packed_bits, uint32_t packed_trits, uint32_t packed_quints, uint32_t range);
	uint32_t unquant_astc_endpoint_val(uint32_t packed_val, uint32_t range);

	const uint8_t* get_anchor_indices(uint32_t subsets, uint32_t mode, uint32_t common_pattern, const uint8_t*& pPartition_pattern);

	// BC7
	const uint32_t BC7ENC_BLOCK_SIZE = 16;

	struct bc7_block
	{
		uint64_t m_qwords[2];
	};

	struct bc7_optimization_results
	{
		uint32_t m_mode;
		uint32_t m_partition;
		uint8_t m_selectors[16];
		uint8_t m_alpha_selectors[16];
		color_quad_u8 m_low[3];
		color_quad_u8 m_high[3];
		uint32_t m_pbits[3][2];
		uint32_t m_index_selector;
		uint32_t m_rotation;
	};

	extern const uint32_t g_bc7_weights1[2];
	extern const uint32_t g_bc7_weights2[4];
	extern const uint32_t g_bc7_weights3[8];
	extern const uint32_t g_bc7_weights4[16];
	extern const uint32_t g_astc_weights4[16];
	extern const uint32_t g_astc_weights5[32];
	extern const uint32_t g_astc_weights_3levels[3];
	extern const uint8_t g_bc7_partition1[16];
	extern const uint8_t g_bc7_partition2[64 * 16];
	extern const uint8_t g_bc7_partition3[64 * 16];
	extern const uint8_t g_bc7_table_anchor_index_second_subset[64];
	extern const uint8_t g_bc7_table_anchor_index_third_subset_1[64];
	extern const uint8_t g_bc7_table_anchor_index_third_subset_2[64];
	extern const uint8_t g_bc7_num_subsets[8];
	extern const uint8_t g_bc7_partition_bits[8];
	extern const uint8_t g_bc7_color_index_bitcount[8];
	extern const uint8_t g_bc7_mode_has_p_bits[8];
	extern const uint8_t g_bc7_mode_has_shared_p_bits[8];
	extern const uint8_t g_bc7_color_precision_table[8];
	extern const int8_t g_bc7_alpha_precision_table[8];
	extern const uint8_t g_bc7_alpha_index_bitcount[8];

	inline bool get_bc7_mode_has_seperate_alpha_selectors(int mode) { return (mode == 4) || (mode == 5); }
	inline int get_bc7_color_index_size(int mode, int index_selection_bit) { return g_bc7_color_index_bitcount[mode] + index_selection_bit; }
	inline int get_bc7_alpha_index_size(int mode, int index_selection_bit) { return g_bc7_alpha_index_bitcount[mode] - index_selection_bit; }

	struct endpoint_err
	{
		uint16_t m_error; uint8_t m_lo; uint8_t m_hi;
	};

	extern endpoint_err g_bc7_mode_6_optimal_endpoints[256][2]; // [c][pbit]
	const uint32_t BC7ENC_MODE_6_OPTIMAL_INDEX = 5;

	extern endpoint_err g_bc7_mode_5_optimal_endpoints[256]; // [c]
	const uint32_t BC7ENC_MODE_5_OPTIMAL_INDEX = 1;

	// Packs a BC7 block from a high-level description. Handles all BC7 modes.
	void encode_bc7_block(void* pBlock, const bc7_optimization_results* pResults);

	// Packs an ASTC block
	// Constraints: Always 4x4, all subset CEM's must be equal, only tested with LDR CEM's.
	bool pack_astc_block(uint32_t* pDst, const astc_block_desc* pBlock, uint32_t mode);

	void pack_astc_solid_block(void* pDst_block, const color32& color);

#ifdef _DEBUG
	int astc_compute_texel_partition(int seed, int x, int y, int z, int partitioncount, bool small_block);
#endif
		
	struct uastc_block
	{
		union
		{
			uint8_t m_bytes[16];
			uint32_t m_dwords[4];
			uint64_t m_qwords[2];
		};
	};

	struct unpacked_uastc_block
	{
		astc_block_desc m_astc;

		uint32_t m_mode;
		uint32_t m_common_pattern;

		color32 m_solid_color;

		bool m_bc1_hint0;
		bool m_bc1_hint1;

		bool m_etc1_flip;
		bool m_etc1_diff;
		uint32_t m_etc1_inten0;
		uint32_t m_etc1_inten1;

		uint32_t m_etc1_bias;

		uint32_t m_etc2_hints;

		uint32_t m_etc1_selector;
		uint32_t m_etc1_r, m_etc1_g, m_etc1_b;
	};

	color32 apply_etc1_bias(const color32 &block_color, uint32_t bias, uint32_t limit, uint32_t subblock);
	
	struct decoder_etc_block;
	struct eac_block;
		
	bool unpack_uastc(uint32_t mode, uint32_t common_pattern, const color32& solid_color, const astc_block_desc& astc, color32* pPixels, bool srgb);
	bool unpack_uastc(const unpacked_uastc_block& unpacked_blk, color32* pPixels, bool srgb);

	bool unpack_uastc(const uastc_block& blk, color32* pPixels, bool srgb);
	bool unpack_uastc(const uastc_block& blk, unpacked_uastc_block& unpacked, bool undo_blue_contract, bool read_hints = true);

	bool transcode_uastc_to_astc(const uastc_block& src_blk, void* pDst);

	bool transcode_uastc_to_bc7(const unpacked_uastc_block& unpacked_src_blk, bc7_optimization_results& dst_blk);
	bool transcode_uastc_to_bc7(const uastc_block& src_blk, bc7_optimization_results& dst_blk);
	bool transcode_uastc_to_bc7(const uastc_block& src_blk, void* pDst);

	void transcode_uastc_to_etc1(unpacked_uastc_block& unpacked_src_blk, color32 block_pixels[4][4], void* pDst);
	bool transcode_uastc_to_etc1(const uastc_block& src_blk, void* pDst);
	bool transcode_uastc_to_etc1(const uastc_block& src_blk, void* pDst, uint32_t channel);

	void transcode_uastc_to_etc2_eac_a8(unpacked_uastc_block& unpacked_src_blk, color32 block_pixels[4][4], void* pDst);
	bool transcode_uastc_to_etc2_rgba(const uastc_block& src_blk, void* pDst);

	// Packs 16 scalar values to BC4. Same PSNR as stb_dxt's BC4 encoder, around 13% faster.
	void encode_bc4(void* pDst, const uint8_t* pPixels, uint32_t stride);
	
	void encode_bc1_solid_block(void* pDst, uint32_t fr, uint32_t fg, uint32_t fb);

	enum
	{
		cEncodeBC1HighQuality = 1,
		cEncodeBC1HigherQuality = 2,
		cEncodeBC1UseSelectors = 4,
	};
	void encode_bc1(void* pDst, const uint8_t* pPixels, uint32_t flags);
	
	// Alternate PCA-free encoder, around 15% faster, same (or slightly higher) avg. PSNR
	void encode_bc1_alt(void* pDst, const uint8_t* pPixels, uint32_t flags);

	void transcode_uastc_to_bc1_hint0(const unpacked_uastc_block& unpacked_src_blk, void* pDst);
	void transcode_uastc_to_bc1_hint1(const unpacked_uastc_block& unpacked_src_blk, const color32 block_pixels[4][4], void* pDst, bool high_quality);

	bool transcode_uastc_to_bc1(const uastc_block& src_blk, void* pDst, bool high_quality);
	bool transcode_uastc_to_bc3(const uastc_block& src_blk, void* pDst, bool high_quality);
	bool transcode_uastc_to_bc4(const uastc_block& src_blk, void* pDst, bool high_quality, uint32_t chan0);
	bool transcode_uastc_to_bc5(const uastc_block& src_blk, void* pDst, bool high_quality, uint32_t chan0, uint32_t chan1);

	bool transcode_uastc_to_etc2_eac_r11(const uastc_block& src_blk, void* pDst, bool high_quality, uint32_t chan0);
	bool transcode_uastc_to_etc2_eac_rg11(const uastc_block& src_blk, void* pDst, bool high_quality, uint32_t chan0, uint32_t chan1);

	bool transcode_uastc_to_pvrtc1_4_rgb(const uastc_block* pSrc_blocks, void* pDst_blocks, uint32_t num_blocks_x, uint32_t num_blocks_y, bool high_quality, bool from_alpha);
	bool transcode_uastc_to_pvrtc1_4_rgba(const uastc_block* pSrc_blocks, void* pDst_blocks, uint32_t num_blocks_x, uint32_t num_blocks_y, bool high_quality);
		
	// uastc_init() MUST be called before using this module.
	void uastc_init();

} // namespace basist
/**** ended inlining basisu_transcoder_uastc.h ****/
/**** start inlining basisu_global_selector_palette.h ****/
// basisu_global_selector_palette.h
// Copyright (C) 2019-2020 Binomial LLC. All Rights Reserved.
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
/**** skipping file: basisu_transcoder_internal.h ****/
#include <algorithm>

namespace basist
{
	class etc1_global_palette_entry_modifier
	{
	public:
		enum { cTotalBits = 15, cTotalValues = 1 << cTotalBits };

		etc1_global_palette_entry_modifier(uint32_t index = 0)
		{
#ifdef _DEBUG
			static bool s_tested;
			if (!s_tested)
			{
				s_tested = true;
				for (uint32_t i = 0; i < cTotalValues; i++)
				{
					etc1_global_palette_entry_modifier m(i);
					etc1_global_palette_entry_modifier n = m;

					assert(n.get_index() == i);
				}
			}
#endif

			set_index(index);
		}

		void set_index(uint32_t index)
		{
			assert(index < cTotalValues);
			m_rot = index & 3;
			m_flip = (index >> 2) & 1;
			m_inv = (index >> 3) & 1;
			m_contrast = (index >> 4) & 3;
			m_shift = (index >> 6) & 1;
			m_median = (index >> 7) & 1;
			m_div = (index >> 8) & 1;
			m_rand = (index >> 9) & 1;
			m_dilate = (index >> 10) & 1;
			m_shift_x = (index >> 11) & 1;
			m_shift_y = (index >> 12) & 1;
			m_erode = (index >> 13) & 1;
			m_high_pass = (index >> 14) & 1;
		}

		uint32_t get_index() const
		{
			return m_rot | (m_flip << 2) | (m_inv << 3) | (m_contrast << 4) | (m_shift << 6) | (m_median << 7) | (m_div << 8) | (m_rand << 9) | (m_dilate << 10) | (m_shift_x << 11) | (m_shift_y << 12) | (m_erode << 13) | (m_high_pass << 14);
		}

		void clear()
		{
			basisu::clear_obj(*this);
		}

		uint8_t m_contrast;
		bool m_rand;
		bool m_median;
		bool m_div;
		bool m_shift;
		bool m_inv;
		bool m_flip;
		bool m_dilate;
		bool m_shift_x;
		bool m_shift_y;
		bool m_erode;
		bool m_high_pass;
		uint8_t m_rot;
	};

	enum modifier_types
	{
		cModifierContrast,
		cModifierRand,
		cModifierMedian,
		cModifierDiv,
		cModifierShift,
		cModifierInv,
		cModifierFlippedAndRotated,
		cModifierDilate,
		cModifierShiftX,
		cModifierShiftY,
		cModifierErode,
		cModifierHighPass,
		cTotalModifiers
	};

#define ETC1_GLOBAL_SELECTOR_CODEBOOK_MAX_MOD_BITS (etc1_global_palette_entry_modifier::cTotalBits)

	struct etc1_selector_palette_entry
	{
		etc1_selector_palette_entry()
		{
			clear();
		}

		void clear()
		{
			basisu::clear_obj(*this);
		}

		uint8_t operator[] (uint32_t i) const { assert(i < 16); return m_selectors[i]; }
		uint8_t&operator[] (uint32_t i) { assert(i < 16); return m_selectors[i]; }

		void set_uint32(uint32_t v)
		{
			for (uint32_t byte_index = 0; byte_index < 4; byte_index++)
			{
				uint32_t b = (v >> (byte_index * 8)) & 0xFF;

				m_selectors[byte_index * 4 + 0] = b & 3;
				m_selectors[byte_index * 4 + 1] = (b >> 2) & 3;
				m_selectors[byte_index * 4 + 2] = (b >> 4) & 3;
				m_selectors[byte_index * 4 + 3] = (b >> 6) & 3;
			}
		}

		uint32_t get_uint32() const
		{
			return get_byte(0) | (get_byte(1) << 8) | (get_byte(2) << 16) | (get_byte(3) << 24);
		}

		uint32_t get_byte(uint32_t byte_index) const
		{
			assert(byte_index < 4);

			return m_selectors[byte_index * 4 + 0] |
				(m_selectors[byte_index * 4 + 1] << 2) |
				(m_selectors[byte_index * 4 + 2] << 4) |
				(m_selectors[byte_index * 4 + 3] << 6);
		}

		uint8_t operator()(uint32_t x, uint32_t y) const { assert((x < 4) && (y < 4)); return m_selectors[x + y * 4]; }
		uint8_t&operator()(uint32_t x, uint32_t y) { assert((x < 4) && (y < 4)); return m_selectors[x + y * 4]; }

		uint32_t calc_distance(const etc1_selector_palette_entry &other) const
		{
			uint32_t dist = 0;
			for (uint32_t i = 0; i < 8; i++)
			{
				int delta = static_cast<int>(m_selectors[i]) - static_cast<int>(other.m_selectors[i]);
				dist += delta * delta;
			}
			return dist;
		}

#if 0
		uint32_t calc_hamming_dist(const etc1_selector_palette_entry &other) const
		{
			uint32_t dist = 0;
			for (uint32_t i = 0; i < 4; i++)
				dist += g_hamming_dist[get_byte(i) ^ other.get_byte(i)];
			return dist;
		}
#endif

		etc1_selector_palette_entry get_inverted() const
		{
			etc1_selector_palette_entry result;

			for (uint32_t i = 0; i < 16; i++)
				result.m_selectors[i] = 3 - m_selectors[i];

			return result;
		}

		etc1_selector_palette_entry get_divided() const
		{
			etc1_selector_palette_entry result;

			const uint8_t div_selector[4] = { 2, 0, 3, 1 };

			for (uint32_t i = 0; i < 16; i++)
				result.m_selectors[i] = div_selector[m_selectors[i]];

			return result;
		}

		etc1_selector_palette_entry get_shifted(int delta) const
		{
			etc1_selector_palette_entry result;

			for (uint32_t i = 0; i < 16; i++)
				result.m_selectors[i] = static_cast<uint8_t>(basisu::clamp<int>(m_selectors[i] + delta, 0, 3));

			return result;
		}

		etc1_selector_palette_entry get_randomized() const
		{
			uint32_t seed = get_uint32();

			etc1_selector_palette_entry result;

			for (uint32_t y = 0; y < 4; y++)
			{
				for (uint32_t x = 0; x < 4; x++)
				{
					int s = (*this)(x, y);

					// between 0 and 10
					uint32_t i = basisd_urand(seed, 6) + basisd_urand(seed, 6);
					if (i == 0)
						s -= 2;
					else if (i == 10)
						s += 2;
					else if (i < 3)
						s -= 1;
					else if (i > 7)
						s += 1;

					result(x, y) = static_cast<uint8_t>(basisu::clamp<int>(s, 0, 3));
				}
			}

			return result;
		}

		etc1_selector_palette_entry get_contrast(int table_index) const
		{
			assert(table_index < 4);

			etc1_selector_palette_entry result;

			static const uint8_t s_contrast_tables[4][4] =
			{
				{ 0, 1, 2, 3 }, // not used
				{ 0, 0, 3, 3 },
				{ 1, 1, 2, 2 },
				{ 1, 1, 3, 3 }
			};

			for (uint32_t i = 0; i < 16; i++)
			{
				result[i] = s_contrast_tables[table_index][(*this)[i]];
			}

			return result;
		}

		etc1_selector_palette_entry get_dilated() const
		{
			etc1_selector_palette_entry result;

			for (uint32_t y = 0; y < 4; y++)
			{
				for (uint32_t x = 0; x < 4; x++)
				{
					uint32_t max_selector = 0;

					for (int yd = -1; yd <= 1; yd++)
					{
						int fy = y + yd;
						if ((fy < 0) || (fy > 3))
							continue;

						for (int xd = -1; xd <= 1; xd++)
						{
							int fx = x + xd;
							if ((fx < 0) || (fx > 3))
								continue;

							max_selector = basisu::maximum<uint32_t>(max_selector, (*this)(fx, fy));
						}
					}

					result(x, y) = static_cast<uint8_t>(max_selector);
				}
			}

			return result;
		}

		etc1_selector_palette_entry get_eroded() const
		{
			etc1_selector_palette_entry result;

			for (uint32_t y = 0; y < 4; y++)
			{
				for (uint32_t x = 0; x < 4; x++)
				{
					uint32_t min_selector = 99;

					for (int yd = -1; yd <= 1; yd++)
					{
						int fy = y + yd;
						if ((fy < 0) || (fy > 3))
							continue;

						for (int xd = -1; xd <= 1; xd++)
						{
							int fx = x + xd;
							if ((fx < 0) || (fx > 3))
								continue;

							min_selector = basisu::minimum<uint32_t>(min_selector, (*this)(fx, fy));
						}
					}

					result(x, y) = static_cast<uint8_t>(min_selector);
				}
			}

			return result;
		}

		etc1_selector_palette_entry get_shift_x() const
		{
			etc1_selector_palette_entry result;

			for (uint32_t y = 0; y < 4; y++)
			{
				for (uint32_t x = 0; x < 4; x++)
				{
					int sx = x - 1;
					if (sx < 0)
						sx = 0;

					result(x, y) = (*this)(sx, y);
				}
			}

			return result;
		}

		etc1_selector_palette_entry get_shift_y() const
		{
			etc1_selector_palette_entry result;

			for (uint32_t y = 0; y < 4; y++)
			{
				int sy = y - 1;
				if (sy < 0)
					sy = 3;

				for (uint32_t x = 0; x < 4; x++)
					result(x, y) = (*this)(x, sy);
			}

			return result;
		}

		etc1_selector_palette_entry get_median() const
		{
			etc1_selector_palette_entry result;

			for (uint32_t y = 0; y < 4; y++)
			{
				for (uint32_t x = 0; x < 4; x++)
				{
					// ABC
					// D F
					// GHI

					uint8_t selectors[8];
					uint32_t n = 0;

					for (int yd = -1; yd <= 1; yd++)
					{
						int fy = y + yd;
						if ((fy < 0) || (fy > 3))
							continue;

						for (int xd = -1; xd <= 1; xd++)
						{
							if ((xd | yd) == 0)
								continue;

							int fx = x + xd;
							if ((fx < 0) || (fx > 3))
								continue;

							selectors[n++] = (*this)(fx, fy);
						}
					}

					std::sort(selectors, selectors + n);

					result(x, y) = selectors[n / 2];
				}
			}

			return result;
		}

		etc1_selector_palette_entry get_high_pass() const
		{
			etc1_selector_palette_entry result;

			static const int kernel[3][3] =
			{
				{ 0,  -1,  0 },
				{ -1,  8, -1 },
				{ 0,  -1,  0 }
			};

			for (uint32_t y = 0; y < 4; y++)
			{
				for (uint32_t x = 0; x < 4; x++)
				{
					// ABC
					// D F
					// GHI

					int sum = 0;

					for (int yd = -1; yd <= 1; yd++)
					{
						int fy = y + yd;
						fy = basisu::clamp<int>(fy, 0, 3);

						for (int xd = -1; xd <= 1; xd++)
						{
							int fx = x + xd;
							fx = basisu::clamp<int>(fx, 0, 3);

							int k = (*this)(fx, fy);
							sum += k * kernel[yd + 1][xd + 1];
						}
					}

					sum = sum / 4;

					result(x, y) = static_cast<uint8_t>(basisu::clamp<int>(sum, 0, 3));
				}
			}

			return result;
		}

		etc1_selector_palette_entry get_flipped_and_rotated(bool flip, uint32_t rotation_index) const
		{
			etc1_selector_palette_entry temp;

			if (flip)
			{
				for (uint32_t y = 0; y < 4; y++)
					for (uint32_t x = 0; x < 4; x++)
						temp(x, y) = (*this)(x, 3 - y);
			}
			else
			{
				temp = *this;
			}

			etc1_selector_palette_entry result;

			switch (rotation_index)
			{
			case 0:
				result = temp;
				break;
			case 1:
				for (uint32_t y = 0; y < 4; y++)
					for (uint32_t x = 0; x < 4; x++)
						result(x, y) = temp(y, 3 - x);
				break;
			case 2:
				for (uint32_t y = 0; y < 4; y++)
					for (uint32_t x = 0; x < 4; x++)
						result(x, y) = temp(3 - x, 3 - y);
				break;
			case 3:
				for (uint32_t y = 0; y < 4; y++)
					for (uint32_t x = 0; x < 4; x++)
						result(x, y) = temp(3 - y, x);
				break;
			default:
				assert(0);
				break;
			}

			return result;
		}

		etc1_selector_palette_entry get_modified(const etc1_global_palette_entry_modifier &modifier) const
		{
			etc1_selector_palette_entry r(*this);

			if (modifier.m_shift_x)
				r = r.get_shift_x();

			if (modifier.m_shift_y)
				r = r.get_shift_y();

			r = r.get_flipped_and_rotated(modifier.m_flip != 0, modifier.m_rot);

			if (modifier.m_dilate)
				r = r.get_dilated();

			if (modifier.m_erode)
				r = r.get_eroded();

			if (modifier.m_high_pass)
				r = r.get_high_pass();

			if (modifier.m_rand)
				r = r.get_randomized();

			if (modifier.m_div)
				r = r.get_divided();

			if (modifier.m_shift)
				r = r.get_shifted(1);

			if (modifier.m_contrast)
				r = r.get_contrast(modifier.m_contrast);

			if (modifier.m_inv)
				r = r.get_inverted();

			if (modifier.m_median)
				r = r.get_median();

			return r;
		}

		etc1_selector_palette_entry apply_modifier(modifier_types mod_type, const etc1_global_palette_entry_modifier &modifier) const
		{
			switch (mod_type)
			{
			case cModifierContrast:
				return get_contrast(modifier.m_contrast);
			case cModifierRand:
				return get_randomized();
			case cModifierMedian:
				return get_median();
			case cModifierDiv:
				return get_divided();
			case cModifierShift:
				return get_shifted(1);
			case cModifierInv:
				return get_inverted();
			case cModifierFlippedAndRotated:
				return get_flipped_and_rotated(modifier.m_flip != 0, modifier.m_rot);
			case cModifierDilate:
				return get_dilated();
			case cModifierShiftX:
				return get_shift_x();
			case cModifierShiftY:
				return get_shift_y();
			case cModifierErode:
				return get_eroded();
			case cModifierHighPass:
				return get_high_pass();
			default:
				assert(0);
				break;
			}

			return *this;
		}

		etc1_selector_palette_entry get_modified(const etc1_global_palette_entry_modifier &modifier, uint32_t num_order, const modifier_types *pOrder) const
		{
			etc1_selector_palette_entry r(*this);

			for (uint32_t i = 0; i < num_order; i++)
			{
				r = r.apply_modifier(pOrder[i], modifier);
			}

			return r;
		}

		bool operator< (const etc1_selector_palette_entry &other) const
		{
			for (uint32_t i = 0; i < 16; i++)
			{
				if (m_selectors[i] < other.m_selectors[i])
					return true;
				else if (m_selectors[i] != other.m_selectors[i])
					return false;
			}

			return false;
		}

		bool operator== (const etc1_selector_palette_entry &other) const
		{
			for (uint32_t i = 0; i < 16; i++)
			{
				if (m_selectors[i] != other.m_selectors[i])
					return false;
			}

			return true;
		}

	private:
		uint8_t m_selectors[16];
	};

	typedef std::vector<etc1_selector_palette_entry> etc1_selector_palette_entry_vec;

	extern const uint32_t g_global_selector_cb[];
	extern const uint32_t g_global_selector_cb_size;

#define ETC1_GLOBAL_SELECTOR_CODEBOOK_MAX_PAL_BITS (12)

	struct etc1_global_selector_codebook_entry_id
	{
		uint32_t m_palette_index;
		etc1_global_palette_entry_modifier m_modifier;

		etc1_global_selector_codebook_entry_id(uint32_t palette_index, const etc1_global_palette_entry_modifier &modifier) : m_palette_index(palette_index), m_modifier(modifier) { }

		etc1_global_selector_codebook_entry_id() { }

		void set(uint32_t palette_index, const etc1_global_palette_entry_modifier &modifier) { m_palette_index = palette_index; m_modifier = modifier; }
	};

	typedef std::vector<etc1_global_selector_codebook_entry_id> etc1_global_selector_codebook_entry_id_vec;

	class etc1_global_selector_codebook
	{
	public:
		etc1_global_selector_codebook() { }
		etc1_global_selector_codebook(uint32_t N, const uint32_t *pEntries) { init(N, pEntries); }

		void init(uint32_t N, const uint32_t* pEntries);

		void print_code(FILE *pFile);

		void clear()
		{
			m_palette.clear();
		}

		uint32_t size() const { return (uint32_t)m_palette.size(); }

		const etc1_selector_palette_entry_vec &get_palette() const
		{
			return m_palette;
		}

		etc1_selector_palette_entry get_entry(uint32_t palette_index) const
		{
			return m_palette[palette_index];
		}

		etc1_selector_palette_entry get_entry(uint32_t palette_index, const etc1_global_palette_entry_modifier &modifier) const
		{
			return m_palette[palette_index].get_modified(modifier);
		}

		etc1_selector_palette_entry get_entry(const etc1_global_selector_codebook_entry_id &id) const
		{
			return m_palette[id.m_palette_index].get_modified(id.m_modifier);
		}

		etc1_selector_palette_entry_vec m_palette;
	};

} // namespace basist
/**** ended inlining basisu_global_selector_palette.h ****/
/**** start inlining basisu_file_headers.h ****/
// basis_file_headers.h
// Copyright (C) 2019-2020 Binomial LLC. All Rights Reserved.
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
/**** skipping file: basisu_transcoder_internal.h ****/

namespace basist
{
	// Slice desc header flags
	enum basis_slice_desc_flags
	{
		cSliceDescFlagsHasAlpha = 1,
		cSliceDescFlagsFrameIsIFrame = 2			// Video only: Frame doesn't refer to previous frame (no usage of conditional replenishment pred symbols)
	};

#pragma pack(push)
#pragma pack(1)
	struct basis_slice_desc
	{
		basisu::packed_uint<3> m_image_index;  // The index of the source image provided to the encoder (will always appear in order from first to last, first image index is 0, no skipping allowed)
		basisu::packed_uint<1> m_level_index;	// The mipmap level index (mipmaps will always appear from largest to smallest)
		basisu::packed_uint<1> m_flags;			// enum basis_slice_desc_flags

		basisu::packed_uint<2> m_orig_width;	// The original image width (may not be a multiple of 4 pixels)
		basisu::packed_uint<2> m_orig_height;  // The original image height (may not be a multiple of 4 pixels)

		basisu::packed_uint<2> m_num_blocks_x;	// The slice's block X dimensions. Each block is 4x4 pixels. The slice's pixel resolution may or may not be a power of 2.
		basisu::packed_uint<2> m_num_blocks_y;	// The slice's block Y dimensions. 

		basisu::packed_uint<4> m_file_ofs;		// Offset from the header to the start of the slice's data
		basisu::packed_uint<4> m_file_size;		// The size of the compressed slice data in bytes

		basisu::packed_uint<2> m_slice_data_crc16; // The CRC16 of the compressed slice data, for extra-paranoid use cases
	};

	// File header files
	enum basis_header_flags
	{
		cBASISHeaderFlagETC1S = 1,					// Always set for ETC1S files. Not set for UASTC files.
		cBASISHeaderFlagYFlipped = 2,				// Set if the texture had to be Y flipped before encoding
		cBASISHeaderFlagHasAlphaSlices = 4		// True if any slices contain alpha (for ETC1S, if the odd slices contain alpha data)
	};

	// The image type field attempts to describe how to interpret the image data in a Basis file.
	// The encoder library doesn't really do anything special or different with these texture types, this is mostly here for the benefit of the user. 
	// We do make sure the various constraints are followed (2DArray/cubemap/videoframes/volume implies that each image has the same resolution and # of mipmap levels, etc., cubemap implies that the # of image slices is a multiple of 6)
	enum basis_texture_type
	{
		cBASISTexType2D = 0,					// An arbitrary array of 2D RGB or RGBA images with optional mipmaps, array size = # images, each image may have a different resolution and # of mipmap levels
		cBASISTexType2DArray = 1,			// An array of 2D RGB or RGBA images with optional mipmaps, array size = # images, each image has the same resolution and mipmap levels
		cBASISTexTypeCubemapArray = 2,	// an array of cubemap levels, total # of images must be divisable by 6, in X+, X-, Y+, Y-, Z+, Z- order, with optional mipmaps
		cBASISTexTypeVideoFrames = 3,		// An array of 2D video frames, with optional mipmaps, # frames = # images, each image has the same resolution and # of mipmap levels
		cBASISTexTypeVolume = 4,			// A 3D texture with optional mipmaps, Z dimension = # images, each image has the same resolution and # of mipmap levels

		cBASISTexTypeTotal
	};

	enum
	{
		cBASISMaxUSPerFrame = 0xFFFFFF
	};

	enum class basis_tex_format
	{
		cETC1S = 0,
		cUASTC4x4 = 1
	};

	struct basis_file_header
	{
		enum
		{
			cBASISSigValue = ('B' << 8) | 's',
			cBASISFirstVersion = 0x10
		};

		basisu::packed_uint<2>      m_sig;				// 2 byte file signature
		basisu::packed_uint<2>      m_ver;				// Baseline file version
		basisu::packed_uint<2>      m_header_size;	// Header size in bytes, sizeof(basis_file_header)
		basisu::packed_uint<2>      m_header_crc16;	// crc16 of the remaining header data

		basisu::packed_uint<4>      m_data_size;		// The total size of all data after the header
		basisu::packed_uint<2>      m_data_crc16;		// The CRC16 of all data after the header

		basisu::packed_uint<3>      m_total_slices;	// The total # of compressed slices (1 slice per image, or 2 for alpha basis files)

		basisu::packed_uint<3>      m_total_images;	// The total # of images
				
		basisu::packed_uint<1>      m_tex_format;		// enum basis_tex_format
		basisu::packed_uint<2>      m_flags;			// enum basist::header_flags
		basisu::packed_uint<1>      m_tex_type;		// enum basist::basis_texture_type
		basisu::packed_uint<3>      m_us_per_frame;	// Framerate of video, in microseconds per frame

		basisu::packed_uint<4>      m_reserved;		// For future use
		basisu::packed_uint<4>      m_userdata0;		// For client use
		basisu::packed_uint<4>      m_userdata1;		// For client use

		basisu::packed_uint<2>      m_total_endpoints;			// The number of endpoints in the endpoint codebook 
		basisu::packed_uint<4>      m_endpoint_cb_file_ofs;	// The compressed endpoint codebook's file offset relative to the header
		basisu::packed_uint<3>      m_endpoint_cb_file_size;	// The compressed endpoint codebook's size in bytes

		basisu::packed_uint<2>      m_total_selectors;			// The number of selectors in the endpoint codebook 
		basisu::packed_uint<4>      m_selector_cb_file_ofs;	// The compressed selectors codebook's file offset relative to the header
		basisu::packed_uint<3>      m_selector_cb_file_size;	// The compressed selector codebook's size in bytes

		basisu::packed_uint<4>      m_tables_file_ofs;			// The file offset of the compressed Huffman codelength tables, for decompressing slices
		basisu::packed_uint<4>      m_tables_file_size;			// The file size in bytes of the compressed huffman codelength tables

		basisu::packed_uint<4>      m_slice_desc_file_ofs;		// The file offset to the slice description array, usually follows the header
		
		basisu::packed_uint<4>      m_extended_file_ofs;		// The file offset of the "extended" header and compressed data, for future use
		basisu::packed_uint<4>      m_extended_file_size;		// The file size in bytes of the "extended" header and compressed data, for future use
	};
#pragma pack (pop)

} // namespace basist
/**** ended inlining basisu_file_headers.h ****/

namespace basist
{
	// High-level composite texture formats supported by the transcoder.
	// Each of these texture formats directly correspond to OpenGL/D3D/Vulkan etc. texture formats.
	// Notes:
	// - If you specify a texture format that supports alpha, but the .basis file doesn't have alpha, the transcoder will automatically output a 
	// fully opaque (255) alpha channel.
	// - The PVRTC1 texture formats only support power of 2 dimension .basis files, but this may be relaxed in a future version.
	// - The PVRTC1 transcoders are real-time encoders, so don't expect the highest quality. We may add a slower encoder with improved quality.
	// - These enums must be kept in sync with Javascript code that calls the transcoder.
	enum class transcoder_texture_format
	{
		// Compressed formats

		// ETC1-2
		cTFETC1_RGB = 0,							// Opaque only, returns RGB or alpha data if cDecodeFlagsTranscodeAlphaDataToOpaqueFormats flag is specified
		cTFETC2_RGBA = 1,							// Opaque+alpha, ETC2_EAC_A8 block followed by a ETC1 block, alpha channel will be opaque for opaque .basis files

		// BC1-5, BC7 (desktop, some mobile devices)
		cTFBC1_RGB = 2,							// Opaque only, no punchthrough alpha support yet, transcodes alpha slice if cDecodeFlagsTranscodeAlphaDataToOpaqueFormats flag is specified
		cTFBC3_RGBA = 3, 							// Opaque+alpha, BC4 followed by a BC1 block, alpha channel will be opaque for opaque .basis files
		cTFBC4_R = 4,								// Red only, alpha slice is transcoded to output if cDecodeFlagsTranscodeAlphaDataToOpaqueFormats flag is specified
		cTFBC5_RG = 5,								// XY: Two BC4 blocks, X=R and Y=Alpha, .basis file should have alpha data (if not Y will be all 255's)
		cTFBC7_RGBA = 6,							// RGB or RGBA, mode 5 for ETC1S, modes (1,2,3,5,6,7) for UASTC
				
		// PVRTC1 4bpp (mobile, PowerVR devices)
		cTFPVRTC1_4_RGB = 8,						// Opaque only, RGB or alpha if cDecodeFlagsTranscodeAlphaDataToOpaqueFormats flag is specified, nearly lowest quality of any texture format.
		cTFPVRTC1_4_RGBA = 9,					// Opaque+alpha, most useful for simple opacity maps. If .basis file doesn't have alpha cTFPVRTC1_4_RGB will be used instead. Lowest quality of any supported texture format.

		// ASTC (mobile, Intel devices, hopefully all desktop GPU's one day)
		cTFASTC_4x4_RGBA = 10,					// Opaque+alpha, ASTC 4x4, alpha channel will be opaque for opaque .basis files. Transcoder uses RGB/RGBA/L/LA modes, void extent, and up to two ([0,47] and [0,255]) endpoint precisions.

		// ATC (mobile, Adreno devices, this is a niche format)
		cTFATC_RGB = 11,							// Opaque, RGB or alpha if cDecodeFlagsTranscodeAlphaDataToOpaqueFormats flag is specified. ATI ATC (GL_ATC_RGB_AMD)
		cTFATC_RGBA = 12,							// Opaque+alpha, alpha channel will be opaque for opaque .basis files. ATI ATC (GL_ATC_RGBA_INTERPOLATED_ALPHA_AMD) 

		// FXT1 (desktop, Intel devices, this is a super obscure format)
		cTFFXT1_RGB = 17,							// Opaque only, uses exclusively CC_MIXED blocks. Notable for having a 8x4 block size. GL_3DFX_texture_compression_FXT1 is supported on Intel integrated GPU's (such as HD 630).
														// Punch-through alpha is relatively easy to support, but full alpha is harder. This format is only here for completeness so opaque-only is fine for now.
														// See the BASISU_USE_ORIGINAL_3DFX_FXT1_ENCODING macro in basisu_transcoder_internal.h.

		cTFPVRTC2_4_RGB = 18,					// Opaque-only, almost BC1 quality, much faster to transcode and supports arbitrary texture dimensions (unlike PVRTC1 RGB).
		cTFPVRTC2_4_RGBA = 19,					// Opaque+alpha, slower to encode than cTFPVRTC2_4_RGB. Premultiplied alpha is highly recommended, otherwise the color channel can leak into the alpha channel on transparent blocks.

		cTFETC2_EAC_R11 = 20,					// R only (ETC2 EAC R11 unsigned)
		cTFETC2_EAC_RG11 = 21,					// RG only (ETC2 EAC RG11 unsigned), R=opaque.r, G=alpha - for tangent space normal maps
		
		// Uncompressed (raw pixel) formats
		cTFRGBA32 = 13,							// 32bpp RGBA image stored in raster (not block) order in memory, R is first byte, A is last byte.
		cTFRGB565 = 14,							// 166pp RGB image stored in raster (not block) order in memory, R at bit position 11
		cTFBGR565 = 15,							// 16bpp RGB image stored in raster (not block) order in memory, R at bit position 0
		cTFRGBA4444 = 16,							// 16bpp RGBA image stored in raster (not block) order in memory, R at bit position 12, A at bit position 0

		cTFTotalTextureFormats = 22,

		// Old enums for compatibility with code compiled against previous versions
		cTFETC1 = cTFETC1_RGB,
		cTFETC2 = cTFETC2_RGBA,
		cTFBC1 = cTFBC1_RGB,
		cTFBC3 = cTFBC3_RGBA,
		cTFBC4 = cTFBC4_R,
		cTFBC5 = cTFBC5_RG,
		
		// Previously, the caller had some control over which BC7 mode the transcoder output. We've simplified this due to UASTC, which supports numerous modes.
		cTFBC7_M6_RGB = cTFBC7_RGBA,			// Opaque only, RGB or alpha if cDecodeFlagsTranscodeAlphaDataToOpaqueFormats flag is specified. Highest quality of all the non-ETC1 formats.
		cTFBC7_M5_RGBA = cTFBC7_RGBA,			// Opaque+alpha, alpha channel will be opaque for opaque .basis files
		cTFBC7_M6_OPAQUE_ONLY = cTFBC7_RGBA,
		cTFBC7_M5 = cTFBC7_RGBA,
		cTFBC7_ALT = 7,

		cTFASTC_4x4 = cTFASTC_4x4_RGBA,
		
		cTFATC_RGBA_INTERPOLATED_ALPHA = cTFATC_RGBA,
	};

	// For compressed texture formats, this returns the # of bytes per block. For uncompressed, it returns the # of bytes per pixel.
	// NOTE: Previously, this function was called basis_get_bytes_per_block(), and it always returned 16*bytes_per_pixel for uncompressed formats which was confusing.
	uint32_t basis_get_bytes_per_block_or_pixel(transcoder_texture_format fmt);

	// Returns format's name in ASCII
	const char* basis_get_format_name(transcoder_texture_format fmt);

	// Returns true if the format supports an alpha channel.
	bool basis_transcoder_format_has_alpha(transcoder_texture_format fmt);

	// Returns the basisu::texture_format corresponding to the specified transcoder_texture_format.
	basisu::texture_format basis_get_basisu_texture_format(transcoder_texture_format fmt);

	// Returns the texture type's name in ASCII.
	const char* basis_get_texture_type_name(basis_texture_type tex_type);
	
	// Returns true if the transcoder texture type is an uncompressed (raw pixel) format.
	bool basis_transcoder_format_is_uncompressed(transcoder_texture_format tex_type);

	// Returns the # of bytes per pixel for uncompressed formats, or 0 for block texture formats.
	uint32_t basis_get_uncompressed_bytes_per_pixel(transcoder_texture_format fmt);
	
	// Returns the block width for the specified texture format, which is currently either 4 or 8 for FXT1.
	uint32_t basis_get_block_width(transcoder_texture_format tex_type);
	
	// Returns the block height for the specified texture format, which is currently always 4.
	uint32_t basis_get_block_height(transcoder_texture_format tex_type);

	// Returns true if the specified format was enabled at compile time.
	bool basis_is_format_supported(transcoder_texture_format tex_type, basis_tex_format fmt = basis_tex_format::cETC1S);
		
	class basisu_transcoder;

	// This struct holds all state used during transcoding. For video, it needs to persist between image transcodes (it holds the previous frame).
	// For threading you can use one state per thread.
	struct basisu_transcoder_state
	{
		struct block_preds
		{
			uint16_t m_endpoint_index;
			uint8_t m_pred_bits;
		};

		std::vector<block_preds> m_block_endpoint_preds[2];
		
		enum { cMaxPrevFrameLevels = 16 };
		std::vector<uint32_t> m_prev_frame_indices[2][cMaxPrevFrameLevels]; // [alpha_flag][level_index] 
	};
	
	// Low-level helper class that does the actual transcoding.
	class basisu_lowlevel_etc1s_transcoder
	{
		friend class basisu_transcoder;
	
	public:
		basisu_lowlevel_etc1s_transcoder(const basist::etc1_global_selector_codebook *pGlobal_sel_codebook);

		bool decode_palettes(
			uint32_t num_endpoints, const uint8_t *pEndpoints_data, uint32_t endpoints_data_size,
			uint32_t num_selectors, const uint8_t *pSelectors_data, uint32_t selectors_data_size);

		bool decode_tables(const uint8_t *pTable_data, uint32_t table_data_size);

		bool transcode_slice(void *pDst_blocks, uint32_t num_blocks_x, uint32_t num_blocks_y, const uint8_t *pImage_data, uint32_t image_data_size, block_format fmt, 
			uint32_t output_block_or_pixel_stride_in_bytes, bool bc1_allow_threecolor_blocks, const basis_file_header &header, const basis_slice_desc& slice_desc, uint32_t output_row_pitch_in_blocks_or_pixels = 0,
			basisu_transcoder_state *pState = nullptr, bool astc_transcode_alpha = false, void* pAlpha_blocks = nullptr, uint32_t output_rows_in_pixels = 0);

		void clear()
		{
			m_endpoints.clear();
			m_selectors.clear();
			m_endpoint_pred_model.clear();
			m_delta_endpoint_model.clear();
			m_selector_model.clear();
			m_selector_history_buf_rle_model.clear();
			m_selector_history_buf_size = 0;
		}

	private:
		typedef std::vector<endpoint> endpoint_vec;
		endpoint_vec m_endpoints;

		typedef std::vector<selector> selector_vec;
		selector_vec m_selectors;

		const etc1_global_selector_codebook *m_pGlobal_sel_codebook;

		huffman_decoding_table m_endpoint_pred_model, m_delta_endpoint_model, m_selector_model, m_selector_history_buf_rle_model;

		uint32_t m_selector_history_buf_size;
		
		basisu_transcoder_state m_def_state;
	};

	enum
	{
		// PVRTC1: decode non-pow2 ETC1S texture level to the next larger power of 2 (not implemented yet, but we're going to support it). Ignored if the slice's dimensions are already a power of 2.
		cDecodeFlagsPVRTCDecodeToNextPow2 = 2,

		// When decoding to an opaque texture format, if the basis file has alpha, decode the alpha slice instead of the color slice to the output texture format.
		// This is primarily to allow decoding of textures with alpha to multiple ETC1 textures (one for color, another for alpha).
		cDecodeFlagsTranscodeAlphaDataToOpaqueFormats = 4,

		// Forbid usage of BC1 3 color blocks (we don't support BC1 punchthrough alpha yet).
		// This flag is used internally when decoding to BC3.
		cDecodeFlagsBC1ForbidThreeColorBlocks = 8,

		// The output buffer contains alpha endpoint/selector indices. 
		// Used internally when decoding formats like ASTC that require both color and alpha data to be available when transcoding to the output format.
		cDecodeFlagsOutputHasAlphaIndices = 16,

		cDecodeFlagsHighQuality = 32
	};

	class basisu_lowlevel_uastc_transcoder
	{
		friend class basisu_transcoder;

	public:
		basisu_lowlevel_uastc_transcoder();

		bool transcode_slice(void* pDst_blocks, uint32_t num_blocks_x, uint32_t num_blocks_y, const uint8_t* pImage_data, uint32_t image_data_size, block_format fmt,
			uint32_t output_block_or_pixel_stride_in_bytes, bool bc1_allow_threecolor_blocks, const basis_file_header& header, const basis_slice_desc& slice_desc, uint32_t output_row_pitch_in_blocks_or_pixels = 0,
			basisu_transcoder_state* pState = nullptr, uint32_t output_rows_in_pixels = 0, int channel0 = -1, int channel1 = -1, uint32_t decode_flags = 0);
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
		bool m_iframe_flag;		// true if the slice is an I-Frame
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
		bool m_iframe_flag;		// true if the image is an I-Frame
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
		bool m_iframe_flag;		// true if the image is an I-Frame
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

		basis_tex_format m_tex_format; // ETC1S, UASTC, etc.
				
		bool m_y_flipped;				// true if the image was Y flipped
		bool m_etc1s;					// true if the file is ETC1S
		bool m_has_alpha_slices;	// true if the texture has alpha slices (for ETC1S: even slices RGB, odd slices alpha)
	};

	// High-level transcoder class which accepts .basis file data and allows the caller to query information about the file and transcode image levels to various texture formats.
	// If you're just starting out this is the class you care about.
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

		basis_tex_format get_tex_format(const void* pData, uint32_t data_size) const;

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
		// For ETC1S files, this call decompresses the selector/endpoint codebooks, so ideally you would only call this once per .basis file (not each image/mipmap level).
		bool start_transcoding(const void *pData, uint32_t data_size);
		
		bool stop_transcoding();
		
		// Returns true if start_transcoding() has been called.
		bool get_ready_to_transcode() const { return m_ready_to_transcode; }
											
		// transcode_image_level() decodes a single mipmap level from the .basis file to any of the supported output texture formats.
		// It'll first find the slice(s) to transcode, then call transcode_slice() one or two times to decode both the color and alpha texture data (or RG texture data from two slices for BC5).
		// If the .basis file doesn't have alpha slices, the output alpha blocks will be set to fully opaque (all 255's).
		// Currently, to decode to PVRTC1 the basis texture's dimensions in pixels must be a power of 2, due to PVRTC1 format requirements. 
		// output_blocks_buf_size_in_blocks_or_pixels should be at least the image level's total_blocks (num_blocks_x * num_blocks_y), or the total number of output pixels if fmt==cTFRGBA32.
		// output_row_pitch_in_blocks_or_pixels: Number of blocks or pixels per row. If 0, the transcoder uses the slice's num_blocks_x or orig_width (NOT num_blocks_x * 4). Ignored for PVRTC1 (due to texture swizzling).
		// output_rows_in_pixels: Ignored unless fmt is cRGBA32. The total number of output rows in the output buffer. If 0, the transcoder assumes the slice's orig_height (NOT num_blocks_y * 4).
		// Notes: 
		// - basisu_transcoder_init() must have been called first to initialize the transcoder lookup tables before calling this function.
		// - This method assumes the output texture buffer is readable. In some cases to handle alpha, the transcoder will write temporary data to the output texture in
		// a first pass, which will be read in a second pass.
		bool transcode_image_level(
			const void *pData, uint32_t data_size, 
			uint32_t image_index, uint32_t level_index, 
			void *pOutput_blocks, uint32_t output_blocks_buf_size_in_blocks_or_pixels,
			transcoder_texture_format fmt,
			uint32_t decode_flags = 0, uint32_t output_row_pitch_in_blocks_or_pixels = 0, basisu_transcoder_state *pState = nullptr, uint32_t output_rows_in_pixels = 0) const;

		// Finds the basis slice corresponding to the specified image/level/alpha params, or -1 if the slice can't be found.
		int find_slice(const void *pData, uint32_t data_size, uint32_t image_index, uint32_t level_index, bool alpha_data) const;

		// transcode_slice() decodes a single slice from the .basis file. It's a low-level API - most likely you want to use transcode_image_level().
		// This is a low-level API, and will be needed to be called multiple times to decode some texture formats (like BC3, BC5, or ETC2).
		// output_blocks_buf_size_in_blocks_or_pixels is just used for verification to make sure the output buffer is large enough.
		// output_blocks_buf_size_in_blocks_or_pixels should be at least the image level's total_blocks (num_blocks_x * num_blocks_y), or the total number of output pixels if fmt==cTFRGBA32.
		// output_block_stride_in_bytes: Number of bytes between each output block.
		// output_row_pitch_in_blocks_or_pixels: Number of blocks or pixels per row. If 0, the transcoder uses the slice's num_blocks_x or orig_width (NOT num_blocks_x * 4). Ignored for PVRTC1 (due to texture swizzling).
		// output_rows_in_pixels: Ignored unless fmt is cRGBA32. The total number of output rows in the output buffer. If 0, the transcoder assumes the slice's orig_height (NOT num_blocks_y * 4).
		// Notes:
		// - basisu_transcoder_init() must have been called first to initialize the transcoder lookup tables before calling this function.
		bool transcode_slice(const void *pData, uint32_t data_size, uint32_t slice_index, 
			void *pOutput_blocks, uint32_t output_blocks_buf_size_in_blocks_or_pixels,
			block_format fmt, uint32_t output_block_stride_in_bytes, uint32_t decode_flags = 0, uint32_t output_row_pitch_in_blocks_or_pixels = 0, basisu_transcoder_state * pState = nullptr, void* pAlpha_blocks = nullptr, 
			uint32_t output_rows_in_pixels = 0, int channel0 = -1, int channel1 = -1) const;

	private:
		mutable basisu_lowlevel_etc1s_transcoder m_lowlevel_etc1s_decoder;
		mutable basisu_lowlevel_uastc_transcoder m_lowlevel_uastc_decoder;

		bool m_ready_to_transcode;

		int find_first_slice_index(const void* pData, uint32_t data_size, uint32_t image_index, uint32_t level_index) const;
		
		bool validate_header_quick(const void* pData, uint32_t data_size) const;
	};

	// basisu_transcoder_init() must be called before a .basis file can be transcoded.
	void basisu_transcoder_init();

	enum debug_flags_t
	{
		cDebugFlagVisCRs = 1,
		cDebugFlagVisBC1Sels = 2,
		cDebugFlagVisBC1Endpoints = 4
	};
	uint32_t get_debug_flags();
	void set_debug_flags(uint32_t f);

} // namespace basisu
