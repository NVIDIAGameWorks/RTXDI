/***************************************************************************
 # Copyright (c) 2020-2021, NVIDIA CORPORATION.  All rights reserved.
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

#include <donut/engine/Scene.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>
#include <donut/core/log.h>
#include <nvrhi/utils.h>
#include <rtxdi/RTXDI.h>

#include <utility>

#include "RtxgiIntegration.h"

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

#ifdef WITH_RTXGI
    auto rtxgiBindingLayoutDesc = nvrhi::BindingLayoutDesc()
        .setVisibility(nvrhi::ShaderType::Compute | nvrhi::ShaderType::AllRayTracing)
        .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(40))
        .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(41))
        .addItem(nvrhi::BindingLayoutItem::Sampler(40));

    m_RtxgiBindingLayout = m_Device->createBindingLayout(rtxgiBindingLayoutDesc);
#endif
}

void LightingPasses::CreateBindingSet(
    nvrhi::rt::IAccelStruct* topLevelAS,
    nvrhi::rt::IAccelStruct* prevTopLevelAS,
    const RenderTargets& renderTargets,
    const RtxdiResources& resources,
    const RtxgiIntegration* rtxgi)
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

            nvrhi::BindingSetItem::TypedBuffer_UAV(10, resources.RisBuffer),
            nvrhi::BindingSetItem::TypedBuffer_UAV(11, resources.RisLightDataBuffer),
            nvrhi::BindingSetItem::TypedBuffer_UAV(12, m_Profiler->GetRayCountBuffer()),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(13, resources.SecondarySurfaceBuffer),

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

#ifdef WITH_RTXGI
    assert(rtxgi);

    auto rtxgiBindingSetDesc = nvrhi::BindingSetDesc()
        .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(40, rtxgi->GetConstantBuffer()))
        .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(41, rtxgi->GetVolumeResourceIndicesBuffer()))
        .addItem(nvrhi::BindingSetItem::Sampler(40, rtxgi->GetProbeSampler()));

    m_RtxgiBindingSet = m_Device->createBindingSet(rtxgiBindingSetDesc, m_RtxgiBindingLayout);
#endif
    
    const auto& environmentPdfDesc = resources.EnvironmentPdfTexture->getDesc();
    m_EnvironmentPdfTextureSize.x = environmentPdfDesc.width;
    m_EnvironmentPdfTextureSize.y = environmentPdfDesc.height;
    
    const auto& localLightPdfDesc = resources.LocalLightPdfTexture->getDesc();
    m_LocalLightPdfTextureSize.x = localLightPdfDesc.width;
    m_LocalLightPdfTextureSize.y = localLightPdfDesc.height;

    m_LightReservoirBuffer = resources.LightReservoirBuffer;
    m_SecondarySurfaceBuffer = resources.SecondarySurfaceBuffer;
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

donut::engine::ShaderMacro LightingPasses::GetRegirMacro(const rtxdi::ContextParameters& contextParameters)
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

void LightingPasses::CreatePipelines(const rtxdi::ContextParameters& contextParameters, bool useRayQuery)
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
    m_ShadeSecondarySurfacesPass.Init(m_Device, *m_ShaderFactory, "app/LightingPasses/ShadeSecondarySurfaces.hlsl", regirMacros, useRayQuery, RTXDI_SCREEN_SPACE_GROUP_SIZE, m_BindingLayout, m_RtxgiBindingLayout, m_BindlessLayout);
    m_FusedResamplingPass.Init(m_Device, *m_ShaderFactory, "app/LightingPasses/FusedResampling.hlsl", regirMacros, useRayQuery, RTXDI_SCREEN_SPACE_GROUP_SIZE, m_BindingLayout, nullptr, m_BindlessLayout);
    m_GradientsPass.Init(m_Device, *m_ShaderFactory, "app/LightingPasses/ComputeGradients.hlsl", {}, useRayQuery, RTXDI_SCREEN_SPACE_GROUP_SIZE, m_BindingLayout, nullptr, m_BindlessLayout);
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
    const rtxdi::FrameParameters& frameParameters)
{
    constants.enablePreviousTLAS = lightingSettings.enablePreviousTLAS;
    constants.denoiserMode = lightingSettings.denoiserMode;
    constants.enableAlphaTestedGeometry = lightingSettings.enableAlphaTestedGeometry;
    constants.enableTransparentGeometry = lightingSettings.enableTransparentGeometry;
    constants.enableDenoiserInputPacking = lightingSettings.enableDenoiserInputPacking;
    constants.visualizeRegirCells = lightingSettings.visualizeRegirCells;
#if WITH_NRD
    if (lightingSettings.denoiserMode != DENOISER_MODE_OFF)
    {
        NrdHitDistanceParamsToFloat4(lightingSettings.reblurDiffHitDistanceParams, constants.reblurDiffHitDistParams);
        NrdHitDistanceParamsToFloat4(lightingSettings.reblurSpecHitDistanceParams, constants.reblurSpecHitDistParams);
    }
#endif

    constants.numPrimaryRegirSamples = lightingSettings.enableReGIR ? lightingSettings.numPrimaryRegirSamples : 0;
    constants.numPrimaryLocalLightSamples = lightingSettings.numPrimaryLocalLightSamples;
    constants.numPrimaryBrdfSamples = lightingSettings.numPrimaryBrdfSamples;
    constants.numPrimaryInfiniteLightSamples = lightingSettings.numPrimaryInfiniteLightSamples;
    constants.brdfCutoff = lightingSettings.brdfCutoff;
    constants.numIndirectRegirSamples = lightingSettings.enableReGIR ? lightingSettings.numIndirectRegirSamples : 0;
    constants.numIndirectLocalLightSamples = lightingSettings.numIndirectLocalLightSamples;
    constants.numIndirectInfiniteLightSamples = lightingSettings.numIndirectInfiniteLightSamples;

    constants.enableInitialVisibility = lightingSettings.enableInitialVisibility;
    constants.enableFinalVisibility = lightingSettings.enableFinalVisibility;
    constants.temporalNormalThreshold = lightingSettings.temporalNormalThreshold;
    constants.temporalDepthThreshold = lightingSettings.temporalDepthThreshold;
    constants.maxHistoryLength = lightingSettings.maxHistoryLength;
    constants.temporalBiasCorrection = lightingSettings.temporalBiasCorrection;
    constants.boilingFilterStrength = lightingSettings.enableBoilingFilter ? lightingSettings.boilingFilterStrength : 0.f;
    constants.numSpatialSamples = lightingSettings.enableSpatialResampling ? lightingSettings.numSpatialSamples : 0;
    constants.numDisocclusionBoostSamples = (lightingSettings.enableTemporalResampling || lightingSettings.useFusedKernel)
        ? lightingSettings.numDisocclusionBoostSamples
        : 0;
    constants.spatialSamplingRadius = lightingSettings.spatialSamplingRadius;
    constants.spatialNormalThreshold = lightingSettings.spatialNormalThreshold;
    constants.spatialDepthThreshold = lightingSettings.spatialDepthThreshold;
    constants.spatialBiasCorrection = lightingSettings.spatialBiasCorrection;
    constants.reuseFinalVisibility = lightingSettings.reuseFinalVisibility;
    constants.finalVisibilityMaxAge = lightingSettings.finalVisibilityMaxAge;
    constants.finalVisibilityMaxDistance = lightingSettings.finalVisibilityMaxDistance;
    constants.discardInvisibleSamples = lightingSettings.discardInvisibleSamples;
    constants.numRegirBuildSamples = lightingSettings.numRegirBuildSamples;
    constants.enablePermutationSampling = lightingSettings.enablePermutationSampling;
    constants.permutationSamplingThreshold = lightingSettings.permutationSamplingThreshold;

    constants.numSecondarySamples = lightingSettings.enableSecondaryResampling ? lightingSettings.numSecondarySamples : 0;
    constants.secondaryBiasCorrection = lightingSettings.secondaryBiasCorrection;
    constants.secondarySamplingRadius = lightingSettings.secondarySamplingRadius;
    constants.secondaryDepthThreshold = lightingSettings.secondaryDepthThreshold;
    constants.secondaryNormalThreshold = lightingSettings.secondaryNormalThreshold;

    if (lightingSettings.useFusedKernel)
    {
        constants.temporalInputBufferIndex = m_LastFrameOutputReservoir;
        constants.shadeInputBufferIndex = (m_LastFrameOutputReservoir + 1) % RtxdiResources::c_NumReservoirBuffers;
    }
    else
    {
        constants.initialOutputBufferIndex = (m_LastFrameOutputReservoir + 1) % RtxdiResources::c_NumReservoirBuffers;
        constants.temporalInputBufferIndex = m_LastFrameOutputReservoir;
        constants.temporalOutputBufferIndex = (constants.temporalInputBufferIndex + 1) % RtxdiResources::c_NumReservoirBuffers;
        constants.spatialInputBufferIndex = lightingSettings.enableTemporalResampling
            ? constants.temporalOutputBufferIndex
            : constants.initialOutputBufferIndex;
        constants.spatialOutputBufferIndex = (constants.spatialInputBufferIndex + 1) % RtxdiResources::c_NumReservoirBuffers;
        constants.shadeInputBufferIndex = lightingSettings.enableSpatialResampling
            ? constants.spatialOutputBufferIndex
            : constants.temporalOutputBufferIndex;
    }

    constants.localLightPdfTextureSize = m_LocalLightPdfTextureSize;
    
    if (frameParameters.environmentLightPresent)
    {
        constants.environmentPdfTextureSize = m_EnvironmentPdfTextureSize;
        constants.numPrimaryEnvironmentSamples = lightingSettings.numPrimaryEnvironmentSamples;
        constants.numIndirectEnvironmentSamples = lightingSettings.numIndirectEnvironmentSamples;
        constants.environmentMapImportanceSampling = 1;
    }

    m_CurrentFrameOutputReservoir = constants.shadeInputBufferIndex;
}

void LightingPasses::FillConstantBufferForProbeTracing(
    nvrhi::ICommandList* commandList,
    rtxdi::Context& context,
    const RenderSettings& localSettings,
    const rtxdi::FrameParameters& frameParameters)
{
    ResamplingConstants constants = {};
    constants.frameIndex = frameParameters.frameIndex;
    context.FillRuntimeParameters(constants.runtimeParams, frameParameters);
    FillResamplingConstants(constants, localSettings, frameParameters);

    constants.numIndirectRegirSamples = localSettings.enableReGIR ? localSettings.numRtxgiRegirSamples : 0;
    constants.numIndirectLocalLightSamples = localSettings.numRtxgiLocalLightSamples;
    constants.numIndirectInfiniteLightSamples = localSettings.numRtxgiInfiniteLightSamples;
    constants.numIndirectEnvironmentSamples = frameParameters.environmentLightPresent ? localSettings.numRtxgiEnvironmentSamples : 0;

    commandList->writeBuffer(m_ConstantBuffer, &constants, sizeof(constants));
}

void LightingPasses::Render(
    nvrhi::ICommandList* commandList,
    rtxdi::Context& context,
    const donut::engine::IView& view,
    const donut::engine::IView& previousView,
    const RenderSettings& localSettings,
    const rtxdi::FrameParameters& frameParameters,
    bool enableAccumulation,
    uint32_t visualizationMode)
{
    ResamplingConstants constants = {};
    constants.frameIndex = frameParameters.frameIndex;
    view.FillPlanarViewConstants(constants.view);
    previousView.FillPlanarViewConstants(constants.prevView);
    context.FillRuntimeParameters(constants.runtimeParams, frameParameters);
    FillResamplingConstants(constants, localSettings, frameParameters);
    constants.enableAccumulation = enableAccumulation;

    commandList->writeBuffer(m_ConstantBuffer, &constants, sizeof(constants));
    
    if (frameParameters.enableLocalLightImportanceSampling && 
        frameParameters.numLocalLights > 0)
    {
        dm::int2 presampleDispatchSize = {
            dm::div_ceil(context.GetParameters().TileSize, RTXDI_PRESAMPLING_GROUP_SIZE),
            int(context.GetParameters().TileCount)
        };

        ExecuteComputePass(commandList, m_PresampleLightsPass, "PresampleLights", presampleDispatchSize, ProfilerSection::PresampleLights);
    }

    if (frameParameters.environmentLightPresent)
    {
        dm::int2 presampleDispatchSize = {
            dm::div_ceil(context.GetParameters().EnvironmentTileSize, RTXDI_PRESAMPLING_GROUP_SIZE),
            int(context.GetParameters().EnvironmentTileCount)
        };

        ExecuteComputePass(commandList, m_PresampleEnvironmentMapPass, "PresampleEnvironmentMap", presampleDispatchSize, ProfilerSection::PresampleEnvMap);
    }

    if (context.GetParameters().ReGIR.Mode != rtxdi::ReGIRMode::Disabled && 
        localSettings.enableReGIR && 
        frameParameters.numLocalLights > 0)
    {
        dm::int2 worldGridDispatchSize = {
            dm::div_ceil(context.GetReGIRLightSlotCount(), RTXDI_GRID_BUILD_GROUP_SIZE),
            1
        };

        ExecuteComputePass(commandList, m_PresampleReGIR, "PresampleReGIR", worldGridDispatchSize, ProfilerSection::PresampleReGIR);
    }

    dm::int2 dispatchSize = { 
        view.GetViewExtent().width(),
        view.GetViewExtent().height()
    };

    if (context.GetParameters().CheckerboardSamplingMode != rtxdi::CheckerboardMode::Off)
        dispatchSize.x /= 2;

    // Run the lighting passes in the necessary sequence: one fused kernel or multiple separate passes.
    //
    // Note: the below code places explicit UAV barriers between subsequent passes
    // because NVRHI misses them, as the binding sets are exactly the same between these passes.
    // That equality makes NVRHI take a shortcut for performance and it doesn't look at bindings at all.

    if (localSettings.useFusedKernel)
    {
        ExecuteRayTracingPass(commandList, m_FusedResamplingPass, localSettings.enableRayCounts, "FusedResampling", dispatchSize, ProfilerSection::Shading);
    }
    else
    {
        ExecuteRayTracingPass(commandList, m_GenerateInitialSamplesPass, localSettings.enableRayCounts, "GenerateInitialSamples", dispatchSize, ProfilerSection::InitialSamples);

        if (localSettings.enableTemporalResampling)
        {
            nvrhi::utils::BufferUavBarrier(commandList, m_LightReservoirBuffer);

            ExecuteRayTracingPass(commandList, m_TemporalResamplingPass, localSettings.enableRayCounts, "TemporalResampling", dispatchSize, ProfilerSection::TemporalResampling);
        }

        if (localSettings.enableSpatialResampling)
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
    rtxdi::Context& context,
    const donut::engine::IView& view,
    const RenderSettings& localSettings,
    const rtxdi::FrameParameters& frameParameters,
    const EnvironmentLight& environmentLight,
    bool enableIndirect,
    bool enableAdditiveBlend,
    uint32_t numRtxgiVolumes,
    bool enableAccumulation)
{
    ResamplingConstants constants = {};
    view.FillPlanarViewConstants(constants.view);
    constants.frameIndex = frameParameters.frameIndex;
    constants.denoiserMode = localSettings.denoiserMode;
    constants.enableBrdfIndirect = enableIndirect;
    constants.enableBrdfAdditiveBlend = enableAdditiveBlend;
    constants.numRtxgiVolumes = numRtxgiVolumes;
    constants.enableEnvironmentMap = (environmentLight.textureIndex >= 0);
    constants.enableAccumulation = enableAccumulation;
    constants.environmentMapTextureIndex = (environmentLight.textureIndex >= 0) ? environmentLight.textureIndex : 0;
    constants.environmentScale = environmentLight.radianceScale.x;
    constants.environmentRotation = environmentLight.rotation;
    context.FillRuntimeParameters(constants.runtimeParams, frameParameters);
    FillResamplingConstants(constants, localSettings, frameParameters);

    commandList->writeBuffer(m_ConstantBuffer, &constants, sizeof(constants));

    dm::int2 dispatchSize = {
        view.GetViewExtent().width(),
        view.GetViewExtent().height()
    };

    if (context.GetParameters().CheckerboardSamplingMode != rtxdi::CheckerboardMode::Off)
        dispatchSize.x /= 2;

    ExecuteRayTracingPass(commandList, m_BrdfRayTracingPass, localSettings.enableRayCounts, "BrdfRayTracingPass", dispatchSize, ProfilerSection::BrdfRays);

    if (enableIndirect)
    {
        // Place an explicit UAV barrier between the passes. See the note on barriers in Render(...)
        nvrhi::utils::BufferUavBarrier(commandList, m_SecondarySurfaceBuffer);

        ExecuteRayTracingPass(commandList, m_ShadeSecondarySurfacesPass, localSettings.enableRayCounts, "ShadeSecondarySurfacesPass", dispatchSize, ProfilerSection::ShadeSecondary, m_RtxgiBindingSet);
    }
}

void LightingPasses::NextFrame()
{
    std::swap(m_BindingSet, m_PrevBindingSet);
    m_LastFrameOutputReservoir = m_CurrentFrameOutputReservoir;
}
