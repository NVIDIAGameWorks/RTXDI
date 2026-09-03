/*
 * SPDX-FileCopyrightText: Copyright (c) 2014-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#include "SharedShaderInclude/ShaderParameters.h"
#include "HelperFunctions.hlsli"
#include <donut/shaders/sky.hlsli>
#include <donut/shaders/binding_helpers.hlsli>

VK_PUSH_CONSTANT ConstantBuffer<RenderEnvironmentMapConstants> g_Const : register(b0);

RWTexture2D<float4> u_EnvironmentMap : register(u0);

[numthreads(16, 16, 1)]
void main(uint2 globalIndex : SV_DispatchThreadId)
{
    float2 uv = (float2(globalIndex) + 0.5) * g_Const.invTextureSize;

    float cosElevation;
    float3 direction = EquirectUVToDirection(uv, cosElevation);
    float angularSizeOfPixel = g_Const.invTextureSize.y * M_PI;

    float3 color = ProceduralSky(g_Const.params, direction, angularSizeOfPixel * 4);

    u_EnvironmentMap[globalIndex] = float4(color, 0);
}
