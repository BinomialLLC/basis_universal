# Important: The Basis Universal encoder and transcoder libraries must be compiled with -fno-strict-aliasing (MSVC's default, and also the Linux kernel).
# It should also work without this option, but we do not test with it.
cmake_minimum_required(VERSION 3.20)

if (NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    # Needed otherwise Xcode builds with the default installed SDK which can often be
    # more recent than the macOS version being used.
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "macOS Deployment Target")
endif()

project(basisu C CXX)

option(BASISU_TOOL "Include basisu tool in build" TRUE)
option(BASISU_EXAMPLES "Include examples in build" TRUE)

option(BASISU_STATIC "static linking" TRUE)
option(BASISU_SAN "sanitize" FALSE)

# Using a generator expression here prevents multi-config generators (VS, Xcode, Ninja Multi-Config)
# from appending a per-configuration subdirectory. NOTE: This means the output could be overwritten
# by a subsequent build for a different configuration.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY $<1:${CMAKE_CURRENT_SOURCE_DIR}/bin>)

# For MSVC builds default to SSE enabled, and determine if it's a 64-bit (-A x64) vs. 32-bit (-A Win32) build.
if (MSVC)
    # TODO: Fix me for Windows ARM
    option(BASISU_SSE "SSE 4.1 support" TRUE)
    if ( CMAKE_GENERATOR_PLATFORM STREQUAL "Win32" )
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
message("initial BASISU_TOOL=${BASISU_TOOL}")
message("initial BASISU_EXAMPLES=${BASISU_EXAMPLES}")

if(MINGW)
    # Check if the Threads package is provided; if using Mingw it MIGHT be
    find_package(Threads)
elseif(LINUX)
    find_package(Threads REQUIRED)
endif()

if ((NOT WIN32) AND BASISU_OPENCL)
    # For Windows builds we use the Khronos lib/include files in the project's "OpenCL" directory, to completely avoid requiring fiddly to install vendor SDK's.
    # Otherwise we use the system's (if any).
    find_package(OpenCL REQUIRED)
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
    add_compile_options($<$<CONFIG:Debug>:-g>)
    # If you want to set an optimization option for non-debug too, use this instead.
    #add_compile_options($<IF:$<CONFIG:Debug>,-g,-O3>)

    if (BASISU_SAN)
        message("Enabling SAN")

        add_compile_options(-fsanitize=address -fno-omit-frame-pointer -fsanitize=undefined -fno-sanitize=alignment)
    endif()

    # Add common non-MSVC flags excluding -fPIC.
    add_compile_options(-fvisibility=hidden -fno-strict-aliasing -D_LARGEFILE64_SOURCE=1
                        -D_FILE_OFFSET_BITS=64 -Wall -Wextra -Wno-unused-local-typedefs
                        -Wno-unused-value -Wno-unused-parameter -Wno-unused-variable
                        -Wno-misleading-indentation
                        -Wno-maybe-uninitialized -Wno-unused-function
                        -Wno-stringop-overflow -Wno-unknown-warning-option)
    # Add -fPIC ONLY on non-Windows platforms
    if (NOT WIN32)
        add_compile_options(-fPIC)
    endif()

    # AppleClang 14 raises this warning in zstd.cpp.
    add_compile_options("$<$<AND:$<CXX_COMPILER_ID:Clang,AppleClang>,$<VERSION_LESS:$<CXX_COMPILER_VERSION>,17>>:-Wno-bitwise-instead-of-logical>")

    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-Wno-reorder;-Wno-class-memaccess;-Wno-deprecated-copy>")

    add_compile_options($<$<NOT:$<BOOL:BASISU_BUILD_X64>>:-m32>)
    add_compile_definitions($<$<CONFIG:Debug>:_DEBUG>)
    if (EMSCRIPTEN)
        add_link_options("SHELL:-s ALLOW_MEMORY_GROWTH=1")
    endif()
