
include("${DONUT_PATH}/compileshaders.cmake")
file(GLOB shaders "*.h*" "LightingPasses/*.h*" "DebugViz/*.h*" "../rtxdi-runtime/shaders/*.hlsl" "../rtxdi-runtime/include/*.hlsli")

set(project rtxdi-sample)
set(folder "RTXDI SDK")
set(shaders_target rtxdi-sample-shaders)

add_custom_target(${shaders_target}
    DEPENDS ShaderMake
    SOURCES ${shaders} Shaders.cfg)

if(TARGET NRD)
   set(NRD_OPTIONS -I "${CMAKE_CURRENT_SOURCE_DIR}/../NRD/Shaders/Include" -D WITH_NRD -D NRD_USE_OCT_NORMAL_ENCODING=0 -D NRD_USE_MATERIAL_ID=0)
else()
   set(NRD_OPTIONS --relaxedInclude NRD.hlsli)
endif()

set (OUTPUT_PATH_BASE "${CMAKE_BINARY_DIR}/bin/shaders/rtxdi-sample")

 if (WIN32)
     set (USE_API_OPTION --useAPI)
 else()
     set (USE_API_OPTION "")
 endif()

if (DONUT_WITH_DX12)
   set(DX12_COMPILER_OPTIONS 
      --platform DXIL
      --shaderModel 6_5
      --binaryBlob
      --outputExt .bin
      -I ${DONUT_SHADER_INCLUDE_DIR}
      -I ${CMAKE_CURRENT_SOURCE_DIR}/../rtxdi-runtime/include
      --relaxedInclude "../Types.h"
      ${NRD_OPTIONS}
      ${USE_API_OPTION}
      --compiler ${DXC_PATH})

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
      --binaryBlob
      --outputExt .bin
      -I ${DONUT_SHADER_INCLUDE_DIR}
      -I ${CMAKE_CURRENT_SOURCE_DIR}/../rtxdi-runtime/include
      --relaxedInclude "../Types.h"
      ${NRD_OPTIONS}
      ${USE_API_OPTION}
      ${NVRHI_DEFAULT_VK_REGISTER_OFFSETS}
      -D SPIRV
      --compiler ${DXC_SPIRV_PATH})

   add_custom_command(TARGET ${shaders_target} PRE_BUILD
                     COMMAND ShaderMake
                              --config ${CMAKE_CURRENT_SOURCE_DIR}/Shaders.cfg
                              --out ${OUTPUT_PATH_BASE}/spirv
                              ${VULKAN_COMPILER_OPTIONS})
endif()

set_target_properties(${shaders_target} PROPERTIES FOLDER ${folder})
set_source_files_properties(${shaders} PROPERTIES VS_TOOL_OVERRIDE "None") 
