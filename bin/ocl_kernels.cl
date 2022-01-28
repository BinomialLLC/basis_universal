//#define _DEBUG

#ifndef NULL
	#define NULL 0L
#endif

typedef char int8_t;
typedef uchar uint8_t;

typedef short int16_t;
typedef ushort uint16_t;

typedef int int32_t;
typedef uint uint32_t;

typedef long int64_t;
typedef ulong uint64_t;

typedef uchar4 color_rgba;

#define UINT32_MAX 0xFFFFFFFFUL
#define INT64_MAX LONG_MAX
#define UINT64_MAX ULONG_MAX

int squarei(int a) { return a * a; }

#ifdef _DEBUG
	inline void internal_assert(bool x, constant char *pMsg, int line)
	{
		if (!x)
			printf("assert() failed on line %i: %s\n", line, pMsg);
	}
	#define assert(x) internal_assert(x, #x, __LINE__)
#else
	#define assert(x)
#endif

inline uint8_t clamp255(int x)
{
	return clamp(x, 0, 255);
}

inline uint8_t clamp255_flag(int x, bool *pDid_clamp)
{
	if (x < 0)
	{
		*pDid_clamp = true;
		return 0;
	}
	else if (x > 255)
	{
		*pDid_clamp = true;
		return 255;
	}

	return (uint8_t)(x);
}

typedef struct __attribute__ ((packed)) encode_etc1s_param_struct_tag
{
	uint32_t m_total_blocks;
	int m_perceptual;
	int m_total_perms;
} encode_etc1s_param_struct;

typedef struct __attribute__ ((packed)) pixel_block_tag
{
	color_rgba m_pixels[16]; // [y*4+x]
} pixel_block;

uint color_distance(bool perceptual, color_rgba e1, color_rgba e2, bool alpha)
{
	if (perceptual)
	{
#if 0
		float3 delta_rgb = (float3)(e1.x - e2.x, e1.y - e2.y, e1.z - e2.z);

		float3 delta_ycbcr;
		delta_ycbcr.x = dot(delta_rgb, (float3)(.2126f, .7152f, .0722f)); // y
		delta_ycbcr.y = delta_rgb.x - delta_ycbcr.x; // cr
		delta_ycbcr.z = delta_rgb.z - delta_ycbcr.x; // cb

		delta_ycbcr *= delta_ycbcr;

		float d = dot(delta_ycbcr, (float3)(1.0f, 0.203125f, 0.0234375f));

		if (alpha)
		{
			int delta_a = e1.w - e2.w;
			d += delta_a * delta_a;
		}

		d = clamp(d * 256.0f + .5f, 0.0f, (float)UINT32_MAX);

		return (uint)(d);
#else
		// This matches the CPU code, which is useful for testing.
		int dr = e1.x - e2.x;
		int dg = e1.y - e2.y;
		int db = e1.z - e2.z;

		int delta_l = dr * 27 + dg * 92 + db * 9;
		int delta_cr = dr * 128 - delta_l;
		int delta_cb = db * 128 - delta_l;

		uint id = ((uint)(delta_l * delta_l) >> 7U) +
			((((uint)(delta_cr * delta_cr) >> 7U) * 26U) >> 7U) +
			((((uint)(delta_cb * delta_cb) >> 7U) * 3U) >> 7U);

		if (alpha)
		{
			int da = (e1.w - e2.w) << 7;
			id += ((uint)(da * da) >> 7U);
		}
		
		return id;
#endif
	}
	else if (alpha)
	{
		int dr = e1.x - e2.x;
		int dg = e1.y - e2.y;
		int db = e1.z - e2.z;	
		int da = e1.w - e2.w;
		return dr * dr + dg * dg + db * db + da * da;
	}
	else
	{
		int dr = e1.x - e2.x;
		int dg = e1.y - e2.y;
		int db = e1.z - e2.z;	
		return dr * dr + dg * dg + db * db;
	}
}

typedef struct __attribute__ ((packed)) etc_block_tag
{
	// big endian uint64:
	// bit ofs:  56  48  40  32  24  16   8   0
	// byte ofs: b0, b1, b2, b3, b4, b5, b6, b7 
	union
	{
		uint64_t m_uint64;
		uint8_t m_bytes[8];
	};

} etc_block;

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

