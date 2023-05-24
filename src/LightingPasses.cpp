/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#include "LightingPasses.h"
#include "RenderTargets.h"
#include "RtxdiResources.h"
#include "Profiler.h"
#include "SampleScene.h"
#include "GBufferPass.h"

#include <donut/engine/Scene.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>
#include <donut/core/log.h>
#include <nvrhi/utils.h>
#include <rtxdi/RTXDI.h>

#include <utility>

#if WITH_NRD
#include <NRD.h>
#endif

using namespace donut::math;
#include "../shaders/ShaderParameters.h"

using namespace donut::engine;

LightingPasses::LightingPasses(
    nvrhi::IDevice* device, 
    std::shared_ptr<ShaderFactory> shaderFactory,
    std::shared_ptr<donut::engine::CommonRenderPasses> commonPasses,
    std::shared_ptr<donut::engine::Scene> scene,
    std::shared_ptr<Profiler> profiler,
    nvrhi::IBindingLayout* bindlessLayout
)
    : m_Device(device)
    , m_BindlessLayout(bindlessLayout)
    , m_ShaderFactory(std::move(shaderFactory))
    , m_CommonPasses(std::move(commonPasses))
    , m_Scene(std::move(scene))
    , m_Profiler(std::move(profiler))
{
    // The binding layout descriptor must match the binding set descriptor defined in CreateBindingSet(...) below

    nvrhi::BindingLayoutDesc globalBindingLayoutDesc;
    globalBindingLayoutDesc.visibility = nvrhi::ShaderType::Compute | nvrhi::ShaderType::AllRayTracing;
    globalBindingLayoutDesc.bindings = {
        nvrhi::BindingLayoutItem::Texture_SRV(0),
        nvrhi::BindingLayoutItem::Texture_SRV(1),
        nvrhi::BindingLayoutItem::Texture_SRV(2),
        nvrhi::BindingLayoutItem::Texture_SRV(3),
        nvrhi::BindingLayoutItem::Texture_SRV(4),
        nvrhi::BindingLayoutItem::Texture_SRV(5),
        nvrhi::BindingLayoutItem::Texture_SRV(6),
        nvrhi::BindingLayoutItem::Texture_SRV(7),
        nvrhi::BindingLayoutItem::Texture_SRV(8),
        nvrhi::BindingLayoutItem::Texture_SRV(9),
        nvrhi::BindingLayoutItem::Texture_SRV(10),
        nvrhi::BindingLayoutItem::Texture_SRV(11),
        nvrhi::BindingLayoutItem::Texture_SRV(12),

        nvrhi::BindingLayoutItem::RayTracingAccelStruct(30),
        nvrhi::BindingLayoutItem::RayTracingAccelStruct(31),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(32),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(33),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(34),

        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(20),
        nvrhi::BindingLayoutItem::TypedBuffer_SRV(21),
        nvrhi::BindingLayoutItem::TypedBuffer_SRV(22),
        nvrhi::BindingLayoutItem::Texture_SRV(23),
        nvrhi::BindingLayoutItem::Texture_SRV(24),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(25),

        nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0),
        nvrhi::BindingLayoutItem::Texture_UAV(1),
        nvrhi::BindingLayoutItem::Texture_UAV(2),
        nvrhi::BindingLayoutItem::Texture_UAV(3),
        nvrhi::BindingLayoutItem::Texture_UAV(4),
        nvrhi::BindingLayoutItem::Texture_UAV(5),
        nvrhi::BindingLayoutItem::StructuredBuffer_UAV(6),

        nvrhi::BindingLayoutItem::TypedBuffer_UAV(10),
        nvrhi::BindingLayoutItem::TypedBuffer_UAV(11),
        nvrhi::BindingLayoutItem::TypedBuffer_UAV(12),
        nvrhi::BindingLayoutItem::StructuredBuffer_UAV(13),

        nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
        nvrhi::BindingLayoutItem::PushConstants(1, sizeof(PerPassConstants)),
        nvrhi::BindingLayoutItem::Sampler(0),
        nvrhi::BindingLayoutItem::Sampler(1),
    };

    m_BindingLayout = m_Device->createBindingLayout(globalBindingLayoutDesc);

    m_ConstantBuffer = m_Device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(ResamplingConstants), "ResamplingConstants", 16));
}

