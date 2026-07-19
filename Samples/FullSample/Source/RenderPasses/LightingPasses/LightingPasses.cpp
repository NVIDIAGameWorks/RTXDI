/*
 * SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#include "LightingPasses.h"
#include "RenderTargets.h"
#include "RtxdiResources.h"
#include "Profiler.h"
#include "Scene/Lights.h"
#include "RenderPasses/GBufferPass.h"

#include <donut/engine/Scene.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>
#include <donut/core/log.h>
#include <nvrhi/utils.h>
#include <donut/core/math/math.h>
#include <Rtxdi/ImportanceSamplingContext.h>

#include "ShaderDebug/ShaderPrint.h"
#include "ShaderDebug/PTPathViz/PTPathVizPass.h"

#ifdef _WIN32
#include "nvapi.h"
#endif

#include <utility>

#if WITH_NRD
#include <NRD.h>
#endif

using namespace donut::math;
#include "SharedShaderInclude/ShaderParameters.h"
#include "SharedShaderInclude/ShaderDebug/ReSTIRShaderDebugParameters.h"
#include "SharedShaderInclude/NvapiIntegration.h"

using namespace donut::engine;

BRDFPathTracing_MaterialOverrideParameters GetDefaultBRDFPathTracingMaterialOverrideParams()
{
    BRDFPathTracing_MaterialOverrideParameters params = {};
    params.metalnessOverride = 0.5;
    params.minSecondaryRoughness = 0.5;
    params.roughnessOverride = 0.5;
    return params;
}

BRDFPathTracing_SecondarySurfaceReSTIRDIParameters GetDefaultBRDFPathTracingSecondarySurfaceReSTIRDIParams()
{
    BRDFPathTracing_SecondarySurfaceReSTIRDIParameters params = {};

    params.initialSamplingParams.localLightSamplingMode = ReSTIRDI_LocalLightSamplingMode::ReGIR_RIS;
    params.initialSamplingParams.numLocalLightSamples = 2;
    params.initialSamplingParams.numInfiniteLightSamples = 1;
    params.initialSamplingParams.numEnvironmentSamples = 1;
    params.initialSamplingParams.numBrdfSamples = 0;
    params.initialSamplingParams.brdfCutoff = 0;
    params.initialSamplingParams.enableInitialVisibility = false;

    params.spatialResamplingParams.numSamples = 1;
    params.spatialResamplingParams.samplingRadius = 4.0f;
    params.spatialResamplingParams.biasCorrectionMode = ReSTIRDI_SpatialBiasCorrectionMode::Basic;
    params.spatialResamplingParams.numDisocclusionBoostSamples = 0; // Disabled
    params.spatialResamplingParams.depthThreshold = 0.1f;
    params.spatialResamplingParams.normalThreshold = 0.9f;

    return params;
}

BRDFPathTracing_Parameters GetDefaultBRDFPathTracingParams()
{
    BRDFPathTracing_Parameters params;
    params.enableIndirectEmissiveSurfaces = false;
    params.enableReSTIRGI = false;
    params.materialOverrideParams = GetDefaultBRDFPathTracingMaterialOverrideParams();
    params.secondarySurfaceReSTIRDIParams = GetDefaultBRDFPathTracingSecondarySurfaceReSTIRDIParams();
    return params;
}

PTParameters GetDefaultPTParameters()
{
    PTParameters params = {};

    params.lightSamplingMode = PTInitialSamplingLightSamplingMode::Mis;
    params.enableRussianRoulette = false;
    params.russianRouletteContinueChance = 0.80f;
    params.enableSecondaryDISpatialResampling = false;
    params.copyReSTIRDISimilarityThresholds = true;
    params.sampleEmissivesOnSecondaryHit = false;
    params.sampleEnvMapOnSecondaryMiss = false;
    params.extraMirrorBounceBudget = 1;
    params.minimumPathThroughput = 0.01f;
    params.nee.initialSamplingParams.brdfCutoff = 0.0;;
    params.nee.initialSamplingParams.brdfRayMinT = 0.001f;
    params.nee.initialSamplingParams.numBrdfSamples = 0;
    params.nee.initialSamplingParams.numEnvironmentSamples = 1;
    params.nee.initialSamplingParams.numInfiniteLightSamples = 1;
    params.nee.initialSamplingParams.numLocalLightSamples = 2;
    params.nee.initialSamplingParams.localLightSamplingMode = ReSTIRDI_LocalLightSamplingMode::Uniform;
    params.nee.initialSamplingParams.environmentMapImportanceSampling = true;
    params.nee.initialSamplingParams.enableInitialVisibility = true;
    params.nee.spatialResamplingParams.biasCorrectionMode = ReSTIRDI_SpatialBiasCorrectionMode::Basic;
    params.nee.spatialResamplingParams.depthThreshold = 0.1f;
    params.nee.spatialResamplingParams.normalThreshold = 0.9f;
    params.nee.spatialResamplingParams.samplingRadius = 4.0f;
    params.nee.spatialResamplingParams.numSamples = 1;
    params.nee.spatialResamplingParams.numDisocclusionBoostSamples = 0; // Disabled
    params.nee.spatialResamplingParams.samplingRadius = 1.0f;
    params.nee.spatialResamplingParams.discountNaiveSamples = false;
    params.nee.spatialResamplingParams.enableMaterialSimilarityTest = true;
    params.nee.spatialResamplingParams.targetHistoryLength = 0;

    return params;
}

ReSTIRShaderDebugParameters GetDefaultReSTIRShaderDebugParams()
{
    ReSTIRShaderDebugParameters params;
    params.mouseSelectedPixel = uint2(0, 0);
    params.spatialResamplingScreenSplitRatio = 0.5;
    params.outputDebugDirectLighting = false;
    params.outputDebugIndirectLighting = false;
    params.pad1 = 0;
    params.pad2 = 0;
    params.pad3 = 0;
    return params;
}

LightingPasses::LightingPasses(
    nvrhi::IDevice* device, 
    std::shared_ptr<ShaderFactory> shaderFactory,
    std::shared_ptr<donut::engine::CommonRenderPasses> commonPasses,
    std::shared_ptr<donut::engine::Scene> scene,
    std::shared_ptr<Profiler> profiler,
    nvrhi::IBindingLayout* bindlessLayout
)
    : m_device(device)
    , m_bindlessLayout(bindlessLayout)
    , m_shaderFactory(std::move(shaderFactory))
    , m_commonPasses(std::move(commonPasses))
    , m_scene(std::move(scene))
    , m_profiler(std::move(profiler))
    , m_restirDIPasses(m_profiler)
    , m_restirGIPasses(m_profiler)
    , m_restirPTPasses(m_profiler)
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

        nvrhi::BindingLayoutItem::Texture_UAV(20),
        nvrhi::BindingLayoutItem::Texture_UAV(21),
        nvrhi::BindingLayoutItem::Texture_UAV(22),
        nvrhi::BindingLayoutItem::Texture_UAV(23),
        nvrhi::BindingLayoutItem::Texture_UAV(24),
        nvrhi::BindingLayoutItem::Texture_UAV(25),
        nvrhi::BindingLayoutItem::Texture_UAV(26),
        nvrhi::BindingLayoutItem::Texture_UAV(27),
        nvrhi::BindingLayoutItem::Texture_UAV(28),

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
        nvrhi::BindingLayoutItem::StructuredBuffer_UAV(7),

        nvrhi::BindingLayoutItem::TypedBuffer_UAV(10),
        nvrhi::BindingLayoutItem::TypedBuffer_UAV(11),
        nvrhi::BindingLayoutItem::TypedBuffer_UAV(12),
        nvrhi::BindingLayoutItem::StructuredBuffer_UAV(13),

        // PTPathViz
        nvrhi::BindingLayoutItem::StructuredBuffer_UAV(14),
        nvrhi::BindingLayoutItem::StructuredBuffer_UAV(15),

        // shader debug print
        nvrhi::BindingLayoutItem::ConstantBuffer(2),
        nvrhi::BindingLayoutItem::RawBuffer_UAV(16),

        // Debug lighting for direct + indirect
        nvrhi::BindingLayoutItem::Texture_UAV(17),
        nvrhi::BindingLayoutItem::Texture_UAV(18),

        nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
        nvrhi::BindingLayoutItem::PushConstants(1, sizeof(PerPassConstants)),
        nvrhi::BindingLayoutItem::Sampler(0),
        nvrhi::BindingLayoutItem::Sampler(1),
    };

    // NVAPI binding
    if (m_device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D12)
        globalBindingLayoutDesc.bindings.push_back(nvrhi::BindingLayoutItem::TypedBuffer_UAV(RTXDI_NVAPI_SHADER_EXT_SLOT));

    m_bindingLayout = m_device->createBindingLayout(globalBindingLayoutDesc);

    m_constantBuffer = m_device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(ResamplingConstants), "ResamplingConstants", 16));
}

void LightingPasses::CreateBindingSet(
    nvrhi::rt::IAccelStruct* topLevelAS,
    nvrhi::rt::IAccelStruct* prevTopLevelAS,
    const RenderTargets& renderTargets,
    PTPathVizPass& ptPathVizPass,
    ShaderPrintReadbackPass& shaderPrintReadbackPass,
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

            nvrhi::BindingSetItem::Texture_UAV(20, renderTargets.PSRDepth),
            nvrhi::BindingSetItem::Texture_UAV(21, renderTargets.PSRNormalRoughness),
            nvrhi::BindingSetItem::Texture_UAV(22, renderTargets.PSRMotionVectors),
            nvrhi::BindingSetItem::Texture_UAV(23, renderTargets.PSRHitT),
            nvrhi::BindingSetItem::Texture_UAV(24, renderTargets.PSRDiffuseAlbedo),
            nvrhi::BindingSetItem::Texture_UAV(25, renderTargets.PSRSpecularF0),
            nvrhi::BindingSetItem::Texture_UAV(26, renderTargets.PSRLightDir),
            nvrhi::BindingSetItem::Texture_UAV(27, renderTargets.PTSampleIDTexture),
            nvrhi::BindingSetItem::Texture_UAV(28, renderTargets.PTDuplicationMap),

            nvrhi::BindingSetItem::RayTracingAccelStruct(30, currentFrame ? topLevelAS : prevTopLevelAS),
            nvrhi::BindingSetItem::RayTracingAccelStruct(31, currentFrame ? prevTopLevelAS : topLevelAS),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(32, m_scene->GetInstanceBuffer()),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(33, m_scene->GetGeometryBuffer()),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(34, m_scene->GetMaterialBuffer()),

            nvrhi::BindingSetItem::StructuredBuffer_SRV(20, resources.LightDataBuffer),
            nvrhi::BindingSetItem::TypedBuffer_SRV(21, resources.NeighborOffsetsBuffer),
            nvrhi::BindingSetItem::TypedBuffer_SRV(22, resources.LightIndexMappingBuffer),
            nvrhi::BindingSetItem::Texture_SRV(23, resources.EnvironmentPdfTexture),
            nvrhi::BindingSetItem::Texture_SRV(24, resources.LocalLightPdfTexture),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(25, resources.GeometryInstanceToLightBuffer),

            nvrhi::BindingSetItem::StructuredBuffer_UAV(0, resources.DIReservoirBuffer),
            nvrhi::BindingSetItem::Texture_UAV(1, renderTargets.DiffuseLighting),
            nvrhi::BindingSetItem::Texture_UAV(2, renderTargets.SpecularLighting),
            nvrhi::BindingSetItem::Texture_UAV(3, renderTargets.TemporalSamplePositions),
            nvrhi::BindingSetItem::Texture_UAV(4, renderTargets.Gradients),
            nvrhi::BindingSetItem::Texture_UAV(5, currentFrame ? renderTargets.RestirLuminance : renderTargets.PrevRestirLuminance),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(6, resources.GIReservoirBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(7, resources.PTReservoirBuffer),

            nvrhi::BindingSetItem::TypedBuffer_UAV(10, resources.RisBuffer),
            nvrhi::BindingSetItem::TypedBuffer_UAV(11, resources.RisLightDataBuffer),
            nvrhi::BindingSetItem::TypedBuffer_UAV(12, m_profiler->GetRayCountBuffer()),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(13, resources.SecondaryGBuffer),

            nvrhi::BindingSetItem::StructuredBuffer_UAV(14, ptPathVizPass.GetPathRecordBuffer()),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(15, ptPathVizPass.GetPathVertexCountBuffer()),

            nvrhi::BindingSetItem::ConstantBuffer(2, shaderPrintReadbackPass.GetCBDataBuffer()),
            nvrhi::BindingSetItem::RawBuffer_UAV(16,shaderPrintReadbackPass.GetPrintBuffer()),

            nvrhi::BindingSetItem::Texture_UAV(17, renderTargets.DirectLightingRaw),
            nvrhi::BindingSetItem::Texture_UAV(18, renderTargets.IndirectLightingRaw),

            nvrhi::BindingSetItem::ConstantBuffer(0, m_constantBuffer),
            nvrhi::BindingSetItem::PushConstants(1, sizeof(PerPassConstants)),
            nvrhi::BindingSetItem::Sampler(0, m_commonPasses->m_LinearWrapSampler),
            nvrhi::BindingSetItem::Sampler(1, m_commonPasses->m_LinearWrapSampler)
        };

        // NVAPI binding
        if (m_device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D12)
            bindingSetDesc.bindings.push_back(nvrhi::BindingSetItem::TypedBuffer_UAV(RTXDI_NVAPI_SHADER_EXT_SLOT, nullptr));


        const nvrhi::BindingSetHandle bindingSet = m_device->createBindingSet(bindingSetDesc, m_bindingLayout);

        if (currentFrame)
            m_bindingSet = bindingSet;
        else
            m_prevBindingSet = bindingSet;
    }

    const auto& environmentPdfDesc = resources.EnvironmentPdfTexture->getDesc();
    m_environmentPdfTextureSize.x = environmentPdfDesc.width;
    m_environmentPdfTextureSize.y = environmentPdfDesc.height;
    
    const auto& localLightPdfDesc = resources.LocalLightPdfTexture->getDesc();
    m_localLightPdfTextureSize.x = localLightPdfDesc.width;
    m_localLightPdfTextureSize.y = localLightPdfDesc.height;

    m_DIReservoirBuffer = resources.DIReservoirBuffer;
    m_secondarySurfaceBuffer = resources.SecondaryGBuffer;
    m_GIReservoirBuffer = resources.GIReservoirBuffer;

    m_PTReservoirBuffer = resources.PTReservoirBuffer;
}

void LightingPasses::CreateComputePass(ComputePass& pass, const char* shaderName, const std::vector<donut::engine::ShaderMacro>& macros)
{
    donut::log::debug("Initializing ComputePass %s...", shaderName);

    pass.Shader = m_shaderFactory->CreateShader(shaderName, "main", &macros, nvrhi::ShaderType::Compute);

    nvrhi::ComputePipelineDesc pipelineDesc;
    pipelineDesc.bindingLayouts = { m_bindingLayout, m_bindlessLayout };
    pipelineDesc.CS = pass.Shader;
    pass.Pipeline = m_device->createComputePipeline(pipelineDesc);
}

void LightingPasses::ExecuteComputePass(nvrhi::ICommandList* commandList, ComputePass& pass, const char* passName, dm::int2 dispatchSize, ProfilerSection::Enum profilerSection)
{
    commandList->beginMarker(passName);
    m_profiler->BeginSection(commandList, profilerSection);

    nvrhi::ComputeState state;
    state.bindings = { m_bindingSet, m_scene->GetDescriptorTable() };
    state.pipeline = pass.Pipeline;
    commandList->setComputeState(state);

    PerPassConstants pushConstants{};
    pushConstants.rayCountBufferIndex = -1;
    commandList->setPushConstants(&pushConstants, sizeof(pushConstants));

    commandList->dispatch(dispatchSize.x, dispatchSize.y, 1);

    m_profiler->EndSection(commandList, profilerSection);
    commandList->endMarker();
}

void LightingPasses::ExecuteRayTracingPass(nvrhi::ICommandList* commandList, RayTracingPass& pass, bool enableRayCounts, const char* passName, dm::int2 dispatchSize, ProfilerSection::Enum profilerSection, nvrhi::IBindingSet* extraBindingSet)
{
    commandList->beginMarker(passName);
    m_profiler->BeginSection(commandList, profilerSection);

    PerPassConstants pushConstants{};
    pushConstants.rayCountBufferIndex = enableRayCounts ? profilerSection : -1;
    
    pass.Execute(commandList, dispatchSize.x, dispatchSize.y, m_bindingSet, extraBindingSet, m_scene->GetDescriptorTable(), &pushConstants, sizeof(pushConstants));
    
    m_profiler->EndSection(commandList, profilerSection);
    commandList->endMarker();
}

donut::engine::ShaderMacro LightingPasses::GetRegirMacro(const rtxdi::ReGIRStaticParameters& regirStaticParams)
{
    std::string regirMode;

    switch (regirStaticParams.Mode)
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

void LightingPasses::CreatePresamplingPipelines()
{
    CreateComputePass(m_presampleLightsPass, "app/LightingPasses/Presampling/PresampleLights.hlsl", {});
    CreateComputePass(m_presampleEnvironmentMapPass, "app/LightingPasses/Presampling/PresampleEnvironmentMap.hlsl", {});
}

void LightingPasses::CreateReGIRPipeline(const rtxdi::ReGIRStaticParameters& regirStaticParams, const std::vector<donut::engine::ShaderMacro>& regirMacros)
{
    if (regirStaticParams.Mode != rtxdi::ReGIRMode::Disabled)
    {
        CreateComputePass(m_presampleReGIR, "app/LightingPasses/Presampling/PresampleReGIR.hlsl", regirMacros);
    }
}

void LightingPasses::CreateReSTIRDIPipelines(const std::vector<donut::engine::ShaderMacro>& regirMacros, bool useRayQuery)
{
    m_restirDIPasses.LoadShaders(m_device, *m_shaderFactory, useRayQuery, regirMacros, m_bindingLayout, m_bindlessLayout);
}

void LightingPasses::CreateReSTIRGIPipelines(const std::vector<donut::engine::ShaderMacro>& regirMacros, bool useRayQuery)
{
    m_brdfRayTracingPass.Init(m_device, *m_shaderFactory, "app/LightingPasses/BrdfRayTracing.hlsl", {}, false, RTXDI_SCREEN_SPACE_GROUP_SIZE, m_bindingLayout, nullptr, m_bindlessLayout);
    m_shadeSecondarySurfacesPass.Init(m_device, *m_shaderFactory, "app/LightingPasses/ShadeSecondarySurfaces.hlsl", regirMacros, false, RTXDI_SCREEN_SPACE_GROUP_SIZE, m_bindingLayout, nullptr, m_bindlessLayout);
    m_restirGIPasses.LoadShaders(m_device, *m_shaderFactory, useRayQuery, m_bindingLayout, m_bindlessLayout);
}

void LightingPasses::CreateReSTIRPTPipelines(bool useRayQuery)
{
    m_restirPTPasses.LoadShaders(m_device, *m_shaderFactory, useRayQuery, m_bindingLayout, m_bindlessLayout);
}

void LightingPasses::CreatePipelines(const rtxdi::ReGIRStaticParameters& regirStaticParams, bool useRayQuery)
{
    std::vector<donut::engine::ShaderMacro> regirMacros = {
        GetRegirMacro(regirStaticParams)
    };

    CreatePresamplingPipelines();
    CreateReGIRPipeline(regirStaticParams, regirMacros);
    CreateReSTIRDIPipelines(regirMacros, useRayQuery);
    CreateReSTIRGIPipelines(regirMacros, useRayQuery);
    CreateReSTIRPTPipelines(useRayQuery);
}

#if WITH_NRD
static void NrdHitDistanceParamsToFloat4(const nrd::ReblurHitDistanceParameters* params, dm::float4& out)
{
    assert(params);
    out.x = params->A;
    out.y = params->B;
    out.z = params->C;
    out.w = params->D;
}
#endif

void FillReSTIRDIConstants(RTXDI_Parameters& params, const rtxdi::ReSTIRDIContext& restirDIContext, const RTXDI_LightBufferParameters& lightBufferParameters)
{
    params.reservoirBufferParams = restirDIContext.GetReservoirBufferParameters();
    params.bufferIndices = restirDIContext.GetBufferIndices();
    params.initialSamplingParams = restirDIContext.GetInitialSamplingParameters();
    params.initialSamplingParams.environmentMapImportanceSampling = lightBufferParameters.environmentLightParams.lightPresent;
    if (!params.initialSamplingParams.environmentMapImportanceSampling)
        params.initialSamplingParams.numEnvironmentSamples = 0;
    params.temporalResamplingParams = restirDIContext.GetTemporalResamplingParameters();
    params.boilingFilterParams = restirDIContext.GetBoilingFilterParameters();
    params.spatialResamplingParams = restirDIContext.GetSpatialResamplingParameters();
    params.spatioTemporalResamplingParams = restirDIContext.GetSpatioTemporalResamplingParameters();
    params.shadingParams = restirDIContext.GetShadingParameters();
}

void FillReGIRConstants(ReGIR_Parameters& params, const rtxdi::ReGIRContext& regirContext)
{
    auto staticParams = regirContext.GetReGIRStaticParameters();
    auto dynamicParams = regirContext.GetReGIRDynamicParameters();
    auto gridParams = regirContext.GetReGIRGridCalculatedParameters();
    auto onionParams = regirContext.GetReGIROnionCalculatedParameters();

    params.gridParams.cellsX = staticParams.gridParameters.GridSize.x;
    params.gridParams.cellsY = staticParams.gridParameters.GridSize.y;
    params.gridParams.cellsZ = staticParams.gridParameters.GridSize.z;

    params.commonParams.numRegirBuildSamples = dynamicParams.regirNumBuildSamples;
    params.commonParams.risBufferOffset = regirContext.GetReGIRCellOffset();
    params.commonParams.lightsPerCell = staticParams.LightsPerCell;
    params.commonParams.centerX = dynamicParams.center.x;
    params.commonParams.centerY = dynamicParams.center.y;
    params.commonParams.centerZ = dynamicParams.center.z;
    params.commonParams.cellSize = (staticParams.Mode == rtxdi::ReGIRMode::Onion)
        ? dynamicParams.regirCellSize * 0.5f // Onion operates with radii, while "size" feels more like diameter
        : dynamicParams.regirCellSize;
    params.commonParams.localLightSamplingFallbackMode = static_cast<uint32_t>(dynamicParams.fallbackSamplingMode);
    params.commonParams.localLightPresamplingMode = static_cast<uint32_t>(dynamicParams.presamplingMode);
    params.commonParams.samplingJitter = std::max(0.f, dynamicParams.regirSamplingJitter * 2.f);
    params.onionParams.cubicRootFactor = onionParams.regirOnionCubicRootFactor;
    params.onionParams.linearFactor = onionParams.regirOnionLinearFactor;
    params.onionParams.numLayerGroups = uint32_t(onionParams.regirOnionLayers.size());

    assert(onionParams.regirOnionLayers.size() <= RTXDI_ONION_MAX_LAYER_GROUPS);
    for (int group = 0; group < int(onionParams.regirOnionLayers.size()); group++)
    {
        params.onionParams.layers[group] = onionParams.regirOnionLayers[group];
        params.onionParams.layers[group].innerRadius *= params.commonParams.cellSize;
        params.onionParams.layers[group].outerRadius *= params.commonParams.cellSize;
    }

    assert(onionParams.regirOnionRings.size() <= RTXDI_ONION_MAX_RINGS);
    for (int n = 0; n < int(onionParams.regirOnionRings.size()); n++)
    {
        params.onionParams.rings[n] = onionParams.regirOnionRings[n];
    }

    params.onionParams.cubicRootFactor = regirContext.GetReGIROnionCalculatedParameters().regirOnionCubicRootFactor;
}

void FillReSTIRGIConstants(RTXDI_GIParameters& constants, const rtxdi::ReSTIRGIContext& restirGIContext)
{
    constants.reservoirBufferParams = restirGIContext.GetReservoirBufferParameters();
    constants.bufferIndices = restirGIContext.GetBufferIndices();
    constants.temporalResamplingParams = restirGIContext.GetTemporalResamplingParameters();
    constants.boilingFilterParams = restirGIContext.GetBoilingFilterParameters();
    constants.spatialResamplingParams = restirGIContext.GetSpatialResamplingParameters();
    constants.spatioTemporalResamplingParams = restirGIContext.GetSpatioTemporalResamplingParameters();
    constants.finalShadingParams = restirGIContext.GetFinalShadingParameters();
}

void FillReSTIRPTConstants(RTXDI_PTParameters& constants, const rtxdi::ReSTIRPTContext& restirPTContext)
{
    constants.reservoirBuffer = restirPTContext.GetReservoirBufferParameters();
    constants.bufferIndices = restirPTContext.GetBufferIndices();
    constants.initialSampling = restirPTContext.GetInitialSamplingParameters();
    constants.hybridShift = restirPTContext.GetHybridShiftParameters();
    constants.reconnection = restirPTContext.GetReconnectionParameters();
    constants.temporalResampling = restirPTContext.GetTemporalResamplingParameters();
    constants.boilingFilter = restirPTContext.GetBoilingFilterParameters();
    constants.spatialResampling = restirPTContext.GetSpatialResamplingParameters();
}

void FillBRDFPTConstants(BRDFPathTracing_Parameters& constants, const GBufferSettings& gbufferSettings, const LightingPasses::RenderSettings& lightingSettings, const RTXDI_LightBufferParameters& lightBufferParameters)
{
    constants = lightingSettings.brdfptParams;
    constants.materialOverrideParams.minSecondaryRoughness = lightingSettings.brdfptParams.materialOverrideParams.minSecondaryRoughness;
    constants.materialOverrideParams.roughnessOverride = gbufferSettings.enableRoughnessOverride ? gbufferSettings.roughnessOverride : -1.f;
    constants.materialOverrideParams.metalnessOverride = gbufferSettings.enableMetalnessOverride ? gbufferSettings.metalnessOverride : -1.f;
    constants.secondarySurfaceReSTIRDIParams.initialSamplingParams.environmentMapImportanceSampling = lightBufferParameters.environmentLightParams.lightPresent;
    if (!constants.secondarySurfaceReSTIRDIParams.initialSamplingParams.environmentMapImportanceSampling)
        constants.secondarySurfaceReSTIRDIParams.initialSamplingParams.numEnvironmentSamples = 0;
}

void LightingPasses::FillResamplingConstants(
    ResamplingConstants& constants,
    const RenderSettings& lightingSettings,
    const rtxdi::ImportanceSamplingContext& isContext)
{
    const RTXDI_LightBufferParameters& lightBufferParameters = isContext.GetLightBufferParameters();

    constants.enablePreviousTLAS = lightingSettings.enablePreviousTLAS;
    constants.denoiserMode = lightingSettings.denoiserMode;
    constants.sceneConstants.enableAlphaTestedGeometry = lightingSettings.enableAlphaTestedGeometry;
    constants.sceneConstants.enableTransparentGeometry = lightingSettings.enableTransparentGeometry;
    constants.visualizeRegirCells = lightingSettings.visualizeRegirCells;
#if WITH_NRD
    if (lightingSettings.denoiserMode != DENOISER_MODE_OFF)
        NrdHitDistanceParamsToFloat4(lightingSettings.reblurHitDistanceParams, constants.reblurHitDistParams);

    constants.enableDenoiserPSR = lightingSettings.enableDenoiserPSR;
#endif
    constants.usePSRMvecForResampling = lightingSettings.usePSRMvecForResampling;
    constants.updatePSRwithResampling = lightingSettings.updatePSRwithResampling;

    constants.lightBufferParams = isContext.GetLightBufferParameters();
    constants.localLightsRISBufferSegmentParams = isContext.GetLocalLightRISBufferSegmentParams();
    constants.environmentLightRISBufferSegmentParams = isContext.GetEnvironmentLightRISBufferSegmentParams();
    constants.runtimeParams = isContext.GetReSTIRDIContext().GetRuntimeParams();
    FillReSTIRDIConstants(constants.restirDI, isContext.GetReSTIRDIContext(), isContext.GetLightBufferParameters());
    FillReGIRConstants(constants.regir, isContext.GetReGIRContext());
    FillReSTIRGIConstants(constants.restirGI, isContext.GetReSTIRGIContext());
    FillReSTIRPTConstants(constants.restirPT, isContext.GetReSTIRPTContext());
    constants.pt = lightingSettings.ptParameters;
    constants.debug = lightingSettings.restirShaderDebugParams;
    constants.localLightPdfTextureSize = m_localLightPdfTextureSize;
    constants.directLightingMode = lightingSettings.directLightingMode;

    if (lightBufferParameters.environmentLightParams.lightPresent)
    {
        constants.environmentPdfTextureSize = m_environmentPdfTextureSize;
    }

    m_currentFrameOutputReservoir = isContext.GetReSTIRDIContext().GetBufferIndices().shadingInputBufferIndex;
}

void LightingPasses::PrepareForLightSampling(
    nvrhi::ICommandList* commandList,
    rtxdi::ImportanceSamplingContext& isContext,
    const donut::engine::IView& view,
    const donut::engine::IView& previousView,
    const RenderSettings& localSettings,
    bool enableAccumulation)
{
    rtxdi::ReSTIRDIContext& restirDIContext = isContext.GetReSTIRDIContext();
    rtxdi::ReGIRContext& regirContext = isContext.GetReGIRContext();

    ResamplingConstants constants = {};
    constants.runtimeParams.frameIndex = restirDIContext.GetFrameIndex();
    view.FillPlanarViewConstants(constants.view);
    previousView.FillPlanarViewConstants(constants.prevView);
    FillResamplingConstants(constants, localSettings, isContext);
    constants.enableAccumulation = enableAccumulation;

    commandList->writeBuffer(m_constantBuffer, &constants, sizeof(constants));

    auto& lightBufferParams = isContext.GetLightBufferParameters();

    bool needLocalLightPresampling = isContext.IsLocalLightPowerRISEnabled() ||
        (localSettings.ptParameters.nee.initialSamplingParams.localLightSamplingMode == ReSTIRDI_LocalLightSamplingMode::Power_RIS ||
        localSettings.ptParameters.nee.initialSamplingParams.localLightSamplingMode == ReSTIRDI_LocalLightSamplingMode::ReGIR_RIS);

    if (needLocalLightPresampling && lightBufferParams.localLightBufferRegion.numLights > 0)
    {
        dm::int2 presampleDispatchSize = {
            dm::div_ceil(isContext.GetLocalLightRISBufferSegmentParams().tileSize, RTXDI_PRESAMPLING_GROUP_SIZE),
            int(isContext.GetLocalLightRISBufferSegmentParams().tileCount)
        };

        ExecuteComputePass(commandList, m_presampleLightsPass, "PresampleLights", presampleDispatchSize, ProfilerSection::PresampleLights);
    }

    if (lightBufferParams.environmentLightParams.lightPresent)
    {
        dm::int2 presampleDispatchSize = {
            dm::div_ceil(isContext.GetEnvironmentLightRISBufferSegmentParams().tileSize, RTXDI_PRESAMPLING_GROUP_SIZE),
            int(isContext.GetEnvironmentLightRISBufferSegmentParams().tileCount)
        };

        ExecuteComputePass(commandList, m_presampleEnvironmentMapPass, "PresampleEnvironmentMap", presampleDispatchSize, ProfilerSection::PresampleEnvMap);
    }

    if (isContext.IsReGIREnabled() &&
        lightBufferParams.localLightBufferRegion.numLights > 0)
    {
        dm::int2 worldGridDispatchSize = {
            dm::div_ceil(regirContext.GetReGIRLightSlotCount(), RTXDI_GRID_BUILD_GROUP_SIZE),
            1
        };

        ExecuteComputePass(commandList, m_presampleReGIR, "PresampleReGIR", worldGridDispatchSize, ProfilerSection::PresampleReGIR);
    }
}

void LightingPasses::RenderDirectLighting(
    nvrhi::ICommandList* commandList,
    rtxdi::ReSTIRDIContext& context,
    const donut::engine::IView& view,
    const RenderSettings& localSettings)
{
    m_restirDIPasses.EnableGradients(localSettings.enableGradients);
    m_restirDIPasses.EnableRayCounts(localSettings.enableRayCounts);
    m_restirDIPasses.SetDescriptorTable(m_scene->GetDescriptorTable());
    m_restirDIPasses.SetBindingSet(m_bindingSet);
    m_restirDIPasses.Render(commandList, view, context, m_DIReservoirBuffer);
}

void LightingPasses::RenderIndirectLighting(
    nvrhi::ICommandList* commandList, 
    rtxdi::ImportanceSamplingContext& isContext,
    const donut::engine::IView& view,
    const donut::engine::IView& previousView,
    const donut::engine::IView& previousPreviousView,
    const RenderSettings& localSettings,
    const GBufferSettings& gbufferSettings,
    const EnvironmentLight& environmentLight,
    IndirectLightingMode indirectLightingMode,
    bool enableAdditiveBlend,
    bool enableEmissiveSurfaces,
    bool enableAccumulation
    )
{
    ResamplingConstants constants = {};
    view.FillPlanarViewConstants(constants.view);
    previousView.FillPlanarViewConstants(constants.prevView);
    previousPreviousView.FillPlanarViewConstants(constants.prevPrevView);

    rtxdi::ReSTIRDIContext& restirDIContext = isContext.GetReSTIRDIContext();
    rtxdi::ReSTIRGIContext& restirGIContext = isContext.GetReSTIRGIContext();
    rtxdi::ReSTIRPTContext& restirPTContext = isContext.GetReSTIRPTContext();

    constants.denoiserMode = localSettings.denoiserMode;
    constants.enableBrdfIndirect = indirectLightingMode != IndirectLightingMode::None;
    constants.enableBrdfAdditiveBlend = enableAdditiveBlend;
    constants.enableAccumulation = enableAccumulation;
    constants.sceneConstants.enableEnvironmentMap = (environmentLight.textureIndex >= 0);
    constants.sceneConstants.environmentMapTextureIndex = (environmentLight.textureIndex >= 0) ? environmentLight.textureIndex : 0;
    constants.sceneConstants.environmentScale = environmentLight.radianceScale.x;
    constants.sceneConstants.environmentRotation = environmentLight.rotation;
    FillResamplingConstants(constants, localSettings, isContext);
    FillBRDFPTConstants(constants.brdfPT, gbufferSettings, localSettings, isContext.GetLightBufferParameters());
    constants.brdfPT.enableIndirectEmissiveSurfaces = enableEmissiveSurfaces;
    constants.brdfPT.enableReSTIRGI = indirectLightingMode == IndirectLightingMode::ReStirGI;
    constants.pt = localSettings.ptParameters;

    RTXDI_GIBufferIndices restirGIBufferIndices = restirGIContext.GetBufferIndices();
    m_currentFrameGIOutputReservoir = restirGIBufferIndices.finalShadingInputBufferIndex;

    commandList->writeBuffer(m_constantBuffer, &constants, sizeof(constants));

    dm::int2 dispatchSize = {
        view.GetViewExtent().width(),
        view.GetViewExtent().height()
    };

    if (restirDIContext.GetStaticParameters().CheckerboardSamplingMode != rtxdi::CheckerboardMode::Off)
        dispatchSize.x /= 2;
    if (localSettings.directLightingMode == DirectLightingMode::Brdf)
    {
        ExecuteRayTracingPass(commandList, m_brdfRayTracingPass, localSettings.enableRayCounts, "BrdfRayTracingPass", dispatchSize, ProfilerSection::BrdfRays);
    }
    if (indirectLightingMode != IndirectLightingMode::None)
    {
        // Place an explicit UAV barrier between the passes. See the note on barriers in RenderDirectLighting(...)
        nvrhi::utils::BufferUavBarrier(commandList, m_secondarySurfaceBuffer);

        if (indirectLightingMode == IndirectLightingMode::Brdf)
        {
            ExecuteRayTracingPass(commandList, m_brdfRayTracingPass, localSettings.enableRayCounts, "BrdfRayTracingPass", dispatchSize, ProfilerSection::BrdfRays);
            ExecuteRayTracingPass(commandList, m_shadeSecondarySurfacesPass, localSettings.enableRayCounts, "ShadeSecondarySurfaces", dispatchSize, ProfilerSection::ShadeSecondary, nullptr);
        }
        else if (indirectLightingMode == IndirectLightingMode::ReStirGI)
        {
            ExecuteRayTracingPass(commandList, m_brdfRayTracingPass, localSettings.enableRayCounts, "BrdfRayTracingPass", dispatchSize, ProfilerSection::BrdfRays);
            ExecuteRayTracingPass(commandList, m_shadeSecondarySurfacesPass, localSettings.enableRayCounts, "ShadeSecondarySurfaces", dispatchSize, ProfilerSection::ShadeSecondary, nullptr);

            m_restirGIPasses.EnableRayCounts(localSettings.enableRayCounts);
            m_restirGIPasses.SetDescriptorTable(m_scene->GetDescriptorTable());
            m_restirGIPasses.SetBindingSet(m_bindingSet);
            m_restirGIPasses.Render(commandList, view, restirGIContext, m_GIReservoirBuffer);
        }
        else if (indirectLightingMode == IndirectLightingMode::ReStirPT)
        {
            m_restirPTPasses.EnableRayCounts(localSettings.enableRayCounts);
            m_restirPTPasses.SetDescriptorTable(m_scene->GetDescriptorTable());
            m_restirPTPasses.SetBindingSet(m_bindingSet);
            m_restirPTPasses.Render(commandList, view, restirPTContext, m_PTReservoirBuffer);
        }
    }
}

nvrhi::BufferHandle LightingPasses::GetConstantBuffer()
{
    return m_constantBuffer;
}

void LightingPasses::NextFrame()
{
    std::swap(m_bindingSet, m_prevBindingSet);
    m_lastFrameOutputReservoir = m_currentFrameOutputReservoir;
}

nvrhi::IBindingLayout* LightingPasses::GetBindingLayout() const
{
    return m_bindingLayout;
}

nvrhi::IBindingSet* LightingPasses::GetCurrentBindingSet() const
{
    return m_bindingSet;
}

uint32_t LightingPasses::GetOutputReservoirBufferIndex() const
{
    return m_currentFrameOutputReservoir;
}

uint32_t LightingPasses::GetGIOutputReservoirBufferIndex() const
{
    return m_currentFrameGIOutputReservoir;
}
