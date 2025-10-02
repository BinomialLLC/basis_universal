# Important: The Basis Universal encoder and transcoder libraries must be compiled with -fno-strict-aliasing (MSVC's default, and also the Linux kernel).
# It should also work without this option, but we do not test with it.
cmake_minimum_required(VERSION 3.16)

project(basisu C CXX)
set(CMAKE_CXX_STANDARD 17)
option(BASISU_STATIC "static linking" FALSE)
option(BASISU_SAN "sanitize" FALSE)

# Using a generator expression here prevents multi-config generators (VS, Xcode, Ninja Multi-Config)
# from appending a per-configuration subdirectory. NOTE: This means the output could be overwritten
# by a subsequent build for a different configuration.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY $<1:${CMAKE_SOURCE_DIR}/bin>)

# For MSVC builds default to SSE enabled, and determine if it's a 64-bit (-A x64) vs. 32-bit (-A Win32) build.
if (MSVC)
    option(BASISU_SSE "SSE 4.1 support" TRUE)
    if ( CMAKE_GENERATOR_PLATFORM STREQUAL Win32 )
        set(BASISU_BUILD_X64 0)
    else()
        set(BASISU_BUILD_X64 1)
    endif()
	add_compile_options(/W4)
else()
    option(BASISU_SSE "SSE 4.1 support" FALSE)
    option(BASISU_BUILD_X64 "build 64-bit" TRUE)
endif()

option(BASISU_ZSTD "ZSTD support for KTX2 transcoding/encoding" TRUE)
option(BASISU_OPENCL "OpenCL support in encoder" FALSE)

message("Initial BASISU_BUILD_X64=${BASISU_BUILD_X64}")
message("Initial CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
message("Initial BASISU_SSE=${BASISU_SSE}")
message("Initial BASISU_ZSTD=${BASISU_ZSTD}")
message("Initial BASISU_OPENCL=${BASISU_OPENCL}")
message("Initial BASISU_SAN=${BASISU_SAN}")

if ((NOT MSVC) AND OPENCL)
    # With MSVC builds we use the Khronos lib/include files in the project's "OpenCL" directory, to completely avoid requiring fiddly to install vendor SDK's.
    # Otherwise we use the system's (if any).
    find_package(OpenCL)
    message(STATUS "OpenCL found: ${OPENCL_FOUND}")
    message(STATUS "OpenCL includes: ${OpenCL_INCLUDE_DIRS}")
    message(STATUS "OpenCL libraries: ${OpenCL_LIBRARIES}")
endif()

if( NOT CMAKE_BUILD_TYPE )
  set( CMAKE_BUILD_TYPE Release )
endif()

message(${PROJECT_NAME} " build type: " ${CMAKE_BUILD_TYPE})

if (BASISU_BUILD_X64)
    message("Building 64-bit")
else()
    message("Building 32-bit")
endif()

if (BASISU_SSE)
    message("SSE enabled")
else()
    message("SSE disabled")
endif()

if (BASISU_ZSTD)
    message("Zstandard enabled")
else()
    message("Zstandard disabled")
endif()

if (NOT MSVC)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g")
   
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")

    if (BASISU_SAN)
		message("Enabling SAN")
		
		  set(SANITIZE_FLAGS "-fsanitize=address -fno-omit-frame-pointer -fsanitize=undefined -fno-sanitize=alignment")
		
		  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${SANITIZE_FLAGS}")
        set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} ${SANITIZE_FLAGS}")
	
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${SANITIZE_FLAGS}")
        set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} ${SANITIZE_FLAGS}")
    endif()

    set(CMAKE_CXX_FLAGS -std=c++17)
    set(GCC_COMPILE_FLAGS "-fvisibility=hidden -fPIC -fno-strict-aliasing -D_LARGEFILE64_SOURCE=1 -D_FILE_OFFSET_BITS=64 -Wall -Wextra -Wno-unused-local-typedefs -Wno-unused-value -Wno-unused-parameter -Wno-unused-variable -Wno-reorder -Wno-misleading-indentation -Wno-class-memaccess -Wno-deprecated-copy -Wno-maybe-uninitialized -Wno-unused-function -Wno-stringop-overflow -Wno-unknown-warning-option")
   
    if (NOT BASISU_BUILD_X64)
        set(GCC_COMPILE_FLAGS "${GCC_COMPILE_FLAGS} -m32")
    endif()

    if (EMSCRIPTEN)
        set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -s ALLOW_MEMORY_GROWTH=1 -DBASISU_SUPPORT_SSE=0")
        set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -s ALLOW_MEMORY_GROWTH=1 -DBASISU_SUPPORT_SSE=0")

        set(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} ${GCC_LINK_FLAGS}")
    elseif (BASISU_STATIC)
        if (BASISU_SSE)
            set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -DBASISU_SUPPORT_SSE=1 -msse4.1")
            set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -DBASISU_SUPPORT_SSE=1 -msse4.1")
        else()
            set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -DBASISU_SUPPORT_SSE=0")
            set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -DBASISU_SUPPORT_SSE=0")
        endif()
      
        set(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} ${GCC_LINK_FLAGS} -static-libgcc -static-libstdc++ -static")
    else()
        if (BASISU_SSE)
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
    if (BASISU_SSE)
        set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -DBASISU_SUPPORT_SSE=1")
        set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -DBASISU_SUPPORT_SSE=1")
    else()
        set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -DBASISU_SUPPORT_SSE=0")
        set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -DBASISU_SUPPORT_SSE=0")
    endif()
