// frontend.c
// Copyright (C) 2019 Binomial LLC. All Rights Reserved.
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

// Define types for oclfrontend_api.h without an #include <OpenCL.h>:
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;
typedef uint8_t cl_uchar;
typedef int cl_int;
typedef uint32_t cl_uint;
typedef uint64_t cl_ulong;
typedef float cl_float;
typedef int4 cl_int4;
// Now ready to #include "oclfrontend_api.h"

#include "oclfrontend_api.h"

/* The following macros are defined in oclhost.cpp when the kernel is compiled. */
#if 0
#define ETC1_QUALITY ETC1_Q_SLOW /*or some similar ETC1_Q_* value*/
#define ETC1_NO_PERCEPTUAL 1 /*because default is PERCEPTUAL*/
#define ETC1_NO_CLUSTER_FIT 1 /*because default is CLUSTER_FIT*/
#define ETC1_FORCE_ETC1S 1
#define ETC1_USE_COLOR4 1 /*because default is no COLOR4 */
#endif

#define UINT64_MAX ((uint64_t) -1)
#define TYPEOF_CONST __constant opencl_const*

uint4 unpackVec4(uint32_t rgba) {
	return (uint4)(rgba & 0xff, (rgba >> 8) & 0xff, (rgba >> 16) & 0xff, (rgba >> 24) & 0xff);
}

__constant int* get_inten_table(TYPEOF_CONST THE_C, uint32_t i) {
	return &THE_C->g_etc1_inten_tables[i][0];
}

void get_cluster_fit_order_tab(TYPEOF_CONST THE_C, uint32_t i, int out[4]) {
	uint v = THE_C->g_cluster_fit_order_tab[i];
	for (int q = 0; q < 4; q++) {
		out[q] = (int)((v >> (8*q)) & 0xff);
	}
}

int4 get_scaled_color(const etc1_solution_coordinates* coords) {
	int4 c = coords->unscaled_color;
#ifdef ETC1_USE_COLOR4
	c |= c << 4;
#else
	c = (c >> 2) | (c << 3);
#endif
	c.w = 255;
	return c;
}

void sort_luma(__global etc1_optimizer* self) {
	for (int i = 0; i < ETC1_OPTIMIZER_NUM_SRC_PIXELS; i++) {
		self->sorted_luma_indices[i] = i;
	}

	int beg[ETC1_OPTIMIZER_NUM_SRC_PIXELS], end[ETC1_OPTIMIZER_NUM_SRC_PIXELS];
	beg[0] = 0;
	end[0] = ETC1_OPTIMIZER_NUM_SRC_PIXELS;
	for (int i = 0; i >= 0; ) {
		int L = beg[i];
		int R = end[i]-1;
		int piv = self->sorted_luma_indices[L];
		if (L >= R) {
			i--;
			continue;
		}
		while (L < R) {
			while (self->luma[self->sorted_luma_indices[R]] >= self->luma[piv] && L<R) R--;
			if (L<R) self->sorted_luma_indices[L++] = self->sorted_luma_indices[R];
			while (self->luma[self->sorted_luma_indices[L]] <= self->luma[piv] && L<R) L++;
			if (L<R) self->sorted_luma_indices[R--] = self->sorted_luma_indices[L];
		}
		self->sorted_luma_indices[L] = piv;
		beg[i+1] = L+1;
		end[i+1] = end[i];
		end[i++] = L;
		if (end[i]-beg[i] > end[i-1]-beg[i-1]) {
			piv = beg[i]; beg[i] = beg[i-1]; beg[i-1] = piv;
			piv = end[i]; end[i] = end[i-1]; end[i-1] = piv;
		}
	}
	for (int i = 0; i < ETC1_OPTIMIZER_NUM_SRC_PIXELS; i++) {
		self->sorted_luma[i] = self->luma[self->sorted_luma_indices[i]];
	}
}

__constant double4 rgbWeight = (double4)(.2126f, .715f, .0722f, 0);
__constant double3 invRgbWeight = (double3)(
		32.0f*4.0f,
		32.0f*2.0f*(.5f / (1.0f - .2126f))*(.5f / (1.0f - .2126f)),
		32.0f*.25f*(.5f / (1.0f - .0722f))*(.5f / (1.0f - .0722f)));