else()
    add_compile_options("$<$<CXX_COMPILER_ID:Clang>:-Wno-unused-variable;-Wno-unused-function>")
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

target_include_directories(basisu_encoder
INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/zstd> # So KTX-Software can use it.
)
# PUBLIC so it will be exported to dependent programs.
target_compile_features(basisu_encoder PUBLIC cxx_std_17)

if (EMSCRIPTEN)
    target_compile_definitions(basisu_encoder PUBLIC BASISU_SUPPORT_SSE=0)
else()
    target_compile_definitions(basisu_encoder PUBLIC
        BASISU_SUPPORT_SSE=$<IF:$<BOOL:${BASISU_SSE}>,1,0>
    )
    target_compile_options(basisu_encoder PRIVATE
        "$<$<AND:$<BOOL:${BASISU_SSE}>,$<CXX_COMPILER_ID:AppleClang,Clang,GNU>>:-msse4.1>"
    )
endif()

target_compile_definitions(basisu_encoder PRIVATE "BASISD_SUPPORT_KTX2_ZSTD=$<IF:$<BOOL:${BASISU_ZSTD}>,1,0>")
if (BASISU_OPENCL)
    # basisu uses this to confirm the library has been compiled with OpenCL support hence PUBLIC.
    target_compile_definitions(basisu_encoder PUBLIC BASISU_SUPPORT_OPENCL=1)
    if (NOT WIN32) # True when the target system is Windows.
        # For Non-Windows builds, use the system OpenCL headers/libs, if cmake found them.
        target_include_directories(basisu_encoder PRIVATE ${OpenCL_INCLUDE_DIRS})
        target_link_libraries(basisu_encoder PRIVATE ${OpenCL_LIBRARIES})
    else()
        # For Windows builds, we use our local copies of the OpenCL import lib and Khronos headers.
        target_include_directories(basisu_encoder PRIVATE "OpenCL")
        if (BASISU_BUILD_X64)
            target_link_libraries(basisu_encoder PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/OpenCL/lib/OpenCL64.lib")
        else()
            target_link_libraries(basisu_encoder PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/OpenCL/lib/OpenCL.lib")
        endif()
    endif()
else()
    target_compile_definitions(basisu_encoder PUBLIC BASISU_SUPPORT_OPENCL=0)
endif()

if (NOT MSVC)
    # Only link 'm' on non-Windows platforms (Linux, macOS)
    if (NOT WIN32)
        target_link_libraries(basisu_encoder INTERFACE m)
    endif()
    if(Threads_FOUND AND CMAKE_USE_PTHREADS_INIT)
        target_link_libraries(basisu_encoder INTERFACE Threads::Threads)
    elseif(LINUX)
        target_link_libraries(basisu_encoder INTERFACE dl Threads::Threads)
    endif()
    if (BASISU_STATIC AND MINGW)
        target_link_options(basisu_encoder INTERFACE -static-libgcc -static-libstdc++ -static)
    endif()
endif()

macro(set_common_executable_properties target)
    #if (MSVC)
        target_sources(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/basisu.manifest")
    #endif()
    target_link_libraries(${target} PRIVATE basisu_encoder)
    if (NOT BASISU_STATIC AND NOT EMSCRIPTEN AND NOT WIN32)
        target_link_options(${target} PUBLIC -Wl,-rpath .)
    endif()
endmacro()

if (BASISU_TOOL)
    # Create the basisu executable and link against the static library
    add_executable(basisu basisu_tool.cpp)
    set_common_executable_properties(basisu)
endif()

if (BASISU_EXAMPLES)
    # Create the new example executable and link against the static library
    add_executable(examples example/example.cpp)
    set_common_executable_properties(examples)
endif()

if (BASISU_TOOL AND NOT EMSCRIPTEN)
    if (UNIX)
        if (CMAKE_BUILD_TYPE STREQUAL "Release")
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
