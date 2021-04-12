cmake_minimum_required(VERSION 3.0)

project(basisu_transcoder_js)

# KTX2 support (and Zstandard) in the transcoder is purely optional. Use "cmake -DKTX2=FALSE" to completely disable KTX2 support (which also disables Zstandard).
# If this is FALSE you'll only be able to transcode .basis files.
option(KTX2 "KTX2" TRUE)

# If KTX2 is enabled, Zstandard is needed to support decompressing supercompressed UASTC files. 
# If you know you'll never be transcoding KTX2 files using this feature you can set KTX2_ZSTANDARD to 0.
option(KTX2_ZSTANDARD "KTX2_ZSTANDARD" TRUE)

message("KTX2=${KTX2}")
message("KTX2_ZSTANDARD=${KTX2_ZSTANDARD}")

if (EMSCRIPTEN)
  set(CMAKE_CXX_STANDARD 11)
  
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

  target_compile_definitions(basis_transcoder.js PRIVATE NDEBUG BASISD_SUPPORT_UASTC=1 BASISD_SUPPORT_BC7=1 BASISD_SUPPORT_ATC=0 BASISD_SUPPORT_ASTC_HIGHER_OPAQUE_QUALITY=0 BASISD_SUPPORT_PVRTC2=0 BASISD_SUPPORT_FXT1=0 BASISD_SUPPORT_ETC2_EAC_RG11=0 BASISU_SUPPORT_ENCODING=0 ${KTX2_DEFINITION} ${ZSTD_DEFINITION} )
  target_compile_options(basis_transcoder.js PRIVATE -O3 -fno-strict-aliasing)
  target_include_directories(basis_transcoder.js PRIVATE ../../transcoder)

  set_target_properties(basis_transcoder.js PROPERTIES
      OUTPUT_NAME "basis_transcoder"
      SUFFIX ".js"
      LINK_FLAGS "--bind -s ALLOW_MEMORY_GROWTH=1 -O3 -s ASSERTIONS=0 -s MALLOC=emmalloc -s MODULARIZE=1 -s EXPORT_NAME=BASIS ")
endif()
