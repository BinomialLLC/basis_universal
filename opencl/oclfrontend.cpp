// oclhost.cpp
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
//
#include "oclhost.h"
#include "oclhost_internal.h"

namespace basisu
{

#define STRINGIFY2(x) #x
#define STRINGIFY(x) STRINGIFY2(x)
	bool oclfrontend::init(oclhostprivate& ctx, std::string commonOptions /*= ""*/) {
		if (ctx.frontendSrc.empty()) {  // Should only need to load the source once.
			ctx.frontendSrc = oclhost::getSourceFor("opencl/frontend.c");
			if (ctx.frontendSrc.empty()) {  // if oclhost::getSourceFor() failed, exit.
				return false;
			}
		}

		prog.code = ctx.frontendSrc;
		prog.options = " -DETC1_QUALITY=";
		prog.funcName = "main";
		if (ctx.params.m_compression_level == BASISU_MAX_COMPRESSION_LEVEL) {
			prog.options += STRINGIFY(ETC1_Q_UBER);
			fprintf(stderr, "BASISU_MAX_COMPRESSION_LEVEL: OpenCL not implemented\n");
			return false;
		} else if (ctx.params.m_compression_level == 0) {
			prog.options += STRINGIFY(ETC1_Q_FAST);
		} else {
			prog.options += STRINGIFY(ETC1_Q_SLOW);
		}
		if (!ctx.params.m_perceptual) {
			prog.options += " -DETC1_NO_PERCEPTUAL=1";
		}
		if (ctx.use_color4) {
			prog.options += " -DETC1_USE_COLOR4=1";
		}
		if (!prog.open(commonOptions)) {
			fprintf(stderr, "oclfrontend.open: prog.open(\"%s\") failed\n", prog.funcName.c_str());
			return false;
		}
		return true;
	}

	bool oclfrontend::run(oclhostprivate& ctx, oclqueue& q, size_t stateSize, const std::vector<pixel_block>& pixel_blocks) {
		if (ctx.gConst.empty()) {
			ctx.gConst.emplace_back();
			memset(&ctx.gConst[0], 0, sizeof(ctx.gConst[0]));
			memcpy(&ctx.gConst[0].g_etc1_inten_tables[0][0], &g_etc1_inten_tables[0][0], sizeof(ctx.gConst[0].g_etc1_inten_tables));
			if (CLUSTER_FIT_ORDER_TABLE_SIZE != BASISU_ETC1_CLUSTER_FIT_ORDER_TABLE_SIZE) {
				fprintf(stderr, "BASISU_ETC1_CLUSTER_FIT_ORDER_TABLE_SIZE = %d (error in oclfrontend_api.h)\n",
						(int)BASISU_ETC1_CLUSTER_FIT_ORDER_TABLE_SIZE);
				return false;
			}
			for (size_t i = 0; i < BASISU_ETC1_CLUSTER_FIT_ORDER_TABLE_SIZE; i++) {
				cl_uint v = 0;
				for (size_t q = 0; q < 4; q++) {
					v |= g_cluster_fit_order_tab[i].m_v[q] << (8*q);
				}
				ctx.gConst[0].g_cluster_fit_order_tab[i] = v;
			}
		}
		if (!constant.createInput(q, ctx.gConst)) {
			fprintf(stderr, "constant.createInput failed\n");
			return false;
		}
		if (!q.writeBuffer(constant.getHandle(), ctx.gConst)) {
			fprintf(stderr, "writeBuffer(constant) failed\n");
			return false;
		}

		state.resize(stateSize);
		result.resize(stateSize);
		std::vector<ocl::etc1_optimizer> onestate;
		onestate.resize(1);
		if (!gpustate.createIO(q, onestate, stateSize)) {
			fprintf(stderr, "gpuState.createIO failed: stateSize=%zu\n", stateSize);
			return false;
		}
		if (!prog.setArg(0, constant) || !prog.setArg(1, gpustate)) {
			fprintf(stderr, "prog.setArg failed\n");
			return false;
		}

		ocl::etc1_optimizer_params init_params;
		memset(&init_params, 0, sizeof(init_params));

		for (size_t i = 0; i < state.size(); i++) {
			auto* pix = pixel_blocks.at(i).get_ptr();

			memcpy(&state.at(i).params, &init_params, sizeof(state[0].params));
			memcpy(&state.at(i).pixels, pix, sizeof(pix[0]) * ETC1_OPTIMIZER_NUM_SRC_PIXELS);
		}

		if (!q.writeBuffer(gpustate.getHandle(), state)) {
			fprintf(stderr, "writeBuffer(gpustate) failed\n");
			return false;
		}

		std::vector<size_t> global_work_size{ stateSize };
		size_t* local_size = NULL;  // OpenCL does better at choosing local_size.
		if (!q.NDRangeKernel(prog, global_work_size.size(), NULL,  global_work_size.data(), local_size)) {
			fprintf(stderr, "NDRangeKernel failed\n");
			return false;
		}
		if (!gpustate.copyTo(q, result, completeEvent)) {
			fprintf(stderr, "gpustate.copyTo or finish failed\n");
			return false;
		}
		return true;
	}

	float oclfrontend::submitTime() {
		cl_ulong submitT, endT;
		if (completeEvent.getSubmitTime(submitT)) {
			fprintf(stderr, "submitTime: getSubmitTime failed\n");
			return 0;
		}
		if (completeEvent.getEndTime(endT)) {
			fprintf(stderr, "submitTime: getEndTime failed\n");
			return 0;
		}
		return float(endT - submitT) * 1e-9;
	}

	float oclfrontend::execTime() {
		cl_ulong startT, endT;
		if (completeEvent.getStartTime(startT)) {
			fprintf(stderr, "submitTime: getStartTime failed\n");
			return 0;
		}
		if (completeEvent.getEndTime(endT)) {
			fprintf(stderr, "submitTime: getEndTime failed\n");
			return 0;
		}
		return float(endT - startT) * 1e-9;
	}

} // namespace basisu