#define BASISU_ETC1_CLUSTER_FIT_ORDER_TABLE_SIZE (165)
constant struct { uint8_t m_v[4]; } g_cluster_fit_order_tab[BASISU_ETC1_CLUSTER_FIT_ORDER_TABLE_SIZE] =
{
	{ { 0, 0, 0, 8 } },{ { 0, 5, 2, 1 } },{ { 0, 6, 1, 1 } },{ { 0, 7, 0, 1 } },{ { 0, 7, 1, 0 } },
	{ { 0, 0, 8, 0 } },{ { 0, 0, 3, 5 } },{ { 0, 1, 7, 0 } },{ { 0, 0, 4, 4 } },{ { 0, 0, 2, 6 } },
	{ { 0, 0, 7, 1 } },{ { 0, 0, 1, 7 } },{ { 0, 0, 5, 3 } },{ { 1, 6, 0, 1 } },{ { 0, 0, 6, 2 } },
	{ { 0, 2, 6, 0 } },{ { 2, 4, 2, 0 } },{ { 0, 3, 5, 0 } },{ { 3, 3, 1, 1 } },{ { 4, 2, 0, 2 } },
	{ { 1, 5, 2, 0 } },{ { 0, 5, 3, 0 } },{ { 0, 6, 2, 0 } },{ { 2, 4, 1, 1 } },{ { 5, 1, 0, 2 } },
	{ { 6, 1, 1, 0 } },{ { 3, 3, 0, 2 } },{ { 6, 0, 0, 2 } },{ { 0, 8, 0, 0 } },{ { 6, 1, 0, 1 } },
	{ { 0, 1, 6, 1 } },{ { 1, 6, 1, 0 } },{ { 4, 1, 3, 0 } },{ { 0, 2, 5, 1 } },{ { 5, 0, 3, 0 } },
	{ { 5, 3, 0, 0 } },{ { 0, 1, 5, 2 } },{ { 0, 3, 4, 1 } },{ { 2, 5, 1, 0 } },{ { 1, 7, 0, 0 } },
	{ { 0, 1, 4, 3 } },{ { 6, 0, 2, 0 } },{ { 0, 4, 4, 0 } },{ { 2, 6, 0, 0 } },{ { 0, 2, 4, 2 } },
	{ { 0, 5, 1, 2 } },{ { 0, 6, 0, 2 } },{ { 3, 5, 0, 0 } },{ { 0, 4, 3, 1 } },{ { 3, 4, 1, 0 } },
	{ { 4, 3, 1, 0 } },{ { 1, 5, 0, 2 } },{ { 0, 3, 3, 2 } },{ { 1, 4, 1, 2 } },{ { 0, 4, 2, 2 } },
	{ { 2, 3, 3, 0 } },{ { 4, 4, 0, 0 } },{ { 1, 2, 4, 1 } },{ { 0, 5, 0, 3 } },{ { 0, 1, 3, 4 } },
	{ { 1, 5, 1, 1 } },{ { 1, 4, 2, 1 } },{ { 1, 3, 2, 2 } },{ { 5, 2, 1, 0 } },{ { 1, 3, 3, 1 } },
	{ { 0, 1, 2, 5 } },{ { 1, 1, 5, 1 } },{ { 0, 3, 2, 3 } },{ { 2, 5, 0, 1 } },{ { 3, 2, 2, 1 } },
	{ { 2, 3, 0, 3 } },{ { 1, 4, 3, 0 } },{ { 2, 2, 1, 3 } },{ { 6, 2, 0, 0 } },{ { 1, 0, 6, 1 } },
	{ { 3, 3, 2, 0 } },{ { 7, 1, 0, 0 } },{ { 3, 1, 4, 0 } },{ { 0, 2, 3, 3 } },{ { 0, 4, 1, 3 } },
	{ { 0, 4, 0, 4 } },{ { 0, 1, 0, 7 } },{ { 2, 0, 5, 1 } },{ { 2, 0, 4, 2 } },{ { 3, 0, 2, 3 } },
	{ { 2, 2, 4, 0 } },{ { 2, 2, 3, 1 } },{ { 4, 0, 3, 1 } },{ { 3, 2, 3, 0 } },{ { 2, 3, 2, 1 } },
	{ { 1, 3, 4, 0 } },{ { 7, 0, 1, 0 } },{ { 3, 0, 4, 1 } },{ { 1, 0, 5, 2 } },{ { 8, 0, 0, 0 } },
	{ { 3, 0, 1, 4 } },{ { 4, 1, 1, 2 } },{ { 4, 0, 2, 2 } },{ { 1, 2, 5, 0 } },{ { 4, 2, 1, 1 } },
	{ { 3, 4, 0, 1 } },{ { 2, 0, 3, 3 } },{ { 5, 0, 1, 2 } },{ { 5, 0, 0, 3 } },{ { 2, 4, 0, 2 } },
	{ { 2, 1, 4, 1 } },{ { 4, 0, 1, 3 } },{ { 2, 1, 5, 0 } },{ { 4, 2, 2, 0 } },{ { 4, 0, 4, 0 } },
	{ { 1, 0, 4, 3 } },{ { 1, 4, 0, 3 } },{ { 3, 0, 3, 2 } },{ { 4, 3, 0, 1 } },{ { 0, 1, 1, 6 } },
	{ { 1, 3, 1, 3 } },{ { 0, 2, 2, 4 } },{ { 2, 0, 2, 4 } },{ { 5, 1, 1, 1 } },{ { 3, 0, 5, 0 } },
	{ { 2, 3, 1, 2 } },{ { 3, 0, 0, 5 } },{ { 0, 3, 1, 4 } },{ { 5, 0, 2, 1 } },{ { 2, 1, 3, 2 } },
	{ { 2, 0, 6, 0 } },{ { 3, 1, 3, 1 } },{ { 5, 1, 2, 0 } },{ { 1, 0, 3, 4 } },{ { 1, 1, 6, 0 } },
	{ { 4, 0, 0, 4 } },{ { 2, 0, 1, 5 } },{ { 0, 3, 0, 5 } },{ { 1, 3, 0, 4 } },{ { 4, 1, 2, 1 } },
	{ { 1, 2, 3, 2 } },{ { 3, 1, 0, 4 } },{ { 5, 2, 0, 1 } },{ { 1, 2, 2, 3 } },{ { 3, 2, 1, 2 } },
	{ { 2, 2, 2, 2 } },{ { 6, 0, 1, 1 } },{ { 1, 2, 1, 4 } },{ { 1, 1, 4, 2 } },{ { 3, 2, 0, 3 } },
	{ { 1, 2, 0, 5 } },{ { 1, 0, 7, 0 } },{ { 3, 1, 2, 2 } },{ { 1, 0, 2, 5 } },{ { 2, 0, 0, 6 } },
	{ { 2, 1, 1, 4 } },{ { 2, 2, 0, 4 } },{ { 1, 1, 3, 3 } },{ { 7, 0, 0, 1 } },{ { 1, 0, 0, 7 } },
	{ { 2, 1, 2, 3 } },{ { 4, 1, 0, 3 } },{ { 3, 1, 1, 3 } },{ { 1, 1, 2, 4 } },{ { 2, 1, 0, 5 } },
	{ { 1, 0, 1, 6 } },{ { 0, 2, 1, 5 } },{ { 0, 2, 0, 6 } },{ { 1, 1, 1, 5 } },{ { 1, 1, 0, 6 } }
};

constant int g_etc1_inten_tables[cETC1IntenModifierValues][cETC1SelectorValues] =
{
	{ -8,  -2,   2,   8 }, { -17,  -5,  5,  17 }, { -29,  -9,   9,  29 }, {  -42, -13, 13,  42 },
	{ -60, -18, 18,  60 }, { -80, -24, 24,  80 }, { -106, -33, 33, 106 }, { -183, -47, 47, 183 }
};

constant uint8_t g_etc1_to_selector_index[cETC1SelectorValues] = { 2, 3, 1, 0 };
constant uint8_t g_selector_index_to_etc1[cETC1SelectorValues] = { 3, 2, 0, 1 };

uint32_t etc_block_get_byte_bits(const etc_block *p, uint32_t ofs, uint32_t num) 
{
	assert((ofs + num) <= 64U);
	assert(num && (num <= 8U));
	assert((ofs >> 3) == ((ofs + num - 1) >> 3));
	const uint32_t byte_ofs = 7 - (ofs >> 3);
	const uint32_t byte_bit_ofs = ofs & 7;
	return (p->m_bytes[byte_ofs] >> byte_bit_ofs) & ((1 << num) - 1);
}

void etc_block_set_byte_bits(etc_block *p, uint32_t ofs, uint32_t num, uint32_t bits)
{
	assert((ofs + num) <= 64U);
	assert(num && (num < 32U));
	assert((ofs >> 3) == ((ofs + num - 1) >> 3));
	assert(bits < (1U << num));
	const uint32_t byte_ofs = 7 - (ofs >> 3);
	const uint32_t byte_bit_ofs = ofs & 7;
	const uint32_t mask = (1 << num) - 1;
	p->m_bytes[byte_ofs] &= ~(mask << byte_bit_ofs);
	p->m_bytes[byte_ofs] |= (bits << byte_bit_ofs);
}

bool etc_block_get_flip_bit(const etc_block *p) 
{
	return (p->m_bytes[3] & 1) != 0;
}

