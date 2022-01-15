/***************************************************************************
 # Copyright (c) 2020-2022, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#include "RenderTargets.h"

#include <donut/engine/FramebufferFactory.h>

using namespace dm;
using namespace donut;

RenderTargets::RenderTargets(nvrhi::IDevice* device, int2 size)
    : Size(size)
{
    nvrhi::TextureDesc desc;
    desc.width = size.x;
    desc.height = size.y;
    desc.keepInitialState = true;
    
    desc.isRenderTarget = false;
    desc.isUAV = true;
    desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
    desc.useClearValue = false;
    desc.clearValue = 0.f;

    desc.format = nvrhi::Format::R32_FLOAT;
    desc.debugName = "DepthBuffer";
    Depth = device->createTexture(desc);
    desc.debugName = "PrevDepthBuffer";
    PrevDepth = device->createTexture(desc);
    
    desc.format = nvrhi::Format::R32_UINT;
    desc.debugName = "GBufferDiffuseAlbedo";
    GBufferDiffuseAlbedo = device->createTexture(desc);
    desc.debugName = "PrevGBufferDiffuseAlbedo";
    PrevGBufferDiffuseAlbedo = device->createTexture(desc);

    desc.format = nvrhi::Format::R32_UINT;
    desc.debugName = "GBufferSpecularRough";
    GBufferSpecularRough = device->createTexture(desc);
    desc.debugName = "PrevGBufferSpecularRough";
    PrevGBufferSpecularRough = device->createTexture(desc);

    desc.format = nvrhi::Format::R32_UINT;
    desc.debugName = "GBufferNormals";
    GBufferNormals = device->createTexture(desc);
    desc.debugName = "PrevGBufferNormals";
    PrevGBufferNormals = device->createTexture(desc);
    
    desc.format = nvrhi::Format::R32_UINT;
    desc.debugName = "GBufferGeoNormals";
    GBufferGeoNormals = device->createTexture(desc);
    desc.debugName = "PrevGBufferGeoNormals";
    PrevGBufferGeoNormals = device->createTexture(desc);
    
    desc.format = nvrhi::Format::RGBA16_FLOAT;
    desc.debugName = "HdrColor";
    HdrColor = device->createTexture(desc);
}

bool RenderTargets::IsUpdateRequired(int2 size)
{
    if (any(Size != size))
        return true;

    return false;
}

void RenderTargets::NextFrame()
{
    std::swap(Depth, PrevDepth);
    std::swap(GBufferDiffuseAlbedo, PrevGBufferDiffuseAlbedo);
    std::swap(GBufferSpecularRough, PrevGBufferSpecularRough);
    std::swap(GBufferNormals, PrevGBufferNormals);
    std::swap(GBufferGeoNormals, PrevGBufferGeoNormals);
}
