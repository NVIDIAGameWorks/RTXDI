
file(GLOB sources "*.cpp" "*.h" "*/*.cpp" "*/*.h")

set(project rtxdi-sample)
set(folder "RTXDI SDK")

include(CMakeDependentOption)

cmake_dependent_option(RTXDI_CONSOLE_APP "Build the sample as a console application" OFF WIN32 OFF)

if (RTXDI_CONSOLE_APP)
	add_executable(${project} ${sources})
	target_compile_definitions(${project} PRIVATE IS_CONSOLE_APP=1)
else()
	add_executable(${project} WIN32 ${sources})
endif()

target_link_libraries(${project} donut_core donut_engine donut_app donut_render rtxdi-sdk cxxopts)
add_dependencies(${project} rtxdi-sample-shaders)
set_target_properties(${project} PROPERTIES FOLDER ${folder})

if (TARGET NRD)
	target_compile_definitions(${project} PRIVATE WITH_NRD=1)
	target_link_libraries(${project} NRD)
endif()

if (TARGET DLSS)
	target_compile_definitions(${project} PRIVATE WITH_DLSS=1)
	target_link_libraries(${project} DLSS)
	add_custom_command(TARGET ${project} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${DLSS_SHARED_LIBRARY_PATH}"
		"$<TARGET_FILE_DIR:${project}>/")
endif()
