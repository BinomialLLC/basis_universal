add_executable(basisu_tool ${BASISU_TOOL_SOURCES})

target_sources(basisu_tool
		PRIVATE
		basisu_tool.cpp)
		
target_include_directories(basisu_tool
		PUBLIC
		# Helps the includes find what they need at build-time
		$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/..>
		# Helps the includes find what they need at runtime
		$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/encoder>
		$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/>
		)

target_compile_features(basisu_tool PUBLIC cxx_std_11)
set_target_properties(basisu_tool PROPERTIES
		POSITION_INDEPENDENT_CODE ON
		FOLDER "basisu"
		)

target_link_libraries(basisu_tool PRIVATE basisu_encoder)

if(BASISU_INSTALL_TOOL)
	install(TARGETS basisu_tool
			RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
			ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
			)
endif()