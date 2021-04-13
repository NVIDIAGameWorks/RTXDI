/***************************************************************************
 # Copyright (c) 2020-2021, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#pragma once

#include <donut/core/math/math.h>
#include <nvrhi/nvrhi.h>
#include <memory>

namespace donut::engine
{
    class FramebufferFactory;
}

class RenderTargets
{
public:
    nvrhi::TextureHandle DeviceDepth;
    nvrhi::TextureHandle Depth;
    nvrhi::TextureHandle PrevDepth;
    nvrhi::TextureHandle GBufferBaseColor;
    nvrhi::TextureHandle GBufferMetalRough;
    nvrhi::TextureHandle GBufferNormals;
    nvrhi::TextureHandle GBufferGeoNormals;
    nvrhi::TextureHandle GBufferEmissive;
    nvrhi::TextureHandle PrevGBufferBaseColor;
    nvrhi::TextureHandle PrevGBufferMetalRough;
    nvrhi::TextureHandle PrevGBufferNormals;
    nvrhi::TextureHandle PrevGBufferGeoNormals;
    nvrhi::TextureHandle MotionVectors;
    nvrhi::TextureHandle NormalRoughness; // for NRD
    
    nvrhi::TextureHandle HdrColor;
    nvrhi::TextureHandle LdrColor;
    nvrhi::TextureHandle DiffuseLighting;
    nvrhi::TextureHandle SpecularLighting;
    nvrhi::TextureHandle DenoisedDiffuseLighting;
    nvrhi::TextureHandle DenoisedSpecularLighting;
    nvrhi::TextureHandle TaaFeedback1;
    nvrhi::TextureHandle TaaFeedback2;
    nvrhi::TextureHandle ResolvedColor;
    nvrhi::TextureHandle AccumulatedColor;
    
    std::shared_ptr<donut::engine::FramebufferFactory> LdrFramebuffer;
    std::shared_ptr<donut::engine::FramebufferFactory> GBufferFramebuffer;
    std::shared_ptr<donut::engine::FramebufferFactory> PrevGBufferFramebuffer;

    dm::int2 Size;

    RenderTargets(nvrhi::IDevice* device, dm::int2 size);

    bool IsUpdateRequired(dm::int2 size);
    void NextFrame();
};