endif()

# Define the source files for the static library
set(ENCODER_LIB_SRC_LIST
    encoder/basisu_backend.cpp
    encoder/basisu_basis_file.cpp
    encoder/basisu_comp.cpp
    encoder/basisu_enc.cpp
    encoder/basisu_etc.cpp
    encoder/basisu_frontend.cpp
    encoder/basisu_gpu_texture.cpp
    encoder/basisu_pvrtc1_4.cpp
    encoder/basisu_resampler.cpp
    encoder/basisu_resample_filters.cpp
    encoder/basisu_ssim.cpp
    encoder/basisu_uastc_enc.cpp
    encoder/basisu_bc7enc.cpp
    encoder/jpgd.cpp
    encoder/basisu_kernels_sse.cpp
    encoder/basisu_opencl.cpp
    encoder/pvpngreader.cpp
    encoder/basisu_uastc_hdr_4x4_enc.cpp
    encoder/basisu_astc_hdr_6x6_enc.cpp
    encoder/basisu_astc_hdr_common.cpp
    encoder/3rdparty/android_astc_decomp.cpp
    encoder/3rdparty/tinyexr.cpp
    transcoder/basisu_transcoder.cpp
)

set(ENCODER_LIB_HDR_LIST
    encoder/basisu_astc_hdr_6x6_enc.h
    encoder/basisu_astc_hdr_common.h
    encoder/basisu_backend.h
    encoder/basisu_basis_file.h
    encoder/basisu_bc7enc.h
    encoder/basisu_comp.h
    encoder/basisu_enc.h
    encoder/basisu_etc.h
    encoder/basisu_frontend.h
    encoder/basisu_gpu_texture.h
    encoder/basisu_kernels_declares.h
    encoder/basisu_kernels_imp.h
    encoder/basisu_math.h
    encoder/basisu_miniz.h
    encoder/basisu_ocl_kernels.h
    encoder/basisu_opencl.h
    encoder/basisu_pvrtc1_4.h
    encoder/basisu_resampler_filters.h
    encoder/basisu_resampler.h
    encoder/basisu_ssim.h
    encoder/basisu_uastc_enc.h
    encoder/basisu_uastc_hdr_4x4_enc.h
    encoder/cppspmd_flow.h
    encoder/cppspmd_math_declares.h
    encoder/cppspmd_math.h
    encoder/cppspmd_sse.h
    encoder/cppspmd_type_aliases.h
    encoder/jpgd.h
    encoder/pvpngreader.h
    encoder/3rdparty/android_astc_decomp.h
    encoder/3rdparty/qoi.h
    encoder/3rdparty/tinyexr.h
    transcoder/basisu_astc_hdr_core.h
    transcoder/basisu_astc_helpers.h
    transcoder/basisu_containers_impl.h
    transcoder/basisu_containers.h
    transcoder/basisu_file_headers.h
    transcoder/basisu_transcoder_internal.h
    transcoder/basisu_transcoder_uastc.h
    transcoder/basisu_transcoder.h
    transcoder/basisu.h
)

