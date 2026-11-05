// basisu_uastc_sse.inl
// Copyright (C) 2019-2026 Binomial LLC. All Rights Reserved.
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

// Included inside basisu after ycbcr and color_diff are defined.
// Widen the signed differences before squaring: YCbCr hint errors exceed 32 bits.
struct uastc_hint_palette_simd
{
	__m128i y;
	__m128i cb;
	__m128i cr;
};

static inline uastc_hint_palette_simd uastc_hint_palette(const color_rgba &base, uint32_t intensity_table)
{
	const __m128i offsets = _mm_loadu_si128(reinterpret_cast<const __m128i *>(g_etc1_inten_tables[intensity_table]));
	const __m128i zero = _mm_setzero_si128(), max = _mm_set1_epi32(255);
	const __m128i r = _mm_min_epi32(max, _mm_max_epi32(zero, _mm_add_epi32(_mm_set1_epi32(base.r), offsets)));
	const __m128i g = _mm_min_epi32(max, _mm_max_epi32(zero, _mm_add_epi32(_mm_set1_epi32(base.g), offsets)));
	const __m128i b = _mm_min_epi32(max, _mm_max_epi32(zero, _mm_add_epi32(_mm_set1_epi32(base.b), offsets)));
	const __m128i y = _mm_add_epi32(_mm_add_epi32(_mm_mullo_epi32(r, _mm_set1_epi32(54)), _mm_mullo_epi32(g, _mm_set1_epi32(183))), _mm_mullo_epi32(b, _mm_set1_epi32(19)));
	uastc_hint_palette_simd result = {y, _mm_sub_epi32(_mm_slli_epi32(b, 8), y), _mm_sub_epi32(_mm_slli_epi32(r, 8), y)};
	return result;
}

static inline void uastc_hint_store_palette(const uastc_hint_palette_simd &palette, ycbcr colors[4])
{
	int32_t y[4], cb[4], cr[4];
	_mm_storeu_si128(reinterpret_cast<__m128i *>(y), palette.y);
	_mm_storeu_si128(reinterpret_cast<__m128i *>(cb), palette.cb);
	_mm_storeu_si128(reinterpret_cast<__m128i *>(cr), palette.cr);
	for (uint32_t i = 0; i < 4; ++i)
	{
		colors[i].m_y = y[i];
		colors[i].m_cb = cb[i];
		colors[i].m_cr = cr[i];
	}
}

// SSE4.1 has no unsigned 64-bit comparison. Compare the high words first,
// then the unsigned low words when the high words are equal.
static inline __m128i uastc_min_epu64(__m128i a, __m128i b)
{
	const __m128i sign = _mm_set1_epi32(INT32_MIN);
	const __m128i ah = _mm_shuffle_epi32(a, _MM_SHUFFLE(3, 3, 1, 1));
	const __m128i bh = _mm_shuffle_epi32(b, _MM_SHUFFLE(3, 3, 1, 1));
	const __m128i al = _mm_shuffle_epi32(a, _MM_SHUFFLE(2, 2, 0, 0));
	const __m128i bl = _mm_shuffle_epi32(b, _MM_SHUFFLE(2, 2, 0, 0));
	const __m128i high_greater = _mm_cmpgt_epi32(_mm_xor_si128(ah, sign), _mm_xor_si128(bh, sign));
	const __m128i low_greater = _mm_cmpgt_epi32(_mm_xor_si128(al, sign), _mm_xor_si128(bl, sign));
	const __m128i greater = _mm_or_si128(high_greater, _mm_and_si128(_mm_cmpeq_epi32(ah, bh), low_greater));
	return _mm_blendv_epi8(a, b, greater);
}

