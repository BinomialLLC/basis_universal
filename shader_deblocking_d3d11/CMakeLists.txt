cmake_minimum_required(VERSION 3.16)
project(shader_deblocking_d3d11 CXX C)

if (NOT WIN32)
    message(FATAL_ERROR "shader_deblocking_d3d11 is a Windows/Direct3D 11 sample. See shader_deblocking_glfw for the portable OpenGL version.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# This sample lives in <basisu_repo_root>/shader_deblocking_d3d11.
set(BASISU_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

# No package dependencies: just the C runtime, Win32, D3D11, and D3DCompiler.
# The Basis Universal transcoder is compiled in directly (it does not link against
# anything else in the repo).
add_executable(deblock_d3d11
    main.cpp
    "${BASISU_ROOT}/transcoder/basisu_transcoder.cpp"
    "${BASISU_ROOT}/zstd/zstddeclib.c"
)

target_include_directories(deblock_d3d11 PRIVATE
    "${BASISU_ROOT}/transcoder"
    "${BASISU_ROOT}/zstd"
)

# Enable KTX2 Zstd supercompression support in the transcoder (XUASTC LDR uses it).
target_compile_definitions(deblock_d3d11 PRIVATE BASISD_SUPPORT_KTX2_ZSTD=1)

target_link_libraries(deblock_d3d11 PRIVATE d3d11 d3dcompiler)

# Put the executable in bin/ (next to deblock.hlsl + the test .ktx2 files) on every
# generator, so it can be run in place. The per-config entries stop multi-config
# generators (Visual Studio) from adding a Debug/Release subdirectory.
set(_BIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/bin")
set_target_properties(deblock_d3d11 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY                "${_BIN_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG          "${_BIN_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE        "${_BIN_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${_BIN_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL     "${_BIN_DIR}"
    # Visual Studio: debugger working directory = bin/ (written into the .vcxproj.user).
    VS_DEBUGGER_WORKING_DIRECTORY           "${_BIN_DIR}")

if (MSVC)
    target_compile_definitions(deblock_d3d11 PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# The single deblock.hlsl lives in bin/ (the debugger working directory); the program
# compiles it from the working directory at runtime (D3DCompileFromFile), so it can be
# edited and reloaded with the R key. Nothing is copied at build time.
