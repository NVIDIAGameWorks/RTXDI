
file(GLOB sources "*.cpp" "*.h" "*/*.cpp" "*/*.h")

set(project rtxdi-runtime-shader-tests)
set(folder "RTXDI SDK")

include(CMakeDependentOption)

add_library(${project} ${sources})
target_compile_definitions(${project} PRIVATE IS_CONSOLE_APP=1)

target_link_libraries(${project} rtxdi-runtime cxxopts)
set_target_properties(${project} PROPERTIES FOLDER ${folder})

# ResamplingCompileTest.hlsl for DXIL target

if (DONUT_WITH_DX12)

	set(source_file "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ResamplingCompileTest.hlsl")
	set(output_file "${CMAKE_CURRENT_BINARY_DIR}/ResamplingCompileTest.hlsl.dxil")

	add_custom_command(
		OUTPUT ${output_file}
		MAIN_DEPENDENCY ${source_file}
		DEPENDS ${shader_dependencies}
		COMMAND ${DXC_PATH} -nologo -WX -Tcs_6_5 ${source_file} -Fo ${output_file} -I${RTXDI_RUNTIME_INCLUDE_PATH}
	)

	target_sources(rtxdi-runtime-shader-tests PRIVATE ${output_file})

endif()


# ResamplingCompileTest.hlsl for SPIR-V target

if (DONUT_WITH_VULKAN)

	set(source_file "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ResamplingCompileTest.hlsl")
	set(output_file "${CMAKE_CURRENT_BINARY_DIR}/ResamplingCompileTest.hlsl.spv")

	add_custom_command(
		OUTPUT ${output_file}
		MAIN_DEPENDENCY ${source_file}
		DEPENDS ${shader_dependencies}
		COMMAND ${DXC_SPIRV_PATH} -nologo -WX -Tcs_6_5 -fspv-target-env=vulkan1.2 ${source_file} -Fo ${output_file} -I${RTXDI_RUNTIME_INCLUDE_PATH}
	)

	target_sources(rtxdi-runtime-shader-tests PRIVATE ${output_file})

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
			COMMAND ${GLSLANG_PATH} --target-env vulkan1.2 --quiet -S comp ${source_file} -o ${output_file} -I${RTXDI_RUNTIME_INCLUDE_PATH}
		)

		target_sources(rtxdi-runtime-shader-tests PRIVATE ${output_file})

	else()
		message(WARNING "The GLSLANG_PATH variable points to a non-existent file: ${GLSLANG_PATH}")
	endif()
endif()
