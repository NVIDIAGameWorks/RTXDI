/*
 * SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#pragma pack_matrix(row_major)

#include "SharedShaderInclude/ShaderParameters.h"
#include "HelperFunctions.hlsli"
#include <donut/shaders/binding_helpers.hlsli>

VK_PUSH_CONSTANT ConstantBuffer<AccumulationConstants> g_Const : register(b0);

RWTexture2D<float4> u_AccumulatedColor : register(u0);
RWTexture2D<float4> u_Visualization : register(u1);

Texture2D<float4> t_CompositedColor : register(t0);

SamplerState s_Sampler : register(s0);

[numthreads(8, 8, 1)]
void main(uint2 globalIndex : SV_DispatchThreadID)
{
    if (any(globalIndex > int2(g_Const.outputSize)))
        return;

    float4 prevColor = u_AccumulatedColor[globalIndex];

    float4 compositedColor;
    if (all(g_Const.inputSize == g_Const.outputSize))
    {
        compositedColor = t_CompositedColor[globalIndex];
    }
    else
    {
        float2 inputPos = (float2(globalIndex) + 0.5) * (g_Const.inputSize / g_Const.outputSize) + g_Const.pixelOffset;
        float2 inputUV = inputPos * g_Const.inputTextureSizeInv;
        
        compositedColor = t_CompositedColor.SampleLevel(s_Sampler, inputUV, 0);
    }
    
    float4 outputColor;
    if (g_Const.blendFactor < 1)
        outputColor = lerp(prevColor, compositedColor, g_Const.blendFactor);
    else
        outputColor = compositedColor;

    u_AccumulatedColor[globalIndex] = outputColor;
}
