// basisu_kernels_neon.cpp
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

#include "basisu_enc.h"

#if BASISU_SUPPORT_NEON
#include "basisu_neon.h"

namespace
{
	template <bool Perceptual>
	static int32x4_t color_distance(int32x4_t dr, int32x4_t dg, int32x4_t db)
	{
		if (!Perceptual)
			return vaddq_s32(vaddq_s32(vmulq_s32(dr, dr), vmulq_s32(dg, dg)), vmulq_s32(db, db));
		int32x4_t l = vaddq_s32(vaddq_s32(vmulq_n_s32(dr, 14), vmulq_n_s32(dg, 45)), vmulq_n_s32(db, 5));
		int32x4_t cr = vsubq_s32(vshlq_n_s32(dr, 6), l);
		int32x4_t cb = vsubq_s32(vshlq_n_s32(db, 6), l);
		return vaddq_s32(vaddq_s32(vshrq_n_s32(vmulq_s32(l, l), 5),
								   vshrq_n_s32(vmulq_n_s32(vshrq_n_s32(vmulq_s32(cr, cr), 5), 26), 7)),
						 vshrq_n_s32(vmulq_n_s32(vshrq_n_s32(vmulq_s32(cb, cb), 5), 3), 7));
	}

	// Mode 0 evaluates given selectors, 1 chooses selectors, and 2 finds only
	// the error. Preserve the SSE kernel's tie breaking and four-pixel cutoff.
	template <bool Perceptual, int Mode>
	static void distance_rgb(int64_t *distance, const uint8_t *selectors, uint8_t *output_selectors, const basisu::color_rgba *colors, const basisu::color_rgba *pixels, uint32_t count, int64_t cutoff)
	{
		*distance = 0;
		uint8x16_t palette = vld1q_u8(reinterpret_cast<const uint8_t *>(colors));
		int32x4_t palette_r[4], palette_g[4], palette_b[4];
		if (Mode != 0)
			for (uint32_t c = 0; c < 4; ++c)
			{
				palette_r[c] = vdupq_n_s32(colors[c].r);
				palette_g[c] = vdupq_n_s32(colors[c].g);
				palette_b[c] = vdupq_n_s32(colors[c].b);
			}
		uint32_t i = 0;
		for (; i + 4 <= count; i += 4)
		{
			// Widen each RGBA channel for four pixels without reading beyond
			// the group. The NEON backend requires little-endian AArch64.
			uint32x4_t packed = vreinterpretq_u32_u8(vld1q_u8(reinterpret_cast<const uint8_t *>(pixels + i)));
			uint32x4_t mask = vdupq_n_u32(255);
			int32x4_t r = vreinterpretq_s32_u32(vandq_u32(packed, mask));
			int32x4_t g = vreinterpretq_s32_u32(vandq_u32(vshrq_n_u32(packed, 8), mask));
			int32x4_t b = vreinterpretq_s32_u32(vandq_u32(vshrq_n_u32(packed, 16), mask));
			int32x4_t best;
			int32x4_t chosen = vdupq_n_s32(0);
			if (Mode == 0)
			{
				// Like SSE, choose each pixel's palette color before computing
				// its distance. The palette and four pixels each fit in 16 bytes.
				uint32_t selector_bytes;
				memcpy(&selector_bytes, selectors + i, sizeof(selector_bytes));
				uint32x4_t selected = vmovl_u16(vget_low_u16(vmovl_u8(vcreate_u8(selector_bytes))));
				// Each 32-bit lane contains the four byte indices for one RGBA
				// palette entry: 4*selector + {0,1,2,3}.
				uint8x16_t table_indices = vreinterpretq_u8_u32(vaddq_u32(
					vmulq_n_u32(selected, 0x04040404), vdupq_n_u32(0x03020100)));
				uint32x4_t base = vreinterpretq_u32_u8(vqtbl1q_u8(palette, table_indices));
				int32x4_t base_r = vreinterpretq_s32_u32(vandq_u32(base, mask));
				int32x4_t base_g = vreinterpretq_s32_u32(vandq_u32(vshrq_n_u32(base, 8), mask));
				int32x4_t base_b = vreinterpretq_s32_u32(vandq_u32(vshrq_n_u32(base, 16), mask));
				best = color_distance<Perceptual>(vsubq_s32(base_r, r), vsubq_s32(base_g, g), vsubq_s32(base_b, b));
			}
			else
			{
				best = color_distance<Perceptual>(vsubq_s32(palette_r[0], r),
												  vsubq_s32(palette_g[0], g),
												  vsubq_s32(palette_b[0], b));
				for (uint32_t c = 1; c < 4; ++c)
				{
					int32x4_t error = color_distance<Perceptual>(vsubq_s32(palette_r[c], r),
																 vsubq_s32(palette_g[c], g),
																 vsubq_s32(palette_b[c], b));
					uint32x4_t use = vcltq_s32(error, best);
					best = vbslq_s32(use, error, best);
					chosen = vbslq_s32(use, vdupq_n_s32(c), chosen);
				}
			}
			if (Mode == 1)
			{
				int32_t lanes[4];
				vst1q_s32(lanes, chosen);
				for (uint32_t c = 0; c < 4; ++c)
					output_selectors[i + c] = static_cast<uint8_t>(lanes[c]);
			}
			// Widen before reduction: clustered pixel lists can exceed 32 bits.
			*distance += vaddvq_s64(vpaddlq_s32(best));
			if (Mode == 2 ? *distance > cutoff : *distance >= cutoff)
				return;
		}
		for (; i < count; ++i)
		{
			uint32_t chosen = Mode == 0 ? selectors[i] : 0;
			uint32_t error = basisu::color_distance(Perceptual, colors[chosen], pixels[i], false);
			if (Mode != 0)
			{
				for (uint32_t c = 1; c < 4; ++c)
				{
					uint32_t trial = basisu::color_distance(Perceptual, colors[c], pixels[i], false);
					if (trial < error)
					{
						error = trial;
						chosen = c;
					}
				}
			}
			if (Mode == 1)
				output_selectors[i] = static_cast<uint8_t>(chosen);
			*distance += error;
			if (Mode == 2 ? *distance > cutoff : *distance >= cutoff)
				return;
		}
	}
} // namespace

