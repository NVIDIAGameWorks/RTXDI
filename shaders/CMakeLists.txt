
include("${DONUT_PATH}/compileshaders.cmake")
file(GLOB shaders "*.hlsl" "*.hlsli" "*.h" "../rtxdi-sdk/shaders/*.hlsl" "../rtxdi-sdk/include/*.hlsli")

set(project rtxdi-sample)
set(folder "RTXDI SDK")
set(shaders_target rtxdi-sample-shaders)

add_custom_target(${shaders_target}
	DEPENDS shaderCompiler
	SOURCES ${shaders})

if(NRD_SHADERS_DIR)
   set(NRD_OPTIONS -I ${NRD_SHADERS_DIR} -D WITH_NRD)
else()
   set(NRD_OPTIONS --ignore NRD.hlsl)
endif()

if (DONUT_WITH_DX12)
   add_custom_command(TARGET ${shaders_target} PRE_BUILD
                     COMMAND shaderCompiler
                              --infile ${CMAKE_CURRENT_SOURCE_DIR}/Shaders-D3D.cfg
                              --parallel
                              --out ${CMAKE_SOURCE_DIR}/bin/shaders/rtxdi-sample/dxil
                              --platform dxil
                              -I ${DONUT_SHADER_INCLUDE_DIR}
                              -I ${CMAKE_CURRENT_SOURCE_DIR}/../rtxdi-sdk/include
                              ${NRD_OPTIONS}
                              --compiler ${DXC_DXIL_EXECUTABLE})
endif()

if (DONUT_WITH_VULKAN)
   add_custom_command(TARGET ${shaders_target} PRE_BUILD
                     COMMAND shaderCompiler
                              --infile ${CMAKE_CURRENT_SOURCE_DIR}/Shaders-Vulkan.cfg
                              --parallel
                              --out ${CMAKE_SOURCE_DIR}/bin/shaders/rtxdi-sample/spirv
                              --platform spirv
                              -I ${DONUT_SHADER_INCLUDE_DIR}
                              -I ${CMAKE_CURRENT_SOURCE_DIR}/../rtxdi-sdk/include
                              ${NRD_OPTIONS}
                              --cflags "-fspv-target-env=vulkan1.2"
                              -D SPIRV
                              --compiler ${DXC_SPIRV_EXECUTABLE})
endif()

set_target_properties(${shaders_target} PROPERTIES FOLDER ${folder})
set_source_files_properties(${shaders} PROPERTIES VS_TOOL_OVERRIDE "None") 
