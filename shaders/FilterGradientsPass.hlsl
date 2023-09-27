/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
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

VK_PUSH_CONSTANT ConstantBuffer<FilterGradientsConstants> g_Const : register(b0);

RWTexture2DArray<float4> u_Gradients : register(u0);

// This shader implements an A-trous spatial filter on the gradients texture.
// The filter is applied repeatedly to get a wide blur with relatively few texture samples.

[numthreads(8, 8, 1)]
void main(uint2 globalIdx : SV_DispatchThreadID)
{
    if (any(globalIdx.xy >= g_Const.viewportSize))
        return;

    // The filtering step increases with each pass
    int2 step = 1l << g_Const.passIndex;
    const int inputBuffer = g_Const.passIndex & 1;

    // Preserve the filter aspect ratio when the gradients are half-resolution in the X dimension
    if (g_Const.checkerboard)
        step.x >>= 1;

    float4 acc = 0;
    float wSum = 0;

    // Accumulate 9 pixels: the center one and 8 neighbors +/- 'step' pixels away
    [unroll] for (int yy = -1; yy <= 1; ++yy)
    [unroll] for (int xx = -1; xx <= 1; ++xx)
    {
        int2 pos = globalIdx.xy + int2(xx, yy) * step;
        float4 c = u_Gradients[int3(pos, inputBuffer)];

        if (all(pos >= 0) && all(pos < int2(g_Const.viewportSize)))
        {
            // Triangular filter kernel produces a smooth result after a few iterations
            float w = (xx == 0 ? 1.0 : 0.5) * (yy == 0 ? 1.0 : 0.5);

            acc += c * w;
            wSum += w;
        }
    }

    // Normalize and store the output into the output buffer
    acc /= wSum;

    u_Gradients[int3(globalIdx, !inputBuffer)] = acc;
}
