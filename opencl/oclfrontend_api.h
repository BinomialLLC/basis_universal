// oclfrontend_api.h
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

#define ETC1_Q_FAST (0x0000)
#define ETC1_Q_MED  (0x0001)
#define ETC1_Q_SLOW (0x0002)
#define ETC1_Q_UBER (0x0003)

#define cPixelBlockWidth (4)
#define cPixelBlockHeight (4)
#define cPixelBlockTotalPixels (cPixelBlockWidth * cPixelBlockHeight)

#define cETC1SelectorBits (2)
#define cETC1SelectorValues (1U << cETC1SelectorBits)
#define cETC1SelectorMask (1 - cETC1SelectorValues)

#define cETC1IntenModifierNumBits (3)
#define cETC1IntenModifierValues (1U << cETC1IntenModifierNumBits)
#define cETC1RightIntenModifierTableBitOffset (34)
#define cETC1LeftIntenModifierTableBitOffset (37)

#define CLUSTER_FIT_ORDER_TABLE_SIZE (165)

typedef struct opencl_const {
	cl_int g_etc1_inten_tables[cETC1IntenModifierValues][cETC1SelectorValues];
	cl_uint g_cluster_fit_order_tab[CLUSTER_FIT_ORDER_TABLE_SIZE];
} opencl_const;

typedef struct etc1_pack_params {
	cl_float flip_bias;
} etc1_pack_params;

#define ETC1_OPTIMIZER_NUM_SRC_PIXELS (cPixelBlockTotalPixels)
typedef struct etc1_optimizer_params {
	etc1_pack_params pack;
	cl_uint flags;
} etc1_optimizer_params;

#define ETC1_SOLUTION_INTEN_TABLE_MASK (0x00ff)
typedef struct etc1_solution_coordinates {
	cl_int4 unscaled_color;
	cl_uint flags;
} etc1_solution_coordinates;

typedef struct etc1_optimizer_solution {
	etc1_solution_coordinates coords;
	cl_uchar selectors[ETC1_OPTIMIZER_NUM_SRC_PIXELS];
	cl_ulong error;
	cl_uint is_valid;
	cl_uint q;
} etc1_optimizer_solution;

typedef struct etc1_optimizer {
	cl_uint pixels[ETC1_OPTIMIZER_NUM_SRC_PIXELS];
	cl_uint luma[ETC1_OPTIMIZER_NUM_SRC_PIXELS];
	cl_uint sorted_luma_indices[ETC1_OPTIMIZER_NUM_SRC_PIXELS];
	cl_uint sorted_luma[ETC1_OPTIMIZER_NUM_SRC_PIXELS];
	etc1_optimizer_solution best;
	etc1_optimizer_params params;
} etc1_optimizer;
