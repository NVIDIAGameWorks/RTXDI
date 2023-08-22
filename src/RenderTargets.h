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
    nvrhi::TextureHandle DeviceDepthUAV;
    nvrhi::TextureHandle Depth;
    nvrhi::TextureHandle PrevDepth;
    nvrhi::TextureHandle GBufferDiffuseAlbedo;
    nvrhi::TextureHandle GBufferSpecularRough;
    nvrhi::TextureHandle GBufferNormals;
    nvrhi::TextureHandle GBufferGeoNormals;
    nvrhi::TextureHandle GBufferEmissive;
    nvrhi::TextureHandle PrevGBufferDiffuseAlbedo;
    nvrhi::TextureHandle PrevGBufferSpecularRough;
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
    nvrhi::TextureHandle RestirLuminance;
    nvrhi::TextureHandle PrevRestirLuminance;
	
    nvrhi::TextureHandle Gradients;
    nvrhi::TextureHandle TemporalSamplePositions;
    nvrhi::TextureHandle DiffuseConfidence;
    nvrhi::TextureHandle SpecularConfidence;
    nvrhi::TextureHandle PrevDiffuseConfidence;
    nvrhi::TextureHandle PrevSpecularConfidence;

    nvrhi::TextureHandle DebugColor;
    nvrhi::TextureHandle ReferenceColor;

    std::shared_ptr<donut::engine::FramebufferFactory> LdrFramebuffer;
    std::shared_ptr<donut::engine::FramebufferFactory> ResolvedFramebuffer;
    std::shared_ptr<donut::engine::FramebufferFactory> GBufferFramebuffer;
    std::shared_ptr<donut::engine::FramebufferFactory> PrevGBufferFramebuffer;

    dm::int2 Size;

    RenderTargets(nvrhi::IDevice* device, dm::int2 size);

    bool IsUpdateRequired(dm::int2 size);
    void NextFrame();
};
