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
#include "basisu_frontend.h"

namespace basisu
{
	static bool getPlatforms(std::vector<cl_platform_id>& platforms) {
		cl_uint platformIdMax = 0;
		cl_int v = clGetPlatformIDs(0, NULL, &platformIdMax);
		if (v != CL_SUCCESS) {
			fprintf(stderr, "%s failed: %d %s\n", "clGetPlatformIDs", v, clerrstr(v));
			return false;
		}

		platforms.resize(platformIdMax);
		v = clGetPlatformIDs(platformIdMax, platforms.data(), NULL);
		if (v != CL_SUCCESS) {
			fprintf(stderr, "%s failed: %d %s\n", "clGetPlatformIDs", v, clerrstr(v));
			return false;
		}
		return true;
	}

	static bool getDeviceIds(cl_platform_id platform, std::vector<cl_device_id>& devs) {
		cl_uint devMax = 0;
		cl_int v = clGetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, 0, NULL, &devMax);
		if (v != CL_SUCCESS) {
			fprintf(stderr, "%s failed: %d %s\n", "clGetDeviceIDs", v, clerrstr(v));
			return false;
		}
		devs.resize(devMax);
		v = clGetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, devMax, devs.data(), NULL);
		if (v != CL_SUCCESS) {
			fprintf(stderr, "%s failed: %d %s\n", "clGetDeviceIDs", v, clerrstr(v));
			return false;
		}
		return true;
	}

	std::string oclhost::getSourceFor(const char* filename) {
#if 1
		std::string r("#include \"");
		r = r + filename + "\"\n";
		return r;
#else
		FILE* f = fopen(filename, "r");
		if (!f) {
			fprintf(stderr, "Unable to read OpenCL source %s: %d %s\n", filename, errno, strerror(errno));
			return "";
		}
		std::vector<char> r(16*1024);
		size_t used = 0;
		while (!feof(f)) {
			size_t nread = fread(&r[0] + used, 1, r.size() - used, f);
			if (!nread) {
				if (feof(f)) {
					break;
				}
				fclose(f);
				fprintf(stderr, "fread OpenCL source %s failed: %d %s\n", filename, errno, strerror(errno));
				return "";
			}
			used += nread;
			if (used >= r.size() - 1 /*reserve space for a null byte*/) {
				r.resize(r.size() * 2);
			}
		}
		fclose(f);
		r.resize(used + 1);
		r[used] = 0;  // Append a null byte
		return std::string(r.data(), r.size());
#endif
	}

	oclhost::~oclhost() {
		if (internal) {
			delete internal;
			internal = nullptr;
		}
	}

	bool oclhost::init(const basisu_frontend::params& params, bool use_color4) {
		if (!internal) {
			internal = new oclhostprivate;
			if (!internal) {
				fprintf(stderr, "new oclhostprivate failed\n");
				return false;
			}
		}
		memcpy(&internal->params, &params, sizeof(internal->params));
		internal->use_color4 = use_color4;

		std::vector<cl_platform_id> platforms;
		if (!getPlatforms(platforms)) {
			return false;
		}
		if (platforms.size() < 1) {
			fprintf(stderr, "no OpenCL hardware found.\n");
			return false;
		}

		for (size_t i = 0; i < platforms.size(); i++) {
			std::vector<cl_device_id> devIDs;
			if (!getDeviceIds(platforms.at(i), devIDs)) {
				return false;
			}

			float bestScore = 0.0f;
			for (size_t j = 0; j < devIDs.size(); j++) {
				devs.emplace_back(std::make_shared<ocldevice>(platforms.at(i), devIDs.at(j)));
				auto dev = devs.back();
				if (!dev->init()) {
					return false;
				}
				if (j == 0 || dev->score > bestScore) {
					bestScore = dev->score;
					// Any time the best is upped, delete everything else
					devs.clear();
					devs.emplace_back(dev);
				} else if (dev->score < bestScore) {
					// Any device that scores too poorly is dropped.
					devs.pop_back();
				}
			}
		}
		if (devs.size() < 1) {
			fprintf(stderr, "no OpenCL devices selected - BUG?\n");
			return false;
		}
		if (!initDevs()) {
			return false;
		}
		for (auto dev : devs) {
			dev->unloadPlatformCompiler();
		}
		return true;
	}

	bool oclhost::initDevs() {
		fprintf(stderr, "Using these OpenCL devices:\n");
		for (auto dev : devs) {
			dev->dump(stderr);

			// ctxProps is a list terminated with a "0, 0" pair.
			const cl_context_properties ctxProps[] = {
				// Specify the OpenCL platform for the device.
				CL_CONTEXT_PLATFORM,
				reinterpret_cast<cl_context_properties>(dev->platId),
				0, 0,  // 0, 0, to terminate the list.
			};
			if (!dev->openCtx(ctxProps)) {
				return false;
			}
			// Compile, link oclprogram objects.
			const char* mainFuncName = "main";

			// This is controlled differently on AMD: see
			// __attribute__((reqd_work_group_size(64,1,1))) such as in
			// https://community.amd.com/thread/158594
			std::string commonOptions;
			if (dev->info.vendor.find("NVIDIA") != std::string::npos) {
				commonOptions += " -cl-nv-verbose -cl-nv-maxrregcount=128";
			}
			dev->frontend = std::make_shared<oclfrontend>(*dev);
			if (!dev->frontend->init(*internal, commonOptions)) {
				fprintf(stderr, "frontend.open failed\n");
				return false;
			}
		}
		return true;
	}




	// clpool manages in-flight input and output buffers for each kernel invocation.
	// OpenCL devices stay busy when the next kernel is submitted before the last one
	// is finished, so clpool generates work to submit in advance and manages cleanup.
	//
	// TODO: not implemented yet.
	struct clpool {
		clpool(ocldevice& dev, oclhostprivate& ctx)
				: dev(dev), maxCU(dev.info.maxCU), ctx(ctx) {}

		ocldevice& dev;
		oclqueue q{dev};

		bool open(size_t n) {
			if (!q.open()) {
				fprintf(stderr, "clpool: q.open failed\n");
				return false;
			}
			setNumWorkers(n);
			return true;
		}

		// setNumWorkers sets the control parameters to assign work to each worker.
		void setNumWorkers(size_t n) {
			numWorkers = n;
		}

		size_t getNumWorkers() const { return numWorkers; }

	protected:
		const cl_uint maxCU;
		size_t numWorkers;
		oclhostprivate& ctx;
	};

	bool oclhost::runFrontend(const std::vector<pixel_block>& pixel_blocks) {
		if (devs.size() < 1) {
			fprintf(stderr, "runFrontend: no devs\n");
			return false;
		} else if (devs.size() > 1) {
			fprintf(stderr, "WARNING: only using first dev. Multi-dev has not been tested yet.\n");
		}

		std::vector<clpool> pool;
		pool.reserve(devs.size());
		size_t workLeft = pixel_blocks.size();
		for (size_t i = 0; i < devs.size(); i++) {
			pool.emplace_back(*devs.at(i), *internal);
			if (!pool.at(i).open(workLeft)) {
				fprintf(stderr, "pool[%zu].open failed\n", i);
				return false;
			}
			workLeft -= workLeft;
		}

		fprintf(stderr, "\e[32mrunFrontend: start kernels\e[0m\n");
		for (size_t i = 0; i < devs.size(); i++) {
			if (!devs.at(i)->frontend->run(*internal, pool.at(i).q, pool.at(i).getNumWorkers(), pixel_blocks)) {
				fprintf(stderr, "clpool: dev.frontend.run failed\n");
				return false;
			}
			if (1) {
				fprintf(stderr, "cpu: dev[%zu]: %zu blocks\n", i, devs.at(i)->frontend->state.size());
			}
		}
		fprintf(stderr, "runFrontend: wait kernels\n");
		for (size_t i = 0; i < devs.size(); i++) {
			devs.at(i)->frontend->completeEvent.waitForSignal();
		}
		fprintf(stderr, "runFrontend: DONE.\n");
		internal->results.clear();
		for (size_t i = 0; i < devs.size(); i++) {
			for (auto& r : devs.at(i)->frontend->result) {
				internal->results.push_back(r.best);
			}
		}
		return true;
	}

	void oclhost::check_results(const std::vector<etc_block>& cpu_results) {
		int num_err = 0;
		for (size_t i = 0; i < cpu_results.size() && i < internal->results.size(); i++) {
			auto& r = cpu_results.at(i);
			auto rc = r.unpack_color5(r.get_base5_color(), false);

			auto& p = internal->results.at(i);
			cl_uint pc[4];
			memcpy(&pc[0], &p.coords.unscaled_color, sizeof(pc));
			bool match = pc[3] == 0;
			for (uint32_t y = 0; y < 4; y++) {
				for (uint32_t x = 0; x < 4; x++) {
					uint32_t rs = r.get_selector(x, y);
					uint32_t os = p.selectors[x + y * 4];
					match = (rs == os) ? match : false;
				}
			}
			if (!match) {
				num_err++;
				if (num_err > 10) {
					continue;
				}
				fprintf(stderr, "cpu_results[%zu]: inten:%x  #%02x,%02x,%02x sel=", i, r.get_inten_table(0), rc.r, rc.g, rc.b);
				for (uint32_t y = 0; y < 4; y++) {
					for (uint32_t x = 0; x < 4; x++) {
						uint32_t rs = r.get_selector(x, y);
						fprintf(stderr, " %u", rs);
					}
				}
				fprintf(stderr, "\ngpu_results[%zu]: inten:%x  #%02x,%02x,%02x", i,
						p.coords.flags & ETC1_SOLUTION_INTEN_TABLE_MASK, pc[0], pc[1], pc[2]);
				if (pc[3]) {
					fprintf(stderr, " %x", pc[3]);
				}
				fprintf(stderr, " sel=");
				for (uint32_t y = 0; y < 4; y++) {
					for (uint32_t x = 0; x < 4; x++) {
						uint32_t os = p.selectors[x + y * 4];
						fprintf(stderr, " %u", os);
					}
				}
				fprintf(stderr, " q=%d\n", (int)p.q);
			}
			// blk.set_flip_bit(true);
		}
		if (internal->results.size() == 0) {
			fprintf(stderr, "cpu_results not checked - gpu_results empty.\n");
		} else {
			fprintf(stderr, "cpu_results all match gpu_results? %s\n", (num_err == 0) ? "YES" : "\e[31mNO\e[0m");
		}
	}
} // namespace basisu
