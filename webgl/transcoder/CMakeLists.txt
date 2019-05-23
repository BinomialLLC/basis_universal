cmake_minimum_required(VERSION 3.0)
project(basisu_transcoder_js)

#find_package(Emscripten)

if(EMSCRIPTEN)
	set(CMAKE_CXX_STANDARD 11)
	
	add_executable(basis_transcoder.js
			../../basisu/transcoder/basisu_transcoder.cpp
			basis_wrappers.cpp
			)
	
	target_include_directories(basis_transcoder.js PRIVATE ../../basisu/transcoder)
	
	set_target_properties(basis_transcoder.js PROPERTIES
			OUTPUT_NAME "basis_transcoder"
			SUFFIX ".js"
			LINK_FLAGS "--bind -s ALLOW_MEMORY_GROWTH=1 -O3 -s ASSERTIONS=0 -s MALLOC=emmalloc -DNDEBUG -DBASISD_SUPPORT_BC7=0"
			)
endif()
