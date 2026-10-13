// basisu_bc15_spmd_sse.cpp -- SSE4.1 ISA translation unit for the standalone BC1 encoder.
//
// Mirrors basisu_kernels_sse.cpp's pattern: select the ISA, include the cppspmd framework, then pull in the
// kernel .inl ("do not directly include"). Multiple TUs may include the framework because cppspmd_math.h's
// print_* helpers are inline.

#include "basisu_enc.h" // basisu::color_rgba + BASISU_SUPPORT_SSE

#if BASISU_SUPPORT_SSE

#define CPPSPMD_SSE2 0
#ifdef _MSC_VER
#include <intrin.h>
#endif
#include "cppspmd_sse.h"
#include "cppspmd_type_aliases.h"

using namespace CPPSPMD;

namespace
{
	inline vint simd_set4(int a, int b, int c, int d)
	{
		return vint(_mm_setr_epi32(a, b, c, d));
	}
} // namespace

#include "basisu_bc15_spmd_kernels.inl"

#endif // BASISU_SUPPORT_SSE
