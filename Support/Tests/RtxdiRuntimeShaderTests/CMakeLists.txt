
set(sources
    Shaders/RtxdiApplicationBridge/RAB_Buffers.glsli
    Shaders/RtxdiApplicationBridge/RAB_Buffers.hlsli
    Shaders/RtxdiApplicationBridge/RAB_LightInfo.hlsli
    Shaders/RtxdiApplicationBridge/RAB_LightSample.hlsli
    Shaders/RtxdiApplicationBridge/RAB_LightSampling.hlsli
    Shaders/RtxdiApplicationBridge/RAB_Material.hlsli
    Shaders/RtxdiApplicationBridge/RAB_NeighborSelection.hlsli
    Shaders/RtxdiApplicationBridge/RAB_RayPayload.hlsli
    Shaders/RtxdiApplicationBridge/RAB_RTShaders.hlsli
    Shaders/RtxdiApplicationBridge/RAB_SpatialHelpers.hlsli
    Shaders/RtxdiApplicationBridge/RAB_Surface.hlsli
    Shaders/RtxdiApplicationBridge/RAB_VisibilityTest.hlsli
    Shaders/RtxdiApplicationBridge/PathTracer/RAB_DenoiserCallbacks.hlsli
    Shaders/RtxdiApplicationBridge/PathTracer/RAB_MISCallbacks.hlsli
    Shaders/RtxdiApplicationBridge/PathTracer/RAB_PathTracer.hlsli
    Shaders/RtxdiApplicationBridge/PathTracer/RAB_PathTracerUserData.hlsli
    Shaders/RtxdiApplicationBridge/RtxdiApplicationBridge.glsli
    Shaders/RtxdiApplicationBridge/RtxdiApplicationBridge.hlsli)

# Organize MSVS filters (the little folders in the solution explorer) to match the folder structure
foreach(source IN LISTS sources)
    get_filename_component(source_path "${source}" PATH)
    string(REPLACE "/" "\\" source_path_msvc "${source_path}")
    source_group("${source_path_msvc}" FILES "${source}")
endforeach()

set(project RtxdiRuntimeShaderTests)
set(folder "RTXDI SDK Tests")

include(CMakeDependentOption)

add_library(${project} ${sources})
target_compile_definitions(${project} PRIVATE IS_CONSOLE_APP=1)

target_link_libraries(${project} Rtxdi cxxopts)
set_target_properties(${project} PROPERTIES FOLDER ${folder})

# ResamplingCompileTest.hlsl for DXIL target

if (DONUT_WITH_DX12)

	set(source_file "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/ResamplingCompileTest.hlsl")
	set(output_file "${CMAKE_CURRENT_BINARY_DIR}/ResamplingCompileTest.hlsl.dxil")

	add_custom_command(
		OUTPUT ${output_file}
		MAIN_DEPENDENCY ${source_file}
		DEPENDS ${shader_dependencies}
		COMMAND ${DXC_PATH} -nologo -WX -Tcs_6_5 ${source_file} -Fo ${output_file} -I${RTXDI_RUNTIME_INCLUDE_PATH} -enable-16bit-types
	)

	target_sources(${project} PRIVATE ${output_file})

endif()


# ResamplingCompileTest.hlsl for SPIR-V target

if (DONUT_WITH_VULKAN)

	set(source_file "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/ResamplingCompileTest.hlsl")
	set(output_file "${CMAKE_CURRENT_BINARY_DIR}/ResamplingCompileTest.hlsl.spv")

	add_custom_command(
		OUTPUT ${output_file}
		MAIN_DEPENDENCY ${source_file}
		DEPENDS ${shader_dependencies}
		COMMAND ${DXC_SPIRV_PATH} -nologo -WX -Tcs_6_5 -fspv-target-env=vulkan1.2 ${source_file} -Fo ${output_file} -I${RTXDI_RUNTIME_INCLUDE_PATH} -enable-16bit-types
	)

	target_sources(${project} PRIVATE ${output_file})

endif()


# ResamplingCompileTest.glsl - optional

if (NOT ${GLSLANG_PATH} STREQUAL "")
	if (EXISTS ${GLSLANG_PATH})	

		set(source_file "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/ResamplingCompileTest.glsl")
		set(output_file "${CMAKE_CURRENT_BINARY_DIR}/ResamplingCompileTest.glsl.spv")

		add_custom_command(
			OUTPUT ${output_file}
			MAIN_DEPENDENCY ${source_file}
			DEPENDS ${shader_dependencies}
			COMMAND ${GLSLANG_PATH} --target-env vulkan1.2 --quiet -S comp ${source_file} -o ${output_file} -I${RTXDI_RUNTIME_INCLUDE_PATH}
		)

		target_sources(${project} PRIVATE ${output_file})

	else()
		message(WARNING "The GLSLANG_PATH variable points to a non-existent file: ${GLSLANG_PATH}")
	endif()
endif()
