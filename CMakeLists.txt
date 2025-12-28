# Important: The Basis Universal encoder and transcoder libraries must be compiled with -fno-strict-aliasing (MSVC's default, and also the Linux kernel).
# It should also work without this option, but we do not test with it.
cmake_minimum_required(VERSION 3.20)

if (NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    # Needed otherwise Xcode builds with the default installed SDK which can often be
    # more recent than the macOS version being used. Must be before project.
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "macOS Deployment Target")
endif()

project(basisu C CXX)

# pybind11: allow old Python finder modules without complaining
if (POLICY CMP0148)
    cmake_policy(SET CMP0148 OLD)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "WASI")
    set(BASISU_BUILD_WASM TRUE)
else()
    set(BASISU_BUILD_WASM FALSE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BASISU_STATIC "static linking" TRUE) # NO-OP. basisu_encoder always built as a static library.
option(BASISU_SAN "sanitize" FALSE)
option(BASISU_EXAMPLES "build examples" TRUE)
option(BASISU_TOOL "build basisu tool" TRUE)
option(BASISU_WASM_THREADING "Enable WASI threading support" OFF)
option(BASISU_BUILD_PYTHON "Build native Python module via pybind11" OFF)

# Using a generator expression here prevents multi-config generators (VS, Xcode,
# Ninja Multi-Config) from appending a per-configuration subdirectory. This is
# a MUCH simpler alternative to specifying RUNTIME_OUTPUT_DIRECTORY_<config>
# for each target and each config.
# NOTE: This means the output will be overwritten by a subsequent build for a
# different but that is true of the alternative too.
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

# Old option to new (BASISU_ prefixed) automatic remapping
foreach(pair
    "STATIC;BASISU_STATIC"
    "SAN;BASISU_SAN"
    "EXAMPLES;BASISU_EXAMPLES"
    "WASM_THREADING;BASISU_WASM_THREADING"
    "BUILD_PYTHON;BASISU_BUILD_PYTHON"
    "BUILD_X64;BASISU_BUILD_X64"
    "SSE;BASISU_SSE"
    "ZSTD;BASISU_ZSTD"
    "OPENCL;BASISU_OPENCL"
)
    list(GET pair 0 OLD)
    list(GET pair 1 NEW)

    if(DEFINED ${OLD})
        message(WARNING "[BASISU] Legacy option '${OLD}' is deprecated. Use '${NEW}' instead.")
        set(${NEW} "${${OLD}}" CACHE BOOL "" FORCE)
    endif()
endforeach()

if (BASISU_BUILD_WASM)
    message(STATUS "Configuring for WASM (WASI-SDK)")

    # WASM is always 32-bit
    set(BASISU_BUILD_X64 OFF CACHE BOOL "" FORCE)

    # WASM cannot use SSE
    set(BASISU_SSE OFF CACHE BOOL "" FORCE)

    # WASM cannot use OpenCL
    set(BASISU_OPENCL OFF CACHE BOOL "" FORCE)

    # WASM cannot use dynamic linking
    # TODO: Fix this untrue statement. basisu_encoder always built as a static library.
    set(BASISU_STATIC OFF CACHE BOOL "" FORCE)

    # WASM cannot use sanitizers
    set(BASISU_SAN OFF CACHE BOOL "" FORCE)
endif()

message("Initial CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
message("Initial BASISU_BUILD_X64=${BASISU_BUILD_X64}")
message("Initial BASISU_BUILD_WASM=${BASISU_BUILD_WASM}")
message("Initial BASISU_WASM_THREADING=${BASISU_WASM_THREADING}")
message("Initial BASISU_BUILD_PYTHON=${BASISU_BUILD_PYTHON}")
message("Initial BASISU_SSE=${BASISU_SSE}")
message("Initial BASISU_ZSTD=${BASISU_ZSTD}")
message("Initial BASISU_OPENCL=${BASISU_OPENCL}")
message("Initial BASISU_SAN=${BASISU_SAN}")
message("Initial BASISU_EXAMPLES=${BASISU_EXAMPLES}")
message("Initial BASISU_TOOL=${BASISU_TOOL}")

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

#######################################################################
# Specify floating point handling
#
# For deterministic results specify FP compile options for uniform
# handling of floating point operations across platforms and compilers

# Compiler info query helpers

# On CMake 3.25 or older CXX_COMPILER_FRONTEND_VARIANT is not always set
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "")
    set(CMAKE_CXX_COMPILER_FRONTEND_VARIANT "${CMAKE_CXX_COMPILER_ID}")
endif()

#cmake_print_variables(
#    CMAKE_CXX_COMPILER_ID
#    CMAKE_CXX_COMPILER_VERSION
#    CMAKE_CXX_COMPILER_FRONTEND_VARIANT
#)

