/***************************************************************************
 # Copyright (c) 2021-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#pragma once

#include <nvrhi/nvrhi.h>
#include <memory>
#include <donut/core/math/math.h>

namespace donut::engine
{
    class DirectionalLight;
    class DescriptorTableManager;
    class ShaderFactory;
}

namespace donut::render
{
    struct SkyParameters;
}

class RenderEnvironmentMapPass
{
private:
    nvrhi::ComputePipelineHandle m_Pipeline;
    nvrhi::BindingSetHandle m_BindingSet;
    nvrhi::TextureHandle m_DestinationTexture;

    std::shared_ptr<donut::engine::DescriptorTableManager> m_DescriptorTable;
    int m_DestinationTextureIndex = -1;

public:

    RenderEnvironmentMapPass(
        nvrhi::IDevice* device,
        std::shared_ptr<donut::engine::ShaderFactory> shaderFactory,
        std::shared_ptr<donut::engine::DescriptorTableManager> descriptorTable,
        uint32_t textureWidth);

    ~RenderEnvironmentMapPass();

    void Render(nvrhi::ICommandList* commandList, const donut::engine::DirectionalLight& light, const donut::render::SkyParameters& params);

    nvrhi::ITexture* GetTexture() const { return m_DestinationTexture; }
    int GetTextureIndex() const { return m_DestinationTextureIndex; }
};
