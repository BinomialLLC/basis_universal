// basisu_uastc_neon.inl
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
// The YCbCr differences can exceed 16 bits, and their squared/weighted
// distances exceed 32 bits. Widen every multiply before summing.
struct uastc_hint_palette_simd
{
	int32x4_t y;
	int32x4_t cb;
	int32x4_t cr;
};

static inline uastc_hint_palette_simd uastc_hint_palette(const color_rgba &base, uint32_t intensity_table)
{
	int32x4_t offsets = vld1q_s32(g_etc1_inten_tables[intensity_table]);
	int32x4_t zero = vdupq_n_s32(0), max = vdupq_n_s32(255);
	int32x4_t r = vminq_s32(max, vmaxq_s32(zero, vaddq_s32(vdupq_n_s32(base.r), offsets)));
	int32x4_t g = vminq_s32(max, vmaxq_s32(zero, vaddq_s32(vdupq_n_s32(base.g), offsets)));
	int32x4_t b = vminq_s32(max, vmaxq_s32(zero, vaddq_s32(vdupq_n_s32(base.b), offsets)));
	int32x4_t y = vmlaq_n_s32(vmlaq_n_s32(vmulq_n_s32(r, 54), g, 183), b, 19);
	uastc_hint_palette_simd result = {y, vsubq_s32(vshlq_n_s32(b, 8), y), vsubq_s32(vshlq_n_s32(r, 8), y)};
	return result;
}

static inline void uastc_hint_store_palette(const uastc_hint_palette_simd &palette, ycbcr colors[4])
{
	int32x4x3_t channels = {palette.y, palette.cb, palette.cr};
	vst3q_s32(&colors[0].m_y, channels);
}

static inline uint64_t uastc_hint_min_error(const uastc_hint_palette_simd &palette, const ycbcr &pixel, bool selector_key)
{
	int32x4_t y = vsubq_s32(palette.y, vdupq_n_s32(pixel.m_y));
	int32x4_t cb = vsubq_s32(palette.cb, vdupq_n_s32(pixel.m_cb));
	int32x4_t cr = vsubq_s32(palette.cr, vdupq_n_s32(pixel.m_cr));
	int64x2_t low = vaddq_s64(vshlq_n_s64(vmull_s32(vget_low_s32(y), vget_low_s32(y)), 2),
							  vaddq_s64(vmull_s32(vget_low_s32(cb), vget_low_s32(cb)), vmull_s32(vget_low_s32(cr), vget_low_s32(cr))));
	int64x2_t high = vaddq_s64(vshlq_n_s64(vmull_s32(vget_high_s32(y), vget_high_s32(y)), 2),
							   vaddq_s64(vmull_s32(vget_high_s32(cb), vget_high_s32(cb)), vmull_s32(vget_high_s32(cr), vget_high_s32(cr))));
	uint64x2_t lo = vreinterpretq_u64_s64(low);
	uint64x2_t hi = vreinterpretq_u64_s64(high);
	if (selector_key)
	{
		const uint64_t low_indices[2] = {0, 1}, high_indices[2] = {2, 3};
		lo = vaddq_u64(vshlq_n_u64(lo, 2), vld1q_u64(low_indices));
		hi = vaddq_u64(vshlq_n_u64(hi, 2), vld1q_u64(high_indices));
	}
	uint64x2_t best = vbslq_u64(vcltq_u64(lo, hi), lo, hi);
	return minimum(vgetq_lane_u64(best, 0), vgetq_lane_u64(best, 1));
}

// Gray-distance numerators lie in [-764, 766]. On this range, multiplying
// the absolute value by ceil(65536/3) and shifting is exact integer division.
// Restore the sign to match C++ truncation toward zero.
static inline int32x4_t uastc_gray_divide3(int32x4_t value)
{
	int32x4_t quotient = vshrq_n_s32(vmulq_n_s32(vabsq_s32(value), 21846), 16);
	return vbslq_s32(vcltq_s32(value, vdupq_n_s32(0)), vnegq_s32(quotient), quotient);
}

static inline int32x4_t uastc_gray_distance4(int32x4_t r, int32x4_t g, int32x4_t b, int32x4_t avg_r, int32x4_t avg_g, int32x4_t avg_b)
{
	int32x4_t sum = vaddq_s32(vaddq_s32(vsubq_s32(r, avg_r), vsubq_s32(g, avg_g)), vsubq_s32(b, avg_b));
	int32x4_t gray = uastc_gray_divide3(vaddq_s32(sum, vdupq_n_s32(1)));
	int32x4_t zero = vdupq_n_s32(0), max = vdupq_n_s32(255);
	int32x4_t dr = vsubq_s32(r, vminq_s32(max, vmaxq_s32(zero, vaddq_s32(avg_r, gray))));
	int32x4_t dg = vsubq_s32(g, vminq_s32(max, vmaxq_s32(zero, vaddq_s32(avg_g, gray))));
	int32x4_t db = vsubq_s32(b, vminq_s32(max, vmaxq_s32(zero, vaddq_s32(avg_b, gray))));
	return vmlaq_s32(vmlaq_s32(vmulq_s32(dr, dr), dg, dg), db, db);
}

static inline bool uastc_estimate_flip_simd(const color_rgba *pixels, const int upper[3], const int lower[3], const int left[3], const int right[3])
{
	int32x4_t left_right_r = vcombine_s32(vdup_n_s32(left[0]), vdup_n_s32(right[0]));
	int32x4_t left_right_g = vcombine_s32(vdup_n_s32(left[1]), vdup_n_s32(right[1]));
	int32x4_t left_right_b = vcombine_s32(vdup_n_s32(left[2]), vdup_n_s32(right[2]));
	int upper_lower_error = 0, left_right_error = 0;
	for (uint32_t y = 0; y < 4; ++y)
	{
		uint32x4_t row = vreinterpretq_u32_u8(vld1q_u8(&pixels[y * 4].r));
		uint32x4_t mask = vdupq_n_u32(255);
		int32x4_t r = vreinterpretq_s32_u32(vandq_u32(row, mask));
		int32x4_t g = vreinterpretq_s32_u32(vandq_u32(vshrq_n_u32(row, 8), mask));
		int32x4_t b = vreinterpretq_s32_u32(vandq_u32(vshrq_n_u32(row, 16), mask));
		const int *average = y < 2 ? upper : lower;
		upper_lower_error += vaddvq_s32(uastc_gray_distance4(r, g, b, vdupq_n_s32(average[0]), vdupq_n_s32(average[1]), vdupq_n_s32(average[2])));
		left_right_error += vaddvq_s32(uastc_gray_distance4(r, g, b, left_right_r, left_right_g, left_right_b));
	}
	return upper_lower_error < left_right_error;
}