void etc_block_set_flip_bit(etc_block *p, bool flip)
{
	p->m_bytes[3] &= ~1;
	p->m_bytes[3] |= (uint8_t)(flip);
}

bool etc_block_get_diff_bit(const etc_block *p) 
{
	return (p->m_bytes[3] & 2) != 0;
}

void etc_block_set_diff_bit(etc_block *p, bool diff)
{
	p->m_bytes[3] &= ~2;
	p->m_bytes[3] |= ((uint32_t)(diff) << 1);
}

// Returns intensity modifier table (0-7) used by subblock subblock_id.
// subblock_id=0 left/top (CW 1), 1=right/bottom (CW 2)
uint32_t etc_block_get_inten_table(const etc_block *p, uint32_t subblock_id) 
{
	assert(subblock_id < 2);
	const uint32_t ofs = subblock_id ? 2 : 5;
	return (p->m_bytes[3] >> ofs) & 7;
}

// Sets intensity modifier table (0-7) used by subblock subblock_id (0 or 1)
void etc_block_set_inten_table(etc_block *p, uint32_t subblock_id, uint32_t t)
{
	assert(subblock_id < 2);
	assert(t < 8);
	const uint32_t ofs = subblock_id ? 2 : 5;
	p->m_bytes[3] &= ~(7 << ofs);
	p->m_bytes[3] |= (t << ofs);
}

void etc_block_set_inten_tables_etc1s(etc_block *p, uint32_t t)
{
	etc_block_set_inten_table(p, 0, t);
	etc_block_set_inten_table(p, 1, t);
}

uint32_t etc_block_get_raw_selector(const etc_block *pBlock, uint32_t x, uint32_t y) 
{
	assert((x | y) < 4);

	const uint32_t bit_index = x * 4 + y;
	const uint32_t byte_bit_ofs = bit_index & 7;
	const uint8_t *p = &pBlock->m_bytes[7 - (bit_index >> 3)];
	const uint32_t lsb = (p[0] >> byte_bit_ofs) & 1;
	const uint32_t msb = (p[-2] >> byte_bit_ofs) & 1;
	const uint32_t val = lsb | (msb << 1);

	return val;
}

// Returned selector value ranges from 0-3 and is a direct index into g_etc1_inten_tables.
uint32_t etc_block_get_selector(const etc_block *pBlock, uint32_t x, uint32_t y) 
{
	return g_etc1_to_selector_index[etc_block_get_raw_selector(pBlock, x, y)];
}

// Selector "val" ranges from 0-3 and is a direct index into g_etc1_inten_tables.
void etc_block_set_selector(etc_block *pBlock, uint32_t x, uint32_t y, uint32_t val)
{
	assert((x | y | val) < 4);
	const uint32_t bit_index = x * 4 + y;

	uint8_t *p = &pBlock->m_bytes[7 - (bit_index >> 3)];

	const uint32_t byte_bit_ofs = bit_index & 7;
	const uint32_t mask = 1 << byte_bit_ofs;

	const uint32_t etc1_val = g_selector_index_to_etc1[val];
	
	const uint32_t lsb = etc1_val & 1;
	const uint32_t msb = etc1_val >> 1;

	p[0] &= ~mask;
	p[0] |= (lsb << byte_bit_ofs);

	p[-2] &= ~mask;
	p[-2] |= (msb << byte_bit_ofs);
}

void etc_block_set_base4_color(etc_block *pBlock, uint32_t idx, uint16_t c)
{
	if (idx)
	{
		etc_block_set_byte_bits(pBlock, cETC1AbsColor4R2BitOffset, 4, (c >> 8) & 15);
		etc_block_set_byte_bits(pBlock, cETC1AbsColor4G2BitOffset, 4, (c >> 4) & 15);
		etc_block_set_byte_bits(pBlock, cETC1AbsColor4B2BitOffset, 4, c & 15);
	}
	else
	{
		etc_block_set_byte_bits(pBlock, cETC1AbsColor4R1BitOffset, 4, (c >> 8) & 15);
		etc_block_set_byte_bits(pBlock, cETC1AbsColor4G1BitOffset, 4, (c >> 4) & 15);
		etc_block_set_byte_bits(pBlock, cETC1AbsColor4B1BitOffset, 4, c & 15);
	}
}

uint16_t etc_block_get_base4_color(const etc_block *pBlock, uint32_t idx) 
{
	uint32_t r, g, b;
	if (idx)
	{
		r = etc_block_get_byte_bits(pBlock, cETC1AbsColor4R2BitOffset, 4);
		g = etc_block_get_byte_bits(pBlock, cETC1AbsColor4G2BitOffset, 4);
		b = etc_block_get_byte_bits(pBlock, cETC1AbsColor4B2BitOffset, 4);
	}
	else
	{
		r = etc_block_get_byte_bits(pBlock, cETC1AbsColor4R1BitOffset, 4);
		g = etc_block_get_byte_bits(pBlock, cETC1AbsColor4G1BitOffset, 4);
		b = etc_block_get_byte_bits(pBlock, cETC1AbsColor4B1BitOffset, 4);
	}
	return (uint16_t)(b | (g << 4U) | (r << 8U));
}

void etc_block_set_base5_color(etc_block *pBlock, uint16_t c)
{
	etc_block_set_byte_bits(pBlock, cETC1BaseColor5RBitOffset, 5, (c >> 10) & 31);
	etc_block_set_byte_bits(pBlock, cETC1BaseColor5GBitOffset, 5, (c >> 5) & 31);
	etc_block_set_byte_bits(pBlock, cETC1BaseColor5BBitOffset, 5, c & 31);
}

uint16_t etc_block_get_base5_color(const etc_block *pBlock)
{
	const uint32_t r = etc_block_get_byte_bits(pBlock, cETC1BaseColor5RBitOffset, 5);
	const uint32_t g = etc_block_get_byte_bits(pBlock, cETC1BaseColor5GBitOffset, 5);
	const uint32_t b = etc_block_get_byte_bits(pBlock, cETC1BaseColor5BBitOffset, 5);
	return (uint16_t)(b | (g << 5U) | (r << 10U));
}

void etc_block_set_delta3_color(etc_block *pBlock, uint16_t c)
{
	etc_block_set_byte_bits(pBlock, cETC1DeltaColor3RBitOffset, 3, (c >> 6) & 7);
	etc_block_set_byte_bits(pBlock, cETC1DeltaColor3GBitOffset, 3, (c >> 3) & 7);
	etc_block_set_byte_bits(pBlock, cETC1DeltaColor3BBitOffset, 3, c & 7);
}

