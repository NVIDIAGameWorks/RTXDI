/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#include "ReSTIRPTRenderPasses.h"

#include <donut/core/math/math.h>
#include <donut/engine/View.h>
#include <donut/engine/ShaderFactory.h>
#include <nvrhi/utils.h>

#include "Rtxdi/PT/ReSTIRPT.h"
#include "Profiler.h"
#include "ProfilerSections.h"
using namespace donut::math;
#include "SharedShaderInclude/NvapiIntegration.h"
#include "SharedShaderInclude/ShaderParameters.h"

ReSTIRPTRenderPasses::ReSTIRPTRenderPasses(std::shared_ptr<Profiler> profiler) :
    m_enableRayCounts(false),
    m_profiler(profiler),
    m_descriptorTable(nullptr)
{

}

ReSTIRPTRenderPasses::~ReSTIRPTRenderPasses()
{

}

void ReSTIRPTRenderPasses::LoadShaders(nvrhi::DeviceHandle device, donut::engine::ShaderFactory& shaderFactory, bool useRayQuery, nvrhi::BindingLayoutHandle bindingLayout, nvrhi::BindingLayoutHandle bindlessLayout)
{
    m_PTGenerateInitialSamplesPass.Init(device,shaderFactory, "app/LightingPasses/PT/GenerateInitialSamples.hlsl", {}, useRayQuery, RTXDI_SCREEN_SPACE_GROUP_SIZE, bindingLayout, nullptr, bindlessLayout, RTXDI_NVAPI_SHADER_EXT_SLOT);
    m_PTTemporalResamplingPass.Init(device, shaderFactory, "app/LightingPasses/PT/TemporalResampling.hlsl", {}, useRayQuery, RTXDI_SCREEN_SPACE_GROUP_SIZE, bindingLayout, nullptr, bindlessLayout, RTXDI_NVAPI_SHADER_EXT_SLOT);
    m_PTComputeDuplicationMapPass.Init(device, shaderFactory, "app/LightingPasses/PT/ComputeDuplicationMap.hlsl", {}, true, 16, bindingLayout, nullptr, bindlessLayout, RTXDI_NVAPI_SHADER_EXT_SLOT); // "useRayQuery == true" makes sure that this shader is compiled as a compute shader, as a raygen shader version is unsupported for this shader
    m_PTComputeSmoothedDuplicationMapPass.Init(device, shaderFactory, "app/LightingPasses/PT/ComputeSmoothedDuplicationMap.hlsl", {}, useRayQuery, 16, bindingLayout, nullptr, bindlessLayout, RTXDI_NVAPI_SHADER_EXT_SLOT);
    m_PTSpatialNeighborSelectionPass.Init(device, shaderFactory, "app/LightingPasses/PT/SpatialNeighborSelection.hlsl", {}, true, RTXDI_SCREEN_SPACE_GROUP_SIZE, bindingLayout, nullptr, bindlessLayout, RTXDI_NVAPI_SHADER_EXT_SLOT);
    m_PTSpatialResamplingPass.Init(device, shaderFactory, "app/LightingPasses/PT/SpatialResampling.hlsl", {}, useRayQuery, RTXDI_SCREEN_SPACE_GROUP_SIZE, bindingLayout, nullptr, bindlessLayout, RTXDI_NVAPI_SHADER_EXT_SLOT);
    m_PTFinalShadingPass.Init(device, shaderFactory, "app/LightingPasses/PT/FinalShading.hlsl", {}, false /*raygen*/, RTXDI_SCREEN_SPACE_GROUP_SIZE, bindingLayout, nullptr, bindlessLayout, RTXDI_NVAPI_SHADER_EXT_SLOT);
}

void ReSTIRPTRenderPasses::SetBindingSet(nvrhi::IBindingSet* bindingSet)
{
    m_bindingSet = bindingSet;
}

void ReSTIRPTRenderPasses::SetDescriptorTable(nvrhi::IDescriptorTable* descriptorTable)
{
    m_descriptorTable = descriptorTable;
}

static void ExecuteRayTracingPass(nvrhi::ICommandList* commandList,
    RayTracingPass& pass,
    bool enableRayCounts,
    const char* passName,
    dm::int2 dispatchSize,
    Profiler& profiler,
    ProfilerSection::Enum profilerSection,
    nvrhi::IDescriptorTable* descriptorTable,
    nvrhi::IBindingSet* bindingSet,
    nvrhi::IBindingSet* extraBindingSet = nullptr)
{
    commandList->beginMarker(passName);
    profiler.BeginSection(commandList, profilerSection);

    PerPassConstants pushConstants{};
    pushConstants.rayCountBufferIndex = enableRayCounts ? profilerSection : -1;

    pass.Execute(commandList, dispatchSize.x, dispatchSize.y, bindingSet, extraBindingSet, descriptorTable, &pushConstants, sizeof(pushConstants));

    profiler.EndSection(commandList, profilerSection);
    commandList->endMarker();
}

