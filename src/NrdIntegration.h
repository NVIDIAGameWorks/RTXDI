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

#if WITH_NRD

#include <NRD.h>
#include <nvrhi/nvrhi.h>
#include <unordered_map>
#include <donut/engine/BindingCache.h>
#include <donut/core/math/math.h>

class RenderTargets;

namespace donut::engine
{
    class PlanarView;
}

class NrdIntegration
{
private:
    nvrhi::DeviceHandle m_Device;
    bool m_Initialized;
    nrd::Instance* m_Instance;
    nrd::Denoiser m_Denoiser;

    struct NrdPipeline
    {
        nvrhi::ShaderHandle Shader;
        nvrhi::BindingLayoutHandle BindingLayout;
        nvrhi::ComputePipelineHandle Pipeline;
    };

    nvrhi::BufferHandle m_ConstantBuffer;
    std::vector<NrdPipeline> m_Pipelines;
    std::vector<nvrhi::SamplerHandle> m_Samplers;
    std::vector<nvrhi::TextureHandle> m_PermanentTextures;
    std::vector<nvrhi::TextureHandle> m_TransientTextures;
    donut::engine::BindingCache m_BindingCache;
    dm::float2 m_PixelOffsetPrev;

public:
    NrdIntegration(nvrhi::IDevice* device, nrd::Denoiser denoiser);

    bool Initialize(uint32_t width, uint32_t height);
    bool IsAvailable() const;

    void RunDenoiserPasses(
        nvrhi::ICommandList* commandList,
        const RenderTargets& renderTargets, 
        const donut::engine::PlanarView& view, 
        const donut::engine::PlanarView& viewPrev,
        uint32_t frameIndex,
        bool enableConfidenceInputs,
        const void* denoiserSettings,
        float debug);

    const nrd::Denoiser GetDenoiser() const { return m_Denoiser; }
};

#endif
