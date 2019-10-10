cmake_minimum_required(VERSION 3.0)

project(basisu_js)

if (EMSCRIPTEN)
  set(CMAKE_CXX_STANDARD 11)

  add_executable(basisu.js
    ../../transcoder/basisu_transcoder.cpp
    ../../basisu_backend.cpp
    ../../basisu_basis_file.cpp
    ../../basisu_comp.cpp
    ../../basisu_enc.cpp
    ../../basisu_etc.cpp
    ../../basisu_frontend.cpp
    ../../basisu_global_selector_palette_helpers.cpp
    ../../basisu_gpu_texture.cpp
    ../../basisu_pvrtc1_4.cpp
    ../../basisu_resampler.cpp
    ../../basisu_resample_filters.cpp
    ../../basisu_ssim.cpp
    ../../basisu_tool.cpp
    ../../basisu_astc_decomp.cpp
    ../../lodepng.cpp
  )

  target_compile_definitions(basisu.js PRIVATE NDEBUG)
  target_compile_options(basisu.js PRIVATE -O3)
  target_include_directories(basisu.js PRIVATE ../../ ../../transcoder)

  set_target_properties(basisu.js PROPERTIES
      OUTPUT_NAME "basisu"
      SUFFIX ".js"
      LINK_FLAGS "-s EXTRA_EXPORTED_RUNTIME_METHODS=['FS'] -s ALLOW_MEMORY_GROWTH=1 -O3 -s ASSERTIONS=0 -s MALLOC=emmalloc -s MODULARIZE=1 -s EXPORT_NAME=basisu -s INVOKE_RUN=0 -s BINARYEN_TRAP_MODE='clamp'"
      COMPILE_FLAGS "-std=c++11 -fvisibility=hidden -fvisibility-inlines-hidden -fPIC -fno-strict-aliasing -D_LARGEFILE64_SOURCE=1 -D_FILE_OFFSET_BITS=64 -Wall -Wextra -Wno-unused-local-typedefs -Wno-unused-value -Wno-unused-parameter -Wno-unused-variable -Wno-reorder")
endif()