void LightingPasses::CreateBindingSet(
    nvrhi::rt::IAccelStruct* topLevelAS,
    nvrhi::rt::IAccelStruct* prevTopLevelAS,
    const RenderTargets& renderTargets,
    const RtxdiResources& resources)
{
    assert(&renderTargets);
    assert(&resources);

    for (int currentFrame = 0; currentFrame <= 1; currentFrame++)
    {
        // This list must match the binding declarations in RtxdiApplicationBridge.hlsli

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings = {
            nvrhi::BindingSetItem::Texture_SRV(0, currentFrame ? renderTargets.Depth : renderTargets.PrevDepth),
            nvrhi::BindingSetItem::Texture_SRV(1, currentFrame ? renderTargets.GBufferNormals : renderTargets.PrevGBufferNormals),
            nvrhi::BindingSetItem::Texture_SRV(2, currentFrame ? renderTargets.GBufferGeoNormals : renderTargets.PrevGBufferGeoNormals),
            nvrhi::BindingSetItem::Texture_SRV(3, currentFrame ? renderTargets.GBufferDiffuseAlbedo : renderTargets.PrevGBufferDiffuseAlbedo),
            nvrhi::BindingSetItem::Texture_SRV(4, currentFrame ? renderTargets.GBufferSpecularRough : renderTargets.PrevGBufferSpecularRough),
            nvrhi::BindingSetItem::Texture_SRV(5, currentFrame ? renderTargets.PrevDepth : renderTargets.Depth),
            nvrhi::BindingSetItem::Texture_SRV(6, currentFrame ? renderTargets.PrevGBufferNormals : renderTargets.GBufferNormals),
            nvrhi::BindingSetItem::Texture_SRV(7, currentFrame ? renderTargets.PrevGBufferGeoNormals : renderTargets.GBufferGeoNormals),
            nvrhi::BindingSetItem::Texture_SRV(8, currentFrame ? renderTargets.PrevGBufferDiffuseAlbedo : renderTargets.GBufferDiffuseAlbedo),
            nvrhi::BindingSetItem::Texture_SRV(9, currentFrame ? renderTargets.PrevGBufferSpecularRough : renderTargets.GBufferSpecularRough),
            nvrhi::BindingSetItem::Texture_SRV(10, currentFrame ? renderTargets.PrevRestirLuminance : renderTargets.RestirLuminance),
            nvrhi::BindingSetItem::Texture_SRV(11, renderTargets.MotionVectors),
            nvrhi::BindingSetItem::Texture_SRV(12, renderTargets.NormalRoughness),
            
            nvrhi::BindingSetItem::RayTracingAccelStruct(30, currentFrame ? topLevelAS : prevTopLevelAS),
            nvrhi::BindingSetItem::RayTracingAccelStruct(31, currentFrame ? prevTopLevelAS : topLevelAS),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(32, m_Scene->GetInstanceBuffer()),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(33, m_Scene->GetGeometryBuffer()),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(34, m_Scene->GetMaterialBuffer()),

            nvrhi::BindingSetItem::StructuredBuffer_SRV(20, resources.LightDataBuffer),
            nvrhi::BindingSetItem::TypedBuffer_SRV(21, resources.NeighborOffsetsBuffer),
            nvrhi::BindingSetItem::TypedBuffer_SRV(22, resources.LightIndexMappingBuffer),
            nvrhi::BindingSetItem::Texture_SRV(23, resources.EnvironmentPdfTexture),
            nvrhi::BindingSetItem::Texture_SRV(24, resources.LocalLightPdfTexture),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(25, resources.GeometryInstanceToLightBuffer),

            nvrhi::BindingSetItem::StructuredBuffer_UAV(0, resources.LightReservoirBuffer),
            nvrhi::BindingSetItem::Texture_UAV(1, renderTargets.DiffuseLighting),
            nvrhi::BindingSetItem::Texture_UAV(2, renderTargets.SpecularLighting),
            nvrhi::BindingSetItem::Texture_UAV(3, renderTargets.TemporalSamplePositions),
            nvrhi::BindingSetItem::Texture_UAV(4, renderTargets.Gradients),
            nvrhi::BindingSetItem::Texture_UAV(5, currentFrame ? renderTargets.RestirLuminance : renderTargets.PrevRestirLuminance),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(6, resources.GIReservoirBuffer),

            nvrhi::BindingSetItem::TypedBuffer_UAV(10, resources.RisBuffer),
            nvrhi::BindingSetItem::TypedBuffer_UAV(11, resources.RisLightDataBuffer),
            nvrhi::BindingSetItem::TypedBuffer_UAV(12, m_Profiler->GetRayCountBuffer()),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(13, resources.SecondaryGBuffer),

            nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
            nvrhi::BindingSetItem::PushConstants(1, sizeof(PerPassConstants)),
            nvrhi::BindingSetItem::Sampler(0, m_CommonPasses->m_LinearWrapSampler),
            nvrhi::BindingSetItem::Sampler(1, m_CommonPasses->m_LinearWrapSampler)
        };

        const nvrhi::BindingSetHandle bindingSet = m_Device->createBindingSet(bindingSetDesc, m_BindingLayout);

        if (currentFrame)
            m_BindingSet = bindingSet;
        else
            m_PrevBindingSet = bindingSet;
    }

    const auto& environmentPdfDesc = resources.EnvironmentPdfTexture->getDesc();
    m_EnvironmentPdfTextureSize.x = environmentPdfDesc.width;
    m_EnvironmentPdfTextureSize.y = environmentPdfDesc.height;
    
    const auto& localLightPdfDesc = resources.LocalLightPdfTexture->getDesc();
    m_LocalLightPdfTextureSize.x = localLightPdfDesc.width;
    m_LocalLightPdfTextureSize.y = localLightPdfDesc.height;

    m_LightReservoirBuffer = resources.LightReservoirBuffer;
    m_SecondarySurfaceBuffer = resources.SecondaryGBuffer;
    m_GIReservoirBuffer = resources.GIReservoirBuffer;
}

void LightingPasses::CreateComputePass(ComputePass& pass, const char* shaderName, const std::vector<donut::engine::ShaderMacro>& macros)
{
    donut::log::debug("Initializing ComputePass %s...", shaderName);

    pass.Shader = m_ShaderFactory->CreateShader(shaderName, "main", &macros, nvrhi::ShaderType::Compute);

    nvrhi::ComputePipelineDesc pipelineDesc;
    pipelineDesc.bindingLayouts = { m_BindingLayout, m_BindlessLayout };
    pipelineDesc.CS = pass.Shader;
    pass.Pipeline = m_Device->createComputePipeline(pipelineDesc);
}