uint64_t color_distance_rgb(uint3 c0, uint3 c1) {
#ifdef ETC1_NO_PERCEPTUAL
	uint3 r = abs_diff(c0, c1);
	return r.x*r.x + r.y*r.y + r.z*r.z;
#else
	// Very small errors crop up when float is used instead of double:
	// c0 = [ca 97 6c] c1 = [bd 8c 62] err=16566 should be 16565 (offset of +1)
	double4 c0d = (double4)(convert_double3(c0), 0);
	c0d -= dot(c0d, rgbWeight);
	double4 c1d = (double4)(convert_double3(c1), 0);
	c1d -= dot(c1d, rgbWeight);
	c1d -= c0d;
	uint64_t d = convert_ulong_rtz(dot(invRgbWeight, c1d.wxz * c1d.wxz));
	/* alpha channel is:
		long da = convert_long_rtz(c0d.w) - convert_long_rtz(c1d.w);
		d += 128 * da * da;
	*/
	return d;
#endif
}

uint64_t evaluate_solution_fast(TYPEOF_CONST THE_C, __global etc1_optimizer* self, int3 base_color,
		etc1_optimizer_solution* trial_solution, cl_uchar selectors[ETC1_OPTIMIZER_NUM_SRC_PIXELS], int inten_table) {
	__constant int* pinten = get_inten_table(THE_C, inten_table);
	uint32_t block_inten[4];
	uint3 block_colors[4];
	for (uint32_t s = 0; s < 4; s++) {
		uint3 block_color = convert_uint3(clamp(base_color + pinten[s], (int)0, 255));
		block_colors[s] = block_color;
		block_inten[s] = block_color.x + block_color.y + block_color.z;
	}

	// evaluate_solution_fast() enforces/assumesd a total ordering of the input colors along the intensity (1,1,1) axis to more quickly classify the inputs to selectors.
	// The inputs colors have been presorted along the projection onto this axis, and ETC1 block colors are always ordered along the intensity axis, so this classification is fast.
	// 0   1   2   3
	//   01  12  23
	uint32_t block_inten_midpoints[3] = { block_inten[0] + block_inten[1], block_inten[1] + block_inten[2], block_inten[2] + block_inten[3] };

	uint64_t total_error = 0;
	if ((self->sorted_luma[ETC1_OPTIMIZER_NUM_SRC_PIXELS - 1] * 2) < block_inten_midpoints[0]) {
		if (block_inten[0] > self->sorted_luma[ETC1_OPTIMIZER_NUM_SRC_PIXELS - 1]) {
			uint32_t min_error = abs_diff((int)block_inten[0], (int)self->sorted_luma[ETC1_OPTIMIZER_NUM_SRC_PIXELS - 1]);
			if (min_error >= trial_solution->error)
				return UINT64_MAX;
		}

		for (uint32_t c = 0; c < ETC1_OPTIMIZER_NUM_SRC_PIXELS; c++) {
			selectors[c] = 0;
			total_error += color_distance_rgb(block_colors[0], unpackVec4(self->pixels[c]).xyz);
		}
	} else if ((self->sorted_luma[0] * 2) >= block_inten_midpoints[2]) {
		if (self->sorted_luma[0] > block_inten[3]) {
			uint32_t min_error = abs_diff((int)self->sorted_luma[0], (int)block_inten[3]);
			if (min_error >= trial_solution->error)
				return UINT64_MAX;
		}

		for (uint32_t c = 0; c < ETC1_OPTIMIZER_NUM_SRC_PIXELS; c++) {
			selectors[c] = 3;
			total_error += color_distance_rgb(block_colors[3], unpackVec4(self->pixels[c]).xyz);
		}
	} else {
		uint32_t cur_selector = 0, c;
		for (c = 0; c < ETC1_OPTIMIZER_NUM_SRC_PIXELS; c++) {
			uint32_t y = self->sorted_luma[c];
			while ((y * 2) >= block_inten_midpoints[cur_selector] && cur_selector < 3) {
				cur_selector++;
			}
			if (cur_selector >= 3) break;
			uint32_t sorted_pixel_index = self->sorted_luma_indices[c];
			selectors[sorted_pixel_index] = (cl_uchar)(cur_selector);
			total_error += color_distance_rgb(block_colors[cur_selector], unpackVec4(self->pixels[sorted_pixel_index]).xyz);
		}
		while (c < ETC1_OPTIMIZER_NUM_SRC_PIXELS) {
			uint32_t sorted_pixel_index = self->sorted_luma_indices[c];
			selectors[sorted_pixel_index] = 3;
			total_error += color_distance_rgb(block_colors[3], unpackVec4(self->pixels[sorted_pixel_index]).xyz);
			++c;
		}
	}
	return total_error;
}

