/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#include "DebugVizPasses.h"

#include "../RenderTargets.h"

DebugVizPasses::DebugVizPasses(
    nvrhi::IDevice* device,
    std::shared_ptr<donut::engine::ShaderFactory> shaderFactory,
    std::shared_ptr<donut::engine::Scene> scene,
    nvrhi::IBindingLayout* bindlessLayout) :
    m_GBufferNormalsViz(std::make_unique<PackedDataVizPass>(device, shaderFactory, scene, bindlessLayout)),
    m_GBufferGeoNormalsViz(std::make_unique<PackedDataVizPass>(device, shaderFactory, scene, bindlessLayout)),
    m_GBufferDiffuseAlbedoViz(std::make_unique<PackedDataVizPass>(device, shaderFactory, scene, bindlessLayout)),
    m_GBufferSpecularRoughnessViz(std::make_unique<PackedDataVizPass>(device, shaderFactory, scene, bindlessLayout))
{

}

void DebugVizPasses::CreatePipelines()
{
    m_GBufferNormalsViz->CreatePipeline("app/DebugViz/NDirOctUNorm32Viz.hlsl");
    m_GBufferGeoNormalsViz->CreatePipeline("app/DebugViz/NDirOctUNorm32Viz.hlsl");
    m_GBufferDiffuseAlbedoViz->CreatePipeline("app/DebugViz/PackedR11G11B10UFloatViz.hlsl");
    m_GBufferSpecularRoughnessViz->CreatePipeline("app/DebugViz/PackedR8G8B8A8GammaUFloatViz.hlsl");
}

void DebugVizPasses::CreateBindingSets(RenderTargets& renderTargets, nvrhi::TextureHandle dst)
{
    m_GBufferNormalsViz->CreateBindingSet(renderTargets.GBufferNormals, renderTargets.PrevGBufferNormals, renderTargets.DebugColor);
    m_GBufferGeoNormalsViz->CreateBindingSet(renderTargets.GBufferGeoNormals, renderTargets.PrevGBufferGeoNormals, renderTargets.DebugColor);
    m_GBufferDiffuseAlbedoViz->CreateBindingSet(renderTargets.GBufferDiffuseAlbedo, renderTargets.PrevGBufferDiffuseAlbedo, renderTargets.DebugColor);
    m_GBufferSpecularRoughnessViz->CreateBindingSet(renderTargets.GBufferSpecularRough, renderTargets.PrevGBufferSpecularRough, renderTargets.DebugColor);
}

void DebugVizPasses::RenderUnpackedNormals(nvrhi::ICommandList* commandList, const donut::engine::IView& view)
{
    m_GBufferNormalsViz->Render(commandList, view);
}

void DebugVizPasses::RenderUnpackedGeoNormals(nvrhi::ICommandList* commandList, const donut::engine::IView& view)
{
    m_GBufferGeoNormalsViz->Render(commandList, view);
}

void DebugVizPasses::RenderUnpackedDiffuseAlbeo(nvrhi::ICommandList* commandList, const donut::engine::IView& view)
{
    m_GBufferDiffuseAlbedoViz->Render(commandList, view);
}

void DebugVizPasses::RenderUnpackedSpecularRoughness(nvrhi::ICommandList* commandList, const donut::engine::IView& view)
{
    m_GBufferSpecularRoughnessViz->Render(commandList, view);
}

void DebugVizPasses::NextFrame()
{
    m_GBufferNormalsViz->NextFrame();
    m_GBufferGeoNormalsViz->NextFrame();
    m_GBufferDiffuseAlbedoViz->NextFrame();
    m_GBufferSpecularRoughnessViz->NextFrame();
}