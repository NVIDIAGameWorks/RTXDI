
file(GLOB sources "*.cpp" "*.h")

set(project rtxdi-sample)
set(folder "RTXDI SDK")

add_executable(${project} WIN32 ${sources})
target_link_libraries(${project} donut_core donut_engine donut_app donut_render rtxdi-sdk)
add_dependencies(${project} rtxdi-sample-shaders)
set_target_properties(${project} PROPERTIES 
	FOLDER ${folder}
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
	RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/bin"
	RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_SOURCE_DIR}/bin"
	RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/bin"
	RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/bin")


if (NRD_INCLUDE_DIR AND NRD_LIB_DIR)
	target_compile_definitions(${project} PRIVATE WITH_NRD=1)
	target_include_directories(${project} PRIVATE ${NRD_INCLUDE_DIR})
	if (WIN32)
		target_link_libraries(${project} "${NRD_LIB_DIR}/NRD.lib")

		add_custom_command(TARGET ${project} POST_BUILD
	    	COMMAND ${CMAKE_COMMAND} -E copy_if_different
	        "${NRD_LIB_DIR}/NRD.dll"
	        $<TARGET_FILE_DIR:${project}>)
	else()
		target_link_libraries(${project} "${NRD_LIB_DIR}/libNRD.so")
	endif()
endif()