void perceptual_distance_rgb_4_N_simd(int64_t *d, const uint8_t *s, const basisu::color_rgba *c, const basisu::color_rgba *p, uint32_t n, int64_t cutoff)
{
	distance_rgb<true, 0>(d, s, 0, c, p, n, cutoff);
}

void linear_distance_rgb_4_N_simd(int64_t *d, const uint8_t *s, const basisu::color_rgba *c, const basisu::color_rgba *p, uint32_t n, int64_t cutoff)
{
	distance_rgb<false, 0>(d, s, 0, c, p, n, cutoff);
}

void find_selectors_perceptual_rgb_4_N_simd(int64_t *d, uint8_t *s, const basisu::color_rgba *c, const basisu::color_rgba *p, uint32_t n, int64_t cutoff)
{
	distance_rgb<true, 1>(d, 0, s, c, p, n, cutoff);
}

void find_selectors_linear_rgb_4_N_simd(int64_t *d, uint8_t *s, const basisu::color_rgba *c, const basisu::color_rgba *p, uint32_t n, int64_t cutoff)
{
	distance_rgb<false, 1>(d, 0, s, c, p, n, cutoff);
}

void find_lowest_error_perceptual_rgb_4_N_simd(int64_t *d, const basisu::color_rgba *c, const basisu::color_rgba *p, uint32_t n, int64_t cutoff)
{
	distance_rgb<true, 2>(d, 0, 0, c, p, n, cutoff);
}

void find_lowest_error_linear_rgb_4_N_simd(int64_t *d, const basisu::color_rgba *c, const basisu::color_rgba *p, uint32_t n, int64_t cutoff)
{
	distance_rgb<false, 2>(d, 0, 0, c, p, n, cutoff);
}

void update_covar_matrix_16x16_simd(uint32_t count, const void *weighted_vectors, const void *origin, const uint32_t *indices, void *matrix)
{
	const std::pair<basisu::vec16F, uint64_t> *vectors = static_cast<const std::pair<basisu::vec16F, uint64_t> *>(weighted_vectors);
	const float *center = static_cast<const float *>(origin);
	float *result = static_cast<float *>(matrix);
	float32x4_t sums[16][4];
	for (uint32_t x = 0; x < 16; ++x)
		for (uint32_t y = 0; y < 4; ++y)
			sums[x][y] = vdupq_n_f32(0);
	for (uint32_t k = 0; k < count; ++k)
	{
		const std::pair<basisu::vec16F, uint64_t> &entry = vectors[indices[k]];
		float delta[16];
		float32x4_t weighted[4];
		for (uint32_t y = 0; y < 4; ++y)
		{
			float32x4_t v = vsubq_f32(vld1q_f32(entry.first.get_ptr() + y * 4), vld1q_f32(center + y * 4));
			vst1q_f32(delta + y * 4, v);
			weighted[y] = vmulq_n_f32(v, static_cast<float>(entry.second));
		}
		for (uint32_t x = 0; x < 16; ++x)
			for (uint32_t y = 0; y < 4; ++y)
				sums[x][y] = vaddq_f32(sums[x][y], vmulq_n_f32(weighted[y], delta[x]));
	}
	for (uint32_t x = 0; x < 16; ++x)
		for (uint32_t y = 0; y < 4; ++y)
			vst1q_f32(result + x * 16 + y * 4, sums[x][y]);
}
#endif