void LightingPasses::ExecuteComputePass(nvrhi::ICommandList* commandList, ComputePass& pass, const char* passName, dm::int2 dispatchSize, ProfilerSection::Enum profilerSection)
{
    commandList->beginMarker(passName);
    m_Profiler->BeginSection(commandList, profilerSection);

    nvrhi::ComputeState state;
    state.bindings = { m_BindingSet, m_Scene->GetDescriptorTable() };
    state.pipeline = pass.Pipeline;
    commandList->setComputeState(state);

    PerPassConstants pushConstants{};
    pushConstants.rayCountBufferIndex = -1;
    commandList->setPushConstants(&pushConstants, sizeof(pushConstants));

    commandList->dispatch(dispatchSize.x, dispatchSize.y, 1);

    m_Profiler->EndSection(commandList, profilerSection);
    commandList->endMarker();
}

void LightingPasses::ExecuteRayTracingPass(nvrhi::ICommandList* commandList, RayTracingPass& pass, bool enableRayCounts, const char* passName, dm::int2 dispatchSize, ProfilerSection::Enum profilerSection, nvrhi::IBindingSet* extraBindingSet)
{
    commandList->beginMarker(passName);
    m_Profiler->BeginSection(commandList, profilerSection);

    PerPassConstants pushConstants{};
    pushConstants.rayCountBufferIndex = enableRayCounts ? profilerSection : -1;
    
    pass.Execute(commandList, dispatchSize.x, dispatchSize.y, m_BindingSet, extraBindingSet, m_Scene->GetDescriptorTable(), &pushConstants, sizeof(pushConstants));
    
    m_Profiler->EndSection(commandList, profilerSection);
    commandList->endMarker();
}

donut::engine::ShaderMacro LightingPasses::GetRegirMacro(const rtxdi::RTXDIStaticParameters& contextParameters)
{
    std::string regirMode;

    switch (contextParameters.ReGIR.Mode)
    {
    case rtxdi::ReGIRMode::Disabled:
        regirMode = "RTXDI_REGIR_DISABLED";
        break;
    case rtxdi::ReGIRMode::Grid:
        regirMode = "RTXDI_REGIR_GRID";
        break;
    case rtxdi::ReGIRMode::Onion:
        regirMode = "RTXDI_REGIR_ONION";
        break;
    }

    return { "RTXDI_REGIR_MODE", regirMode };
}

void LightingPasses::CreatePipelines(const rtxdi::RTXDIStaticParameters& contextParameters, bool useRayQuery)
{
    std::vector<donut::engine::ShaderMacro> regirMacros = {
        GetRegirMacro(contextParameters) 
    };

    CreateComputePass(m_PresampleLightsPass, "app/LightingPasses/PresampleLights.hlsl", {});
    CreateComputePass(m_PresampleEnvironmentMapPass, "app/LightingPasses/PresampleEnvironmentMap.hlsl", {});

    if (contextParameters.ReGIR.Mode != rtxdi::ReGIRMode::Disabled)
    {
        CreateComputePass(m_PresampleReGIR, "app/LightingPasses/PresampleReGIR.hlsl", regirMacros);
    }

    m_GenerateInitialSamplesPass.Init(m_Device, *m_ShaderFactory, "app/LightingPasses/GenerateInitialSamples.hlsl", regirMacros, useRayQuery, RTXDI_SCREEN_SPACE_GROUP_SIZE, m_BindingLayout, nullptr, m_BindlessLayout);
    m_TemporalResamplingPass.Init(m_Device, *m_ShaderFactory, "app/LightingPasses/TemporalResampling.hlsl", {}, useRayQuery, RTXDI_SCREEN_SPACE_GROUP_SIZE, m_BindingLayout, nullptr, m_BindlessLayout);
    m_SpatialResamplingPass.Init(m_Device, *m_ShaderFactory, "app/LightingPasses/SpatialResampling.hlsl", {}, useRayQuery, RTXDI_SCREEN_SPACE_GROUP_SIZE, m_BindingLayout, nullptr, m_BindlessLayout);
    m_ShadeSamplesPass.Init(m_Device, *m_ShaderFactory, "app/LightingPasses/ShadeSamples.hlsl", regirMacros, useRayQuery, RTXDI_SCREEN_SPACE_GROUP_SIZE, m_BindingLayout, nullptr, m_BindlessLayout);
    m_BrdfRayTracingPass.Init(m_Device, *m_ShaderFactory, "app/LightingPasses/BrdfRayTracing.hlsl", {}, useRayQuery, RTXDI_SCREEN_SPACE_GROUP_SIZE, m_BindingLayout, nullptr, m_BindlessLayout);
    m_ShadeSecondarySurfacesPass.Init(m_Device, *m_ShaderFactory, "app/LightingPasses/ShadeSecondarySurfaces.hlsl", regirMacros, useRayQuery, RTXDI_SCREEN_SPACE_GROUP_SIZE, m_BindingLayout, nullptr, m_BindlessLayout);
    m_FusedResamplingPass.Init(m_Device, *m_ShaderFactory, "app/LightingPasses/FusedResampling.hlsl", regirMacros, useRayQuery, RTXDI_SCREEN_SPACE_GROUP_SIZE, m_BindingLayout, nullptr, m_BindlessLayout);
    m_GradientsPass.Init(m_Device, *m_ShaderFactory, "app/LightingPasses/ComputeGradients.hlsl", {}, useRayQuery, RTXDI_SCREEN_SPACE_GROUP_SIZE, m_BindingLayout, nullptr, m_BindlessLayout);
    m_GITemporalResamplingPass.Init(m_Device, *m_ShaderFactory, "app/LightingPasses/GITemporalResampling.hlsl", {}, useRayQuery, RTXDI_SCREEN_SPACE_GROUP_SIZE, m_BindingLayout, nullptr, m_BindlessLayout);
    m_GISpatialResamplingPass.Init(m_Device, *m_ShaderFactory, "app/LightingPasses/GISpatialResampling.hlsl", {}, useRayQuery, RTXDI_SCREEN_SPACE_GROUP_SIZE, m_BindingLayout, nullptr, m_BindlessLayout);
    m_GIFusedResamplingPass.Init(m_Device, *m_ShaderFactory, "app/LightingPasses/GIFusedResampling.hlsl", {}, useRayQuery, RTXDI_SCREEN_SPACE_GROUP_SIZE, m_BindingLayout, nullptr, m_BindlessLayout);
    m_GIFinalShadingPass.Init(m_Device, *m_ShaderFactory, "app/LightingPasses/GIFinalShading.hlsl", {}, useRayQuery, RTXDI_SCREEN_SPACE_GROUP_SIZE, m_BindingLayout, nullptr, m_BindlessLayout);
}

