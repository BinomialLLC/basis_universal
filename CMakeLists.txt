cmake_minimum_required(VERSION 3.9...3.22 FATAL_ERROR)

project(basisu
        VERSION 1.16.3
        LANGUAGES C CXX)

if (NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 11)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

option(SAN "sanitize" FALSE)
option(BUILD_SHARED_LIBS "Build Shared Libraries" OFF)

option(BUILD_APPS "Build the tool" ON)

# GNU filesystem conventions
include(GNUInstallDirs)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# Windows build shared libraries
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

# For MSVC builds default to SSE enabled, and determine if it's a 64-bit (-A x64) vs. 32-bit (-A Win32) build.
if (MSVC)
	option(SSE "SSE 4.1 support" TRUE)
	if ( CMAKE_GENERATOR_PLATFORM STREQUAL Win32 )	
		set(BUILD_X64 0)
	else()
		set(BUILD_X64 1)
	endif()
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

if (NOT MSVC)
	# With MSVC builds we use the Khronos lib/include files in the project's "OpenCL" directory, to completely avoid requiring fiddly to install vendor SDK's.
	# Otherwise we use the system's (if any).
	find_package( OpenCL )
	message(STATUS "OpenCL found: ${OPENCL_FOUND}")
	message(STATUS "OpenCL includes: ${OpenCL_INCLUDE_DIRS}")
	message(STATUS "OpenCL libraries: ${OpenCL_LIBRARIES}")
endif()

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
   set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g")
   
   set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
   set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")

   if (SAN)
      set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fsanitize=address -fno-omit-frame-pointer -fsanitize=undefined")
      set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -fsanitize=address -fno-omit-frame-pointer -fsanitize=undefined")
   endif()

   set(GCC_COMPILE_FLAGS "-fPIC -fno-strict-aliasing -D_LARGEFILE64_SOURCE=1 -D_FILE_OFFSET_BITS=64 -Wall -Wextra -Wno-unused-local-typedefs -Wno-unused-value -Wno-unused-parameter -Wno-unused-variable")

   if (NOT BUILD_X64)
	  set(GCC_COMPILE_FLAGS "${GCC_COMPILE_FLAGS} -m32")
   endif()

   if (EMSCRIPTEN)
	  set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -s ALLOW_MEMORY_GROWTH=1 -DBASISU_SUPPORT_SSE=0")
	  set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -s ALLOW_MEMORY_GROWTH=1 -DBASISU_SUPPORT_SSE=0")

	  set(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} ${GCC_LINK_FLAGS}")
   else()
   	  if (SSE)
		set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -DBASISU_SUPPORT_SSE=1 -msse4.1")
		set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -DBASISU_SUPPORT_SSE=1 -msse4.1")
	  else()
	  	set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -DBASISU_SUPPORT_SSE=0")
		set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -DBASISU_SUPPORT_SSE=0")
	  endif()
	  
	  set(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} ${GCC_LINK_FLAGS}")
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
	transcoder/basisu_transcoder.cpp
	)