uint16_t etc_block_get_delta3_color(const etc_block *pBlock) 
{
	const uint32_t r = etc_block_get_byte_bits(pBlock, cETC1DeltaColor3RBitOffset, 3);
	const uint32_t g = etc_block_get_byte_bits(pBlock, cETC1DeltaColor3GBitOffset, 3);
	const uint32_t b = etc_block_get_byte_bits(pBlock, cETC1DeltaColor3BBitOffset, 3);
	return (uint16_t)(b | (g << 3U) | (r << 6U));
}

void etc_block_unpack_delta3(int *pR, int *pG, int *pB, uint16_t packed_delta3)
{
	int r = (packed_delta3 >> 6) & 7;
	int g = (packed_delta3 >> 3) & 7;
	int b = packed_delta3 & 7;
	if (r >= 4) r -= 8;
	if (g >= 4) g -= 8;
	if (b >= 4) b -= 8;
	*pR = r;
	*pG = g;
	*pB = b;
}

bool etc_block_unpack_color5_delta3(color_rgba *pResult, uint16_t packed_color5, uint16_t packed_delta3, bool scaled, uint32_t alpha)
{
	int dr, dg, db;
	etc_block_unpack_delta3(&dr, &dg, &db, packed_delta3);

	int b = (packed_color5 & 31U) + db;
	int g = ((packed_color5 >> 5U) & 31U) + dg;
	int r = ((packed_color5 >> 10U) & 31U) + dr;

	bool success = true;
	if ((uint32_t)(r | g | b) > 31U)
	{
		success = false;
		r = clamp(r, 0, 31);
		g = clamp(g, 0, 31);
		b = clamp(b, 0, 31);
	}

	if (scaled)
	{
		b = (b << 3U) | (b >> 2U);
		g = (g << 3U) | (g >> 2U);
		r = (r << 3U) | (r >> 2U);
	}

	*pResult = (color_rgba)(r, g, b, min(alpha, 255U));
	return success;
}

color_rgba etc_block_unpack_color5(uint16_t packed_color5, bool scaled, uint32_t alpha)
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

	return (color_rgba)(r, g, b, min(alpha, 255U));
}

color_rgba etc_block_unpack_color4(uint16_t packed_color4, bool scaled, uint32_t alpha)
{
	uint32_t b = packed_color4 & 15U;
	uint32_t g = (packed_color4 >> 4U) & 15U;
	uint32_t r = (packed_color4 >> 8U) & 15U;

	if (scaled)
	{
		b = (b << 4U) | b;
		g = (g << 4U) | g;
		r = (r << 4U) | r;
	}

	return (color_rgba)(r, g, b, min(alpha, 255U));
}

// false if didn't clamp, true if any component clamped
bool etc_block_get_block_colors(const etc_block *pBlock, color_rgba* pBlock_colors, uint32_t subblock_index) 
{
	color_rgba b;

	if (etc_block_get_diff_bit(pBlock))
	{
		if (subblock_index)
			etc_block_unpack_color5_delta3(&b, etc_block_get_base5_color(pBlock), etc_block_get_delta3_color(pBlock), true, 255);
		else
			b = etc_block_unpack_color5(etc_block_get_base5_color(pBlock), true, 255);
	}
	else
	{
		b = etc_block_unpack_color4(etc_block_get_base4_color(pBlock, subblock_index), true, 255);
	}

	constant int* pInten_table = g_etc1_inten_tables[etc_block_get_inten_table(pBlock, subblock_index)];

	bool dc = false;
	pBlock_colors[0] = (color_rgba)(clamp255_flag(b.x + pInten_table[0], &dc), clamp255_flag(b.y + pInten_table[0], &dc), clamp255_flag(b.z + pInten_table[0], &dc), 255);
	pBlock_colors[1] = (color_rgba)(clamp255_flag(b.x + pInten_table[1], &dc), clamp255_flag(b.y + pInten_table[1], &dc), clamp255_flag(b.z + pInten_table[1], &dc), 255);
	pBlock_colors[2] = (color_rgba)(clamp255_flag(b.x + pInten_table[2], &dc), clamp255_flag(b.y + pInten_table[2], &dc), clamp255_flag(b.z + pInten_table[2], &dc), 255);
	pBlock_colors[3] = (color_rgba)(clamp255_flag(b.x + pInten_table[3], &dc), clamp255_flag(b.y + pInten_table[3], &dc), clamp255_flag(b.z + pInten_table[3], &dc), 255);
	return dc;
}

void get_block_colors5(color_rgba *pBlock_colors, const color_rgba *pBase_color5, uint32_t inten_table, bool scaled /* false */) 
{
	color_rgba b = *pBase_color5;

	if (!scaled)
	{
		b.x = (b.x << 3) | (b.x >> 2);
		b.y = (b.y << 3) | (b.y >> 2);
		b.z = (b.z << 3) | (b.z >> 2);
	}

	constant int* pInten_table = g_etc1_inten_tables[inten_table];

	pBlock_colors[0] = (color_rgba)(clamp255(b.x + pInten_table[0]), clamp255(b.y + pInten_table[0]), clamp255(b.z + pInten_table[0]), 255);
	pBlock_colors[1] = (color_rgba)(clamp255(b.x + pInten_table[1]), clamp255(b.y + pInten_table[1]), clamp255(b.z + pInten_table[1]), 255);
	pBlock_colors[2] = (color_rgba)(clamp255(b.x + pInten_table[2]), clamp255(b.y + pInten_table[2]), clamp255(b.z + pInten_table[2]), 255);
	pBlock_colors[3] = (color_rgba)(clamp255(b.x + pInten_table[3]), clamp255(b.y + pInten_table[3]), clamp255(b.z + pInten_table[3]), 255);
}

uint64_t etc_block_determine_selectors(etc_block *pBlock, const color_rgba* pSource_pixels, bool perceptual, uint32_t begin_subblock /*= 0*/, uint32_t end_subblock /*= 2*/)
{
	uint64_t total_error = 0;

	for (uint32_t subblock = begin_subblock; subblock < end_subblock; subblock++)
	{
		color_rgba block_colors[4];
		etc_block_get_block_colors(pBlock, block_colors, subblock);

		if (etc_block_get_flip_bit(pBlock))
		{
			for (uint32_t y = 0; y < 2; y++)
			{
				for (uint32_t x = 0; x < 4; x++)
				{
					uint32_t best_selector = 0;
					uint64_t best_error = UINT64_MAX;

					for (uint32_t s = 0; s < 4; s++)
					{
						uint64_t err = color_distance(perceptual, block_colors[s], pSource_pixels[x + (subblock * 2 + y) * 4], false);
						if (err < best_error)
						{
							best_error = err;
							best_selector = s;
						}
					}

					etc_block_set_selector(pBlock, x, subblock * 2 + y, best_selector);

					total_error += best_error;
				}
			}
		}
		else
		{
			for (uint32_t y = 0; y < 4; y++)
			{
				for (uint32_t x = 0; x < 2; x++)
				{
					uint32_t best_selector = 0;
					uint64_t best_error = UINT64_MAX;

					for (uint32_t s = 0; s < 4; s++)
					{
						uint64_t err = color_distance(perceptual, block_colors[s], pSource_pixels[(subblock * 2) + x + y * 4], false);
						if (err < best_error)
						{
							best_error = err;
							best_selector = s;
						}
					}

					etc_block_set_selector(pBlock, subblock * 2 + x, y, best_selector);

					total_error += best_error;
				}
			}
		}
	}

	return total_error;
}

