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

#include "DebugVizPasses.h"

#include "../RenderTargets.h"
#include "../RtxdiResources.h"

#include <nvrhi/utils.h>

using namespace donut::math;
#include "SharedShaderInclude/ShaderParameters.h"
#include "SharedShaderInclude/ShaderDebug/StructuredBufferVisualizationParameters.h"

DebugVizPasses::DebugVizPasses(
    nvrhi::IDevice* device,
    std::shared_ptr<donut::engine::ShaderFactory> shaderFactory,
    std::shared_ptr<donut::engine::Scene> scene,
    nvrhi::IBindingLayout* bindlessLayout) :
    m_gBufferNormalsViz(std::make_unique<PackedDataVizPass>(device, shaderFactory, scene, bindlessLayout)),
    m_gBufferGeoNormalsViz(std::make_unique<PackedDataVizPass>(device, shaderFactory, scene, bindlessLayout)),
    m_gBufferDiffuseAlbedoViz(std::make_unique<PackedDataVizPass>(device, shaderFactory, scene, bindlessLayout)),
    m_gBufferSpecularRoughnessViz(std::make_unique<PackedDataVizPass>(device, shaderFactory, scene, bindlessLayout)),
    m_psrDiffuseAlbedoViz(std::make_unique<PackedDataVizPass>(device, shaderFactory, scene, bindlessLayout)),
    m_psrSpecularF0Viz(std::make_unique<PackedDataVizPass>(device, shaderFactory, scene, bindlessLayout)),
    m_ptDuplicationMapViz(std::make_unique<PackedDataVizPass>(device, shaderFactory, scene, bindlessLayout)),
    m_smoothedPtDuplicationMapViz(std::make_unique<PackedDataVizPass>(device, shaderFactory, scene, bindlessLayout)),
    m_ptDecorrelationFactorViz(std::make_unique<PackedDataVizPass>(device, shaderFactory, scene, bindlessLayout)),
    m_ptSampleIDViz(std::make_unique<PackedDataVizPass>(device, shaderFactory, scene, bindlessLayout))
{

}

void DebugVizPasses::CreatePipelines()
{
    m_gBufferNormalsViz->CreatePipeline("app/ShaderDebug/PackedDataVizPasses/NDirOctUNorm32Viz.hlsl");
    m_gBufferGeoNormalsViz->CreatePipeline("app/ShaderDebug/PackedDataVizPasses/NDirOctUNorm32Viz.hlsl");
    m_gBufferDiffuseAlbedoViz->CreatePipeline("app/ShaderDebug/PackedDataVizPasses/PackedR11G11B10UFloatViz.hlsl");
    m_gBufferSpecularRoughnessViz->CreatePipeline("app/ShaderDebug/PackedDataVizPasses/PackedR8G8B8A8GammaUFloatViz.hlsl");
    m_psrDiffuseAlbedoViz->CreatePipeline("app/ShaderDebug/PackedDataVizPasses/PackedR11G11B10UFloatViz.hlsl");
    m_psrSpecularF0Viz->CreatePipeline("app/ShaderDebug/PackedDataVizPasses/PackedR11G11B10UFloatViz.hlsl");
    m_ptDuplicationMapViz->CreatePipeline("app/ShaderDebug/PackedDataVizPasses/DuplicationMapViz.hlsl");
    m_smoothedPtDuplicationMapViz->CreatePipeline("app/ShaderDebug/PackedDataVizPasses/SmoothedDuplicationMapViz.hlsl");
    m_ptDecorrelationFactorViz->CreatePipeline("app/ShaderDebug/PackedDataVizPasses/DecorrelationFactorViz.hlsl");
    m_ptSampleIDViz->CreatePipeline("app/ShaderDebug/PackedDataVizPasses/SampleIDViz.hlsl");
}

