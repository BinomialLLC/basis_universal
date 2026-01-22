cmake_minimum_required(VERSION 3.5)
project(basisu_encoder_js)

# Toggle Zstd support for KTX2 (ON by default).
option(KTX2_ZSTANDARD "Enable KTX2 Zstandard support" TRUE)
message("KTX2_ZSTANDARD=${KTX2_ZSTANDARD}")

# Only for Emscripten builds.
if(EMSCRIPTEN)
  set(CMAKE_CXX_STANDARD 17)

  # ---------- Pick config once (single-config generators) ----------
  # Supports: Release (default), Debug, SAN
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
  endif()
  string(TOUPPER "${CMAKE_BUILD_TYPE}" BUILD_MODE)

  # Per-config compile/link flags
  set(CONFIG_CFLAGS "")
  set(CONFIG_DEFS  "")
  set(CONFIG_LINK  "")

  if(BUILD_MODE STREQUAL "RELEASE")
    set(CONFIG_CFLAGS "-O3")
    set(CONFIG_DEFS   "NDEBUG")
    set(CONFIG_LINK   "-O3 -s ASSERTIONS=0")
  elseif(BUILD_MODE STREQUAL "DEBUG")
    set(CONFIG_CFLAGS "-g -O0")
    set(CONFIG_DEFS   "DEBUG")
    set(CONFIG_LINK   "-g -s ASSERTIONS=2")
  elseif(BUILD_MODE STREQUAL "SAN")
    set(CONFIG_CFLAGS "-g -O1 -fsanitize=undefined -fsanitize=address")
    set(CONFIG_DEFS   "DEBUG")
    set(CONFIG_LINK   "-g -s ASSERTIONS=2 -fsanitize=undefined -fsanitize=address")
  else()
    message(WARNING "Unknown CMAKE_BUILD_TYPE='${CMAKE_BUILD_TYPE}', defaulting to Release-like flags.")
    set(CONFIG_CFLAGS "-O3")
    set(CONFIG_DEFS   "NDEBUG")
    set(CONFIG_LINK   "-O3 -s ASSERTIONS=0")
  endif()

  # ---------- Sources (shared) ----------
  set(SRC_LIST
    ../transcoder/basis_wrappers.cpp
    ../../transcoder/basisu_transcoder.cpp
    ../../encoder/basisu_backend.cpp
    ../../encoder/basisu_basis_file.cpp
    ../../encoder/basisu_comp.cpp
    ../../encoder/basisu_enc.cpp
    ../../encoder/basisu_etc.cpp
    ../../encoder/basisu_frontend.cpp
    ../../encoder/basisu_gpu_texture.cpp
    ../../encoder/basisu_pvrtc1_4.cpp
    ../../encoder/basisu_resampler.cpp
    ../../encoder/basisu_resample_filters.cpp
    ../../encoder/basisu_ssim.cpp
    ../../encoder/basisu_uastc_enc.cpp
    ../../encoder/basisu_bc7enc.cpp
    ../../encoder/basisu_kernels_sse.cpp
    ../../encoder/basisu_opencl.cpp
    ../../encoder/pvpngreader.cpp
    ../../encoder/jpgd.cpp
    ../../encoder/3rdparty/android_astc_decomp.cpp
    ../../encoder/basisu_uastc_hdr_4x4_enc.cpp
    ../../encoder/basisu_astc_hdr_6x6_enc.cpp
    ../../encoder/basisu_astc_hdr_common.cpp
    ../../encoder/basisu_astc_ldr_common.cpp
    ../../encoder/basisu_astc_ldr_encode.cpp
    ../../encoder/3rdparty/tinyexr.cpp
  )
  if(KTX2_ZSTANDARD)
    list(APPEND SRC_LIST ../../zstd/zstd.c)
    set(ZSTD_DEFINITION BASISD_SUPPORT_KTX2_ZSTD=1)
  else()
    set(ZSTD_DEFINITION BASISD_SUPPORT_KTX2_ZSTD=0)
  endif()

  # Common preprocessor defines (same as your original)
  set(COMMON_DEFS
    BASISD_SUPPORT_UASTC=1
    BASISD_SUPPORT_BC7=1
    BASISD_SUPPORT_ATC=0
    BASISD_SUPPORT_ASTC_HIGHER_OPAQUE_QUALITY=0
    BASISD_SUPPORT_PVRTC2=0
    BASISD_SUPPORT_FXT1=0
    BASISD_SUPPORT_ETC2_EAC_RG11=0
    BASISU_SUPPORT_ENCODING=1
    BASISU_SUPPORT_SSE=0
    BASISD_SUPPORT_XUASTC=1
    ${ZSTD_DEFINITION}
  )

  # Base link flags 
  set(LINK_BASE "--bind -s ALLOW_MEMORY_GROWTH=1 -s INITIAL_MEMORY=536870912 -s STACK_SIZE=2097152 -s MODULARIZE=1 -s EXPORT_NAME=BASIS -s EXPORTED_RUNTIME_METHODS=['HEAP8']")
  set(LINK_THREADS "-s USE_PTHREADS=1 -s PTHREAD_POOL_SIZE=18 -s ENVIRONMENT=web,worker")
  set(LINK_WASM64  "-s MEMORY64=1 -sWASM_BIGINT=1 --profiling-funcs")

  # Helper to avoid repetition
  function(add_encoder target out_name use_threads use_wasm64)
    add_executable(${target} ${SRC_LIST})
    set_target_properties(${target} PROPERTIES OUTPUT_NAME "${out_name}" SUFFIX ".js")
    target_include_directories(${target} PRIVATE ../../transcoder)

    # Compile defs and options
    target_compile_definitions(${target} PRIVATE ${COMMON_DEFS} ${CONFIG_DEFS})
    target_compile_options(${target} PRIVATE -fno-strict-aliasing ${CONFIG_CFLAGS})

    if(${use_threads})
      target_compile_options(${target} PRIVATE -matomics -mbulk-memory)
      target_compile_definitions(${target} PRIVATE WASM_THREADS_ENABLED=1)
    endif()
	
    if(${use_wasm64})
      target_compile_options(${target} PRIVATE -s MEMORY64=1)
    endif()

    # Link flags (no generator expressions)
    set(_lf "${LINK_BASE} ${CONFIG_LINK}")
	
    if(${use_threads})
      set(_lf "${_lf} ${LINK_THREADS}")
    endif()
	
    if(${use_wasm64})
      set(_lf "${_lf} ${LINK_WASM64} -s INITIAL_MEMORY=1073741824 -s MAXIMUM_MEMORY=12884901888")
    endif()
	
    set_target_properties(${target} PROPERTIES LINK_FLAGS "${_lf}")
  endfunction()

  # The three outputs (names unchanged)
  add_encoder(basis_encoder.js                 "basis_encoder"                OFF OFF) # wasm32
  add_encoder(basis_encoder_threads.js         "basis_encoder_threads"        ON  OFF) # wasm32 + threads
  add_encoder(basis_encoder_threads_wasm64.js  "basis_encoder_threads_wasm64" ON  ON ) # wasm64 + threads
endif()