uint16_t etc_block_pack_color4_rgb(uint32_t r, uint32_t g, uint32_t b, bool scaled)
{
	uint32_t bias = 127;

	if (scaled)
	{
		r = (r * 15U + bias) / 255U;
		g = (g * 15U + bias) / 255U;
		b = (b * 15U + bias) / 255U;
	}

	r = min(r, 15U);
	g = min(g, 15U);
	b = min(b, 15U);

	return (uint16_t)(b | (g << 4U) | (r << 8U));
}

uint16_t etc_block_pack_color4(color_rgba color, bool scaled)
{
	uint32_t bias = 127;
	return etc_block_pack_color4_rgb(color.x, color.y, color.z, scaled);
}

uint16_t etc_block_pack_delta3(int r, int g, int b)
{
	assert((r >= cETC1ColorDeltaMin) && (r <= cETC1ColorDeltaMax));
	assert((g >= cETC1ColorDeltaMin) && (g <= cETC1ColorDeltaMax));
	assert((b >= cETC1ColorDeltaMin) && (b <= cETC1ColorDeltaMax));
	if (r < 0) r += 8;
	if (g < 0) g += 8;
	if (b < 0) b += 8;
	return (uint16_t)(b | (g << 3) | (r << 6));
}

void etc_block_set_block_color4(etc_block *pBlock, color_rgba c0_unscaled, color_rgba c1_unscaled)
{
	etc_block_set_diff_bit(pBlock, false);

	etc_block_set_base4_color(pBlock, 0, etc_block_pack_color4(c0_unscaled, false));
	etc_block_set_base4_color(pBlock, 1, etc_block_pack_color4(c1_unscaled, false));
}

uint16_t etc_block_pack_color5_rgb(uint32_t r, uint32_t g, uint32_t b, bool scaled)
{
	uint32_t bias = 127;

	if (scaled)
	{
		r = (r * 31U + bias) / 255U;
		g = (g * 31U + bias) / 255U;
		b = (b * 31U + bias) / 255U;
	}

	r = min(r, 31U);
	g = min(g, 31U);
	b = min(b, 31U);

	return (uint16_t)(b | (g << 5U) | (r << 10U));
}

uint16_t etc_block_pack_color5(color_rgba c, bool scaled)
{
	return etc_block_pack_color5_rgb(c.x, c.y, c.z, scaled);
}

void etc_block_set_block_color5(etc_block *pBlock, color_rgba c0_unscaled, color_rgba c1_unscaled)
{
	etc_block_set_diff_bit(pBlock, true);

	etc_block_set_base5_color(pBlock, etc_block_pack_color5(c0_unscaled, false));

	int dr = c1_unscaled.x - c0_unscaled.x;
	int dg = c1_unscaled.y - c0_unscaled.y;
	int db = c1_unscaled.z - c0_unscaled.z;

	etc_block_set_delta3_color(pBlock, etc_block_pack_delta3(dr, dg, db));
}

void etc_block_set_block_color5_etc1s(etc_block *pBlock, color_rgba c_unscaled)
{
	etc_block_set_diff_bit(pBlock, true);
			
	etc_block_set_base5_color(pBlock, etc_block_pack_color5(c_unscaled, false));
	etc_block_set_delta3_color(pBlock, etc_block_pack_delta3(0, 0, 0));
}

bool etc_block_set_block_color5_check(etc_block *pBlock, color_rgba c0_unscaled, color_rgba c1_unscaled)
{
	etc_block_set_diff_bit(pBlock, true);

	etc_block_set_base5_color(pBlock, etc_block_pack_color5(c0_unscaled, false));

	int dr = c1_unscaled.x - c0_unscaled.x;
	int dg = c1_unscaled.y - c0_unscaled.y;
	int db = c1_unscaled.z - c0_unscaled.z;

	if (((dr < cETC1ColorDeltaMin) || (dr > cETC1ColorDeltaMax)) ||
		((dg < cETC1ColorDeltaMin) || (dg > cETC1ColorDeltaMax)) ||
		((db < cETC1ColorDeltaMin) || (db > cETC1ColorDeltaMax)))
		return false;

	etc_block_set_delta3_color(pBlock, etc_block_pack_delta3(dr, dg, db));

	return true;
}

void etc_block_pack_raw_selectors(etc_block *pBlock, const uint8_t *pSelectors)
{
	uint32_t word3 = 0, word2 = 0;
	for (uint32_t y = 0; y < 4; y++)
	{
		for (uint32_t x = 0; x < 4; x++)
		{
			const uint32_t bit_index = x * 4 + y;
			const uint32_t s = pSelectors[x + y * 4];
		
			const uint32_t lsb = s & 1, msb = s >> 1;
		
			word3 |= (lsb << bit_index);
			word2 |= (msb << bit_index);
		}
	}

	pBlock->m_bytes[7] = (uint8_t)(word3);
	pBlock->m_bytes[6] = (uint8_t)(word3 >> 8);
	pBlock->m_bytes[5] = (uint8_t)(word2);
	pBlock->m_bytes[4] = (uint8_t)(word2 >> 8);
}

// ---- EC1S block encoding/endpoint optimization

constant uint8_t g_eval_dist_tables[8][256] =
{
	// 99% threshold
	{ 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,},
	{ 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,},
	{ 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,},
	{ 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,1,1,0,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,0,1,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,},
	{ 1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,0,1,0,0,0,0,0,0,0,0,0,1,0,0,1,},
	{ 1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,1,0,0,0,0,1,0,1,1,0,1,1,1,1,1,0,1,1,1,0,1,1,0,0,0,1,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,},
	{ 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,1,1,1,0,1,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,},
	{ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,1,1,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,}
};

typedef struct etc1s_optimizer_solution_coordinates_tag
{
	color_rgba m_unscaled_color;
	uint32_t m_inten_table;
} etc1s_optimizer_solution_coordinates;

color_rgba get_scaled_color(color_rgba unscaled_color) 
{
	int br, bg, bb;
	
	br = (unscaled_color.x >> 2) | (unscaled_color.x << 3);
	bg = (unscaled_color.y >> 2) | (unscaled_color.y << 3);
	bb = (unscaled_color.z >> 2) | (unscaled_color.z << 3);
	
	return (color_rgba)((uint8_t)br, (uint8_t)bg, (uint8_t)bb, 255);
}

