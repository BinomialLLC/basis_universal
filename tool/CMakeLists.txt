set(BASISU_TOOL_HEADER_FILES
		)
set(BASISU_TOOL_SOURCE_FILES
		basisu_tool.cpp
		)
set(BASISU_TOOL_SOURCES
		${BASISU_TOOL_HEADER_FILES}
		${BASISU_TOOL_SOURCE_FILES}
        )
        
add_executable(basisuTool ${BASISU_TOOL_SOURCES})
add_dependencies(basisuTool basisu)

target_compile_features(basisuTool PUBLIC cxx_std_11)
set_target_properties(basisuTool PROPERTIES
		POSITION_INDEPENDENT_CODE ON
		FOLDER "basisu"
		)

target_link_libraries(basisuTool PRIVATE basisu)

if(BASISU_INSTALL_TOOL)
	install(TARGETS basisuTool
			RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
			ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
			)
endif()