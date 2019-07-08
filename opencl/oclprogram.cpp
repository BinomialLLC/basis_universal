// oclprogram.cpp
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

	bool oclmemory::create(cl_mem_flags flags, size_t size) {
		if (handle) {
			fprintf(stderr, "validation: oclmemory::create called twice\n");
			return false;
		}
		cl_int v;
		handle = clCreateBuffer(dev.getContext(), flags, size, NULL, &v);
		if (v != CL_SUCCESS) {
			fprintf(stderr, "%s failed: %d %s\n", "clCreateBuffer", v, clerrstr(v));
			return false;
		}
		return true;
	}

	static void printBuildLog(char* log) {
		if (!log) {
			return;
		}
		if (strspn(log, "\r\n") != strlen(log)) {
			fprintf(stderr, "%s", log);
			if (strlen(log) && log[strlen(log) - 1] != '\n') {
				fprintf(stderr, "\n");
			}
		}
		free(log);
		return;
	}

	bool oclprogram::open(std::string commonOptions /*= ""*/) {
		if (prog) {
			fprintf(stderr, "validation: oclprogram::open called twice\n");
			return false;
		}
		const char* codePtr = code.c_str();
		cl_int v;
		prog = clCreateProgramWithSource(dev.getContext(), 1, &codePtr, NULL, &v);
		if (v != CL_SUCCESS) {
			fprintf(stderr, "%s failed: %d %s\n", "clCreateProgramWithSource", v,
					clerrstr(v));
			return false;
		}

		commonOptions += options;
		v = clBuildProgram(prog, 1, &dev.devId, commonOptions.c_str(), NULL, NULL);
		if (v != CL_SUCCESS) {
			fprintf(stderr, "%s failed: %d %s\n", "clBuildProgram", v, clerrstr(v));
			printBuildLog(getBuildLog());
			return false;
		}
		printBuildLog(getBuildLog());

		kern = clCreateKernel(prog, funcName.c_str(), &v);
		if (v != CL_SUCCESS) {
			fprintf(stderr, "%s failed: %d %s\n", "clCreateKernel", v, clerrstr(v));
			return false;
		}
		return true;
	}

	void* oclprogram::getProgramBuildInfo(cl_program_build_info field) {
		size_t len = 0;
		cl_int v = clGetProgramBuildInfo(prog, dev.devId, field, 0, NULL, &len);
		if (v != CL_SUCCESS) {
			fprintf(stderr, "%s failed: %d %s\n", "clGetProgramBuildInfo", v,
						clerrstr(v));
			return NULL;
		}
		void* mem = malloc(len);
		if (!mem) {
			fprintf(stderr, "%s malloc failed\n", "clGetProgramBuildInfo");
			return NULL;
		}
		v = clGetProgramBuildInfo(prog, dev.devId, field, len, mem, NULL);
		if (v != CL_SUCCESS) {
			fprintf(stderr, "%s failed: %d %s\n", "clGetProgramBuildInfo", v,
						clerrstr(v));
			free(mem);
			return NULL;
		}
		return mem;
	}

}  // namespace basisu