typedef struct etc1s_optimizer_potential_solution_tag
{
	uint64_t					m_error;
	etc1s_optimizer_solution_coordinates m_coords;
		
	uint8_t						m_selectors[16];
	bool						m_valid;
} etc1s_optimizer_potential_solution;

typedef struct etc1s_optimizer_state_tag
{
	int m_br, m_bg, m_bb;
	float3 m_avg_color;
	int m_max_comp_spread;
	etc1s_optimizer_potential_solution m_best_solution;
} etc1s_optimizer_state;

bool etc1s_optimizer_evaluate_solution(
	etc1s_optimizer_state *pState,
	const global encode_etc1s_param_struct *pParams,
	uint64_t num_pixels, const global color_rgba *pPixels, 
	const global uint32_t *pWeights,
	etc1s_optimizer_solution_coordinates coords, 
	etc1s_optimizer_potential_solution* pTrial_solution, 
	etc1s_optimizer_potential_solution* pBest_solution)
{
	uint8_t temp_selectors[16];

	pTrial_solution->m_valid = false;
		
	const color_rgba base_color = get_scaled_color(coords.m_unscaled_color);
	
	pTrial_solution->m_error = INT64_MAX;
		
	for (uint32_t inten_table = 0; inten_table < cETC1IntenModifierValues; inten_table++)
	{
		// TODO: This check is equivalent to medium quality in the C++ version.
		if (!g_eval_dist_tables[inten_table][pState->m_max_comp_spread])
			continue;

		constant int* pInten_table = g_etc1_inten_tables[inten_table];

		color_rgba block_colors[4];
		for (uint32_t s = 0; s < 4; s++)
		{
			int yd = pInten_table[s];
			block_colors[s] = (color_rgba)(clamp255(base_color.x + yd), clamp255(base_color.y + yd), clamp255(base_color.z + yd), 255);
		}

		uint64_t total_error = 0;
				
		for (uint64_t c = 0; c < num_pixels; c++)
		{
			color_rgba src_pixel = pPixels[c];

			uint32_t best_selector_index = 3;
			uint32_t best_error = color_distance(pParams->m_perceptual, src_pixel, block_colors[0], false);

			uint32_t trial_error = color_distance(pParams->m_perceptual, src_pixel, block_colors[1], false);
			if (trial_error < best_error)
			{
				best_error = trial_error;
				best_selector_index = 2;
			}

			trial_error = color_distance(pParams->m_perceptual, src_pixel, block_colors[2], false);
			if (trial_error < best_error)
			{
				best_error = trial_error;
				best_selector_index = 0;
			}

			trial_error = color_distance(pParams->m_perceptual, src_pixel, block_colors[3], false);
			if (trial_error < best_error)
			{
				best_error = trial_error;
				best_selector_index = 1;
			}

			if (num_pixels <= 16)
				temp_selectors[c] = (uint8_t)(best_selector_index);

			total_error += pWeights ? (best_error * (uint64_t)pWeights[c]) : best_error;
			
			if (total_error >= pTrial_solution->m_error)
				break;
		}

		if (total_error < pTrial_solution->m_error)
		{
			pTrial_solution->m_error = total_error;
			pTrial_solution->m_coords.m_inten_table = inten_table;
			if (num_pixels <= 16)
			{
				for (uint32_t i = 0; i < num_pixels; i++)
					pTrial_solution->m_selectors[i] = temp_selectors[i];
			}
			pTrial_solution->m_valid = true;
		}
	}
	pTrial_solution->m_coords.m_unscaled_color = coords.m_unscaled_color;

	bool success = false;
	if (pBest_solution)
	{
		if (pTrial_solution->m_error < pBest_solution->m_error)
		{
			*pBest_solution = *pTrial_solution;
			success = true;
		}
	}
				
	return success;
}

void etc1s_optimizer_init(
	etc1s_optimizer_state *pState,
	const global encode_etc1s_param_struct *pParams,
	uint64_t num_pixels, const global color_rgba *pPixels, 
	const global uint32_t *pWeights)
{
	const int LIMIT = 31;
		
	color_rgba min_color = 255;
	color_rgba max_color = 0;
	uint64_t total_weight = 0;
	uint64_t sum_r = 0, sum_g = 0, sum_b = 0;
				
	for (uint64_t i = 0; i < num_pixels; i++)
	{
		const color_rgba c = pPixels[i];

		min_color = min(min_color, c);
		max_color = max(max_color, c);

		if (pWeights)
		{
			uint64_t weight = pWeights[i];

			sum_r += weight * c.x;
			sum_g += weight * c.y;
			sum_b += weight * c.z;
		
			total_weight += weight;
		}
		else
		{
			sum_r += c.x;
			sum_g += c.y;
			sum_b += c.z;

			total_weight++;
		}
	}
				
	float3 avg_color;
	avg_color.x = (float)sum_r / total_weight;
	avg_color.y = (float)sum_g / total_weight;
	avg_color.z = (float)sum_b / total_weight;

	pState->m_avg_color = avg_color;
	pState->m_max_comp_spread = max(max((int)max_color.x - (int)min_color.x, (int)max_color.y - (int)min_color.y), (int)max_color.z - (int)min_color.z);
		
	// TODO: The rounding here could be improved, like with DXT1/BC1.
	pState->m_br = clamp((int)(avg_color.x * (LIMIT / 255.0f) + .5f), 0, LIMIT);
	pState->m_bg = clamp((int)(avg_color.y * (LIMIT / 255.0f) + .5f), 0, LIMIT);
	pState->m_bb = clamp((int)(avg_color.z * (LIMIT / 255.0f) + .5f), 0, LIMIT);

	pState->m_best_solution.m_valid = false;
	pState->m_best_solution.m_error = UINT64_MAX;
}

