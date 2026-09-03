/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
#include <Rtxdi/Utils/Math.hlsli>
#include <donut/shaders/binding_helpers.hlsli>

RWTexture2D<float> u_IntegratedMips[] : register(u0);

VK_PUSH_CONSTANT ConstantBuffer<PreprocessEnvironmentMapConstants> g_Const : register(b0);

#if INPUT_ENVIRONMENT_MAP
Texture2D<float4> t_EnvironmentMap : register(t0);

float GetPixelWeight(uint2 position)
{
    float3 color = t_EnvironmentMap[position].rgb;
    float luma = max(CalcLuminance(color), 0);

    // Do not sample invalid colors.
    if (isinf(luma) || isnan(luma))
        return 0;
    
    // Compute the solid angle of the pixel assuming equirectangular projection.
    // We don't need the absolute value of the solid angle here, just one at the same scale as the other pixels.
    float elevation = ((float(position.y) + 0.5) / float(g_Const.sourceSize.y) - 0.5) * c_pi;
    float relativeSolidAngle = cos(elevation);

    const float maxWeight = 65504.0; // maximum value that can be encoded in a float16 texture

    return clamp(luma * relativeSolidAngle, 0, maxWeight);
}
#endif

groupshared float s_weights[16];

// Warning: do not change the group size. The algorithm is hardcoded to process 16x16 tiles.
[numthreads(256, 1, 1)]
void main(uint2 groupIndex : SV_GroupID, uint threadIndex : SV_GroupThreadID)
{
    uint2 localIndex = RTXDI_LinearIndexToZCurve(threadIndex);
    uint2 globalIndex = (groupIndex * 16) + localIndex;

    // Step 0: Load a 2x2 quad of pixels from the source texture or the source mip level.
    float4 sourceWeights;
#if INPUT_ENVIRONMENT_MAP
    if (g_Const.sourceMipLevel == 0)
    {
        uint2 sourcePos = globalIndex.xy * 2;

        sourceWeights.x = GetPixelWeight(sourcePos + int2(0, 0));
        sourceWeights.y = GetPixelWeight(sourcePos + int2(0, 1));
        sourceWeights.z = GetPixelWeight(sourcePos + int2(1, 0));
        sourceWeights.w = GetPixelWeight(sourcePos + int2(1, 1));

        RWTexture2D<float> dest = u_IntegratedMips[0];
        dest[sourcePos + int2(0, 0)] = sourceWeights.x;
        dest[sourcePos + int2(0, 1)] = sourceWeights.y;
        dest[sourcePos + int2(1, 0)] = sourceWeights.z;
        dest[sourcePos + int2(1, 1)] = sourceWeights.w;
    }
    else
#endif
    {
        uint2 sourcePos = globalIndex.xy * 2;

        RWTexture2D<float> src = u_IntegratedMips[g_Const.sourceMipLevel];
        sourceWeights.x = src[sourcePos + int2(0, 0)];
        sourceWeights.y = src[sourcePos + int2(0, 1)];
        sourceWeights.z = src[sourcePos + int2(1, 0)];
        sourceWeights.w = src[sourcePos + int2(1, 1)];
    }

    uint mipLevelsToWrite = g_Const.numDestMipLevels - g_Const.sourceMipLevel - 1;
    if (mipLevelsToWrite < 1) return;

    // Average those weights and write out the first mip.
    float weight = (sourceWeights.x + sourceWeights.y + sourceWeights.z + sourceWeights.w) * 0.25;

    u_IntegratedMips[g_Const.sourceMipLevel + 1][globalIndex.xy] = weight;

    if (mipLevelsToWrite < 2) return;

    // The following sequence is an optimized hierarchical downsampling algorithm using wave ops.
    // It assumes that the wave size is at least 16 lanes, which is true for both NV and AMD GPUs.
    // It also assumes that the threads are laid out in the group using the Z-curve pattern.

    // Step 1: Average 2x2 groups of pixels.
    uint lane = WaveGetLaneIndex();
    weight = (weight 
        + WaveReadLaneAt(weight, lane + 1)
        + WaveReadLaneAt(weight, lane + 2)
        + WaveReadLaneAt(weight, lane + 3)) * 0.25;

    if ((lane & 3) == 0)
    {
        u_IntegratedMips[g_Const.sourceMipLevel + 2][globalIndex.xy >> 1] = weight;
    }

    if (mipLevelsToWrite < 3) return;

    // Step 2: Average the previous results from 2 pixels away.
    weight = (weight 
        + WaveReadLaneAt(weight, lane + 4)
        + WaveReadLaneAt(weight, lane + 8)
        + WaveReadLaneAt(weight, lane + 12)) * 0.25;

    if ((lane & 15) == 0)
    {
        u_IntegratedMips[g_Const.sourceMipLevel + 3][globalIndex.xy >> 2] = weight;

        // Store the intermediate result into shared memory.
        s_weights[threadIndex >> 4] = weight;
    }

    if (mipLevelsToWrite < 4) return;

    GroupMemoryBarrierWithGroupSync();

    // The rest operates on a 4x4 group of values for the entire thread group
    if (threadIndex >= 16)
        return;

    // Load the intermediate results
    weight = s_weights[threadIndex];

    // Change the output texture addressing because we'll be only writing a 2x2 block of pixels
    globalIndex = (groupIndex * 2) + (localIndex >> 1);

    // Step 3: Average the previous results from adjacent threads, meaning from 4 pixels away.
    weight = (weight 
        + WaveReadLaneAt(weight, lane + 1)
        + WaveReadLaneAt(weight, lane + 2)
        + WaveReadLaneAt(weight, lane + 3)) * 0.25;

    if ((lane & 3) == 0)
    {
        u_IntegratedMips[g_Const.sourceMipLevel + 4][globalIndex.xy] = weight;
    }

    if (mipLevelsToWrite < 5) return;

    // Step 4: Average the previous results from 8 pixels away.
    weight = (weight 
        + WaveReadLaneAt(weight, lane + 4)
        + WaveReadLaneAt(weight, lane + 8)
        + WaveReadLaneAt(weight, lane + 12)) * 0.25;

    if (lane == 0)
    {
        u_IntegratedMips[g_Const.sourceMipLevel + 5][globalIndex.xy >> 1] = weight;
    }
}