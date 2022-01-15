/***************************************************************************
 # Copyright (c) 2020-2022, NVIDIA CORPORATION.  All rights reserved.
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

class RenderTargets
{
public:
    nvrhi::TextureHandle Depth;
    nvrhi::TextureHandle PrevDepth;
    nvrhi::TextureHandle GBufferDiffuseAlbedo;
    nvrhi::TextureHandle GBufferSpecularRough;
    nvrhi::TextureHandle GBufferNormals;
    nvrhi::TextureHandle GBufferGeoNormals;
    nvrhi::TextureHandle PrevGBufferDiffuseAlbedo;
    nvrhi::TextureHandle PrevGBufferSpecularRough;
    nvrhi::TextureHandle PrevGBufferNormals;
    nvrhi::TextureHandle PrevGBufferGeoNormals;
    
    nvrhi::TextureHandle HdrColor;

    dm::int2 Size;

    RenderTargets(nvrhi::IDevice* device, dm::int2 size);

    bool IsUpdateRequired(dm::int2 size);
    void NextFrame();
};
