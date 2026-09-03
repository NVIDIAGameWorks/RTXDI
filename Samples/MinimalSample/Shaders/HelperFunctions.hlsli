/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#ifndef HELPER_FUNCTIONS_HLSLI
#define HELPER_FUNCTIONS_HLSLI

#include <donut/shaders/utils.hlsli>
#include <Rtxdi/Utils/Math.hlsli>

struct RandomSamplerState
{
    uint seed;
    uint index;
};

RandomSamplerState InitRandomSampler(uint2 pixelPos, uint frameIndex)
{
    RandomSamplerState state;

    uint linearPixelIndex = RTXDI_ZCurveToLinearIndex(pixelPos);

    state.index = 1;
    state.seed = RTXDI_JenkinsHash(linearPixelIndex) + frameIndex;

    return state;
}

uint Murmur3(inout RandomSamplerState r)
{
#define ROT32(x, y) ((x << y) | (x >> (32 - y)))

    // https://en.wikipedia.org/wiki/MurmurHash
    uint c1 = 0xcc9e2d51;
    uint c2 = 0x1b873593;
    uint r1 = 15;
    uint r2 = 13;
    uint m = 5;
    uint n = 0xe6546b64;

    uint hash = r.seed;
    uint k = r.index++;
    k *= c1;
    k = ROT32(k, r1);
    k *= c2;

    hash ^= k;
    hash = ROT32(hash, r2) * m + n;

    hash ^= 4;
    hash ^= (hash >> 16);
    hash *= 0x85ebca6b;
    hash ^= (hash >> 13);
    hash *= 0xc2b2ae35;
    hash ^= (hash >> 16);

#undef ROT32

    return hash;
}

float SampleUniformRng(inout RandomSamplerState r)
{
    uint v = Murmur3(r);
    const uint one = asuint(1.f);
    const uint mask = (1 << 23) - 1;
    return asfloat((mask & v) | one) - 1.f;
}

float3 HitUVToBarycentric(float2 hitUV)
{
    return float3(1 - hitUV.x - hitUV.y, hitUV.x, hitUV.y);
}

// Inverse of SampleTriangle
float2 RandomFromBarycentric(float3 barycentric)
{
    float sqrtx = 1 - barycentric.x;
    return float2(sqrtx * sqrtx, barycentric.z / sqrtx);
}

float CalcLuminance(float3 color)
{
    return dot(color.xyz, float3(0.299f, 0.587f, 0.114f));
}

float3 BasicToneMapping(float3 color, float bias)
{
    float lum = CalcLuminance(color);

    if (lum > 0)
    {
        float newlum = lum / (bias + lum);
        color *= newlum / lum;
    }

    return color;
}

#endif // HELPER_FUNCTIONS_HLSLI