void ReSTIRPTRenderPasses::Render(nvrhi::ICommandList* commandList,
    const donut::engine::IView& view,
    rtxdi::ReSTIRPTContext& context,
    nvrhi::BufferHandle ptReservoirBuffer,
    nvrhi::BufferHandle spatialNeighborSelectionBuffer)
{
    dm::int2 dispatchSize = { view.GetViewExtent().width(), view.GetViewExtent().height() };

    bool spatialHeuristicMode = context.GetSpatialResamplingParameters().enableSpatialHeuristicMode != 0;
    bool hasSpatial = (context.GetResamplingMode() == rtxdi::ReSTIRPT_ResamplingMode::Spatial ||
                       context.GetResamplingMode() == rtxdi::ReSTIRPT_ResamplingMode::TemporalAndSpatial);

    bool hasTemporal = (context.GetResamplingMode() == rtxdi::ReSTIRPT_ResamplingMode::Temporal ||
                       context.GetResamplingMode() == rtxdi::ReSTIRPT_ResamplingMode::TemporalAndSpatial);

    nvrhi::utils::BufferUavBarrier(commandList, ptReservoirBuffer);
    ExecuteRayTracingPass(commandList, m_PTGenerateInitialSamplesPass, m_enableRayCounts, "PTGenerateInitialSamples", dispatchSize, *m_profiler, ProfilerSection::PTGenerateInitialSamples, m_descriptorTable, m_bindingSet, nullptr);

    if (spatialHeuristicMode && hasSpatial)
    {
        ExecuteRayTracingPass(commandList, m_PTSpatialNeighborSelectionPass, false, "PTSpatialNeighborSelection", dispatchSize, *m_profiler, ProfilerSection::PTSpatialNeighborSelection, m_descriptorTable, m_bindingSet, nullptr);
        nvrhi::utils::BufferUavBarrier(commandList, spatialNeighborSelectionBuffer);
    }

    switch (context.GetResamplingMode())
    {
    case rtxdi::ReSTIRPT_ResamplingMode::None:
        break;
    case rtxdi::ReSTIRPT_ResamplingMode::Temporal:
        ExecuteRayTracingPass(commandList, m_PTTemporalResamplingPass, m_enableRayCounts, "PTTemporalResampling", dispatchSize, *m_profiler, ProfilerSection::PTTemporalResampling, m_descriptorTable, m_bindingSet, nullptr);
        break;
    case rtxdi::ReSTIRPT_ResamplingMode::Spatial:
        ExecuteRayTracingPass(commandList, m_PTSpatialResamplingPass, m_enableRayCounts, "PTSpatialResampling", dispatchSize, *m_profiler, ProfilerSection::PTSpatialResampling, m_descriptorTable, m_bindingSet, nullptr);
        break;
    case rtxdi::ReSTIRPT_ResamplingMode::TemporalAndSpatial:
        ExecuteRayTracingPass(commandList, m_PTTemporalResamplingPass, m_enableRayCounts, "PTTemporalResampling", dispatchSize, *m_profiler, ProfilerSection::PTTemporalResampling, m_descriptorTable, m_bindingSet, nullptr);
        nvrhi::utils::BufferUavBarrier(commandList, ptReservoirBuffer);
        ExecuteRayTracingPass(commandList, m_PTSpatialResamplingPass, m_enableRayCounts, "PTSpatialResampling", dispatchSize, *m_profiler, ProfilerSection::PTSpatialResampling, m_descriptorTable, m_bindingSet, nullptr);
        break;
    }

    // Temporally-smoothed duplication map feeds the FinalShading decorrelation factor,
    // but only the Stagnancy-based mode actually consumes it. Skip the pass otherwise.
    const auto decorrelationParams = context.GetDecorrelationParameters();
    const bool needsSmoothedDupmap =
        decorrelationParams.decorrelationMode == RTXDI_PTDecorrelationMode::Stagnancy &&
        decorrelationParams.decorrelationFactor > 0.0f && hasTemporal;

    if (rtxdi::NeedsDuplicationMap(context.GetTemporalResamplingParameters(), decorrelationParams) && hasTemporal)
    {
        ExecuteRayTracingPass(commandList, m_PTComputeDuplicationMapPass, m_enableRayCounts, "PTComputeDuplicationMap", dispatchSize, *m_profiler, ProfilerSection::PTComputeDuplicationMap, m_descriptorTable, m_bindingSet, nullptr);
    }

    if (needsSmoothedDupmap)
    {
        nvrhi::utils::BufferUavBarrier(commandList, ptReservoirBuffer);
        ExecuteRayTracingPass(commandList, m_PTComputeSmoothedDuplicationMapPass, m_enableRayCounts, "PTComputeSmoothedDuplicationMap", dispatchSize, *m_profiler, ProfilerSection::PTComputeSmoothedDuplicationMap, m_descriptorTable, m_bindingSet, nullptr);
    }

    nvrhi::utils::BufferUavBarrier(commandList, ptReservoirBuffer);
    ExecuteRayTracingPass(commandList, m_PTFinalShadingPass, m_enableRayCounts, "PTFinalShading", dispatchSize, *m_profiler, ProfilerSection::PTFinalShading, m_descriptorTable, m_bindingSet, nullptr);
}

void ReSTIRPTRenderPasses::EnableRayCounts(bool enable)
{
    m_enableRayCounts = enable;
}

bool ReSTIRPTRenderPasses::RayCountsEnabled() const
{
    return m_enableRayCounts;
}