# Compiler accepts MSVC-style command line options
set(is_msvc_fe "$<STREQUAL:${CMAKE_CXX_COMPILER_FRONTEND_VARIANT},MSVC>")
# Compiler accepts GNU-style command line options
set(is_gnu_fe1 "$<STREQUAL:${CMAKE_CXX_COMPILER_FRONTEND_VARIANT},GNU>")
# Compiler accepts AppleClang-style command line options, which is also GNU-style
set(is_gnu_fe2 "$<STREQUAL:${CMAKE_CXX_COMPILER_FRONTEND_VARIANT},AppleClang>")
# Compiler accepts GNU-style command line options
set(is_gnu_fe "$<OR:${is_gnu_fe1},${is_gnu_fe2}>")

# Compiler is Visual Studio cl.exe
set(is_msvccl "$<AND:${is_msvc_fe},$<CXX_COMPILER_ID:MSVC>>")
# Compiler is Visual Studio clangcl.exe
set(is_clangcl "$<AND:${is_msvc_fe},$<CXX_COMPILER_ID:Clang>>")
# Compiler is upstream clang with the standard frontend
set(is_clang "$<AND:${is_gnu_fe},$<CXX_COMPILER_ID:Clang,AppleClang>>")

# Genex debug helper. Note that CXX_COMPILER_ID can only be used in
# compilation targets so won't work in a custom target.
#if(NOT TARGET debug_isgnufe1)
#    add_custom_target(debug_isgnufe1
#        COMMAND ${CMAKE_COMMAND} -E echo "is_gnufe1_exp = $<STREQUAL:${CMAKE_CXX_COMPILER_FRONTEND_VARIANT},GNU>"
#    )
#endif()

# On MSVC and CLangCL, set /fp:strict and /fp:precise, on clang
# -ffp-model=precise and on GNU CC -ffp-contract=off. These result in
# precise semantics always being used and fused multiply-add never
# being used,
add_compile_options(
    $<$<AND:${is_msvccl},$<VERSION_LESS:$<CXX_COMPILER_VERSION>,19.30>>:/fp:strict>
    $<$<AND:${is_msvccl},$<VERSION_GREATER_EQUAL:$<CXX_COMPILER_VERSION>,19.30>>:/fp:precise>
    $<${is_clangcl}:/fp:precise>
    $<$<AND:${is_clangcl},$<VERSION_GREATER_EQUAL:$<CXX_COMPILER_VERSION>,14.0.0>>:-Xclang$<SEMICOLON>-ffp-contract=off>
    $<$<AND:${is_clang},$<VERSION_GREATER_EQUAL:$<CXX_COMPILER_VERSION>,10.0.0>>:-ffp-model=precise>
    $<${is_gnu_fe}:-ffp-contract=off>
    # Hide noise from clang and clangcl 20 warning the 2nd fp option changes
    # one of the settings made the first.
    $<$<AND:${is_clang},$<VERSION_GREATER_EQUAL:$<CXX_COMPILER_VERSION>,20.0.0>>:-Wno-overriding-option>
)
#######################################################################

if (NOT MSVC AND NOT BASISU_BUILD_WASM)
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
elseif (BASISU_BUILD_WASM)
    # _WASI_EMULATED_PROCESS_CLOCKS/-lwasi-emulated-process-clocks is only for ZStd
    add_compile_definitions(_WASI_EMULATED_PROCESS_CLOCKS)
    add_link_options(-lwasi-emulated-process-clocks)

    add_compile_options(-fno-strict-aliasing -fvisibility=hidden -Wall -Wextra -Wno-unknown-warning-option)

    add_compile_options($<IF:$<CONFIG:Debug>,-g,-O2>)

    # We need a few MB of stack - don't skip this or WASMTime will silently allow the stack to grow into the heap or static memory, causing corruption.
    add_link_options(-Wl,--stack-first -Wl,-z,stack-size=8388608)
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
    encoder/basisu_astc_ldr_common.cpp
    encoder/basisu_astc_ldr_encode.cpp
    encoder/3rdparty/android_astc_decomp.cpp
    encoder/3rdparty/tinyexr.cpp
    transcoder/basisu_transcoder.cpp
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
    encoder/basisu_astc_ldr_common.h
    encoder/basisu_astc_ldr_encode.h
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
    transcoder/basisu_idct.h
    transcoder/basisu.h
    zstd/zstd.h
)

if (BASISU_ZSTD)
    set(ENCODER_LIB_SRC_LIST ${ENCODER_LIB_SRC_LIST} zstd/zstd.c)
endif()

# Create the static library
add_library(basisu_encoder STATIC ${ENCODER_LIB_SRC_LIST})

