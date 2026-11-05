// basisu_uastc_selectors_neon.inl
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

// Included inside basisu after color_cell_compressor_params is defined.
// The caller selects linear, unit-weight RGB/RGBA and palettes of 4/8/16/32.
// Keep the scalar projection's float operations separate and truncate toward
// zero. On ties it prefers the high neighbor, except for selector zero.
struct uastc_selector_palette_neon
{
	uint8x16x4_t low;
	uint8x16x4_t high;
	uint32_t count;
};

static inline uastc_selector_palette_neon uastc_selector_palette(const color_quad_u8 *colors, uint32_t count)
{
	const uint8_t *bytes = colors[0].m_c;
	uint8x16_t zero = vdupq_n_u8(0);
	uastc_selector_palette_neon result;
	result.count = count;
	result.low.val[0] = vld1q_u8(bytes);
	result.low.val[1] = count >= 8 ? vld1q_u8(bytes + 16) : zero;
	result.low.val[2] = count >= 16 ? vld1q_u8(bytes + 32) : zero;
	result.low.val[3] = count >= 16 ? vld1q_u8(bytes + 48) : zero;
	for (uint32_t i = 0; i < 4; ++i)
		result.high.val[i] = count == 32 ? vld1q_u8(bytes + 64 + i * 16) : zero;
	return result;
}

static inline uint32x4_t uastc_selector_colors(const uastc_selector_palette_neon &palette, int32x4_t selectors)
{
	uint8x16_t indices = vreinterpretq_u8_u32(vaddq_u32(vmulq_n_u32(vreinterpretq_u32_s32(selectors), 0x04040404), vdupq_n_u32(0x03020100)));
	uint8x16_t colors;
	if (palette.count == 4)
		colors = vqtbl1q_u8(palette.low.val[0], indices);
	else if (palette.count == 8)
	{
		uint8x16x2_t table = {palette.low.val[0], palette.low.val[1]};
		colors = vqtbl2q_u8(table, indices);
	}
	else
	{
		colors = vqtbl4q_u8(palette.low, indices);
		if (palette.count == 32)
			colors = vorrq_u8(colors, vqtbl4q_u8(palette.high, vsubq_u8(indices, vdupq_n_u8(64))));
	}
	return vreinterpretq_u32_u8(colors);
}

static inline uint32x4_t uastc_selector_error(uint32x4_t colors, int32x4_t r, int32x4_t g, int32x4_t b, int32x4_t a, bool alpha)
{
	uint32x4_t mask = vdupq_n_u32(255);
	int32x4_t dr = vsubq_s32(vreinterpretq_s32_u32(vandq_u32(colors, mask)), r);
	int32x4_t dg = vsubq_s32(vreinterpretq_s32_u32(vandq_u32(vshrq_n_u32(colors, 8), mask)), g);
	int32x4_t db = vsubq_s32(vreinterpretq_s32_u32(vandq_u32(vshrq_n_u32(colors, 16), mask)), b);
	int32x4_t error = vmlaq_s32(vmlaq_s32(vmulq_s32(dr, dr), dg, dg), db, db);
	if (alpha)
	{
		int32x4_t da = vsubq_s32(vreinterpretq_s32_u32(vshrq_n_u32(colors, 24)), a);
		error = vmlaq_s32(error, da, da);
	}
	return vreinterpretq_u32_s32(error);
}

