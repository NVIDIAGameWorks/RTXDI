/***************************************************************************
 # Copyright (c) 2020-2021, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#pragma pack_matrix(row_major)

#include "ShaderParameters.h"
#include <donut/shaders/vulkan.hlsli>

VK_PUSH_CONSTANT ConstantBuffer<AccumulationConstants> g_Const : register(b0);

RWTexture2D<float4> u_AccumulatedColor : register(u0);

Texture2D<float4> t_CompositedColor : register(t0);

[numthreads(8, 8, 1)]
void main(uint2 globalIdx : SV_DispatchThreadID)
{
    float4 compositedColor = t_CompositedColor[globalIdx];
    float4 prevColor = u_AccumulatedColor[globalIdx];

    float4 outputColor;
    if (g_Const.blendFactor < 1)
        outputColor = lerp(prevColor, compositedColor, g_Const.blendFactor);
    else
        outputColor = compositedColor;

    u_AccumulatedColor[globalIdx] = outputColor;
}
