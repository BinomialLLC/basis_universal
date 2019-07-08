// oclhost_internal.h
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

namespace ocl
{
#include "oclfrontend_api.h"
} // namespace ocl

namespace basisu
{
	const char* clerrstr(cl_int v);
	bool getDeviceInfoAsBuffer(cl_device_id devId, cl_device_info field, void** buf, size_t* len);

	template<typename T>
	inline bool getDeviceInfo(cl_device_id devId, cl_device_info field, T& out) {
		void* r;
		size_t len;
		if (!getDeviceInfoAsBuffer(devId, field, &r, &len)) {
			return false;
		}
		if (len != sizeof(out)) {
			fprintf(stderr, "getDeviceInfo(%d): size %zu, want %zu\n", (int)field,
					len, sizeof(out));
			return false;
		}
		memcpy(&out, r, sizeof(out));
		free(r);
		return true;
	}

	template<>
	inline bool getDeviceInfo(cl_device_id devId, cl_device_info field, std::string& out) {
		void* r;
		size_t len;
		if (!getDeviceInfoAsBuffer(devId, field, &r, &len)) {
			return false;
		}
		out.assign(reinterpret_cast<const char*>(r), len);
		free(r);
		return true;
	}

	class oclprogram {
	public:
		oclprogram(ocldevice& dev) : dev(dev), prog(NULL), kern(NULL) {}
		~oclprogram() {
			if (kern) {
				clReleaseKernel(kern);
				kern = NULL;
			}
			if (prog) {
				clReleaseProgram(prog);
				prog = NULL;
			}
		}

		// open should only be called after code, options and funcName are set.
		// Allows a cleaner separation between device code (which just needs an
		// oclprogrma) and implementation code (which has the particulars on how to
		// set up memory buffers, what the options are, etc.)
		//
		// commonOptions can be passed in to specify options for all oclprograms.
		bool open(std::string commonOptions = "");

		ocldevice& dev;
		std::string code;
		std::string options;
		std::string funcName;

		char* getBuildLog() {
			return reinterpret_cast<char*>(getProgramBuildInfo(CL_PROGRAM_BUILD_LOG));
		}

		template<typename T>
		bool setArg(cl_uint argIndex, const T& arg) {
			if (!kern) {
				fprintf(stderr, "setArg(%u) before open\n", argIndex);
				return false;
			}
			cl_int v = clSetKernelArg(kern, argIndex, sizeof(arg), reinterpret_cast<const void*>(&arg));
			if (v != CL_SUCCESS) {
				fprintf(stderr, "%s(%u) failed: %d %s\n", "clSetKernelArg", argIndex, v,
						clerrstr(v));
				return false;
			}
			return true;
		}

		cl_kernel getKern() const { return kern; }
		bool copyFrom(oclprogram& other, const char* mainFuncName) {
			prog = other.prog;
			cl_int v;
			kern = clCreateKernel(prog, mainFuncName, &v);
			if (v != CL_SUCCESS) {
				fprintf(stderr, "%s failed: %d %s\n", "clCreateKernel", v, clerrstr(v));
				return false;
			}
			return true;
		}

	private:
		void* getProgramBuildInfo(cl_program_build_info field);

		cl_program prog;
		cl_kernel kern;
	};

	class oclevent {
	public:
		oclevent() : handle(NULL) {}
		~oclevent() {
			clReleaseEvent(handle);
		}

		void waitForSignal() {
			clWaitForEvents(1, &handle);
		}

		bool getQueuedTime(cl_ulong& t) {
			return getProfilingInfo(CL_PROFILING_COMMAND_QUEUED, t);
		}

		bool getSubmitTime(cl_ulong& t) {
			return getProfilingInfo(CL_PROFILING_COMMAND_SUBMIT, t);
		}

		bool getStartTime(cl_ulong& t) {
			return getProfilingInfo(CL_PROFILING_COMMAND_START, t);
		}

		bool getEndTime(cl_ulong& t) {
			return getProfilingInfo(CL_PROFILING_COMMAND_END, t);
		}

