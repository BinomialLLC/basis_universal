// basisu_bc15_spmd_neon.cpp
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

// The BC1/BC4 algorithms stay in the shared kernel .inl. This translation
// unit implements only the vector operations and predicated stores they use.
namespace
{
	struct vfloat;
	struct vbool
	{
		uint32x4_t m_value;
		explicit vbool(uint32x4_t value)
			: m_value(value)
		{
		}
	};
	struct gather_int
	{
		int32x4_t indices;
		const int *values;
	};
	struct vint
	{
		int32x4_t m_value;
		vint() {}
		vint(int value)
			: m_value(vdupq_n_s32(value))
		{
		}
		explicit vint(int32x4_t value)
			: m_value(value)
		{
		}
		explicit vint(const vfloat &value);
		gather_int operator[](const int *values) const
		{
			gather_int result = {m_value, values};
			return result;
		}
	};
	struct vfloat
	{
		float32x4_t m_value;
		vfloat() {}
		vfloat(float value)
			: m_value(vdupq_n_f32(value))
		{
		}
		explicit vfloat(float32x4_t value)
			: m_value(value)
		{
		}
		explicit vfloat(const vint &value)
			: m_value(vcvtq_f32_s32(value.m_value))
		{
		}
	};
	inline vint::vint(const vfloat &value)
		: m_value(vcvtq_s32_f32(value.m_value))
	{
	}

	inline vint operator+(const vint &a, const vint &b)
	{
		return vint(vaddq_s32(a.m_value, b.m_value));
	}
	inline vint operator-(const vint &a, const vint &b)
	{
		return vint(vsubq_s32(a.m_value, b.m_value));
	}
	inline vint operator*(const vint &a, const vint &b)
	{
		return vint(vmulq_s32(a.m_value, b.m_value));
	}
	inline vint operator&(const vint &a, const vint &b)
	{
		return vint(vandq_s32(a.m_value, b.m_value));
	}
	inline vint operator|(const vint &a, const vint &b)
	{
		return vint(vorrq_s32(a.m_value, b.m_value));
	}
	inline vint operator^(const vint &a, const vint &b)
	{
		return vint(veorq_s32(a.m_value, b.m_value));
	}
	inline vint operator<<(const vint &a, int shift)
	{
		return vint(vshlq_s32(a.m_value, vdupq_n_s32(shift)));
	}
	inline vint shift_right_unsigned(const vint &a, int shift)
	{
		return vint(vreinterpretq_s32_u32(vshlq_u32(vreinterpretq_u32_s32(a.m_value), vdupq_n_s32(-shift))));
	}
	inline vint min(const vint &a, const vint &b)
	{
		return vint(vminq_s32(a.m_value, b.m_value));
	}
	inline vint max(const vint &a, const vint &b)
	{
		return vint(vmaxq_s32(a.m_value, b.m_value));
	}
	inline vbool operator==(const vint &a, const vint &b)
	{
		return vbool(vceqq_s32(a.m_value, b.m_value));
	}
	inline vbool operator!=(const vint &a, const vint &b)
	{
		return vbool(vmvnq_u32(vceqq_s32(a.m_value, b.m_value)));
	}
	inline vbool operator<(const vint &a, const vint &b)
	{
		return vbool(vcltq_s32(a.m_value, b.m_value));
	}
	inline vbool operator>(const vint &a, const vint &b)
	{
		return vbool(vcgtq_s32(a.m_value, b.m_value));
	}
	inline vbool operator<=(const vint &a, const vint &b)
	{
		return vbool(vcleq_s32(a.m_value, b.m_value));
	}
	inline vbool operator>=(const vint &a, const vint &b)
	{
		return vbool(vcgeq_s32(a.m_value, b.m_value));
	}
	inline vbool operator&&(const vbool &a, const vbool &b)
	{
		return vbool(vandq_u32(a.m_value, b.m_value));
	}

