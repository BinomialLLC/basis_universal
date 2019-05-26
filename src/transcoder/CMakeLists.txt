add_library(basisu_transcoder "")

target_sources(basisu_transcoder
		PRIVATE
		basisu.h
		basisu_file_headers.h
		basisu_global_selector_palette.h
		basisu_transcoder.h
		basisu_transcoder_internal.h
		basisu_transcoder.cpp
		)

target_compile_features(basisu_transcoder PUBLIC cxx_std_11)
set_target_properties(basisu_transcoder PROPERTIES
		POSITION_INDEPENDENT_CODE ON
		FOLDER "basisu"
        )

target_include_directories(basisu_transcoder
		PUBLIC
		# Helps the includes find what they need at runtime
		$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/transcoder>
		)

target_link_libraries(basisu_transcoder 
		PRIVATE
		Threads::Threads
		)

if(BASISU_INSTALL)
	install(DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/"
			DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/transcoder"
			FILES_MATCHING
			PATTERN "*.h"
			PATTERN "*.hpp"
			)
	install(TARGETS basisu_transcoder
			LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
			ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
			RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
			)
endif()
