cmake_minimum_required(VERSION 3.0)

project(basisu_transcoder_js)

if (EMSCRIPTEN)
  set(CMAKE_CXX_STANDARD 11)

  add_executable(basis_transcoder.js
    ../../transcoder/basisu_transcoder.cpp
    basis_wrappers.cpp
  )

  target_include_directories(basis_transcoder.js PRIVATE ../../transcoder)

  set_target_properties(basis_transcoder.js PROPERTIES
      OUTPUT_NAME "basis_transcoder"
      SUFFIX ".js"
      LINK_FLAGS "--bind -s ALLOW_MEMORY_GROWTH=1 -O3 -s ASSERTIONS=0 -s MALLOC=emmalloc -DNDEBUG -DBASISD_SUPPORT_BC7=0")
endif()