uint64_t evaluate_solution_slow(TYPEOF_CONST THE_C, __global etc1_optimizer* self, int3 base_color,
		cl_ulong trial_solution_error, cl_uchar selectors[ETC1_OPTIMIZER_NUM_SRC_PIXELS], int inten_table) {
	__constant int* pinten = get_inten_table(THE_C, inten_table);
	uint64_t total_error = 0;

	for (uint32_t c = 0; c < ETC1_OPTIMIZER_NUM_SRC_PIXELS; c++) {
		uint3 src_pixel = unpackVec4(self->pixels[c]).xyz;
		uint32_t best_selector_index = 0;
		uint64_t best_error = UINT64_MAX;
#if 0
		if (m_pParams->m_pForce_selectors) {
			best_selector_index = m_pParams->m_pForce_selectors[c];
			best_error = color_distance_rgb(src_pixel, block_colors[best_selector_index]);
		} else
#endif
		{
			for (uint32_t s = 0; s < 4; s++) {
				uint3 block_color = convert_uint3(clamp(base_color + pinten[s], (int)0, 255));
				uint64_t err = color_distance_rgb(src_pixel, block_color);
				if (err < best_error) {
					best_error = err;
					best_selector_index = s;
				}
			}
		}
		selectors[c] = (cl_uchar)(best_selector_index);
		total_error += best_error;
		if (total_error > trial_solution_error) {
			break;
		}
	}
	return total_error;
}

