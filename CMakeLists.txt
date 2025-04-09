# Important: The Basis Universal encoder and transcoder libraries must be compiled with -fno-strict-aliasing (MSVC's default, and also the Linux kernel).
# It should also work without this option, but we do not test with it.
cmake_minimum_required(VERSION 3.5)

project(basisu C CXX)
set(CMAKE_CXX_STANDARD 17)
option(STATIC "static linking" FALSE)
option(SAN "sanitize" FALSE)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# For MSVC builds default to SSE enabled, and determine if it's a 64-bit (-A x64) vs. 32-bit (-A Win32) build.
if (MSVC)
    option(SSE "SSE 4.1 support" TRUE)
    if ( CMAKE_GENERATOR_PLATFORM STREQUAL Win32 )    
        set(BUILD_X64 0)
    else()
        set(BUILD_X64 1)
    endif()
	add_compile_options(/W4)
else()
    option(SSE "SSE 4.1 support" FALSE)
    option(BUILD_X64 "build 64-bit" TRUE)
endif()

option(ZSTD "ZSTD support for KTX2 transcoding/encoding" TRUE)
option(OPENCL "OpenCL support in encoder" FALSE)

message("Initial BUILD_X64=${BUILD_X64}")
message("Initial CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
message("Initial SSE=${SSE}")
message("Initial ZSTD=${ZSTD}")
message("Initial OPENCL=${OPENCL}")
message("Initial SAN=${SAN}")

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
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g")
   
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")

    if (SAN)
		message("Enabling SAN")
		
		  set(SANITIZE_FLAGS "-fsanitize=address -fno-omit-frame-pointer -fsanitize=undefined -fno-sanitize=alignment")
		
		  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${SANITIZE_FLAGS}")
        set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} ${SANITIZE_FLAGS}")
	
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${SANITIZE_FLAGS}")
        set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} ${SANITIZE_FLAGS}")
    endif()

    set(CMAKE_CXX_FLAGS -std=c++17)
    set(GCC_COMPILE_FLAGS "-fvisibility=hidden -fPIC -fno-strict-aliasing -D_LARGEFILE64_SOURCE=1 -D_FILE_OFFSET_BITS=64 -Wall -Wextra -Wno-unused-local-typedefs -Wno-unused-value -Wno-unused-parameter -Wno-unused-variable -Wno-reorder -Wno-misleading-indentation -Wno-class-memaccess -Wno-deprecated-copy -Wno-maybe-uninitialized -Wno-unused-function -Wno-stringop-overflow -Wno-unknown-warning-option")
   
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

if (ZSTD)
    set(ENCODER_LIB_SRC_LIST ${ENCODER_LIB_SRC_LIST} zstd/zstd.c)
endif()

# Create the static library
add_library(basisu_encoder STATIC ${ENCODER_LIB_SRC_LIST})

# Create the basisu executable and link against the static library
add_executable(basisu basisu_tool.cpp)
target_link_libraries(basisu PRIVATE basisu_encoder)

# Create the new example executable and link against the static library
add_executable(examples example/example.cpp)
target_link_libraries(examples PRIVATE basisu_encoder)

if (ZSTD)
    target_compile_definitions(basisu PRIVATE BASISD_SUPPORT_KTX2_ZSTD=1)
    target_compile_definitions(examples PRIVATE BASISD_SUPPORT_KTX2_ZSTD=1)
else()
    target_compile_definitions(basisu PRIVATE BASISD_SUPPORT_KTX2_ZSTD=0)
    target_compile_definitions(examples PRIVATE BASISD_SUPPORT_KTX2_ZSTD=0)
endif()

if (NOT MSVC)
    # For Non-Windows builds, let cmake try and find the system OpenCL headers/libs for us.
    if (OPENCL AND OPENCL_FOUND)
        set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -DBASISU_SUPPORT_OPENCL=1")
        set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -DBASISU_SUPPORT_OPENCL=1")    
        
        target_include_directories(basisu PRIVATE ${OpenCL_INCLUDE_DIRS})
        target_include_directories(examples PRIVATE ${OpenCL_INCLUDE_DIRS})
		target_include_directories(basisu_encoder PRIVATE ${OpenCL_INCLUDE_DIRS})
        set(BASISU_EXTRA_LIBS ${OpenCL_LIBRARIES})
    endif()
else()
    # For Windows builds, we use our local copies of the OpenCL import lib and Khronos headers.
    if (OPENCL)
        set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -DBASISU_SUPPORT_OPENCL=1")
        set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -DBASISU_SUPPORT_OPENCL=1")    
        
        target_include_directories(basisu PRIVATE "OpenCL")
        target_include_directories(examples PRIVATE "OpenCL")
		target_include_directories(basisu_encoder PRIVATE "OpenCL")

        if (BUILD_X64)
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