void etc1s_optimizer_internal_cluster_fit(
	uint32_t total_perms_to_try,
	etc1s_optimizer_state *pState,
	const global encode_etc1s_param_struct *pParams,
	uint64_t num_pixels, const global color_rgba *pPixels,
	const global uint32_t *pWeights)
{
	const int LIMIT = 31;

	etc1s_optimizer_potential_solution trial_solution;

	etc1s_optimizer_solution_coordinates cur_coords;
	cur_coords.m_unscaled_color = (color_rgba)(pState->m_br, pState->m_bg, pState->m_bb, 255);
	etc1s_optimizer_evaluate_solution(pState, pParams, num_pixels, pPixels, pWeights, cur_coords, &trial_solution, &pState->m_best_solution);
			
	if (pState->m_best_solution.m_error == 0)
		return;

	for (uint32_t i = 0; i < total_perms_to_try; i++)
	{
		int delta_sum_r = 0, delta_sum_g = 0, delta_sum_b = 0;

		constant int *pInten_table = g_etc1_inten_tables[pState->m_best_solution.m_coords.m_inten_table];
		const color_rgba base_color = get_scaled_color(pState->m_best_solution.m_coords.m_unscaled_color);

		constant uint8_t *pNum_selectors = g_cluster_fit_order_tab[i].m_v;

		for (uint32_t q = 0; q < 4; q++)
		{
			const int yd_temp = pInten_table[q];

			delta_sum_r += pNum_selectors[q] * (clamp(base_color.x + yd_temp, 0, 255) - base_color.x);
			delta_sum_g += pNum_selectors[q] * (clamp(base_color.y + yd_temp, 0, 255) - base_color.y);
			delta_sum_b += pNum_selectors[q] * (clamp(base_color.z + yd_temp, 0, 255) - base_color.z);
		}

		if ((!delta_sum_r) && (!delta_sum_g) && (!delta_sum_b))
			continue;

		const float avg_delta_r_f = (float)(delta_sum_r) / 8;
		const float avg_delta_g_f = (float)(delta_sum_g) / 8;
		const float avg_delta_b_f = (float)(delta_sum_b) / 8;

		const int br1 = clamp((int)((pState->m_avg_color.x - avg_delta_r_f) * (LIMIT / 255.0f) + .5f), 0, LIMIT);
		const int bg1 = clamp((int)((pState->m_avg_color.y - avg_delta_g_f) * (LIMIT / 255.0f) + .5f), 0, LIMIT);
		const int bb1 = clamp((int)((pState->m_avg_color.z - avg_delta_b_f) * (LIMIT / 255.0f) + .5f), 0, LIMIT);
		
		cur_coords.m_unscaled_color = (color_rgba)(br1, bg1, bb1, 255);

		etc1s_optimizer_evaluate_solution(pState, pParams, num_pixels, pPixels, pWeights, cur_coords, &trial_solution, &pState->m_best_solution);
	
		if (pState->m_best_solution.m_error == 0)
			break;
	}
}

// Encode an ETC1S block given a 4x4 pixel block.
kernel void encode_etc1s_blocks(
    const global encode_etc1s_param_struct *pParams, 
    const global pixel_block *pInput_blocks,
    global etc_block *pOutput_blocks)
{
	const uint32_t block_index = get_global_id(0);
	
	const global pixel_block *pInput_block = &pInput_blocks[block_index];

	etc1s_optimizer_state state;
	etc1s_optimizer_init(&state, pParams, 16, pInput_block->m_pixels, NULL);
	etc1s_optimizer_internal_cluster_fit(pParams->m_total_perms, &state, pParams, 16, pInput_block->m_pixels, NULL);
	
	etc_block blk;
	etc_block_set_flip_bit(&blk, true);
	etc_block_set_block_color5_etc1s(&blk, state.m_best_solution.m_coords.m_unscaled_color);
	etc_block_set_inten_tables_etc1s(&blk, state.m_best_solution.m_coords.m_inten_table);
	etc_block_pack_raw_selectors(&blk, state.m_best_solution.m_selectors);
							
	pOutput_blocks[block_index] = blk;
}

typedef struct __attribute__ ((packed)) pixel_cluster_tag
{
	uint64_t m_total_pixels;
	uint64_t m_first_pixel_index;
} pixel_cluster;

// Determine the optimal ETC1S color5/intensity given an arbitrary large array of 4x4 input pixel blocks.
kernel void encode_etc1s_from_pixel_cluster(
    const global encode_etc1s_param_struct *pParams, 
    const global pixel_cluster *pInput_pixel_clusters,
	const global color_rgba *pInput_pixels,
	const global uint32_t *pInput_weights,
    global etc_block *pOutput_blocks)
{
	const uint32_t cluster_index = get_global_id(0);
	
	const global pixel_cluster *pInput_cluster = &pInput_pixel_clusters[cluster_index];

	uint64_t total_pixels = pInput_cluster->m_total_pixels;
	const global color_rgba *pPixels = pInput_pixels + pInput_cluster->m_first_pixel_index;
	const global uint32_t *pWeights = pInput_weights + pInput_cluster->m_first_pixel_index;

	etc1s_optimizer_state state;
	etc1s_optimizer_init(&state, pParams, total_pixels, pPixels, pWeights);
	etc1s_optimizer_internal_cluster_fit(pParams->m_total_perms, &state, pParams, total_pixels, pPixels, pWeights);
	
	etc_block blk;
	etc_block_set_flip_bit(&blk, true);
	etc_block_set_block_color5_etc1s(&blk, state.m_best_solution.m_coords.m_unscaled_color);
	etc_block_set_inten_tables_etc1s(&blk, state.m_best_solution.m_coords.m_inten_table);
								
	pOutput_blocks[cluster_index] = blk;
}

// ---- refine_endpoint_clusterization
typedef struct __attribute__ ((packed)) rec_block_struct_tag
{
	uint16_t m_first_cluster_ofs;
	uint16_t m_num_clusters;
	uint16_t m_cur_cluster_index;
	uint8_t m_cur_cluster_etc_inten;
} rec_block_struct;

typedef struct __attribute__ ((packed)) rec_endpoint_cluster_struct_tag
{
	color_rgba m_unscaled_color;
	uint8_t m_etc_inten;
	uint16_t m_cluster_index;
} rec_endpoint_cluster_struct;

typedef struct __attribute__ ((packed)) rec_param_struct_tag
{
	uint32_t m_total_blocks;
	int m_perceptual;
} rec_param_struct;

