/*
* Copyright (c) 2014-2023, NVIDIA CORPORATION. All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto. Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "ShaderParameters.h"
#include "HelperFunctions.hlsli"
#include <donut/shaders/sky.hlsli>
#include <donut/shaders/vulkan.hlsli>

VK_PUSH_CONSTANT ConstantBuffer<RenderEnvironmentMapConstants> g_Const : register(b0);

RWTexture2D<float4> u_EnvironmentMap : register(u0);

[numthreads(16, 16, 1)]
void main(uint2 GlobalIndex : SV_DispatchThreadId)
{
    float2 uv = (float2(GlobalIndex) + 0.5) * g_Const.invTextureSize;

    float cosElevation;
    float3 direction = equirectUVToDirection(uv, cosElevation);
    float angularSizeOfPixel = g_Const.invTextureSize.y * M_PI;

    float3 color = ProceduralSky(g_Const.params, direction, angularSizeOfPixel * 4);

    u_EnvironmentMap[GlobalIndex] = float4(color, 0);
}