static uint64_t uastc_evaluate_selectors_neon(const color_quad_u8 *colors, const color_cell_compressor_params *params, uint8_t *selectors)
{
	uint32_t count = params->m_num_selector_weights;
	bool alpha = params->m_has_alpha != 0;
	const color_quad_u8 &low = colors[0];
	const color_quad_u8 &high = colors[count - 1];
	int lr = low.m_c[0], lg = low.m_c[1], lb = low.m_c[2];
	int la = alpha ? low.m_c[3] : 0;
	int dr = high.m_c[0] - lr, dg = high.m_c[1] - lg, db = high.m_c[2] - lb;
	int da = alpha ? high.m_c[3] - la : 0;
	float factor = count / (float)(dr * dr + dg * dg + db * db + da * da + .00000125f);
	uastc_selector_palette_neon palette = uastc_selector_palette(colors, count);
	uint64_t total = 0;
	uint32_t i = 0;
	for (; i + 4 <= params->m_num_pixels; i += 4)
	{
		uint32x4_t pixels = vreinterpretq_u32_u8(vld1q_u8(params->m_pPixels[i].m_c));
		uint32x4_t mask = vdupq_n_u32(255);
		int32x4_t r = vreinterpretq_s32_u32(vandq_u32(pixels, mask));
		int32x4_t g = vreinterpretq_s32_u32(vandq_u32(vshrq_n_u32(pixels, 8), mask));
		int32x4_t b = vreinterpretq_s32_u32(vandq_u32(vshrq_n_u32(pixels, 16), mask));
		int32x4_t a = vreinterpretq_s32_u32(vshrq_n_u32(pixels, 24));
		int32x4_t projection = vmlaq_n_s32(vmlaq_n_s32(vmulq_n_s32(vsubq_s32(r, vdupq_n_s32(lr)), dr), vsubq_s32(g, vdupq_n_s32(lg)), dg), vsubq_s32(b, vdupq_n_s32(lb)), db);
		if (alpha)
			projection = vmlaq_n_s32(projection, vsubq_s32(a, vdupq_n_s32(la)), da);
		float32x4_t position = vaddq_f32(vmulq_n_f32(vcvtq_f32_s32(projection), factor), vdupq_n_f32(.5f));
		int32x4_t chosen = vminq_s32(vmaxq_s32(vcvtq_s32_f32(position), vdupq_n_s32(1)), vdupq_n_s32(count - 1));
		uint32x4_t err0 = uastc_selector_error(uastc_selector_colors(palette, vsubq_s32(chosen, vdupq_n_s32(1))), r, g, b, a, alpha);
		uint32x4_t err1 = uastc_selector_error(uastc_selector_colors(palette, chosen), r, g, b, a, alpha);
		uint32x4_t prefer_low = vorrq_u32(vcltq_u32(err0, err1), vandq_u32(vceqq_u32(err0, err1), vceqq_s32(chosen, vdupq_n_s32(1))));
		chosen = vsubq_s32(chosen, vreinterpretq_s32_u32(vandq_u32(prefer_low, vdupq_n_u32(1))));
		uint8x8_t bytes = vmovn_u16(vcombine_u16(vmovn_u32(vreinterpretq_u32_s32(chosen)), vdup_n_u16(0)));
		uint32_t packed = vget_lane_u32(vreinterpret_u32_u8(bytes), 0);
		memcpy(selectors + i, &packed, 4);
		// Four unit-weight RGBA errors sum to at most 4*4*255*255.
		total += vaddvq_u32(vminq_u32(err0, err1));
	}
	for (; i < params->m_num_pixels; ++i)
	{
		const color_quad_u8 &pixel = params->m_pPixels[i];
		int projection = (pixel.m_c[0] - lr) * dr + (pixel.m_c[1] - lg) * dg + (pixel.m_c[2] - lb) * db;
		if (alpha)
			projection += (pixel.m_c[3] - la) * da;
		int chosen = clampi((int)((float)projection * factor + .5f), 1, count - 1);
		uint32_t errors[2] = {};
		for (uint32_t neighbor = 0; neighbor < 2; ++neighbor)
			for (uint32_t channel = 0; channel < (alpha ? 4U : 3U); ++channel)
			{
				int diff = colors[chosen - 1 + neighbor].m_c[channel] - pixel.m_c[channel];
				errors[neighbor] += diff * diff;
			}
		if (errors[0] < errors[1] || (errors[0] == errors[1] && chosen == 1))
			--chosen;
		total += minimum(errors[0], errors[1]);
		selectors[i] = chosen;
	}
	return total;
}
