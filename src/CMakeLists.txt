
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


if (TARGET NRD)
	target_compile_definitions(${project} PRIVATE WITH_NRD=1)
	target_link_libraries(${project} NRD)
endif()
