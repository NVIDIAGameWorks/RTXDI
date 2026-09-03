/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#ifndef RESERVOIR_VIZ_UTILS_HLSLI
#define RESERVOIR_VIZ_UTILS_HLSLI

uint Murmur3(uint r)
{
#define ROT32(x, y) ((x << y) | (x >> (32 - y)))

    // https://en.wikipedia.org/wiki/MurmurHash
    uint c1 = 0xcc9e2d51;
    uint c2 = 0x1b873593;
    uint r1 = 15;
    uint r2 = 13;
    uint m = 5;
    uint n = 0xe6546b64;

    uint hash = r;
    uint k = 39041; // random large prime number
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

float RandU2F(uint u)
{
    const uint one = asuint(1.f);
    const uint mask = (1 << 23) - 1;
    return asfloat((mask & u) | one) - 1.f;
}

// Draws a random number X from the sampler, so that (0 <= X < 1).
float4 IndexToColor(uint index)
{
    float r = RandU2F(Murmur3(index));
    float g = RandU2F(Murmur3(index + 239));
    float b = RandU2F(Murmur3(index + 701));
    return float4(r, g, b, 1.0);
}

float4 RgLerp(float t)
{
    float3 red = float3(1.0, 0.0, 0.0);
    float3 green = float3(0.0, 1.0, 0.0);
    return float4(lerp(red, green, t), 1.0);
}

#endif // RESERVOIR_VIZ_UTILS_HLSLI