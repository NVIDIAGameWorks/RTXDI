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

VK_PUSH_CONSTANT ConstantBuffer<ConfidenceConstants> g_Const : register(b0);

Texture2DArray<float4> t_Gradients : register(t0);
Texture2D<float4> t_MotionVectors : register(t1);
Texture2D<float> t_PrevDiffuseConfidence : register(t2);
Texture2D<float> t_PrevSpecularConfidence : register(t3);
RWTexture2D<float> u_DiffuseConfidence : register(u0);
RWTexture2D<float> u_SpecularConfidence : register(u1);
SamplerState s_Sampler : register(s0);

// This shader implements the conversion of the filtered gradients into
// the confidence channel suitable for NRD consumption, and applies an exponential
// temporal filter on top of that confidence channel. Typically, the temporal filter
// has very short history, like 1 frame or less, just to reduce the flicker.

[numthreads(8, 8, 1)]
void main(uint2 globalIdx : SV_DispatchThreadID)
{
    if (any(globalIdx.xy >= g_Const.viewportSize))
        return;

    // Convert the output pixel position into UV in the gradients texture.
    float2 inputPos = (float2(globalIdx) + 0.5) / RTXDI_GRAD_FACTOR;

    if (g_Const.checkerboard)
        inputPos.x *= 0.5;

    inputPos.xy *= g_Const.invGradientTextureSize;
    
    // Sample the gradients texture with linear interpolation.
    float4 gradient = t_Gradients.SampleLevel(s_Sampler, float3(inputPos, g_Const.inputBufferIndex), 0);
    gradient = max(gradient, 0);

    // Apply the "darkness bias" to avoid discarding history because of noise on very dark surfaces.
    gradient.zw += g_Const.darknessBias * RTXDI_GRAD_STORAGE_SCALE;

    // Convert gradients to confidence.
    float diffuseConfidence = saturate(1.0 - gradient.x / gradient.z);
    float specularConfidence = saturate(1.0 - gradient.y / gradient.w);

    diffuseConfidence = saturate(pow(diffuseConfidence, g_Const.sensitivity));
    specularConfidence = saturate(pow(specularConfidence, g_Const.sensitivity));

    // Apply the temporal filter, if enabled.
    if (g_Const.blendFactor < 1.0)
    {
        // Find the previous input position using the motion vector.
        float2 motionVector = t_MotionVectors[globalIdx].xy;
        
        int2 prevInputPos = int2(float2(globalIdx) + 0.5 + motionVector);

        if (all(prevInputPos >= 0) && all(prevInputPos < g_Const.viewportSize))
        {
            // Blend the history in a non-linear space to make the result
            // hold on to lower confidence values longer than to high confidence.
            // We want to discard history for more than one frame when a singular change happens
            // (e.g. a light turning off), because ReSTIR needs a little time to stabilize.
            const float power = 0.25;

            // Load the previous confidence values
            float prevDiffuseConfidence = t_PrevDiffuseConfidence[prevInputPos];
            float prevSpecularConfidence = t_PrevSpecularConfidence[prevInputPos];

            // Convert the current and previous values to the non-linear space
            diffuseConfidence = pow(diffuseConfidence, power);
            specularConfidence = pow(specularConfidence, power);
            prevDiffuseConfidence = pow(prevDiffuseConfidence, power);
            prevSpecularConfidence = pow(prevSpecularConfidence, power);

            // Blend
            diffuseConfidence = lerp(prevDiffuseConfidence, diffuseConfidence, g_Const.blendFactor);
            specularConfidence = lerp(prevSpecularConfidence, specularConfidence, g_Const.blendFactor);

            // Convert the result back into linear space
            diffuseConfidence = pow(saturate(diffuseConfidence), 1.0 / power);
            specularConfidence = pow(saturate(specularConfidence), 1.0 / power);
        }
    }

    // Store the output
    u_DiffuseConfidence[globalIdx] = diffuseConfidence;
    u_SpecularConfidence[globalIdx] = specularConfidence;
}
