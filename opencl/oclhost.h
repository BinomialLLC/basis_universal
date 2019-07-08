// oclhost.h
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
#pragma once
#include "basisu_frontend.h"

// OpenCL header is defined differently on different platforms
#ifdef __APPLE__
#include "OpenCL/opencl.h"
#else
#include "CL/cl.h"
#endif

namespace basisu
{
	// Forward declarations:
	class oclfrontend;
	class oclqueue;
	class oclmemory;
	struct oclhostprivate;

	// ocldevice is used internally by oclhost.
	class ocldevice {
	public:
		ocldevice(cl_platform_id platId, cl_device_id devId) : platId(platId), devId(devId), ctx(NULL) {}
		~ocldevice();

		// init populates score and DevInfo info.
		bool init();

		void dump(FILE* out);
		void unloadPlatformCompiler();

		// openCtx is a wrapper for clCreateContext.
		bool openCtx(const cl_context_properties* props);
		cl_context getContext() const { return ctx; }

		const cl_platform_id platId;
		const cl_device_id devId;

		// score is to allow oclhost to automatically select the "best" device.
		// TODO: use of all devices in the system is not implemented yet.
		float score;

		struct DevInfo {
			cl_bool avail;
			cl_ulong globalMemSize;
			cl_ulong localMemSize;
			cl_uint maxCU;
			size_t maxWG;
			cl_uint maxWI;
			std::string name;
			std::string vendor;
			std::string openclver;
			std::string driver;
		} info;

		std::shared_ptr<oclfrontend> frontend;

	private:
		cl_context ctx;

		static void oclErrorCb(const char *errMsg,
				/* binary and binary_size are implementation-specific data */
				const void */*binary*/, size_t /*binary_size*/, void *user_data);
	};

	// class oclhost wraps OpenCL init.
	// It also exposes functions to run OpenCL kernels and retrieve the results.
	class oclhost {
		BASISU_NO_EQUALS_OR_COPY_CONSTRUCT(oclhost);
	public:
		oclhost() {}
		~oclhost();

		// init selects OpenCL device(s) to use.
		bool init(const basisu_frontend::params& params, bool use_color4);

		// runFrontend runs basisu_frontend on OpenCL device(s).
		bool runFrontend(const std::vector<pixel_block>& pixel_blocks);

		void check_results(const std::vector<etc_block>& cpu_results);

		static std::string getSourceFor(const char* filename);

	private:
		std::vector<std::shared_ptr<ocldevice>> devs;
		bool initDevs();
		oclhostprivate* internal{nullptr};
	};

} // namespace basisu
