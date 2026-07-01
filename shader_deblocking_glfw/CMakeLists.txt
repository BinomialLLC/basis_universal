cmake_minimum_required(VERSION 3.16)
project(shader_deblocking_glfw CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# This sample lives in <basisu_repo_root>/shader_deblocking_glfw.
set(BASISU_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

find_package(glfw3 CONFIG REQUIRED)   # target: glfw (vcpkg on Windows, or system libglfw3-dev on Linux)
find_package(OpenGL REQUIRED)         # target: OpenGL::GL
find_package(Threads REQUIRED)        # target: Threads::Threads (pthreads on Linux)

# glad is vendored (a generated GL 3.3 core loader, third_party/glad) and built as a
# small static library, so no package manager is needed for it on any platform.
add_library(glad STATIC third_party/glad/src/glad.c)
target_include_directories(glad PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glad/include")

add_executable(deblock
    main.cpp
    "${BASISU_ROOT}/transcoder/basisu_transcoder.cpp"
    "${BASISU_ROOT}/zstd/zstddeclib.c"
)

target_include_directories(deblock PRIVATE
    "${BASISU_ROOT}/transcoder"
    "${BASISU_ROOT}/zstd"
)

# Enable KTX2 Zstd supercompression support in the transcoder (XUASTC LDR uses it).
target_compile_definitions(deblock PRIVATE BASISD_SUPPORT_KTX2_ZSTD=1)

target_link_libraries(deblock PRIVATE glfw glad OpenGL::GL
    Threads::Threads ${CMAKE_DL_LIBS})   # 'glad' is the local vendored target; dl/pthread no-op on Windows

# macOS deprecated OpenGL (10.14+); silence the deprecation warning flood.
if (APPLE)
    target_compile_definitions(deblock PRIVATE GL_SILENCE_DEPRECATION)
endif()

# Put the executable in bin/ (next to shader.glsl + the test .ktx2 files) on every
# generator, so it can be run in place. The per-config entries stop multi-config
# generators (Visual Studio/Xcode) from adding a Debug/Release subdirectory.
set(_BIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/bin")
set_target_properties(deblock PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY                "${_BIN_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG          "${_BIN_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE        "${_BIN_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${_BIN_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL     "${_BIN_DIR}"
    # Visual Studio: debugger working directory = bin/ (written into the .vcxproj.user).
    VS_DEBUGGER_WORKING_DIRECTORY           "${_BIN_DIR}")

if (MSVC)
    target_compile_definitions(deblock PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# The single shader.glsl lives in bin/ (the debugger working directory); the program
# loads it from the working directory at runtime. Nothing is copied at build time.
