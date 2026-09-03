
include("${DONUT_PATH}/compileshaders.cmake")

set(shaders
   DenoisingPasses/ComputeGradients.hlsl
   DenoisingPasses/ConfidencePass.hlsl
   DenoisingPasses/DLSSRRInputFormattingPass.hlsl
   DenoisingPasses/FilterGradientsPass.hlsl
   LightingPasses/DI/FusedResampling.hlsl
   LightingPasses/DI/GenerateInitialSamples.hlsl
   LightingPasses/DI/ShadeSamples.hlsl
   LightingPasses/DI/SpatialResampling.hlsl
   LightingPasses/DI/TemporalResampling.hlsl
   LightingPasses/GI/FinalShading.hlsl
   LightingPasses/GI/FusedResampling.hlsl
   LightingPasses/GI/SpatialResampling.hlsl
   LightingPasses/GI/TemporalResampling.hlsl
   LightingPasses/Presampling/PresampleEnvironmentMap.hlsl
   LightingPasses/Presampling/PresampleLights.hlsl
   LightingPasses/Presampling/PresampleReGIR.hlsl
   LightingPasses/PT/ComputeDuplicationMap.hlsl
   LightingPasses/PT/ComputeSmoothedDuplicationMap.hlsl
   LightingPasses/PT/FinalShading.hlsl
   LightingPasses/PT/GenerateInitialSamples.hlsl
   LightingPasses/PT/PSRUtils.hlsli
   LightingPasses/PT/SpatialNeighborSelection.hlsl
   LightingPasses/PT/SpatialResampling.hlsl
   LightingPasses/PT/TemporalResampling.hlsl
   LightingPasses/RtxdiApplicationBridge/RAB_Buffers.hlsli
   LightingPasses/RtxdiApplicationBridge/RAB_LightInfo.hlsli
   LightingPasses/RtxdiApplicationBridge/RAB_LightSample.hlsli
   LightingPasses/RtxdiApplicationBridge/RAB_LightSampling.hlsli
   LightingPasses/RtxdiApplicationBridge/RAB_Material.hlsli
   LightingPasses/RtxdiApplicationBridge/RAB_NeighborSelection.hlsli
   LightingPasses/RtxdiApplicationBridge/RAB_RayPayload.hlsli
   LightingPasses/RtxdiApplicationBridge/RAB_RTShaders.hlsli
   LightingPasses/RtxdiApplicationBridge/RAB_SpatialHelpers.hlsli
   LightingPasses/RtxdiApplicationBridge/RAB_Surface.hlsli
   LightingPasses/RtxdiApplicationBridge/RAB_VisibilityTest.hlsli
   LightingPasses/RtxdiApplicationBridge/RtxdiApplicationBridge.hlsli
   LightingPasses/RtxdiApplicationBridge/PathTracer/RAB_DenoiserCallbacks.hlsli
   LightingPasses/RtxdiApplicationBridge/PathTracer/RAB_MISCallbacks.hlsli
   LightingPasses/RtxdiApplicationBridge/PathTracer/RAB_PathTracer.hlsli
   LightingPasses/RtxdiApplicationBridge/PathTracer/RAB_PathTracerUserData.hlsli
   LightingPasses/BrdfRayTracing.hlsl
   LightingPasses/ShadeSecondarySurfaces.hlsl
   LightingPasses/ShadingHelpers.hlsli
   ShaderDebug/PackedDataVizPasses/NDirOctUNorm32Viz.hlsl
   ShaderDebug/PackedDataVizPasses/PackedR11G11B10UFloatViz.hlsl
   ShaderDebug/PackedDataVizPasses/PackedR8G8B8A8GammaUFloatViz.hlsl
   ShaderDebug/PackedDataVizPasses/DuplicationMapViz.hlsl
   ShaderDebug/PackedDataVizPasses/SmoothedDuplicationMapViz.hlsl
   ShaderDebug/PackedDataVizPasses/DecorrelationFactorViz.hlsl
   ShaderDebug/PackedDataVizPasses/SampleIDViz.hlsl
   ShaderDebug/PTPathViz/PTPathVizLines_ps.hlsl
   ShaderDebug/PTPathViz/PTPathVizLines_vs.hlsl
   ShaderDebug/PTPathViz/PTPathVizPreprocess_cs.hlsl
   ShaderDebug/PTPathViz/PTPathVizRecording.hlsli
   ShaderDebug/ReservoirSubfieldVizPasses/ReservoirVizUtils.hlsli
   ShaderDebug/ReservoirSubfieldVizPasses/DIReservoirViz.hlsl
   ShaderDebug/ReservoirSubfieldVizPasses/GIReservoirViz.hlsl
   ShaderDebug/ReservoirSubfieldVizPasses/PTReservoirViz.hlsl
   ShaderDebug/ShaderDebugPrint/RuntimeShaderDebugPrintUtilities.hlsli
   ShaderDebug/ShaderDebugPrint/ShaderDebugPrint.hlsli
   ShaderDebug/VisualizeConfidence.hlsl
   ShaderDebug/VisualizeHdrSignals.hlsl
   SharedShaderInclude/SharedShaderInclude/ShaderDebug/PTPathViz/PTPathVizConstants.h
   SharedShaderInclude/SharedShaderInclude/ShaderDebug/PTPathViz/PTPathVertexRecord.h
   SharedShaderInclude/SharedShaderInclude/ShaderDebug/PTPathViz/PTPathSetRecord.h
   SharedShaderInclude/SharedShaderInclude/ShaderDebug/ReSTIRShaderDebugParameters.h
   SharedShaderInclude/SharedShaderInclude/ShaderDebug/ShaderDebugPrintShared.h
   SharedShaderInclude/SharedShaderInclude/ShaderDebug/StructuredBufferVisualizationParameters.h
   SharedShaderInclude/SharedShaderInclude/ShaderDebug/VisualizationOverlayMode.h
   SharedShaderInclude/SharedShaderInclude/ShaderDebug/ReservoirSubfieldVizPasses/DIReservoirVizParameters.h
   SharedShaderInclude/SharedShaderInclude/ShaderDebug/ReservoirSubfieldVizPasses/GIReservoirVizParameters.h
   SharedShaderInclude/SharedShaderInclude/ShaderDebug/ReservoirSubfieldVizPasses/PTReservoirVizParameters.h
   SharedShaderInclude/SharedShaderInclude/BRDFPTParameters.h
   SharedShaderInclude/SharedShaderInclude/DirectLightingMode.h
   SharedShaderInclude/SharedShaderInclude/NvapiIntegration.h
   SharedShaderInclude/SharedShaderInclude/PTParameters.h
   SharedShaderInclude/SharedShaderInclude/ShaderParameters.h
   AccumulationPass.hlsl
   CompositingPass.hlsl
   GBufferHelpers.hlsli
   GlassPass.hlsl
   HelperFunctions.hlsli
   LightShaping.hlsli
   PolymorphicLight.hlsli
   PostprocessGBuffer.hlsl
   PrepareLights.hlsl
   PreprocessEnvironmentMap.hlsl
   RasterizedGBuffer.hlsl
   RaytracedGBuffer.hlsl
   RenderEnvironmentMap.hlsl
   SceneGeometry.hlsli)

