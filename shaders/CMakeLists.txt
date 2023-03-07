
include("${DONUT_PATH}/compileshaders.cmake")
file(GLOB shaders "*.h*" "LightingPasses/*.h*" "RTXGI/*.h*" "DebugViz/*.h*" "../rtxdi-sdk/shaders/*.hlsl" "../rtxdi-sdk/include/*.hlsli")

set(project rtxdi-sample)
set(folder "RTXDI SDK")
set(shaders_target rtxdi-sample-shaders)

add_custom_target(${shaders_target}
    DEPENDS shaderCompiler
    SOURCES ${shaders} Shaders.cfg)

if(TARGET NRD)
   set(NRD_OPTIONS -I "${CMAKE_CURRENT_SOURCE_DIR}/../NRD/Shaders/Include" -D WITH_NRD -D NRD_USE_OCT_NORMAL_ENCODING=0 -D NRD_USE_MATERIAL_ID=0)
else()
   set(NRD_OPTIONS --ignore NRD.hlsli)
endif()

if (TARGET RTXGI-D3D12 OR TARGET RTXGI-VK)
   set(RTXGI_OPTIONS -D WITH_RTXGI)
else()
   set(RTXGI_OPTIONS --ignore "ddgi/Irradiance.hlsl")
endif()

set (OUTPUT_PATH_BASE "${CMAKE_BINARY_DIR}/bin/shaders/rtxdi-sample")

if (DONUT_WITH_DX12)
   set(DX12_COMPILER_OPTIONS 
      --parallel
      --platform dxil
      -I ${DONUT_SHADER_INCLUDE_DIR}
      -I ${CMAKE_CURRENT_SOURCE_DIR}/../rtxdi-sdk/include
      -I ${CMAKE_CURRENT_SOURCE_DIR}/../RTXGI/shaders
      -I ${CMAKE_CURRENT_SOURCE_DIR}/RTXGI
      --ignore "../Types.h"
      --ignore "rtxgi\\Types.h"
      ${NRD_OPTIONS}
      ${RTXGI_OPTIONS}
      --cflags "$<IF:$<CONFIG:Debug>,-Zi -Qembed_debug,-Qstrip_debug -Qstrip_reflect> -O3 -WX -Zpr"
      --compiler ${DXC_DXIL_EXECUTABLE})

   add_custom_command(TARGET ${shaders_target} PRE_BUILD
                     COMMAND shaderCompiler
                              --infile ${CMAKE_CURRENT_SOURCE_DIR}/Shaders.cfg
                              --out ${OUTPUT_PATH_BASE}/dxil
                              ${DX12_COMPILER_OPTIONS})

   if (TARGET RTXGI-D3D12)
      add_custom_command(TARGET ${shaders_target} PRE_BUILD
                        COMMAND shaderCompiler
                                 --infile ${CMAKE_CURRENT_SOURCE_DIR}/RTXGI/RtxgiShaders.cfg
                                 --out ${OUTPUT_PATH_BASE}/dxil/RTXGI
                                 ${DX12_COMPILER_OPTIONS})
   endif()
endif()

if (DONUT_WITH_VULKAN)
   set(VULKAN_COMPILER_OPTIONS
      --parallel
      --platform spirv
      -I ${DONUT_SHADER_INCLUDE_DIR}
      -I ${CMAKE_CURRENT_SOURCE_DIR}/../rtxdi-sdk/include
      -I ${CMAKE_CURRENT_SOURCE_DIR}/../RTXGI/shaders
      -I ${CMAKE_CURRENT_SOURCE_DIR}/RTXGI
      --ignore "../Types.h"
      --ignore "rtxgi\\Types.h"
      ${NRD_OPTIONS}
      ${RTXGI_OPTIONS}
      --cflags "$<IF:$<CONFIG:Debug>,-Zi,> -fspv-target-env=vulkan1.2 -O3 -WX -Zpr"
      -D SPIRV
      --compiler ${DXC_SPIRV_EXECUTABLE})

   add_custom_command(TARGET ${shaders_target} PRE_BUILD
                     COMMAND shaderCompiler
                              --infile ${CMAKE_CURRENT_SOURCE_DIR}/Shaders.cfg
                              --out ${OUTPUT_PATH_BASE}/spirv
                              ${VULKAN_COMPILER_OPTIONS})

   if (TARGET RTXGI-VK)
      add_custom_command(TARGET ${shaders_target} PRE_BUILD
                        COMMAND shaderCompiler
                                 --infile ${CMAKE_CURRENT_SOURCE_DIR}/RTXGI/RtxgiShaders.cfg
                                 --out ${OUTPUT_PATH_BASE}/spirv/RTXGI
                                 ${VULKAN_COMPILER_OPTIONS})
   endif()
endif()

set_target_properties(${shaders_target} PROPERTIES FOLDER ${folder})
set_source_files_properties(${shaders} PROPERTIES VS_TOOL_OVERRIDE "None") 
