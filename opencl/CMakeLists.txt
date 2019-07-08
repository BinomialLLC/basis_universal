cmake_minimum_required(VERSION 3.0)
include(ExternalData)

set(BASISU_OPENCL_SRC_LIST
	oclhost.cpp
	ocldevice.cpp
	oclfrontend.cpp
	oclprogram.cpp
	oclhost.h
	oclhost_internal.h
	oclfrontend_api.h
	)

add_library(basisu_opencl ${BASISU_OPENCL_SRC_LIST})
target_link_libraries(basisu_opencl OpenCL)
target_include_directories(basisu_opencl PRIVATE ..)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
	message(WARNING "TODO: build frontend.c.i into the binary")
endif()