void DebugVizPasses::CreateBindingSets(RenderTargets& renderTargets, RtxdiResources& rtxdiResources, nvrhi::TextureHandle dst)
{
    m_gBufferNormalsViz->CreateBindingSet(renderTargets.GBufferNormals, renderTargets.PrevGBufferNormals, dst);
    m_gBufferGeoNormalsViz->CreateBindingSet(renderTargets.GBufferGeoNormals, renderTargets.PrevGBufferGeoNormals, dst);
    m_gBufferDiffuseAlbedoViz->CreateBindingSet(renderTargets.GBufferDiffuseAlbedo, renderTargets.PrevGBufferDiffuseAlbedo, dst);
    m_gBufferSpecularRoughnessViz->CreateBindingSet(renderTargets.GBufferSpecularRough, renderTargets.PrevGBufferSpecularRough, dst);
    m_psrDiffuseAlbedoViz->CreateBindingSet(renderTargets.PSRDiffuseAlbedo, renderTargets.PSRDiffuseAlbedo, dst);
    m_psrSpecularF0Viz->CreateBindingSet(renderTargets.PSRSpecularF0, renderTargets.PSRSpecularF0, dst);
    m_ptDuplicationMapViz->CreateBindingSet(renderTargets.PTDuplicationMap, renderTargets.PTDuplicationMap, dst);
    // Smoothed PT dupmap ping-pongs: pass the two swapping textures so the viz pass
    // always reads the freshly-written one after its own NextFrame() swap.
    m_smoothedPtDuplicationMapViz->CreateBindingSet(renderTargets.SmoothedPTDuplicationMap, renderTargets.PrevSmoothedPTDuplicationMap, dst);
    m_ptDecorrelationFactorViz->CreateBindingSet(renderTargets.PTDecorrelationFactor, renderTargets.PTDecorrelationFactor, dst);
    m_ptSampleIDViz->CreateBindingSet(renderTargets.PTSampleIDTexture, renderTargets.PTSampleIDTexture, dst);
}

void DebugVizPasses::RenderUnpackedNormals(nvrhi::ICommandList* commandList, const donut::engine::IView& view)
{
    m_gBufferNormalsViz->Render(commandList, view);
}

void DebugVizPasses::RenderUnpackedGeoNormals(nvrhi::ICommandList* commandList, const donut::engine::IView& view)
{
    m_gBufferGeoNormalsViz->Render(commandList, view);
}

void DebugVizPasses::RenderUnpackedDiffuseAlbeo(nvrhi::ICommandList* commandList, const donut::engine::IView& view)
{
    m_gBufferDiffuseAlbedoViz->Render(commandList, view);
}

void DebugVizPasses::RenderUnpackedSpecularRoughness(nvrhi::ICommandList* commandList, const donut::engine::IView& view)
{
    m_gBufferSpecularRoughnessViz->Render(commandList, view);
}

void DebugVizPasses::RenderUnpackedPSRDiffuseAlbedo(nvrhi::ICommandList* commandList, const donut::engine::IView& view)
{
    m_psrDiffuseAlbedoViz->Render(commandList, view);
}

void DebugVizPasses::RenderUnpackedPSRSpecularF0(nvrhi::ICommandList* commandList, const donut::engine::IView& view)
{
    m_psrSpecularF0Viz->Render(commandList, view);
}

void DebugVizPasses::RenderPTDuplicationMap(nvrhi::ICommandList* commandList, const donut::engine::IView& view)
{
    m_ptDuplicationMapViz->Render(commandList, view);
}

void DebugVizPasses::RenderSmoothedPTDuplicationMap(nvrhi::ICommandList* commandList, const donut::engine::IView& view)
{
    m_smoothedPtDuplicationMapViz->Render(commandList, view);
}

void DebugVizPasses::RenderPTDecorrelationFactor(nvrhi::ICommandList* commandList, const donut::engine::IView& view)
{
    m_ptDecorrelationFactorViz->Render(commandList, view);
}

void DebugVizPasses::RenderPTSampleID(nvrhi::ICommandList* commandList, const donut::engine::IView& view)
{
    m_ptSampleIDViz->Render(commandList, view);
}

void DebugVizPasses::NextFrame()
{
    m_gBufferNormalsViz->NextFrame();
    m_gBufferGeoNormalsViz->NextFrame();
    m_gBufferDiffuseAlbedoViz->NextFrame();
    m_gBufferSpecularRoughnessViz->NextFrame();
    m_psrDiffuseAlbedoViz->NextFrame();
    m_psrSpecularF0Viz->NextFrame();
    m_ptDuplicationMapViz->NextFrame();
    m_smoothedPtDuplicationMapViz->NextFrame();
    m_ptDecorrelationFactorViz->NextFrame();
    m_ptSampleIDViz->NextFrame();
}