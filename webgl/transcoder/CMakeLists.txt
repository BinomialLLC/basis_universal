cmake_minimum_required(VERSION 3.0)

project(basisu_transcoder_js)

# use release build if none specified
if( NOT CMAKE_BUILD_TYPE )
  set( CMAKE_BUILD_TYPE Release )
endif( NOT CMAKE_BUILD_TYPE )

function(build_target name flags enabled)
  add_executable(${name}.js
    ../../transcoder/basisu_transcoder.cpp
    basis_wrappers.cpp
  )

  set(allTypes ASTC ATC BC7 DXT1 DXT5A ETC2_EAC_A8 ETC2_EAC_RG11 FXT1 PVRTC1 PVRTC2)
  set(typeList "")
  
  foreach(type ${allTypes})
    string(FIND ${enabled} ${type} found)
    if(${found} EQUAL -1)
      list(APPEND typeList "BASISD_SUPPORT_${type}=0")
    else()
      list(APPEND typeList "BASISD_SUPPORT_${type}=1")
    endif()
  endforeach()

  target_compile_definitions(${name}.js PRIVATE NDEBUG ${typeList})
  target_compile_options(${name}.js PRIVATE -O3)
  target_include_directories(${name}.js PRIVATE ../../transcoder)

  set_target_properties(${name}.js PROPERTIES
      OUTPUT_NAME "${name}"
      SUFFIX ".js"
      LINK_FLAGS "${flags} --bind -s ALLOW_MEMORY_GROWTH=1 -O3 -s ASSERTIONS=0 -s MALLOC=emmalloc -s MODULARIZE=1 -s EXPORT_NAME=BASIS"
      COMPILE_FLAGS ${flags})
endfunction()

function(build name enabled)
  build_target("${name}.wasm" "-s WASM=1" ${enabled})
  build_target(${name} "-s WASM=0 -s SINGLE_FILE=1" ${enabled})
endFunction()

if (EMSCRIPTEN)
  set(CMAKE_CXX_STANDARD 11)

  build(basis         "ASTC|ATC|DXT1|DXT5A|ETC2_EAC_A8|ETC2_EAC_RG11|PVRTC1|PVRTC2")
  #build(basist_all    "ASTC|ATC|BC7|DXT1|DXT5A|ETC2_EAC_A8|ETC2_EAC_RG11|FXT1|PVRTC1|PVRTC2")
  #build(basist_atc    "ATC|DXT5A")
  #build(basist_astc   "ASTC")
  #build(basist_bc7    "BC7")
  #build(basist_dxt    "DXT1|DXT5A")
  #build(basist_etc1   " ")
  #build(basist_etc2   "ETC2_EAC_A8|ETC2_EAC_RG11")
  #build(basist_fxt1   "FXT1|DXT1")
  #build(basist_pvr    "PVRTC1|PVRTC2|ATC|DXT5A")
endif()