// For each input block: find the best endpoint cluster that encodes it.
kernel void refine_endpoint_clusterization(
    const rec_param_struct params, 
    const global pixel_block *pInput_blocks,
	const global rec_block_struct *pInput_block_info,
	const global rec_endpoint_cluster_struct *pInput_clusters,
	const global uint32_t *pSorted_block_indices,
    global uint32_t *pOutput_indices)
{
	const uint32_t sorted_block_index = get_global_id(0);
	const uint32_t block_index = pSorted_block_indices[sorted_block_index];
	const int perceptual = params.m_perceptual;

	const global pixel_block *pInput_block = &pInput_blocks[block_index];
			
	pixel_block priv_pixel_block;
	priv_pixel_block = *pInput_block;

	const uint32_t first_cluster_ofs = pInput_block_info[block_index].m_first_cluster_ofs;
	const uint32_t num_clusters = pInput_block_info[block_index].m_num_clusters;
	const uint32_t cur_block_cluster_index = pInput_block_info[block_index].m_cur_cluster_index;
	const uint32_t cur_block_cluster_etc_inten = pInput_block_info[block_index].m_cur_cluster_etc_inten;
	
	uint64_t overall_best_err = UINT64_MAX;
	uint32_t best_cluster_index = 0;

	for (uint32_t i = 0; i < num_clusters; i++)
	{
		const uint32_t cluster_index = first_cluster_ofs + i;
		color_rgba unscaled_color = pInput_clusters[cluster_index].m_unscaled_color;
		const uint8_t etc_inten = pInput_clusters[cluster_index].m_etc_inten;
		const uint16_t orig_cluster_index = pInput_clusters[cluster_index].m_cluster_index;

		if (etc_inten > cur_block_cluster_etc_inten)
			continue;

		color_rgba block_colors[4];
		get_block_colors5(block_colors, &unscaled_color, etc_inten, false);

		uint64_t total_error = 0;
				
		for (uint32_t c = 0; c < 16; c++)
		{
			color_rgba src_pixel = priv_pixel_block.m_pixels[c];

			uint32_t best_error = color_distance(perceptual, src_pixel, block_colors[0], false);

			uint32_t trial_error = color_distance(perceptual, src_pixel, block_colors[1], false);
			if (trial_error < best_error)
				best_error = trial_error;

			trial_error = color_distance(perceptual, src_pixel, block_colors[2], false);
			if (trial_error < best_error)
				best_error = trial_error;

			trial_error = color_distance(perceptual, src_pixel, block_colors[3], false);
			if (trial_error < best_error)
				best_error = trial_error;
							
			total_error += best_error;
		}

		if ( (total_error < overall_best_err) ||
		     ((orig_cluster_index == cur_block_cluster_index) && (total_error == overall_best_err))
			)
		{
			overall_best_err = total_error;
			best_cluster_index = orig_cluster_index;
			if (!overall_best_err)
				break;
		}
	}

	pOutput_indices[block_index] = best_cluster_index;
}

// ---- find_optimal_selector_clusters_for_each_block

typedef struct __attribute__ ((packed)) fosc_selector_struct_tag
{
	uint32_t m_packed_selectors;	// 4x4 grid of 2-bit selectors
} fosc_selector_struct;

typedef struct __attribute__ ((packed)) fosc_block_struct_tag
{
	color_rgba m_etc_color5_inten;  // unscaled 5-bit block color in RGB, alpha has block's intensity index
	uint32_t m_first_selector;		// offset into selector table
	uint32_t m_num_selectors;		// number of selectors to check
} fosc_block_struct;

typedef struct __attribute__ ((packed)) fosc_param_struct_tag
{
	uint32_t m_total_blocks;
	int m_perceptual;
} fosc_param_struct;

// For each input block: Find the quantized selector which results in the lowest error.
kernel void find_optimal_selector_clusters_for_each_block(
    const fosc_param_struct params, 
    const global pixel_block *pInput_blocks,
	const global fosc_block_struct *pInput_block_info,
	const global fosc_selector_struct *pInput_selectors,
	const global uint32_t *pSelector_cluster_indices,
    global uint32_t *pOutput_selector_cluster_indices)
{
	const uint32_t block_index = get_global_id(0);
	
	const global color_rgba *pBlock_pixels = pInput_blocks[block_index].m_pixels;
	const global fosc_block_struct *pBlock_info = &pInput_block_info[block_index];
	
	const global fosc_selector_struct *pSelectors = &pInput_selectors[pBlock_info->m_first_selector];
	const uint32_t num_selectors = pBlock_info->m_num_selectors;

	color_rgba trial_block_colors[4];
	color_rgba etc_color5_inten = pBlock_info->m_etc_color5_inten;
	get_block_colors5(trial_block_colors, &etc_color5_inten, etc_color5_inten.w, false);

	uint32_t trial_errors[4][16];

	if (params.m_perceptual)
	{
		for (uint32_t sel = 0; sel < 4; ++sel)
			for (uint32_t i = 0; i < 16; ++i)
				trial_errors[sel][i] = color_distance(true, pBlock_pixels[i], trial_block_colors[sel], false);
	}
	else
	{
		for (uint32_t sel = 0; sel < 4; ++sel)
			for (uint32_t i = 0; i < 16; ++i)
				trial_errors[sel][i] = color_distance(false, pBlock_pixels[i], trial_block_colors[sel], false);
	}

	uint64_t best_err = UINT64_MAX;
	uint32_t best_index = 0;

	for (uint32_t sel_index = 0; sel_index < num_selectors; sel_index++)
	{
		uint32_t sels = pSelectors[sel_index].m_packed_selectors;
		
		uint64_t total_err = 0;
		for (uint32_t i = 0; i < 16; i++, sels >>= 2)
			total_err += trial_errors[sels & 3][i];

		if (total_err < best_err)
		{
			best_err = total_err;
			best_index = sel_index;

			if (!best_err)
				break;
		}
	}

	pOutput_selector_cluster_indices[block_index] = pSelector_cluster_indices[pBlock_info->m_first_selector + best_index];
}

// determine_selectors

typedef struct __attribute__ ((packed)) ds_param_struct_tag
{
	uint32_t m_total_blocks;
	int m_perceptual;
} ds_param_struct;

// For each input block: Determine the ETC1S selectors that result in the lowest error, given each block's predetermined ETC1S color5/intensities. 
kernel void determine_selectors(
    const ds_param_struct params, 
    const global pixel_block *pInput_blocks,
	const global color_rgba *pInput_etc_color5_and_inten,
    global etc_block *pOutput_blocks)
{
	const uint32_t block_index = get_global_id(0);
	
	const global color_rgba *pBlock_pixels = pInput_blocks[block_index].m_pixels;

	color_rgba etc_color5_inten = pInput_etc_color5_and_inten[block_index];

	color_rgba block_colors[4];
	get_block_colors5(block_colors, &etc_color5_inten, etc_color5_inten.w, false);

	etc_block output_block;
	etc_block_set_flip_bit(&output_block, true);
	etc_block_set_block_color5_etc1s(&output_block, etc_color5_inten);
	etc_block_set_inten_tables_etc1s(&output_block, etc_color5_inten.w);

	for (uint32_t i = 0; i < 16; i++)
	{
		color_rgba pixel_color = pBlock_pixels[i];

		uint err0 = color_distance(params.m_perceptual, pixel_color, block_colors[0], false);
		uint err1 = color_distance(params.m_perceptual, pixel_color, block_colors[1], false);
		uint err2 = color_distance(params.m_perceptual, pixel_color, block_colors[2], false);
		uint err3 = color_distance(params.m_perceptual, pixel_color, block_colors[3], false);

		uint best_err = min(min(min(err0, err1), err2), err3);

		uint32_t best_sel = (best_err == err2) ? 2 : 3;
		best_sel = (best_err == err1) ? 1 : best_sel;
		best_sel = (best_err == err0) ? 0 : best_sel;

		etc_block_set_selector(&output_block, i & 3, i >> 2, best_sel);
	}

	pOutput_blocks[block_index] = output_block;
}