#if WITH_NRD
static void NrdHitDistanceParamsToFloat4(const nrd::HitDistanceParameters* params, dm::float4& out)
{
    assert(params);
    out.x = params->A;
    out.y = params->B;
    out.z = params->C;
    out.w = params->D;
}
#endif

void LightingPasses::FillResamplingConstants(
    ResamplingConstants& constants,
    const RenderSettings& lightingSettings,
    const rtxdi::RTXDIContext& rtxdiContext,
    const rtxdi::LightBufferParameters& lightBufferParameters)
{
    const bool useTemporalResampling =
        lightingSettings.resamplingMode == ResamplingMode::Temporal ||
        lightingSettings.resamplingMode == ResamplingMode::TemporalAndSpatial ||
        lightingSettings.resamplingMode == ResamplingMode::FusedSpatiotemporal;

    const bool useSpatialResampling =
        lightingSettings.resamplingMode == ResamplingMode::Spatial ||
        lightingSettings.resamplingMode == ResamplingMode::TemporalAndSpatial ||
        lightingSettings.resamplingMode == ResamplingMode::FusedSpatiotemporal;

    constants.enablePreviousTLAS = lightingSettings.enablePreviousTLAS;
    constants.denoiserMode = lightingSettings.denoiserMode;
    constants.sceneConstants.enableAlphaTestedGeometry = lightingSettings.enableAlphaTestedGeometry;
    constants.sceneConstants.enableTransparentGeometry = lightingSettings.enableTransparentGeometry;
    constants.shadingConstants.enableDenoiserInputPacking = lightingSettings.enableDenoiserInputPacking;
    constants.visualizeRegirCells = lightingSettings.visualizeRegirCells;
#if WITH_NRD
    if (lightingSettings.denoiserMode != DENOISER_MODE_OFF)
    {
        NrdHitDistanceParamsToFloat4(lightingSettings.reblurDiffHitDistanceParams, constants.reblurDiffHitDistParams);
        NrdHitDistanceParamsToFloat4(lightingSettings.reblurSpecHitDistanceParams, constants.reblurSpecHitDistParams);
    }
#endif

    auto initialSamplingSettings = rtxdiContext.getInitialSamplingSettings();
    auto temporalResamplingSettings = rtxdiContext.getTemporalResamplingSettings();
    auto boilingFilterSettings = rtxdiContext.getBoilingFilterSettings();
    auto spatialResamplingSettings = rtxdiContext.getSpatialResamplingSettings();
    auto shadingSettings = rtxdiContext.getShadingSettings();

    switch (initialSamplingSettings.localLightInitialSamplingMode)
    {
    default:
    case rtxdi::LocalLightSamplingMode::Uniform:
        constants.initialSamplingConstants.numPrimaryLocalLightSamples = initialSamplingSettings.numPrimaryLocalLightUniformSamples;
        break;
    case rtxdi::LocalLightSamplingMode::Power_RIS:
        constants.initialSamplingConstants.numPrimaryLocalLightSamples = initialSamplingSettings.numPrimaryLocalLightPowerRISSamples;
        break;
    case rtxdi::LocalLightSamplingMode::ReGIR_RIS:
        constants.initialSamplingConstants.numPrimaryLocalLightSamples = initialSamplingSettings.numPrimaryLocalLightReGIRRISSamples;
        break;
    }
    
    constants.initialSamplingConstants.numPrimaryBrdfSamples = initialSamplingSettings.numPrimaryBrdfSamples;
    constants.initialSamplingConstants.numPrimaryInfiniteLightSamples = initialSamplingSettings.numPrimaryInfiniteLightSamples;
    constants.initialSamplingConstants.brdfCutoff = initialSamplingSettings.brdfCutoff;
    constants.initialSamplingConstants.enableInitialVisibility = initialSamplingSettings.enableInitialVisibility;
    constants.temporalResamplingConstants.temporalNormalThreshold = temporalResamplingSettings.temporalNormalThreshold;
    constants.temporalResamplingConstants.temporalDepthThreshold = temporalResamplingSettings.temporalDepthThreshold;
    constants.temporalResamplingConstants.maxHistoryLength = temporalResamplingSettings.maxHistoryLength;
    constants.temporalResamplingConstants.temporalBiasCorrection = temporalResamplingSettings.temporalBiasCorrection;
    constants.temporalResamplingConstants.discardInvisibleSamples = temporalResamplingSettings.discardInvisibleSamples;
    constants.temporalResamplingConstants.enablePermutationSampling = lightingSettings.enablePermutationSampling;
    constants.temporalResamplingConstants.permutationSamplingThreshold = temporalResamplingSettings.permutationSamplingThreshold;
    constants.boilingFilterStrength = boilingFilterSettings.enableBoilingFilter ? boilingFilterSettings.boilingFilterStrength : 0.f;
    constants.spatialResamplingConstants.numSpatialSamples = useSpatialResampling ? spatialResamplingSettings.numSpatialSamples : 0;
    constants.spatialResamplingConstants.numDisocclusionBoostSamples = useTemporalResampling ? spatialResamplingSettings.numDisocclusionBoostSamples : 0;
    constants.spatialResamplingConstants.spatialSamplingRadius = spatialResamplingSettings.spatialSamplingRadius;
    constants.spatialResamplingConstants.spatialNormalThreshold = spatialResamplingSettings.spatialNormalThreshold;
    constants.spatialResamplingConstants.spatialDepthThreshold = spatialResamplingSettings.spatialDepthThreshold;
    constants.spatialResamplingConstants.spatialBiasCorrection = spatialResamplingSettings.spatialBiasCorrection;
    constants.spatialResamplingConstants.discountNaiveSamples = spatialResamplingSettings.discountNaiveSamples;
    constants.shadingConstants.enableFinalVisibility = lightingSettings.enableFinalVisibility;
    constants.shadingConstants.reuseFinalVisibility = shadingSettings.reuseFinalVisibility;
    constants.shadingConstants.finalVisibilityMaxAge = shadingSettings.finalVisibilityMaxAge;
    constants.shadingConstants.finalVisibilityMaxDistance = shadingSettings.finalVisibilityMaxDistance;
    constants.giSamplingConstants.numIndirectLocalLightSamples = lightingSettings.giSamplingSettings.numIndirectLocalLightSamples;
    constants.giSamplingConstants.numIndirectInfiniteLightSamples = lightingSettings.giSamplingSettings.numIndirectInfiniteLightSamples;
    constants.giSamplingConstants.numSecondarySamples = lightingSettings.giSamplingSettings.enableSecondaryResampling ? lightingSettings.giSamplingSettings.numSecondarySamples : 0;
    constants.giSamplingConstants.secondaryBiasCorrection = lightingSettings.giSamplingSettings.secondaryBiasCorrection;
    constants.giSamplingConstants.secondarySamplingRadius = lightingSettings.giSamplingSettings.secondarySamplingRadius;
    constants.giSamplingConstants.secondaryDepthThreshold = lightingSettings.giSamplingSettings.secondaryDepthThreshold;
    constants.giSamplingConstants.secondaryNormalThreshold = lightingSettings.giSamplingSettings.secondaryNormalThreshold;
    
    if (lightingSettings.resamplingMode == ResamplingMode::FusedSpatiotemporal)
    {
        constants.initialSamplingConstants.initialOutputBufferIndex = (m_LastFrameOutputReservoir + 1) % RtxdiResources::c_NumReservoirBuffers;
        constants.temporalResamplingConstants.temporalInputBufferIndex = m_LastFrameOutputReservoir;
        constants.shadingConstants.shadeInputBufferIndex = constants.initialSamplingConstants.initialOutputBufferIndex;
    }
    else
    {
        constants.initialSamplingConstants.initialOutputBufferIndex = (m_LastFrameOutputReservoir + 1) % RtxdiResources::c_NumReservoirBuffers;
        constants.temporalResamplingConstants.temporalInputBufferIndex = m_LastFrameOutputReservoir;
        constants.temporalResamplingConstants.temporalOutputBufferIndex = (constants.temporalResamplingConstants.temporalInputBufferIndex + 1) % RtxdiResources::c_NumReservoirBuffers;
        constants.spatialResamplingConstants.spatialInputBufferIndex = useTemporalResampling
            ? constants.temporalResamplingConstants.temporalOutputBufferIndex
            : constants.initialSamplingConstants.initialOutputBufferIndex;
        constants.spatialResamplingConstants.spatialOutputBufferIndex = (constants.spatialResamplingConstants.spatialInputBufferIndex + 1) % RtxdiResources::c_NumReservoirBuffers;
        constants.shadingConstants.shadeInputBufferIndex = useSpatialResampling
            ? constants.spatialResamplingConstants.spatialOutputBufferIndex
            : constants.temporalResamplingConstants.temporalOutputBufferIndex;
    }

    constants.localLightPdfTextureSize = m_LocalLightPdfTextureSize;
    
    if (lightBufferParameters.environmentLightPresent)
    {
        constants.environmentPdfTextureSize = m_EnvironmentPdfTextureSize;
        constants.initialSamplingConstants.numPrimaryEnvironmentSamples = initialSamplingSettings.numPrimaryEnvironmentSamples;
        constants.giSamplingConstants.numIndirectEnvironmentSamples = lightingSettings.giSamplingSettings.numIndirectEnvironmentSamples;
        constants.initialSamplingConstants.environmentMapImportanceSampling = 1;
    }

    m_CurrentFrameOutputReservoir = constants.shadingConstants.shadeInputBufferIndex;
}

