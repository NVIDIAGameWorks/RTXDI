/***************************************************************************
 # Copyright (c) 2021-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#include <donut/shaders/vulkan.hlsli>

Buffer<uint> t_Source : register(t0);
RWTexture2D<float> u_Dest : register(u0);

struct Constants {
    float scale;
};

VK_PUSH_CONSTANT ConstantBuffer<Constants> g_Const : register(b0);

// Converts the exposure (adapted luminance) value computed by Donut's ToneMappingPass
// into a texture consumable by DLSS.

[numthreads(1, 1, 1)]
void main()
{
    float adaptedLuminance = asfloat(t_Source[0]);

    float exposure = 1.0;
    if (adaptedLuminance > 0)
    {
        // Use the conversion suggested in the DLSS Programming Guide,
        // section 3.9 "Exposure Parameter"
        const float midGray = 0.18;
        exposure = midGray / (adaptedLuminance * (1.0 - midGray));
    }

    exposure *= g_Const.scale;

    u_Dest[int2(0, 0)] = exposure;
}