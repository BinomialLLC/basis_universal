project(basisu)

cmake_minimum_required(VERSION 3.0)
option(BUILD_X64 "build 64-bit" TRUE)
option(STATIC "static linking" FALSE)
option(SSE "SSE 4.1 support" FALSE)
option(ZSTD "ZSTD support for KTX2 transcoding/encoding" TRUE)

message("Initial BUILD_X64=${BUILD_X64}")
message("Initial CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
message("Initial SSE=${SSE}")
message("Initial ZSTD=${ZSTD}")

if( NOT CMAKE_BUILD_TYPE )
  set( CMAKE_BUILD_TYPE Release )
endif()

message( ${PROJECT_NAME} " build type: " ${CMAKE_BUILD_TYPE} )

if (BUILD_X64)
	message("Building 64-bit")
else()
	message("Building 32-bit")
endif()

if (SSE)
	message("SSE enabled")
else()
	message("SSE disabled")
endif()

if (ZSTD)
	message("Zstandard enabled")
else()
	message("Zstandard disabled")
endif()

if (NOT MSVC)
   set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g")
   set(CMAKE_C_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g")

   set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
   set(CMAKE_C_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")

   set(CMAKE_CXX_FLAGS -std=c++11)
   set(GCC_COMPILE_FLAGS "-fvisibility=hidden -fPIC -fno-strict-aliasing -D_LARGEFILE64_SOURCE=1 -D_FILE_OFFSET_BITS=64 -Wall -Wextra -Wno-unused-local-typedefs -Wno-unused-value -Wno-unused-parameter -Wno-unused-variable")

   if (NOT BUILD_X64)
	  set(GCC_COMPILE_FLAGS "${GCC_COMPILE_FLAGS} -m32")
   endif()

   if (EMSCRIPTEN)
	  set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -s ALLOW_MEMORY_GROWTH=1 -DBASISU_SUPPORT_SSE=0")
	  set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -s ALLOW_MEMORY_GROWTH=1 -DBASISU_SUPPORT_SSE=0")

	  set(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} ${GCC_LINK_FLAGS}")
   elseif (STATIC)
      if (SSE)
		set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -DBASISU_SUPPORT_SSE=1 -msse4.1")
		set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -DBASISU_SUPPORT_SSE=1 -msse4.1")
	  else()
	  	set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -DBASISU_SUPPORT_SSE=0")
		set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -DBASISU_SUPPORT_SSE=0")
	  endif()
	  
	  set(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} ${GCC_LINK_FLAGS} -static-libgcc -static-libstdc++ -static")
   else()
   	  if (SSE)
		set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -DBASISU_SUPPORT_SSE=1 -msse4.1")
		set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -DBASISU_SUPPORT_SSE=1 -msse4.1")
	  else()
	  	set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -DBASISU_SUPPORT_SSE=0")
		set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -DBASISU_SUPPORT_SSE=0")
	  endif()
	  
	  set(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} ${GCC_LINK_FLAGS} -Wl,-rpath .")
   endif()

   set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} ${GCC_COMPILE_FLAGS}")
   set(CMAKE_C_FLAGS_RELEASE  "${CMAKE_C_FLAGS_RELEASE} ${GCC_COMPILE_FLAGS}")
   set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} ${GCC_COMPILE_FLAGS} -D_DEBUG")

   set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} ${GCC_COMPILE_FLAGS}")
   set(CMAKE_CXX_FLAGS_RELEASE  "${CMAKE_CXX_FLAGS_RELEASE} ${GCC_COMPILE_FLAGS}")
   set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${GCC_COMPILE_FLAGS} -D_DEBUG")
else()
	if (SSE)
		set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -DBASISU_SUPPORT_SSE=1")
		set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -DBASISU_SUPPORT_SSE=1")
	else()
		set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -DBASISU_SUPPORT_SSE=0")
		set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -DBASISU_SUPPORT_SSE=0")
	endif()
endif()

set(BASISU_SRC_LIST ${COMMON_SRC_LIST} 
	basisu_tool.cpp
	encoder/basisu_backend.cpp
	encoder/basisu_basis_file.cpp
	encoder/basisu_comp.cpp
	encoder/basisu_enc.cpp
	encoder/basisu_etc.cpp
	encoder/basisu_frontend.cpp
	encoder/basisu_global_selector_palette_helpers.cpp
	encoder/basisu_gpu_texture.cpp
	encoder/basisu_pvrtc1_4.cpp
	encoder/basisu_resampler.cpp
	encoder/basisu_resample_filters.cpp
	encoder/basisu_ssim.cpp
	encoder/basisu_astc_decomp.cpp
	encoder/basisu_uastc_enc.cpp
	encoder/basisu_bc7enc.cpp
	encoder/lodepng.cpp
	encoder/apg_bmp.c
	encoder/jpgd.cpp
	encoder/basisu_kernels_sse.cpp
	transcoder/basisu_transcoder.cpp
	)

if (ZSTD)
	set(BASISU_SRC_LIST ${BASISU_SRC_LIST} zstd/zstd.c)
endif()

if (APPLE)
   set(BIN_DIRECTORY "bin_osx")
else()
   set(BIN_DIRECTORY "bin")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${BIN_DIRECTORY})

add_executable(basisu ${BASISU_SRC_LIST})

if (ZSTD)
	target_compile_definitions(basisu PRIVATE BASISD_SUPPORT_KTX2_ZSTD=1)
else()
	target_compile_definitions(basisu PRIVATE BASISD_SUPPORT_KTX2_ZSTD=0)
endif()

if (NOT MSVC)
   target_link_libraries(basisu m pthread)
endif()

if (NOT EMSCRIPTEN)
	install(TARGETS basisu DESTINATION bin)
	
	if (UNIX)
		if (CMAKE_BUILD_TYPE STREQUAL Release)
			if (APPLE)
				add_custom_command(TARGET basisu POST_BUILD COMMAND strip -X -x ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/basisu)
			else()
				add_custom_command(TARGET basisu POST_BUILD COMMAND strip -g -X -x ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/basisu)
			endif()
		endif()
	endif()
endif()
