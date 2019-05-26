add_subdirectory(transcoder)
add_subdirectory(encoder)

if(BASISU_BUILD_TOOL)
	add_subdirectory(tool)
	#add_subdirectory(webgl/transcoder)
endif()