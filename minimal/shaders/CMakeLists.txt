
include("${DONUT_PATH}/compileshaders.cmake")
file(GLOB shaders "*.hlsl" "*.hlsli" "*.h" "../rtxdi-sdk/shaders/*.hlsl" "../rtxdi-sdk/include/*.hlsli")

set(project minimal-sample)
set(folder "RTXDI SDK")
set(shaders_target minimal-sample-shaders)

add_custom_target(${shaders_target}
	DEPENDS shaderCompiler
	SOURCES ${shaders} Shaders.cfg)

set (OUTPUT_PATH_BASE "${CMAKE_BINARY_DIR}/bin/shaders/minimal-sample")

 if (WIN32)
     set (USE_API_OPTION --useAPI)
 else()
     set (USE_API_OPTION "")
 endif()
 
if (DONUT_WITH_DX12)
   set(DX12_COMPILER_OPTIONS 
      --platform DXIL
      --shaderModel 6_5
      --blob
      --outputExt .bin
      -I ${DONUT_SHADER_INCLUDE_DIR}
      -I ${CMAKE_CURRENT_SOURCE_DIR}/../../rtxdi-sdk/include
      ${USE_API_OPTION}
      --relaxedInclude "../Types.h"
      --compiler ${DXC_DXIL_EXECUTABLE})

   add_custom_command(TARGET ${shaders_target} PRE_BUILD
                     COMMAND ShaderMake
                              --config ${CMAKE_CURRENT_SOURCE_DIR}/Shaders.cfg
                              --out ${OUTPUT_PATH_BASE}/dxil
                              ${DX12_COMPILER_OPTIONS})
endif()

if (DONUT_WITH_VULKAN)
   set(VULKAN_COMPILER_OPTIONS
      --platform SPIRV
      --shaderModel 6_5
      --vulkanVersion 1.2
      --blob
      --outputExt .bin
      -I ${DONUT_SHADER_INCLUDE_DIR}
      -I ${CMAKE_CURRENT_SOURCE_DIR}/../../rtxdi-sdk/include
      ${USE_API_OPTION}
      --relaxedInclude "../Types.h"
      -D SPIRV
      ${NVRHI_DEFAULT_VK_REGISTER_OFFSETS}
      --compiler ${DXC_SPIRV_EXECUTABLE})

   add_custom_command(TARGET ${shaders_target} PRE_BUILD
                     COMMAND ShaderMake
                              --config ${CMAKE_CURRENT_SOURCE_DIR}/Shaders.cfg
                              --out ${OUTPUT_PATH_BASE}/spirv
                              ${VULKAN_COMPILER_OPTIONS})
endif()

set_target_properties(${shaders_target} PROPERTIES FOLDER ${folder})
set_source_files_properties(${shaders} PROPERTIES VS_TOOL_OVERRIDE "None") 