	inline vfloat operator+(const vfloat &a, const vfloat &b)
	{
		return vfloat(vaddq_f32(a.m_value, b.m_value));
	}
	inline vfloat operator-(const vfloat &a, const vfloat &b)
	{
		return vfloat(vsubq_f32(a.m_value, b.m_value));
	}
	inline vfloat operator*(const vfloat &a, const vfloat &b)
	{
		return vfloat(vmulq_f32(a.m_value, b.m_value));
	}
	inline vfloat operator/(const vfloat &a, const vfloat &b)
	{
		return vfloat(vdivq_f32(a.m_value, b.m_value));
	}
	inline vfloat min(const vfloat &a, const vfloat &b)
	{
		return vfloat(vminq_f32(a.m_value, b.m_value));
	}
	inline vfloat max(const vfloat &a, const vfloat &b)
	{
		return vfloat(vmaxq_f32(a.m_value, b.m_value));
	}
	inline vfloat abs(const vfloat &a)
	{
		return vfloat(vabsq_f32(a.m_value));
	}
	inline vfloat round_nearest(const vfloat &a)
	{
		return vfloat(vrndnq_f32(a.m_value));
	}
	inline vbool operator>(const vfloat &a, const vfloat &b)
	{
		return vbool(vcgtq_f32(a.m_value, b.m_value));
	}

	inline vint spmd_ternaryi(const vbool &condition, const vint &a, const vint &b)
	{
		return vint(vbslq_s32(condition.m_value, a.m_value, b.m_value));
	}
	inline vfloat spmd_ternaryf(const vbool &condition, const vfloat &a, const vfloat &b)
	{
		return vfloat(vbslq_f32(condition.m_value, a.m_value, b.m_value));
	}
	inline vint load_all(const gather_int &source)
	{
		int indices[4];
		vst1q_s32(indices, source.indices);
		int values[4] = {source.values[indices[0]], source.values[indices[1]], source.values[indices[2]], source.values[indices[3]]};
		return vint(vld1q_s32(values));
	}
	inline void storeu_linear_all(int *destination, const vint &value)
	{
		vst1q_s32(destination, value.m_value);
	}
	inline vint simd_set4(int a, int b, int c, int d)
	{
		int values[4] = {a, b, c, d};
		return vint(vld1q_s32(values));
	}
	struct spmd_kernel
	{
		uint32x4_t active_mask;
		spmd_kernel()
			: active_mask(vdupq_n_u32(~0U))
		{
		}
		void store(vint &destination, const vint &value)
		{
			destination.m_value = vbslq_s32(active_mask, value.m_value, destination.m_value);
		}
	};
	template <typename Kernel, typename... Args>
	static void spmd_call(const Args &...args)
	{
		Kernel kernel;
		kernel._call(args...);
	}
} // namespace

// Freeze each branch's lane mask, including nested branches. Assignments in a
// branch must never change which lanes execute its corresponding else arm.
#define SPMD_SIF(condition)                                 \
	{                                                       \
		const uint32x4_t parent_mask = active_mask;         \
		const uint32x4_t branch_mask = (condition).m_value; \
		active_mask = vandq_u32(parent_mask, branch_mask);  \
		if (vmaxvq_u32(active_mask))
#define SPMD_SELSE(condition)                          \
	active_mask = vbicq_u32(parent_mask, branch_mask); \
	if (vmaxvq_u32(active_mask))
#define SPMD_SENDIF            \
	active_mask = parent_mask; \
	}
#define CPPSPMD_DECL(type, declaration) type declaration
#define VINT_SHIFT_LEFT(value, shift) ((value) << (shift))
#define VINT_SHIFT_RIGHT(value, shift) vint(vshlq_s32((value).m_value, vdupq_n_s32(-(shift))))
#define VUINT_SHIFT_RIGHT(value, shift) shift_right_unsigned(value, shift)
#include "basisu_bc15_spmd_kernels.inl"
#endif