void LightingPasses::PrepareForLightSampling(
    nvrhi::ICommandList* commandList,
    rtxdi::RTXDIContext& context,
    const donut::engine::IView& view,
    const donut::engine::IView& previousView,
    const RenderSettings& localSettings,
    bool enableAccumulation)
{
    ResamplingConstants constants = {};
    constants.frameIndex = context.getFrameIndex();
    view.FillPlanarViewConstants(constants.view);
    previousView.FillPlanarViewConstants(constants.prevView);
    context.FillRuntimeParameters(constants.runtimeParams);
    FillResamplingConstants(constants, localSettings, context, context.getLightBufferParameters());
    constants.enableAccumulation = enableAccumulation;

    commandList->writeBuffer(m_ConstantBuffer, &constants, sizeof(constants));

    auto& lightBufferParams = context.getLightBufferParameters();

    if (context.isLocalLightPowerRISEnabled() &&
        lightBufferParams.numLocalLights > 0)
    {
        dm::int2 presampleDispatchSize = {
            dm::div_ceil(context.getStaticParameters().localLightPowerRISBufferSegmentParams.tileSize, RTXDI_PRESAMPLING_GROUP_SIZE),
            int(context.getStaticParameters().localLightPowerRISBufferSegmentParams.tileCount)
        };

        ExecuteComputePass(commandList, m_PresampleLightsPass, "PresampleLights", presampleDispatchSize, ProfilerSection::PresampleLights);
    }

    if (lightBufferParams.environmentLightPresent)
    {
        dm::int2 presampleDispatchSize = {
            dm::div_ceil(context.getStaticParameters().environmentLightRISBufferSegmentParams.tileSize, RTXDI_PRESAMPLING_GROUP_SIZE),
            int(context.getStaticParameters().environmentLightRISBufferSegmentParams.tileCount)
        };

        ExecuteComputePass(commandList, m_PresampleEnvironmentMapPass, "PresampleEnvironmentMap", presampleDispatchSize, ProfilerSection::PresampleEnvMap);
    }

    if (context.getStaticParameters().ReGIR.Mode != rtxdi::ReGIRMode::Disabled &&
        context.getInitialSamplingSettings().localLightInitialSamplingMode == rtxdi::LocalLightSamplingMode::ReGIR_RIS &&
        lightBufferParams.numLocalLights > 0)
    {
        dm::int2 worldGridDispatchSize = {
            dm::div_ceil(context.getReGIRContext().getReGIRLightSlotCount(), RTXDI_GRID_BUILD_GROUP_SIZE),
            1
        };

        ExecuteComputePass(commandList, m_PresampleReGIR, "PresampleReGIR", worldGridDispatchSize, ProfilerSection::PresampleReGIR);
    }
}

