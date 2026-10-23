cmake_minimum_required(VERSION 3.5)

project(basisu_transcoder_js)

# KTX2 support (and Zstandard) in the transcoder is purely optional. Use "cmake -DKTX2=FALSE" to completely disable KTX2 support (which also disables Zstandard).
# If this is FALSE you'll only be able to transcode .basis files.
option(KTX2 "KTX2" TRUE)

# If KTX2 is enabled, Zstandard is needed to support decompressing supercompressed UASTC files.
# If you know you'll never be transcoding KTX2 files using this feature you can set KTX2_ZSTANDARD to 0.
option(KTX2_ZSTANDARD "KTX2_ZSTANDARD" TRUE)

# Use "cmake -DNO_DYNAMIC_EXECUTION=TRUE" to build JavaScript glue that never calls eval() or new Function().
# This lets the module load under a Content-Security-Policy without 'unsafe-eval'.
option(NO_DYNAMIC_EXECUTION "NO_DYNAMIC_EXECUTION" FALSE)

# With NO_DYNAMIC_EXECUTION, generate the embind call wrappers at build time (fast) instead of
# using the slower generic wrappers. Only has an effect when NO_DYNAMIC_EXECUTION is TRUE.
option(EMBIND_AOT "EMBIND_AOT" TRUE)

message("KTX2=${KTX2}")
message("KTX2_ZSTANDARD=${KTX2_ZSTANDARD}")
message("NO_DYNAMIC_EXECUTION=${NO_DYNAMIC_EXECUTION}")
message("EMBIND_AOT=${EMBIND_AOT}")

if (EMSCRIPTEN)
  set(CMAKE_CXX_STANDARD 17)

  set(SRC_LIST
  	../../transcoder/basisu_transcoder.cpp
	basis_wrappers.cpp
  )

  if (KTX2)
	set(KTX2_DEFINITION BASISD_SUPPORT_KTX2=1)
  else()
  	set(KTX2_DEFINITION BASISD_SUPPORT_KTX2=0)
  endif()

  if (KTX2_ZSTANDARD)
 	set(SRC_LIST ${SRC_LIST}
		../../zstd/zstddeclib.c
	)
	set(ZSTD_DEFINITION BASISD_SUPPORT_KTX2_ZSTD=1)
  else()
  	set(ZSTD_DEFINITION BASISD_SUPPORT_KTX2_ZSTD=0)
  endif()

#  message("KTX2_DEFINITION=${KTX2_DEFINITION})
#  message("ZSTD_DEFINITION=${ZSTD_DEFINITION})

  add_executable(basis_transcoder.js ${SRC_LIST})

  target_compile_definitions(basis_transcoder.js PRIVATE NDEBUG
  	BASISD_SUPPORT_UASTC_HDR=1
	BASISD_SUPPORT_UASTC=1
	BASISD_SUPPORT_BC7=1
	BASISD_SUPPORT_ATC=0
	BASISD_SUPPORT_ASTC_HIGHER_OPAQUE_QUALITY=0
	BASISD_SUPPORT_PVRTC2=0
	BASISD_SUPPORT_FXT1=0
	BASISD_SUPPORT_ETC2_EAC_RG11=0
	BASISU_SUPPORT_ENCODING=0
	BASISD_ENABLE_DEBUG_FLAGS=1
	${KTX2_DEFINITION}
	${ZSTD_DEFINITION}
  )

  target_compile_options(basis_transcoder.js PRIVATE -O3 -fno-strict-aliasing)
  #target_compile_options(basis_transcoder.js PRIVATE -O0 -g -fno-strict-aliasing)

  target_include_directories(basis_transcoder.js PRIVATE ../../transcoder)

  set(TRANSCODER_LINK_FLAGS "--bind -s ALLOW_MEMORY_GROWTH=1 -O3 -s MALLOC=emmalloc -s MODULARIZE=1 -s EXPORT_NAME=BASIS -s ASSERTIONS=0 -s EXPORTED_RUNTIME_METHODS=['HEAP8']")

  if (NO_DYNAMIC_EXECUTION)
	set(TRANSCODER_LINK_FLAGS "${TRANSCODER_LINK_FLAGS} -s DYNAMIC_EXECUTION=0")
	if (EMBIND_AOT)
	  set(TRANSCODER_LINK_FLAGS "${TRANSCODER_LINK_FLAGS} -s EMBIND_AOT=1")
	endif()
  endif()

  set_target_properties(basis_transcoder.js PROPERTIES
      OUTPUT_NAME "basis_transcoder"
      SUFFIX ".js"
      LINK_FLAGS "${TRANSCODER_LINK_FLAGS}")
endif()