set(BASISU_LIB_TARGET "basisu_lib")
add_library(${BASISU_LIB_TARGET} ${BASISU_SRC_LIST})
set_target_properties(${BASISU_LIB_TARGET} PROPERTIES OUTPUT_NAME "basisu")
target_include_directories(${BASISU_LIB_TARGET} PUBLIC
                           $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/transcoder>
                           $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/encoder>
                           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

if (ZSTD)
    option(USE_EXTERNAL_ZSTD "Use external ZSTD library when found" ON)
    find_package(ZSTD)
    if (ZSTD_FOUND AND USE_EXTERNAL_ZSTD)
        message("Using external ZSTD library")
        target_link_libraries(${BASISU_LIB_TARGET} PRIVATE ZSTD::zstd)
    else()
        target_sources(${BASISU_LIB_TARGET} PRIVATE zstd/zstd.c)
        target_include_directories(${BASISU_LIB_TARGET} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/zstd")
    endif()
endif()

if (ZSTD)
	target_compile_definitions(${BASISU_LIB_TARGET} PRIVATE BASISD_SUPPORT_KTX2_ZSTD=1)
else()
	target_compile_definitions(${BASISU_LIB_TARGET} PRIVATE BASISD_SUPPORT_KTX2_ZSTD=0)
endif()

if (NOT MSVC)
	# For Non-Windows builds, let cmake try and find the system OpenCL headers/libs for us.
	if (OPENCL_FOUND)
		set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -DBASISU_SUPPORT_OPENCL=1")
		set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -DBASISU_SUPPORT_OPENCL=1")	
		
		target_include_directories( ${BASISU_LIB_TARGET} PRIVATE ${OpenCL_INCLUDE_DIRS} )
		target_link_libraries(${BASISU_LIB_TARGET} PRIVATE ${OpenCL_LIBRARIES})
	endif()

else()
	# For Windows builds, we use our local copies of the OpenCL import lib and Khronos headers.
	if (OPENCL)
		set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -DBASISU_SUPPORT_OPENCL=1")
		set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -DBASISU_SUPPORT_OPENCL=1")	
		
		target_include_directories( ${BASISU_LIB_TARGET} PRIVATE "OpenCL" )

		if ( BUILD_X64 )
			target_link_libraries( ${BASISU_LIB_TARGET} PRIVATE "OpenCL/lib/OpenCL64" )
		else()
			target_link_libraries( ${BASISU_LIB_TARGET} PRIVATE "OpenCL/lib/OpenCL" )
		endif()

	endif()
endif()	

if (NOT MSVC)
    include(CheckLibraryExists)
    check_library_exists(m sin "" HAVE_LIB_M)
    if (HAVE_LIB_M)
        target_link_libraries(${BASISU_LIB_TARGET} PRIVATE m)
    endif (HAVE_LIB_M)

    find_package(Threads REQUIRED)
    target_link_libraries(${BASISU_LIB_TARGET} PRIVATE Threads::Threads)
endif()

option(INSTALL_LIB "Whether to install the library and its include files" ON)
if (INSTALL_LIB )

    file(GLOB BASISU_LIB_ENCODER_PUBLIC_HEADERS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "encoder/basisu*.h*")
    file(GLOB BASISU_LIB_TRANSCODER_PUBLIC_HEADERS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "transcoder/basisu*.h*")
    set_target_properties(${BASISU_LIB_TARGET} PROPERTIES PUBLIC_HEADER
                          "${BASISU_LIB_ENCODER_PUBLIC_HEADERS};${BASISU_LIB_TRANSCODER_PUBLIC_HEADERS}")

    include(CMakePackageConfigHelpers)

    install(TARGETS ${BASISU_LIB_TARGET}
            EXPORT basisu_lib_export
            PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/basisu")

    set(namespace "basisu")
    set(install_cmake_config "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}")

    install(EXPORT basisu_lib_export
        FILE "${PROJECT_NAME}Targets.cmake"
        NAMESPACE ${namespace}::
        DESTINATION "${install_cmake_config}"
    )

    # generate the version file for the config file
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
        VERSION "${version}"
        COMPATIBILITY ExactVersion
    )

    # create config file
    configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Config.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
        INSTALL_DESTINATION "${install_cmake_config}"
    )

    # install config files
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
        DESTINATION "${install_cmake_config}"
    )

    # generate the export targets for the build tree
    export(EXPORT basisu_lib_export
        FILE "${CMAKE_CURRENT_BINARY_DIR}/cmake/${PROJECT_NAME}Targets.cmake"
        NAMESPACE ${namespace}::
    )
endif()

if (BUILD_APPS)
    set(BASISU_TOOL_TARGET "basisu")
    add_executable(${BASISU_TOOL_TARGET} basisu_tool.cpp)
    target_link_libraries(${BASISU_TOOL_TARGET} PRIVATE ${BASISU_LIB_TARGET})
endif()

if (BUILD_APPS AND NOT EMSCRIPTEN)
	install(TARGETS ${BASISU_TOOL_TARGET} DESTINATION ${CMAKE_INSTALL_BINDIR})
	
	if (UNIX)
		if (CMAKE_BUILD_TYPE STREQUAL Release)
			if (APPLE)
				add_custom_command(TARGET ${BASISU_TOOL_TARGET} POST_BUILD COMMAND strip -X -x $<TARGET_FILE:${BASISU_TOOL_TARGET}>)
			else()
				add_custom_command(TARGET ${BASISU_TOOL_TARGET} POST_BUILD COMMAND strip -g -X -x $<TARGET_FILE:${BASISU_TOOL_TARGET}>)
			endif()
		endif()
	endif()
endif()
