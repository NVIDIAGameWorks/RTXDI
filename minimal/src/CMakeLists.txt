
file(GLOB sources "*.cpp" "*.h")

set(project minimal-sample)
set(folder "RTXDI SDK")

include(CMakeDependentOption)

add_executable(${project} WIN32 ${sources})

target_link_libraries(${project} donut_core donut_engine donut_app donut_render rtxdi-sdk cxxopts)
add_dependencies(${project} minimal-sample-shaders)
set_target_properties(${project} PROPERTIES 
	FOLDER ${folder}
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
	RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
	RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/bin"
	RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
	RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin")