void LightingPasses::RenderDirectLighting(
    nvrhi::ICommandList* commandList,
    rtxdi::RTXDIContext& context,
    const donut::engine::IView& view,
    const RenderSettings& localSettings)
{
    dm::int2 dispatchSize = { 
        view.GetViewExtent().width(),
        view.GetViewExtent().height()
    };

    if (context.getStaticParameters().CheckerboardSamplingMode != rtxdi::CheckerboardMode::Off)
        dispatchSize.x /= 2;

    // Run the lighting passes in the necessary sequence: one fused kernel or multiple separate passes.
    //
    // Note: the below code places explicit UAV barriers between subsequent passes
    // because NVRHI misses them, as the binding sets are exactly the same between these passes.
    // That equality makes NVRHI take a shortcut for performance and it doesn't look at bindings at all.

    ExecuteRayTracingPass(commandList, m_GenerateInitialSamplesPass, localSettings.enableRayCounts, "GenerateInitialSamples", dispatchSize, ProfilerSection::InitialSamples);

    if (localSettings.resamplingMode == ResamplingMode::FusedSpatiotemporal)
    {
        nvrhi::utils::BufferUavBarrier(commandList, m_LightReservoirBuffer);

        ExecuteRayTracingPass(commandList, m_FusedResamplingPass, localSettings.enableRayCounts, "FusedResampling", dispatchSize, ProfilerSection::Shading);
    }
    else
    {
        if (localSettings.resamplingMode == ResamplingMode::Temporal || localSettings.resamplingMode == ResamplingMode::TemporalAndSpatial)
        {
            nvrhi::utils::BufferUavBarrier(commandList, m_LightReservoirBuffer);

            ExecuteRayTracingPass(commandList, m_TemporalResamplingPass, localSettings.enableRayCounts, "TemporalResampling", dispatchSize, ProfilerSection::TemporalResampling);
        }

        if (localSettings.resamplingMode == ResamplingMode::Spatial || localSettings.resamplingMode == ResamplingMode::TemporalAndSpatial)
        {
            nvrhi::utils::BufferUavBarrier(commandList, m_LightReservoirBuffer);

            ExecuteRayTracingPass(commandList, m_SpatialResamplingPass, localSettings.enableRayCounts, "SpatialResampling", dispatchSize, ProfilerSection::SpatialResampling);
        }

        nvrhi::utils::BufferUavBarrier(commandList, m_LightReservoirBuffer);

        ExecuteRayTracingPass(commandList, m_ShadeSamplesPass, localSettings.enableRayCounts, "ShadeSamples", dispatchSize, ProfilerSection::Shading);
    }
    
    if (localSettings.enableGradients)
    {
        nvrhi::utils::BufferUavBarrier(commandList, m_LightReservoirBuffer);

        ExecuteRayTracingPass(commandList, m_GradientsPass, localSettings.enableRayCounts, "Gradients", (dispatchSize + RTXDI_GRAD_FACTOR - 1) / RTXDI_GRAD_FACTOR, ProfilerSection::Gradients);
    }
}

