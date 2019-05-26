add_library(basisu_encoder "")

target_sources(basisu_encoder
		PRIVATE
		basisu_backend.h
		basisu_basis_file.h
		basisu_comp.h
		basisu_enc.h
		basisu_etc.h
		basisu_frontend.h
		basisu_global_selector_palette_helpers.h
		basisu_gpu_texture.h
		basisu_pvrtc1_4.h
		basisu_resampler.h
		basisu_ssim.h
		basisu_backend.cpp
		basisu_basis_file.cpp
		basisu_comp.cpp
		basisu_enc.cpp
		basisu_etc.cpp
		basisu_frontend.cpp
		basisu_global_selector_palette_helpers.cpp
		basisu_gpu_texture.cpp
		basisu_pvrtc1_4.cpp
		basisu_resampler.cpp
		basisu_resample_filters.cpp
		basisu_ssim.cpp
		lodepng.cpp
		)

target_compile_features(basisu_encoder PUBLIC cxx_std_11)
set_target_properties(basisu_encoder PROPERTIES
		POSITION_INDEPENDENT_CODE ON
		FOLDER "basisu"
        )

target_include_directories(basisu_encoder
		PUBLIC
		# Helps the includes find what they need at build-time
		$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/..>
		# Helps the includes find what they need at runtime
		$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/encoder>
		$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/>
		)

target_link_libraries(basisu_encoder 
		PUBLIC
		basisu_transcoder
		PRIVATE
		Threads::Threads
		)

if(BASISU_INSTALL)
	install(DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/"
			DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/encoder"
			FILES_MATCHING
			PATTERN "*.h"
			PATTERN "*.hpp"
			)
	install(TARGETS basisu_encoder
			LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
			ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
			RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
			)
endif()