# Organize MSVS filters (the little folders in the solution explorer) to match the folder structure
foreach(source IN LISTS shaders)
    get_filename_component(source_path "${source}" PATH)
    string(REPLACE "/" "\\" source_path_msvc "${source_path}")
    source_group("${source_path_msvc}" FILES "${source}")
endforeach()

set(project FullSample)
set(folder "RTXDI SDK")
set(shaders_target FullSampleShaders)

add_custom_target(${shaders_target}
    DEPENDS ShaderMake
    SOURCES ${shaders} Shaders.cfg)

set(FULL_SAMPLE_SHARED_INCLUDE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/SharedShaderInclude/")
set(FULL_SAMPLE_SHARED_INCLUDE_PATH ${FULL_SAMPLE_SHARED_INCLUDE_PATH} PARENT_SCOPE)

if(TARGET NRD)
   set(NRD_OPTIONS -I "${CMAKE_CURRENT_SOURCE_DIR}/../../../External/NRD/Shaders" -D WITH_NRD)
else()
   set(NRD_OPTIONS --relaxedInclude NRD.hlsli)
endif()

set (OUTPUT_PATH_BASE "${CMAKE_BINARY_DIR}/bin/shaders/full-sample")

if (DONUT_WITH_DX12)
   set(DX12_COMPILER_OPTIONS
      --platform DXIL
      --shaderModel 6_5
      --binaryBlob
      --outputExt .bin
      -I ${DONUT_SHADER_INCLUDE_DIR}
      -I ${RTXDI_RUNTIME_INCLUDE_PATH}
      -I ${NVAPI_INCLUDE_DIR}
      -I ${FULL_SAMPLE_SHARED_INCLUDE_PATH}
      --relaxedInclude "../Types.h"
      ${NRD_OPTIONS}
     --hlsl2021
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
      --vulkanVersion 1.3
      --binaryBlob
      --outputExt .bin
      -I ${DONUT_SHADER_INCLUDE_DIR}
      -I ${RTXDI_RUNTIME_INCLUDE_PATH}
      -I ${NVAPI_INCLUDE_DIR}
      -I ${FULL_SAMPLE_SHARED_INCLUDE_PATH}
      --relaxedInclude "../Types.h"
      --verbose
      ${NRD_OPTIONS}
      ${NVRHI_DEFAULT_VK_REGISTER_OFFSETS}
      -D SPIRV
      --compiler ${DXC_SPIRV_PATH})

   add_custom_command(TARGET ${shaders_target} PRE_BUILD
                     COMMAND ShaderMake
                              --config ${CMAKE_CURRENT_SOURCE_DIR}/Shaders.cfg
                              --out ${OUTPUT_PATH_BASE}/spirv
                              ${VULKAN_COMPILER_OPTIONS})
endif()

# Organize MSVS filters (the little folders in the solution explorer) to match the folder structure
foreach(shader IN LISTS shaders)
    get_filename_component(source_path "${shader}" PATH)
    string(REPLACE "/" "\\" source_path_msvc "${source_path}")
    source_group("${source_path_msvc}" FILES "${shader}")
endforeach()

set_target_properties(${shaders_target} PROPERTIES FOLDER ${folder})
set_source_files_properties(${shaders} PROPERTIES VS_TOOL_OVERRIDE "None")
