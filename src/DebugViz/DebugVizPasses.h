/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#pragma once

#include <memory>

#include "PackedDataVizPass.h"

class DebugVizPasses
{
public:
    DebugVizPasses(
        nvrhi::IDevice* device,
        std::shared_ptr<donut::engine::ShaderFactory> shaderFactory,
        std::shared_ptr<donut::engine::Scene> scene,
        nvrhi::IBindingLayout* bindlessLayout);

    void CreatePipelines();

    void CreateBindingSets(RenderTargets& renderTargets, nvrhi::TextureHandle dst);

    void RenderUnpackedNormals(nvrhi::ICommandList* commandList, const donut::engine::IView& view);
    void RenderUnpackedGeoNormals(nvrhi::ICommandList* commandList, const donut::engine::IView& view);
    void RenderUnpackedDiffuseAlbeo(nvrhi::ICommandList* commandList, const donut::engine::IView& view);
    void RenderUnpackedSpecularRoughness(nvrhi::ICommandList* commandList, const donut::engine::IView& view);

    void NextFrame();

private:
    std::unique_ptr<PackedDataVizPass> m_GBufferNormalsViz;
    std::unique_ptr<PackedDataVizPass> m_GBufferGeoNormalsViz;
    std::unique_ptr<PackedDataVizPass> m_GBufferDiffuseAlbedoViz;
    std::unique_ptr<PackedDataVizPass> m_GBufferSpecularRoughnessViz;
};

