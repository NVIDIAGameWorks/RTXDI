
include("${DONUT_PATH}/compileshaders.cmake")
file(GLOB shaders "*.hlsl" "*.hlsli" "*.h" "../rtxdi-sdk/shaders/*.hlsl" "../rtxdi-sdk/include/*.hlsli")

set(project minimal-sample)
set(folder "RTXDI SDK")
set(shaders_target minimal-sample-shaders)

add_custom_target(${shaders_target}
	DEPENDS shaderCompiler
	SOURCES ${shaders} Shaders.cfg)

set (OUTPUT_PATH_BASE "${CMAKE_BINARY_DIR}/bin/shaders/minimal-sample")

if (DONUT_WITH_DX12)
   set(DX12_COMPILER_OPTIONS 
      --parallel
      --platform dxil
      -I ${DONUT_SHADER_INCLUDE_DIR}
      -I ${CMAKE_CURRENT_SOURCE_DIR}/../../rtxdi-sdk/include
      --ignore "../Types.h"
      --cflags "$<IF:$<CONFIG:Debug>,-Zi -Qembed_debug,-Qstrip_debug -Qstrip_reflect> -O3 -WX -Zpr"
      --compiler ${DXC_DXIL_EXECUTABLE})

   add_custom_command(TARGET ${shaders_target} PRE_BUILD
                     COMMAND shaderCompiler
                              --infile ${CMAKE_CURRENT_SOURCE_DIR}/Shaders.cfg
                              --out ${OUTPUT_PATH_BASE}/dxil
                              ${DX12_COMPILER_OPTIONS})
endif()

if (DONUT_WITH_VULKAN)
   set(VULKAN_COMPILER_OPTIONS
      --parallel
      --platform spirv
      -I ${DONUT_SHADER_INCLUDE_DIR}
      -I ${CMAKE_CURRENT_SOURCE_DIR}/../../rtxdi-sdk/include
      --ignore "../Types.h"
      --cflags "$<IF:$<CONFIG:Debug>,-Zi,> -fspv-target-env=vulkan1.2 -O3 -WX -Zpr"
      -D SPIRV
      --compiler ${DXC_SPIRV_EXECUTABLE})

   add_custom_command(TARGET ${shaders_target} PRE_BUILD
                     COMMAND shaderCompiler
                              --infile ${CMAKE_CURRENT_SOURCE_DIR}/Shaders.cfg
                              --out ${OUTPUT_PATH_BASE}/spirv
                              ${VULKAN_COMPILER_OPTIONS})
endif()

set_target_properties(${shaders_target} PROPERTIES FOLDER ${folder})
set_source_files_properties(${shaders} PROPERTIES VS_TOOL_OVERRIDE "None") 