		bool getProfilingInfo(cl_profiling_info param, cl_ulong& out) {
			if (!handle) {
				fprintf(stderr, "Event must first be passed to an Enqueue... function\n");
				return false;
			}
			size_t size_ret = 0;
			cl_int v = clGetEventProfilingInfo(handle, param, sizeof(out), &out, &size_ret);
			if (v != CL_PROFILING_INFO_NOT_AVAILABLE && size_ret != sizeof(out)) {
				fprintf(stderr, "%s: told to provide %zu bytes, want %zu bytes\n",
						"oclevent::getProfilingInfo", size_ret, sizeof(out));
			}
			if (v != CL_SUCCESS) {
				fprintf(stderr, "%s failed: %d %s\n", "oclevent::getProfilingInfo", v, clerrstr(v));
				return false;
			}
			return true;
		}

		cl_event handle;
	};

	class oclqueue {
	public:
		oclqueue(ocldevice& dev) : dev(dev), handle(NULL) {}

		~oclqueue() {
			if (handle) {
				clReleaseCommandQueue(handle);
				handle = NULL;
			}
		}

		bool open(std::vector<cl_queue_properties> props = std::vector<cl_queue_properties>{
					CL_QUEUE_PROPERTIES, CL_QUEUE_PROFILING_ENABLE,
				}) {
			if (handle) {
				fprintf(stderr, "validation: oclqueue::open called twice\n");
				return false;
			}
			cl_queue_properties* pprops = NULL;
			if (props.size()) {
				// Make sure props includes the required terminating 0.
				props.push_back(0);
				pprops = props.data();
			}
			cl_int v;
			handle = clCreateCommandQueueWithProperties(
				dev.getContext(), dev.devId, pprops, &v);
			if (v != CL_SUCCESS) {
				fprintf(stderr, "%s failed: %d %s\n", "clCreateCommandQueue", v,
						clerrstr(v));
				return false;
			}
			return true;
		}

		// writeBuffer does a non-blocking write
		template<typename T>
		bool writeBuffer(cl_mem hnd, const std::vector<T>& src) {
			cl_int v = clEnqueueWriteBuffer(handle, hnd, CL_FALSE /*blocking*/, 0 /*offset*/,
				sizeof(src[0]) * src.size(), reinterpret_cast<const void*>(src.data()), 0, NULL, NULL);
			if (v != CL_SUCCESS) {
				fprintf(stderr, "%s failed: %d %s\n", "clEnqueueWriteBuffer", v,
						clerrstr(v));
				return false;
			}
			return true;
		}

		// writeBuffer does a non-blocking write and outputs the cl_event that
		// will be signalled when it completes.
		template<typename T>
		bool writeBuffer(cl_mem hnd, const std::vector<T>& src, cl_event& complete) {
			cl_int v = clEnqueueWriteBuffer(handle, hnd, CL_FALSE /*blocking*/, 0 /*offset*/,
				sizeof(src[0]) * src.size(), reinterpret_cast<const void*>(src.data()), 0, NULL, &complete);
			if (v != CL_SUCCESS) {
				fprintf(stderr, "%s failed: %d %s\n", "clEnqueueWriteBuffer", v, clerrstr(v));
				return false;
			}
			return true;
		}

		// readBuffer does a blocking read
		template<typename T>
		bool readBuffer(cl_mem hnd, std::vector<T>& dst) {
			cl_int v = clEnqueueReadBuffer(handle, hnd, CL_TRUE /*blocking*/, 0 /*offset*/,
				sizeof(dst[0]) * dst.size(), reinterpret_cast<void*>(dst.data()), 0, NULL, NULL);
			if (v != CL_SUCCESS) {
				fprintf(stderr, "%s failed: %d %s\n", "clEnqueueReadBuffer", v, clerrstr(v));
				return false;
			}
			return true;
		}

		// readBufferNonBlock does a non-blocking read
		template<typename T>
		bool readBufferNonBlock(cl_mem hnd, std::vector<T>& dst, cl_event& complete) {
			cl_int v = clEnqueueReadBuffer(handle, hnd, CL_FALSE /*blocking*/, 0 /*offset*/,
				sizeof(dst[0]) * dst.size(), reinterpret_cast<void*>(dst.data()), 0, NULL, &complete);
			if (v != CL_SUCCESS) {
				fprintf(stderr, "%s failed: %d %s\n", "clEnqueueReadBuffer", v, clerrstr(v));
				return false;
			}
			return true;
		}

		ocldevice& dev;