void LightingPasses::RenderBrdfRays(
    nvrhi::ICommandList* commandList, 
    rtxdi::RTXDIContext& context,
    const donut::engine::IView& view,
    const donut::engine::IView& previousView,
    const RenderSettings& localSettings,
    const GBufferSettings& gbufferSettings,
    const EnvironmentLight& environmentLight,
    bool enableIndirect,
    bool enableAdditiveBlend,
    bool enableEmissiveSurfaces,
    bool enableAccumulation,
    bool enableReStirGI
    )
{
    ResamplingConstants constants = {};
    view.FillPlanarViewConstants(constants.view);
    previousView.FillPlanarViewConstants(constants.prevView);

    constants.frameIndex = context.getFrameIndex();
    constants.denoiserMode = localSettings.denoiserMode;
    constants.enableBrdfIndirect = enableIndirect;
    constants.enableBrdfAdditiveBlend = enableAdditiveBlend;
    constants.enableAccumulation = enableAccumulation;
    constants.sceneConstants.enableEnvironmentMap = (environmentLight.textureIndex >= 0);
    constants.sceneConstants.environmentMapTextureIndex = (environmentLight.textureIndex >= 0) ? environmentLight.textureIndex : 0;
    constants.sceneConstants.environmentScale = environmentLight.radianceScale.x;
    constants.sceneConstants.environmentRotation = environmentLight.rotation;
    constants.giSamplingConstants.enableIndirectEmissiveSurfaces = enableEmissiveSurfaces;
    constants.giSamplingConstants.roughnessOverride = gbufferSettings.enableRoughnessOverride ? gbufferSettings.roughnessOverride : -1.f;
    constants.giSamplingConstants.metalnessOverride = gbufferSettings.enableMetalnessOverride ? gbufferSettings.metalnessOverride : -1.f;
    constants.giSamplingConstants.minSecondaryRoughness = localSettings.giSamplingSettings.minSecondaryRoughness;
    constants.giSamplingConstants.enableFallbackSampling = localSettings.reStirGI.enableFallbackSampling;
    constants.giSamplingConstants.giEnableFinalMIS = localSettings.reStirGI.enableFinalMIS;
    constants.giSamplingConstants.enableReSTIRIndirect = enableReStirGI;
    context.FillRuntimeParameters(constants.runtimeParams);
    FillResamplingConstants(constants, localSettings, context, context.getLightBufferParameters());

    // Override various DI related settings set in FillResamplingConstants

    constants.boilingFilterStrength = localSettings.reStirGI.enableBoilingFilter ? localSettings.reStirGI.boilingFilterStrength : 0.f;
    
    // There are 2 sets of GI reservoirs in total.
    switch(localSettings.reStirGI.resamplingMode)
    {
    case ResamplingMode::None:
        constants.initialSamplingConstants.initialOutputBufferIndex = 0;
        constants.shadingConstants.shadeInputBufferIndex = 0;
        break;
    case ResamplingMode::Temporal:
        constants.initialSamplingConstants.initialOutputBufferIndex = context.getFrameIndex() & 1;
        constants.temporalResamplingConstants.temporalInputBufferIndex = !constants.initialSamplingConstants.initialOutputBufferIndex;
        constants.temporalResamplingConstants.temporalOutputBufferIndex = constants.initialSamplingConstants.initialOutputBufferIndex;
        constants.shadingConstants.shadeInputBufferIndex = constants.temporalResamplingConstants.temporalOutputBufferIndex;
        break;
    case ResamplingMode::Spatial:
        constants.initialSamplingConstants.initialOutputBufferIndex = 0;
        constants.spatialResamplingConstants.spatialInputBufferIndex = 0;
        constants.spatialResamplingConstants.spatialOutputBufferIndex = 1;
        constants.shadingConstants.shadeInputBufferIndex = 1;
        break;
    case ResamplingMode::TemporalAndSpatial:
        constants.initialSamplingConstants.initialOutputBufferIndex = 0;
        constants.temporalResamplingConstants.temporalInputBufferIndex = 1;
        constants.temporalResamplingConstants.temporalOutputBufferIndex = 0;
        constants.spatialResamplingConstants.spatialInputBufferIndex = 0;
        constants.spatialResamplingConstants.spatialOutputBufferIndex = 1;
        constants.shadingConstants.shadeInputBufferIndex = 1;
        break;
    case ResamplingMode::FusedSpatiotemporal:
        constants.initialSamplingConstants.initialOutputBufferIndex = context.getFrameIndex() & 1;
        constants.temporalResamplingConstants.temporalInputBufferIndex = !constants.initialSamplingConstants.initialOutputBufferIndex;
        constants.spatialResamplingConstants.spatialOutputBufferIndex = constants.initialSamplingConstants.initialOutputBufferIndex;
        constants.shadingConstants.shadeInputBufferIndex = constants.spatialResamplingConstants.spatialOutputBufferIndex;
        break;
    }

    m_CurrentFrameGIOutputReservoir = constants.shadingConstants.shadeInputBufferIndex;
    
    constants.temporalResamplingConstants.temporalDepthThreshold = localSettings.reStirGI.depthThreshold;
    constants.temporalResamplingConstants.temporalNormalThreshold = localSettings.reStirGI.normalThreshold;
    constants.temporalResamplingConstants.maxHistoryLength = localSettings.reStirGI.maxHistoryLength;
    constants.temporalResamplingConstants.enablePermutationSampling = localSettings.reStirGI.enablePermutationSampling;
    constants.temporalResamplingConstants.temporalBiasCorrection = localSettings.reStirGI.temporalBiasCorrection;
    constants.spatialResamplingConstants.numSpatialSamples = localSettings.reStirGI.numSpatialSamples;
    constants.spatialResamplingConstants.spatialSamplingRadius = localSettings.reStirGI.samplingRadius;
    constants.spatialResamplingConstants.spatialDepthThreshold = localSettings.reStirGI.depthThreshold;
    constants.spatialResamplingConstants.spatialNormalThreshold = localSettings.reStirGI.normalThreshold;
    constants.spatialResamplingConstants.spatialBiasCorrection = localSettings.reStirGI.spatialBiasCorrection;
    constants.giSamplingConstants.giReservoirMaxAge = localSettings.reStirGI.maxReservoirAge;
    constants.giSamplingConstants.giEnableFinalVisibility = localSettings.reStirGI.enableFinalVisibility;

    // Pairwise bias correction is not supported, fallback to RT for safety (although UI should not allow it anyway)
    if (constants.temporalResamplingConstants.temporalBiasCorrection == RTXDI_BIAS_CORRECTION_PAIRWISE) {
        constants.temporalResamplingConstants.temporalBiasCorrection = RTXDI_BIAS_CORRECTION_RAY_TRACED;
    }
    if (constants.spatialResamplingConstants.spatialBiasCorrection == RTXDI_BIAS_CORRECTION_PAIRWISE) {
        constants.spatialResamplingConstants.spatialBiasCorrection = RTXDI_BIAS_CORRECTION_RAY_TRACED;
    }

    commandList->writeBuffer(m_ConstantBuffer, &constants, sizeof(constants));

    dm::int2 dispatchSize = {
        view.GetViewExtent().width(),
        view.GetViewExtent().height()
    };

    if (context.getStaticParameters().CheckerboardSamplingMode != rtxdi::CheckerboardMode::Off)
        dispatchSize.x /= 2;

    ExecuteRayTracingPass(commandList, m_BrdfRayTracingPass, localSettings.enableRayCounts, "BrdfRayTracingPass", dispatchSize, ProfilerSection::BrdfRays);

    if (enableIndirect)
    {
        // Place an explicit UAV barrier between the passes. See the note on barriers in RenderDirectLighting(...)
        nvrhi::utils::BufferUavBarrier(commandList, m_SecondarySurfaceBuffer);

        ExecuteRayTracingPass(commandList, m_ShadeSecondarySurfacesPass, localSettings.enableRayCounts, "ShadeSecondarySurfaces", dispatchSize, ProfilerSection::ShadeSecondary, nullptr);
        
        if (enableReStirGI)
        {
            if (localSettings.reStirGI.resamplingMode == ResamplingMode::FusedSpatiotemporal)
            {
                nvrhi::utils::BufferUavBarrier(commandList, m_GIReservoirBuffer);

                ExecuteRayTracingPass(commandList, m_GIFusedResamplingPass, localSettings.enableRayCounts, "GIFusedResampling", dispatchSize, ProfilerSection::GIFusedResampling, nullptr);
            }
            else
            {
                if (localSettings.reStirGI.resamplingMode == ResamplingMode::Temporal || 
                    localSettings.reStirGI.resamplingMode == ResamplingMode::TemporalAndSpatial)
                {
                    nvrhi::utils::BufferUavBarrier(commandList, m_GIReservoirBuffer);

                    ExecuteRayTracingPass(commandList, m_GITemporalResamplingPass, localSettings.enableRayCounts, "GITemporalResampling", dispatchSize, ProfilerSection::GITemporalResampling, nullptr);
                }

                if (localSettings.reStirGI.resamplingMode == ResamplingMode::Spatial ||
                    localSettings.reStirGI.resamplingMode == ResamplingMode::TemporalAndSpatial)
                {
                    nvrhi::utils::BufferUavBarrier(commandList, m_GIReservoirBuffer);

                    ExecuteRayTracingPass(commandList, m_GISpatialResamplingPass, localSettings.enableRayCounts, "GISpatialResampling", dispatchSize, ProfilerSection::GISpatialResampling, nullptr);
                }
            }

            nvrhi::utils::BufferUavBarrier(commandList, m_GIReservoirBuffer);

            ExecuteRayTracingPass(commandList, m_GIFinalShadingPass, localSettings.enableRayCounts, "GIFinalShading", dispatchSize, ProfilerSection::GIFinalShading, nullptr);
        }
    }
}

void LightingPasses::NextFrame()
{
    std::swap(m_BindingSet, m_PrevBindingSet);
    m_LastFrameOutputReservoir = m_CurrentFrameOutputReservoir;
}