if (BASISU_ZSTD)
    set(ENCODER_LIB_SRC_LIST ${ENCODER_LIB_SRC_LIST} zstd/zstd.c)
    set(ENCODER_LIB_HDR_LIST ${ENCODER_LIB_HDR_LIST} zstd/zstd.h)
endif()

# Create the static library
add_library(basisu_encoder STATIC ${ENCODER_LIB_SRC_LIST} ${ENCODER_LIB_HDR_LIST})

# Create the basisu executable and link against the static library
add_executable(basisu basisu_tool.cpp)
target_link_libraries(basisu PRIVATE basisu_encoder)

# Create the new example executable and link against the static library
add_executable(examples example/example.cpp)
target_link_libraries(examples PRIVATE basisu_encoder)

if (BASISU_ZSTD)
    target_compile_definitions(basisu PRIVATE BASISD_SUPPORT_KTX2_ZSTD=1)
    target_compile_definitions(examples PRIVATE BASISD_SUPPORT_KTX2_ZSTD=1)
else()
    target_compile_definitions(basisu PRIVATE BASISD_SUPPORT_KTX2_ZSTD=0)
    target_compile_definitions(examples PRIVATE BASISD_SUPPORT_KTX2_ZSTD=0)
endif()

if (NOT MSVC)
    # For Non-Windows builds, let cmake try and find the system OpenCL headers/libs for us.
    if (BASISU_OPENCL AND OPENCL_FOUND)
        set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -DBASISU_SUPPORT_OPENCL=1")
        set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -DBASISU_SUPPORT_OPENCL=1")    
        
        target_include_directories(basisu PRIVATE ${OpenCL_INCLUDE_DIRS})
        target_include_directories(examples PRIVATE ${OpenCL_INCLUDE_DIRS})
		target_include_directories(basisu_encoder PRIVATE ${OpenCL_INCLUDE_DIRS})
        set(BASISU_EXTRA_LIBS ${OpenCL_LIBRARIES})
    endif()
else()
    # For Windows builds, we use our local copies of the OpenCL import lib and Khronos headers.
    if (BASISU_OPENCL)
        set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -DBASISU_SUPPORT_OPENCL=1")
        set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -DBASISU_SUPPORT_OPENCL=1")    
        
        target_include_directories(basisu PRIVATE "OpenCL")
        target_include_directories(examples PRIVATE "OpenCL")
		target_include_directories(basisu_encoder PRIVATE "OpenCL")

        if (BASISU_BUILD_X64)
            target_link_libraries(basisu PRIVATE "${CMAKE_SOURCE_DIR}/OpenCL/lib/OpenCL64.lib")
            target_link_libraries(examples PRIVATE "${CMAKE_SOURCE_DIR}/OpenCL/lib/OpenCL64.lib")
        else()
            target_link_libraries(basisu PRIVATE "${CMAKE_SOURCE_DIR}/OpenCL/lib/OpenCL.lib")
            target_link_libraries(examples PRIVATE "${CMAKE_SOURCE_DIR}/OpenCL/lib/OpenCL.lib")
        endif()
    endif()
endif()    

if (NOT MSVC)
    target_link_libraries(basisu PRIVATE m pthread ${BASISU_EXTRA_LIBS})
    target_link_libraries(examples PRIVATE m pthread ${BASISU_EXTRA_LIBS})
endif()

if (NOT EMSCRIPTEN)
    if (UNIX)
        if (CMAKE_BUILD_TYPE STREQUAL Release)
            if (APPLE)
                add_custom_command(TARGET basisu POST_BUILD COMMAND strip -X -x ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/basisu)
				#message("strip command: strip -X -x ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/basisu")
            else()
                add_custom_command(TARGET basisu POST_BUILD COMMAND strip -g -X -x ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/basisu)
				#message("strip command: strip -g -X -x ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/basisu")
            endif()
        endif()
    endif()
endif()

if (MSVC)
	set_target_properties(basisu PROPERTIES 
        RUNTIME_OUTPUT_NAME "basisu"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    )	
	
	set_target_properties(examples PROPERTIES 
        RUNTIME_OUTPUT_NAME "examples"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    )	
endif()