target_include_directories(basisu_encoder
INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/zstd> # So KTX-Software can use it.
)

if (EMSCRIPTEN)
    target_compile_definitions(basisu_encoder PUBLIC BASISU_SUPPORT_SSE=0)
else()
    target_compile_definitions(basisu_encoder PUBLIC
        BASISU_SUPPORT_SSE=$<IF:$<BOOL:${BASISU_SSE}>,1,0>
    )
    target_compile_definitions(basisu_encoder PUBLIC
        BASISU_WASI_THREADS=$<IF:$<BOOL:${BASISU_BUILD_WASM}>,1,0>
    )
    target_compile_options(basisu_encoder PRIVATE
        "$<$<AND:$<BOOL:${BASISU_SSE}>,$<CXX_COMPILER_ID:AppleClang,Clang,GNU>>:-msse4.1>"
    )
    target_compile_options(basisu_encoder PUBLIC
        "$<$<BOOL:${BASISU_BUILD_WASM}>:-fno-exceptions -fno-rtti>"
    )
endif()

target_compile_definitions(basisu_encoder PRIVATE "BASISD_SUPPORT_KTX2_ZSTD=$<IF:$<BOOL:${BASISU_ZSTD}>,1,0>")
if (BASISU_OPENCL)
    # basisu uses this to confirm the library has been compiled with OpenCL support hence PUBLIC.
    target_compile_definitions(basisu_encoder PUBLIC BASISU_SUPPORT_OPENCL=1)
    if (NOT WIN32) # True when the target system is not Windows.
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

if (NOT MSVC AND NOT BASISU_BUILD_WASM)
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