static inline uint64_t uastc_hint_min_error(const uastc_hint_palette_simd &palette, const ycbcr &pixel, bool selector_key)
{
	const __m128i y = _mm_sub_epi32(palette.y, _mm_set1_epi32(pixel.m_y));
	const __m128i cb = _mm_sub_epi32(palette.cb, _mm_set1_epi32(pixel.m_cb));
	const __m128i cr = _mm_sub_epi32(palette.cr, _mm_set1_epi32(pixel.m_cr));
	const __m128i odd_y = _mm_srli_si128(y, 4), odd_cb = _mm_srli_si128(cb, 4), odd_cr = _mm_srli_si128(cr, 4);
	__m128i even = _mm_add_epi64(_mm_slli_epi64(_mm_mul_epi32(y, y), 2), _mm_add_epi64(_mm_mul_epi32(cb, cb), _mm_mul_epi32(cr, cr)));
	__m128i odd = _mm_add_epi64(_mm_slli_epi64(_mm_mul_epi32(odd_y, odd_y), 2), _mm_add_epi64(_mm_mul_epi32(odd_cb, odd_cb), _mm_mul_epi32(odd_cr, odd_cr)));
	if (selector_key)
	{
		even = _mm_add_epi64(_mm_slli_epi64(even, 2), _mm_set_epi64x(2, 0));
		odd = _mm_add_epi64(_mm_slli_epi64(odd, 2), _mm_set_epi64x(3, 1));
	}
	__m128i best = uastc_min_epu64(even, odd);
	best = uastc_min_epu64(best, _mm_shuffle_epi32(best, _MM_SHUFFLE(1, 0, 3, 2)));
	uint64_t result;
	_mm_storel_epi64(reinterpret_cast<__m128i *>(&result), best);
	return result;
}

// On [-764, 766], abs(value) * ceil(65536/3) >> 16 is exact division.
// Applying the original sign preserves C++ truncation toward zero.
static inline __m128i uastc_gray_divide3(__m128i value)
{
	const __m128i quotient = _mm_srli_epi32(_mm_mullo_epi32(_mm_abs_epi32(value), _mm_set1_epi32(21846)), 16);
	return _mm_sign_epi32(quotient, value);
}

static inline __m128i uastc_gray_distance4(__m128i r, __m128i g, __m128i b, __m128i avg_r, __m128i avg_g, __m128i avg_b)
{
	const __m128i sum = _mm_add_epi32(_mm_add_epi32(_mm_sub_epi32(r, avg_r), _mm_sub_epi32(g, avg_g)), _mm_sub_epi32(b, avg_b));
	const __m128i gray = uastc_gray_divide3(_mm_add_epi32(sum, _mm_set1_epi32(1)));
	const __m128i zero = _mm_setzero_si128(), max = _mm_set1_epi32(255);
	const __m128i dr = _mm_sub_epi32(r, _mm_min_epi32(max, _mm_max_epi32(zero, _mm_add_epi32(avg_r, gray))));
	const __m128i dg = _mm_sub_epi32(g, _mm_min_epi32(max, _mm_max_epi32(zero, _mm_add_epi32(avg_g, gray))));
	const __m128i db = _mm_sub_epi32(b, _mm_min_epi32(max, _mm_max_epi32(zero, _mm_add_epi32(avg_b, gray))));
	return _mm_add_epi32(_mm_add_epi32(_mm_mullo_epi32(dr, dr), _mm_mullo_epi32(dg, dg)), _mm_mullo_epi32(db, db));
}

static inline bool uastc_estimate_flip_simd(const color_rgba *pixels, const int upper[3], const int lower[3], const int left[3], const int right[3])
{
	const __m128i left_right_r = _mm_setr_epi32(left[0], left[0], right[0], right[0]);
	const __m128i left_right_g = _mm_setr_epi32(left[1], left[1], right[1], right[1]);
	const __m128i left_right_b = _mm_setr_epi32(left[2], left[2], right[2], right[2]);
	const __m128i mask = _mm_set1_epi32(255);
	__m128i upper_lower_error = _mm_setzero_si128(), left_right_error = _mm_setzero_si128();
	for (uint32_t y = 0; y < 4; ++y)
	{
		const __m128i row = _mm_loadu_si128(reinterpret_cast<const __m128i *>(&pixels[y * 4].r));
		const __m128i r = _mm_and_si128(row, mask);
		const __m128i g = _mm_and_si128(_mm_srli_epi32(row, 8), mask);
		const __m128i b = _mm_and_si128(_mm_srli_epi32(row, 16), mask);
		const int *average = y < 2 ? upper : lower;
		upper_lower_error = _mm_add_epi32(upper_lower_error, uastc_gray_distance4(r, g, b, _mm_set1_epi32(average[0]), _mm_set1_epi32(average[1]), _mm_set1_epi32(average[2])));
		left_right_error = _mm_add_epi32(left_right_error, uastc_gray_distance4(r, g, b, left_right_r, left_right_g, left_right_b));
	}
	__m128i errors = _mm_hadd_epi32(upper_lower_error, left_right_error);
	errors = _mm_hadd_epi32(errors, errors);
	return _mm_cvtsi128_si32(errors) < _mm_extract_epi32(errors, 1);
}
