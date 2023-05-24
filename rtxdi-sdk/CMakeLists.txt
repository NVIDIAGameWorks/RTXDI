
file(GLOB sources "src/*.cpp" "include/rtxdi/*")

add_library(rtxdi-sdk STATIC EXCLUDE_FROM_ALL ${sources})
target_include_directories(rtxdi-sdk PUBLIC include)
set_target_properties(rtxdi-sdk PROPERTIES FOLDER "RTXDI SDK")

# Dependencies for the resampling compile tests
file(GLOB shader_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/include/rtxdi/*")

cmake_dependent_option(RTXDI_SKIP_SHADER_VALIDATION "Skip shader compilation when building just the rtxdi-sdk project" OFF WIN32 OFF)

if (NOT RTXDI_SKIP_SHADER_VALIDATION)

# ResamplingCompileTest.hlsl for DXIL target

if (DONUT_WITH_DX12)

	set(source_file "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ResamplingCompileTest.hlsl")
	set(output_file "${CMAKE_CURRENT_BINARY_DIR}/ResamplingCompileTest.hlsl.dxil")

	add_custom_command(
		OUTPUT ${output_file}
		MAIN_DEPENDENCY ${source_file}
		DEPENDS ${shader_dependencies}
		COMMAND ${DXC_PATH} -nologo -WX -Tcs_6_5 ${source_file} -Fo ${output_file} -I${CMAKE_CURRENT_SOURCE_DIR}/include
	)

	target_sources(rtxdi-sdk PRIVATE ${output_file})

endif()


# ResamplingCompileTest.hlsl for SPIR-V target

if (DONUT_WITH_VULKAN)

	set(source_file "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ResamplingCompileTest.hlsl")
	set(output_file "${CMAKE_CURRENT_BINARY_DIR}/ResamplingCompileTest.hlsl.spv")

	add_custom_command(
		OUTPUT ${output_file}
		MAIN_DEPENDENCY ${source_file}
		DEPENDS ${shader_dependencies}
		COMMAND ${DXC_SPIRV_PATH} -nologo -WX -Tcs_6_5 -fspv-target-env=vulkan1.2 ${source_file} -Fo ${output_file} -I${CMAKE_CURRENT_SOURCE_DIR}/include
	)

	target_sources(rtxdi-sdk PRIVATE ${output_file})

endif()


# ResamplingCompileTest.glsl - optional

if (NOT ${GLSLANG_PATH} STREQUAL "")
	if (EXISTS ${GLSLANG_PATH})	

		set(source_file "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ResamplingCompileTest.glsl")
		set(output_file "${CMAKE_CURRENT_BINARY_DIR}/ResamplingCompileTest.glsl.spv")

		add_custom_command(
			OUTPUT ${output_file}
			MAIN_DEPENDENCY ${source_file}
			DEPENDS ${shader_dependencies}
			COMMAND ${GLSLANG_PATH} --target-env vulkan1.2 --quiet -S comp ${source_file} -o ${output_file} -I${CMAKE_CURRENT_SOURCE_DIR}/include
		)

		target_sources(rtxdi-sdk PRIVATE ${output_file})

	else()
		message(WARNING "The GLSLANG_PATH variable points to a non-existent file: ${GLSLANG_PATH}")
	endif()
endif()

endif()