macro(set_common_executable_properties target link_encoder)
    #if (MSVC)
        target_sources(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/basisu.manifest")
    #endif()
    if (${link_encoder})
        target_link_libraries(${target} PRIVATE basisu_encoder)
    endif()
    if (NOT BASISU_STATIC AND NOT EMSCRIPTEN AND NOT WIN32)
        target_link_options(${target} PUBLIC -Wl,-rpath .)
    endif()
    if (BASISU_BUILD_WASM)
        # Add proper suffix
        set_target_properties(${target} PROPERTIES SUFFIX ".wasm")
        if (BASISU_WASM_THREADING)
            set_target_properties(${target} PROPERTIES OUTPUT_NAME "$<TARGET_NAME:${target}>_mt")
        else()
            set_target_properties(${target} PROPERTIES OUTPUT_NAME "$<TARGET_NAME:${target}>_st")
        endif()
        # 256 MB initial, 3.5 GB max safe defaults for BasisU
        target_link_options(${target} PRIVATE
            -Wl,--initial-memory=268435456
            -Wl,--max-memory=3758096384
        )
    endif()
endmacro()

if (BASISU_TOOL)
    # Create the basisu executable
    add_executable(basisu basisu_tool.cpp)
    set_common_executable_properties(basisu TRUE)
endif()

if (BASISU_EXAMPLES)
    # Create the example executables
    add_executable(examples example/example.cpp)
    set_common_executable_properties(examples TRUE)

    add_executable(example_capi example_capi/example_capi.c encoder/basisu_wasm_api.cpp encoder/basisu_wasm_transcoder_api.cpp)
    set_common_executable_properties(example_capi TRUE)

    add_executable(example_transcoding example_transcoding/example_transcoding.cpp example_transcoding/utils.cpp zstd/zstddeclib.c transcoder/basisu_transcoder.cpp)
    set_common_executable_properties(example_transcoding FALSE)
    # As target is not linked with basisu_encoder, these values won't be imported.
    target_compile_definitions(example_transcoding PRIVATE "BASISD_SUPPORT_KTX2_ZSTD=$<IF:$<BOOL:${BASISU_ZSTD}>,1,0>")
    target_compile_options(example_transcoding PUBLIC
        "$<$<BOOL:${BASISU_BUILD_WASM}>:-fno-exceptions -fno-rtti>"
    )
endif()

if (BASISU_TOOL AND NOT EMSCRIPTEN)
    if (UNIX AND NOT BASISU_BUILD_WASM)
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

# ------------------------------------------------------------
# Build WASM WASI API module (single or multi-threaded)
# ------------------------------------------------------------
if (BASISU_BUILD_WASM)
    set(BASISU_WASM_API_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/encoder/basisu_wasm_api.cpp
    )

    # Select output name based on threading flag
    if (BASISU_WASM_THREADING)
        set(BASISU_WASM_OUTPUT_NAME "basisu_module_mt")
    else()
        set(BASISU_WASM_OUTPUT_NAME "basisu_module_st")
    endif()

    add_executable(${BASISU_WASM_OUTPUT_NAME} ${BASISU_WASM_API_SRC})
    target_link_libraries(${BASISU_WASM_OUTPUT_NAME} PRIVATE basisu_encoder)

    set_target_properties(${BASISU_WASM_OUTPUT_NAME} PROPERTIES SUFFIX ".wasm")

    # Common WASM options
    target_link_options(${BASISU_WASM_OUTPUT_NAME} PRIVATE
        -Wl,--initial-memory=268435456
        -Wl,--max-memory=3758096384
        -Wl,--stack-first
        -Wl,-z,stack-size=8388608
    )

    set_target_properties(${BASISU_WASM_OUTPUT_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY $<1:${CMAKE_CURRENT_SOURCE_DIR}/python/basisu_py/wasm>
    )

    # Threading options
    if (BASISU_WASM_THREADING)
        target_compile_options(${BASISU_WASM_OUTPUT_NAME} PRIVATE
            -pthread
            -matomics
        )
        target_link_options(${BASISU_WASM_OUTPUT_NAME} PRIVATE
            -pthread
            -Wl,--shared-memory
            -Wl,--export-memory
        )

        target_compile_definitions(${BASISU_WASM_OUTPUT_NAME} PRIVATE BASISU_WASI_THREADS=1)
    endif()
endif()

if (BASISU_BUILD_WASM)
    # Select output name based on threading flag
    if (BASISU_WASM_THREADING)
        set(BASISU_TRANSCODER_WASM_OUTPUT_NAME "basisu_transcoder_module_mt")
    else()
        set(BASISU_TRANSCODER_WASM_OUTPUT_NAME "basisu_transcoder_module_st")
    endif()

    add_executable(${BASISU_TRANSCODER_WASM_OUTPUT_NAME}
        ${CMAKE_CURRENT_SOURCE_DIR}/encoder/basisu_wasm_transcoder_api.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/transcoder/basisu_transcoder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/zstd/zstddeclib.c)

    set_target_properties(${BASISU_TRANSCODER_WASM_OUTPUT_NAME} PROPERTIES SUFFIX ".wasm")

    target_link_options(${BASISU_TRANSCODER_WASM_OUTPUT_NAME} PRIVATE
        -Wl,--initial-memory=16777216
        -Wl,--stack-first
        -Wl,-z,stack-size=4194304
    )
    target_compile_options(${BASISU_TRANSCODER_WASM_OUTPUT_NAME} PRIVATE -fno-exceptions -fno-rtti)

    set_target_properties(${BASISU_TRANSCODER_WASM_OUTPUT_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY $<1:${CMAKE_CURRENT_SOURCE_DIR}/python/basisu_py/wasm>
    )
endif()


# ------------------------------------------------------------
# Optional: Build native Python modules (pybind11)
# ------------------------------------------------------------
if (BASISU_BUILD_PYTHON AND NOT BASISU_BUILD_WASM)
    find_package(pybind11 CONFIG REQUIRED)

    message(STATUS "Building pybind11 Python extension: basisu_python")

    pybind11_add_module(basisu_python
        python/basisu_encoder_pybind11.cpp
        encoder/basisu_wasm_api.cpp
    )

    # Ensure PIC ONLY for this target
    set_property(TARGET basisu_python PROPERTY POSITION_INDEPENDENT_CODE ON)

    target_link_libraries(basisu_python PRIVATE basisu_encoder)

    # Put basisu_python so into python/basisu_py
    set_target_properties(basisu_python PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY $<1:${CMAKE_CURRENT_SOURCE_DIR}/python/basisu_py>
        PREFIX ""                      # Required by Python
        OUTPUT_NAME "basisu_python"    # Just to be explicit
    )

    if (MSVC)
        set_target_properties(basisu_python PROPERTIES SUFFIX ".pyd")
    endif()
endif()

if (BASISU_BUILD_PYTHON AND NOT BASISU_BUILD_WASM)
    find_package(pybind11 CONFIG REQUIRED)

    message(STATUS "Building pybind11 Python extension: basisu_transcoder_python")

    pybind11_add_module(basisu_transcoder_python
        python/basisu_transcoder_pybind11.cpp
        encoder/basisu_wasm_transcoder_api.cpp
        transcoder/basisu_transcoder.cpp
        zstd/zstddeclib.c
    )

    # Ensure PIC ONLY for this target
    set_property(TARGET basisu_transcoder_python PROPERTY POSITION_INDEPENDENT_CODE ON)

    set_target_properties(basisu_transcoder_python PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY $<1:${CMAKE_CURRENT_SOURCE_DIR}/python/basisu_py>
        PREFIX ""
        OUTPUT_NAME "basisu_transcoder_python"
    )

    if (MSVC)
        set_target_properties(basisu_transcoder_python PROPERTIES SUFFIX ".pyd")
    endif()
endif()