		bool NDRangeKernel(oclprogram& prog, cl_uint work_dim,
								const size_t* global_work_offset,
								const size_t* global_work_size,
								const size_t* local_work_size,
								cl_event* completeEvent = NULL,
								const std::vector<cl_event>& waitList
										= std::vector<cl_event>()) {
			cl_int v = clEnqueueNDRangeKernel(handle, prog.getKern(), work_dim,
														global_work_offset,
														global_work_size, local_work_size,
														waitList.size(), waitList.data(),
														completeEvent);
			if (v != CL_SUCCESS) {
				fprintf(stderr, "%s failed: %d %s\n", "clEnqueueNDRangeKernel", v, clerrstr(v));
				return false;
			}
			return true;
		}

		bool NDRangeKernel(oclprogram& prog, cl_uint work_dim,
								const size_t* global_work_offset,
								const size_t* global_work_size,
								const size_t* local_work_size,
								oclevent& completeEvent,
								const std::vector<cl_event>& waitList
										= std::vector<cl_event>()) {
			return NDRangeKernel(prog, work_dim, global_work_offset, global_work_size,
										local_work_size, &completeEvent.handle, waitList);
		}

		bool finish() {
			cl_int v = clFinish(handle);
			if (v != CL_SUCCESS) {
				fprintf(stderr, "%s failed: %d %s\n", "clFinish", v, clerrstr(v));
				return false;
			}
			return true;
		}

		private:
		cl_command_queue handle;
	};

	class oclmemory {
	public:
		oclmemory(ocldevice& dev) : dev(dev), handle(NULL) {}
		virtual ~oclmemory() {
			if (handle) {
				clReleaseMemObject(handle);
				handle = NULL;
			}
		}
		ocldevice& dev;

		bool create(cl_mem_flags flags, size_t size);

		template<typename T>
		bool createInput(oclqueue& q, std::vector<T>& in, size_t copies = 1) {
			if (!create(CL_MEM_READ_ONLY, sizeof(in[0]) * in.size() * copies)) {
				fprintf(stderr, "createInput failed\n");
				return false;
			}
			if (copies == 1) {
				if (!q.writeBuffer(getHandle(), in)) {
					fprintf(stderr, "createInput: writeBuffer failed\n");
					return false;
				}
			}
			return true;
		}

		template<typename T>
		bool createIO(oclqueue& q, std::vector<T>& in, size_t copies = 1) {
			if (!create(CL_MEM_READ_WRITE, sizeof(in[0]) * in.size() * copies)) {
				fprintf(stderr, "createIO failed\n");
				return false;
			}
			if (copies == 1) {
				if (!q.writeBuffer(getHandle(), in)) {
					fprintf(stderr, "createIO: writeBuffer failed\n");
					return false;
				}
			}
			return true;
		}

		template<typename T>
		bool createOutput(std::vector<T>& out) {
			// The oclqueue::readBuffer() call is done using copyTo, below.
			return create(CL_MEM_WRITE_ONLY, sizeof(out[0]) * out.size());
		}

		template<typename T>
		bool copyTo(oclqueue& q, std::vector<T>& out) {
			return q.readBuffer(getHandle(), out);
		}

		template<typename T>
		bool copyTo(oclqueue& q, std::vector<T>& out, oclevent& completeEvent) {
			return q.readBufferNonBlock(getHandle(), out, completeEvent.handle);
		}

		cl_mem getHandle() const { return handle; }

		private:
		cl_mem handle;
	};

	template<>
	inline bool oclprogram::setArg(cl_uint argIndex, const oclmemory& mem) {
		return setArg(argIndex, mem.getHandle());
	};

	struct oclhostprivate {
		// Inputs to frontend
		basisu_frontend::params params;
		bool use_color4;
		std::string frontendSrc;
		std::vector<ocl::opencl_const> gConst;

		// Combined output from all devices after frontend
		std::vector<ocl::etc1_optimizer_solution> results;
	};

	class oclfrontend {
	public:
		oclfrontend(ocldevice& dev) : prog(dev), constant(dev), gpustate(dev) {}
		oclprogram prog;
		oclmemory constant;
		oclmemory gpustate;
		oclevent completeEvent;
		std::vector<ocl::etc1_optimizer> state;
		std::vector<ocl::etc1_optimizer> result;

		bool init(oclhostprivate& ctx, std::string commonOptions = "");

		bool run(oclhostprivate& ctx, oclqueue& q, size_t stateSize, const std::vector<pixel_block>& pixel_blocks);

		float submitTime();
		float execTime();
	};

} // namespace basisu
