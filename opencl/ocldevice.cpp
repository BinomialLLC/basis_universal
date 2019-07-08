// ocldevice.cpp
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
	const char* clerrstr(cl_int v) {
		if (v == -1001) {
			// TODO: check this - AMD bundles -lOpenCL if memory serves.
			return "-1001: try apt-get install nvidia-opencl-dev";
		}
		switch (v) {
		#define stringify(d) #d
		#define case_to_string(d) case d: return #d
			case_to_string(CL_DEVICE_NOT_FOUND);
			case_to_string(CL_DEVICE_NOT_AVAILABLE);
			case_to_string(CL_COMPILER_NOT_AVAILABLE);
			case_to_string(CL_MEM_OBJECT_ALLOCATION_FAILURE);
			case_to_string(CL_OUT_OF_RESOURCES);
			case_to_string(CL_OUT_OF_HOST_MEMORY);
			case_to_string(CL_PROFILING_INFO_NOT_AVAILABLE);
			case_to_string(CL_MEM_COPY_OVERLAP);
			case_to_string(CL_IMAGE_FORMAT_MISMATCH);
			case_to_string(CL_IMAGE_FORMAT_NOT_SUPPORTED);
			case_to_string(CL_BUILD_PROGRAM_FAILURE);
			case_to_string(CL_MAP_FAILURE);
			case_to_string(CL_MISALIGNED_SUB_BUFFER_OFFSET);
			case_to_string(CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST);
			case_to_string(CL_COMPILE_PROGRAM_FAILURE);
			case_to_string(CL_LINKER_NOT_AVAILABLE);
			case_to_string(CL_LINK_PROGRAM_FAILURE);
			case_to_string(CL_DEVICE_PARTITION_FAILED);
			case_to_string(CL_KERNEL_ARG_INFO_NOT_AVAILABLE);

			case_to_string(CL_INVALID_VALUE);
			case_to_string(CL_INVALID_DEVICE_TYPE);
			case_to_string(CL_INVALID_PLATFORM);
			case_to_string(CL_INVALID_DEVICE);
			case_to_string(CL_INVALID_CONTEXT);
			case_to_string(CL_INVALID_QUEUE_PROPERTIES);
			case_to_string(CL_INVALID_COMMAND_QUEUE);
			case_to_string(CL_INVALID_HOST_PTR);
			case_to_string(CL_INVALID_MEM_OBJECT);
			case_to_string(CL_INVALID_IMAGE_FORMAT_DESCRIPTOR);
			case_to_string(CL_INVALID_IMAGE_SIZE);
			case_to_string(CL_INVALID_SAMPLER);
			case_to_string(CL_INVALID_BINARY);
			case_to_string(CL_INVALID_BUILD_OPTIONS);
			case_to_string(CL_INVALID_PROGRAM);
			case_to_string(CL_INVALID_PROGRAM_EXECUTABLE);
			case_to_string(CL_INVALID_KERNEL_NAME);
			case_to_string(CL_INVALID_KERNEL_DEFINITION);
			case_to_string(CL_INVALID_KERNEL);
			case_to_string(CL_INVALID_ARG_INDEX);
			case_to_string(CL_INVALID_ARG_VALUE);
			case_to_string(CL_INVALID_ARG_SIZE);
			case_to_string(CL_INVALID_KERNEL_ARGS);
			case_to_string(CL_INVALID_WORK_DIMENSION);
			case_to_string(CL_INVALID_WORK_GROUP_SIZE);
			case_to_string(CL_INVALID_WORK_ITEM_SIZE);
			case_to_string(CL_INVALID_GLOBAL_OFFSET);
			case_to_string(CL_INVALID_EVENT_WAIT_LIST);
			case_to_string(CL_INVALID_EVENT);
			case_to_string(CL_INVALID_OPERATION);
			case_to_string(CL_INVALID_GL_OBJECT);
			case_to_string(CL_INVALID_BUFFER_SIZE);
			case_to_string(CL_INVALID_MIP_LEVEL);
			case_to_string(CL_INVALID_GLOBAL_WORK_SIZE);
			case_to_string(CL_INVALID_PROPERTY);
			case_to_string(CL_INVALID_IMAGE_DESCRIPTOR);
			case_to_string(CL_INVALID_COMPILER_OPTIONS);
			case_to_string(CL_INVALID_LINKER_OPTIONS);
			case_to_string(CL_INVALID_DEVICE_PARTITION_COUNT);
			case_to_string(CL_INVALID_PIPE_SIZE);
			case_to_string(CL_INVALID_DEVICE_QUEUE);

		#undef case_to_string
			default: return "(unknown)";
		}
	}

	bool getDeviceInfoAsBuffer(cl_device_id devId, cl_device_info field, void** buf, size_t* len) {
		size_t llen = 0;
		cl_int v = clGetDeviceInfo(devId, field, 0, NULL, &llen);
		if (v != CL_SUCCESS) {
			fprintf(stderr, "%s(%d) failed: %d %s\n", "clGetDeviceInfo", (int)field, v, clerrstr(v));
			return false;
		}
		*buf = malloc(llen);
		if (!*buf) {
			fprintf(stderr, "getDeviceInfoAsBuffer(%d): malloc(%zu) failed\n", (int)field, llen);
			return false;
		}
		v = clGetDeviceInfo(devId, field, llen, *buf, NULL);
		if (v != CL_SUCCESS) {
			fprintf(stderr, "%s(%d) failed: %d %s\n", "clGetDeviceInfo", (int)field, v, clerrstr(v));
			free(*buf);
			*buf = NULL;
			return false;
		}
		*len = llen;
		return true;
	}

	ocldevice::~ocldevice() {
		if (ctx) {
			clReleaseContext(ctx);
			ctx = NULL;
		}
	}

	bool ocldevice::init() {
		if (!getDeviceInfo(devId, CL_DEVICE_AVAILABLE, info.avail)) {
			return false;
		}
		if (!info.avail) {
			fprintf(stderr, "CL_DEVICE_AVAILABLE is false\n");
			return false;
		}
		if (!getDeviceInfo(devId, CL_DEVICE_COMPILER_AVAILABLE, info.avail)) {
			return false;
		}
		if (!info.avail) {
			fprintf(stderr, "CL_DEVICE_COMPILER_AVAILABLE is false\n");
			return false;
		}
		if (!(getDeviceInfo(devId, CL_DEVICE_GLOBAL_MEM_SIZE, info.globalMemSize) &&
				getDeviceInfo(devId, CL_DEVICE_LOCAL_MEM_SIZE, info.localMemSize) &&
				getDeviceInfo(devId, CL_DEVICE_MAX_COMPUTE_UNITS, info.maxCU) &&
				getDeviceInfo(devId, CL_DEVICE_MAX_WORK_GROUP_SIZE, info.maxWG) &&
				getDeviceInfo(devId, CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS, info.maxWI) &&
				getDeviceInfo(devId, CL_DEVICE_NAME, info.name) &&
				getDeviceInfo(devId, CL_DEVICE_VENDOR, info.vendor) &&
				getDeviceInfo(devId, CL_DEVICE_VERSION, info.openclver) &&
				getDeviceInfo(devId, CL_DRIVER_VERSION, info.driver))) {
			return false;
		}

		score = info.globalMemSize / 1048576;
		score *= info.maxCU;
		score *= info.maxWG;
		return true;
	}

	void ocldevice::dump(FILE* out) {
		fprintf(out, "  %s: %6.1fGB / %lluKB. CU=%u WG=%zu (v%s) %s %s\n",
				info.name.c_str(), ((float) info.globalMemSize / 1048576.0f) / 1024.0f,
				(unsigned long long) info.localMemSize / 1024, info.maxCU, info.maxWG,
				info.driver.c_str(), info.vendor.c_str(), info.openclver.c_str());
	}

	void ocldevice::unloadPlatformCompiler() {
		clUnloadPlatformCompiler(platId);
	}

	bool ocldevice::openCtx(const cl_context_properties* props) {
		cl_int v = CL_SUCCESS;
		ctx = clCreateContext(props, 1 /*numDevs*/, &devId, ocldevice::oclErrorCb, this /*user_data*/, &v);
		if (v != CL_SUCCESS) {
			fprintf(stderr, "%s failed: %d %s\n", "clCreateContext", v, clerrstr(v));
			return false;
		}
		return true;
	}

	void ocldevice::oclErrorCb(const char *errMsg,
			/* binary and binary_size are implementation-specific data */
			const void */*binary*/, size_t /*binary_size*/, void *user_data) {
		BASISU_NOTE_UNUSED(user_data);
		fprintf(stderr, "oclErrorCb: \"%s\"\n", errMsg);
	}

} // namespace basisu