void evaluate_solution(TYPEOF_CONST THE_C, __global etc1_optimizer* self, const etc1_solution_coordinates* coords,
		etc1_optimizer_solution* trial_solution) {
	/*uint32_t k = coords.m_unscaled_color.r | (coords.m_unscaled_color.g << 8) | (coords.m_unscaled_color.b << 16);
	if (!m_solutions_tried.insert(k).second)
		return false;*/

	/*m_constrain_against_base_color5 is not implemented yet:
	if (m_pParams->m_constrain_against_base_color5) {
		const int dr = (int)coords.m_unscaled_color.r - (int)m_pParams->m_base_color5.r;
		const int dg = (int)coords.m_unscaled_color.g - (int)m_pParams->m_base_color5.g;
		const int db = (int)coords.m_unscaled_color.b - (int)m_pParams->m_base_color5.b;

		if ((minimum(dr, dg, db) < cETC1ColorDeltaMin) || (maximum(dr, dg, db) > cETC1ColorDeltaMax))
		{
			return false;
		}
	}*/

	cl_uchar selectors[ETC1_OPTIMIZER_NUM_SRC_PIXELS];
	int3 base_color = get_scaled_color(coords).xyz;
	trial_solution->error = UINT64_MAX;
#if (ETC1_QUALITY == ETC1_Q_FAST) || (ETC1_QUALITY == ETC1_Q_MED)
	for (int inten_table = cETC1IntenModifierValues - 1; inten_table >= 0; --inten_table) {
		uint64_t total_error = evaluate_solution_fast(THE_C, self, base_color, trial_solution, selectors, inten_table);
#else
	for (int inten_table = 0; inten_table < cETC1IntenModifierValues; inten_table++) {
		uint64_t total_error = evaluate_solution_slow(THE_C, self, base_color, trial_solution->error, selectors, inten_table);
#endif

		if (total_error < trial_solution->error) {
			for (uint32_t c = 0; c < ETC1_OPTIMIZER_NUM_SRC_PIXELS; c++) {
				trial_solution->selectors[c] = selectors[c];
			}
			trial_solution->error = total_error;
			trial_solution->coords.flags = inten_table;
			trial_solution->is_valid = true;
#if (ETC1_QUALITY == ETC1_Q_FAST) || (ETC1_QUALITY == ETC1_Q_MED)
			if (!total_error)
				break;
#endif
		}
	}

	if (trial_solution->error < self->best.error) {
		self->best.is_valid = true;
		self->best.coords.unscaled_color = coords->unscaled_color;
		self->best.coords.flags = (coords->flags & ~ETC1_SOLUTION_INTEN_TABLE_MASK) | trial_solution->coords.flags;
		self->best.error = trial_solution->error;
		for (uint32_t c = 0; c < ETC1_OPTIMIZER_NUM_SRC_PIXELS; c++) {
			self->best.selectors[c] = trial_solution->selectors[c];
		}
	}
}

void etc1_optimizer_compute(TYPEOF_CONST THE_C, __global etc1_optimizer* self) {
	// inlined function etc1_optimizer_init:
	self->best.is_valid = false;

#ifdef ETC1_USE_COLOR4
#define LIMIT 15
#else
#define LIMIT 31
#endif
	int3 sum = (int3)(0);
	for (uint32_t i = 0; i < ETC1_OPTIMIZER_NUM_SRC_PIXELS; i++)
	{
		uint3 c = unpackVec4(self->pixels[i]).xyz;
		sum += convert_int3(c);
		self->luma[i] = (uint16_t)(c.x + c.y + c.z);
		self->sorted_luma_indices[i] = i;
	}
	float3 avg_color = convert_float3(sum) / (float)(ETC1_OPTIMIZER_NUM_SRC_PIXELS);
	etc1_solution_coordinates coords;
	coords.unscaled_color = (int4)(clamp(convert_int3(avg_color * (float)LIMIT / 255.0f + .5f), (int)0, LIMIT), 0);
	coords.flags = 0;

	// begin etc1_optimizer_compute:
#ifdef ETC1_NO_CLUSTER_FIT
#error not implemented yet: compute_internal_neighborhood(coords.unscaled_color.r, coords.unscaled_color.g, coords.unscaled_color.b);
#endif

#ifndef ETC1_QUALITY
#error Kernel must be compiled with -DETC1_QUALITY=nnn
#elif ETC1_QUALITY == ETC1_Q_FAST
#define TOTAL_PERMS_TO_TRY (4)
#elif ETC1_QUALITY == ETC1_Q_MED
#define TOTAL_PERMS_TO_TRY (32)
#elif ETC1_QUALITY == ETC1_Q_SLOW
#define TOTAL_PERMS_TO_TRY (64)
#elif ETC1_QUALITY == ETC1_Q_UBER
#define TOTAL_PERMS_TO_TRY CLUSTER_FIT_ORDER_TABLE_SIZE
#else
#error invalid ETC1_QUALITY
#endif

#if TOTAL_PERMS_TO_TRY <= 32
	// Sort luma into sorted_luma and sorted_luma_indices.
	sort_luma(self);
#endif

	etc1_optimizer_solution trial_solution;
	// inlined function compute_internal_cluster_fit() here:
	self->best.error = UINT64_MAX;
	evaluate_solution(THE_C, self, &coords, &trial_solution);

	if (self->best.error == 0 || !self->best.is_valid) {
		return;
	}

	for (uint32_t i = 0; i < TOTAL_PERMS_TO_TRY && self->best.error; i++) {
		etc1_solution_coordinates base_coords;
		base_coords.unscaled_color = self->best.coords.unscaled_color;
		base_coords.flags = self->best.coords.flags;
		int3 base_color = get_scaled_color(&base_coords).xyz;

      __constant int* pinten = get_inten_table(THE_C, self->best.coords.flags & ETC1_SOLUTION_INTEN_TABLE_MASK);
		int num_selectors[4];
		get_cluster_fit_order_tab(THE_C, i, num_selectors);
		sum = (int3)(0);
		for (uint32_t q = 0; q < 4; q++) {
			sum += num_selectors[q] * (clamp(base_color + pinten[q], (int)0, 255) - base_color);
		}

		if ((!sum.x) && (!sum.y) && (!sum.z)) continue;

		float3 avg_delta = convert_float3(sum) / 8.f;
		coords.unscaled_color = (int4)(clamp(convert_int3((avg_color - avg_delta) * LIMIT / 255.0f + .5f), (int)0, LIMIT), 0);

		evaluate_solution(THE_C, self, &coords, &trial_solution);
	}
#undef LIMIT
}

__kernel void main(TYPEOF_CONST THE_C, __global etc1_optimizer* states) {
	#define idx get_global_id(0)
	#define state (&states[idx])
	#if 1
	state->best.q = 41;
	etc1_optimizer_compute(THE_C, state);
	#endif
}
