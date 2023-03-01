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

