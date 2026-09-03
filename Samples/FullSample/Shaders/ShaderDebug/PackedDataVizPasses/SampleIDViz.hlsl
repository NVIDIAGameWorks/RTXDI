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

/*
 * Visualize ReSTIR PT sample ID map with false colors: each unique sample ID
 * (RandomSeed) maps to a distinct RGB via hashing. ID 0 (invalid) = black.
 */

Texture2D<uint> t_SampleID : register(t0);
RWTexture2D<float4> t_Output : register(u0);

uint HashUint(uint x)
{
    x = (x ^ (x >> 16u)) * 0x85ebca6bu;
    x = (x ^ (x >> 13u)) * 0xc2b2ae35u;
    return x ^ (x >> 16u);
}

float3 IdToFalseColor(uint id)
{
    if (id == 0u)
        return float3(0.0, 0.0, 0.0);
    uint h = HashUint(id);
    return float3(
        float(h & 0xFFu) / 255.0,
        float((h >> 8u) & 0xFFu) / 255.0,
        float((h >> 16u) & 0xFFu) / 255.0
    );
}

[numthreads(16, 16, 1)]
void main(uint2 pixelPosition : SV_DispatchThreadID)
{
    uint sampleID = t_SampleID[pixelPosition];
    float3 color = IdToFalseColor(sampleID);
    t_Output[pixelPosition] = float4(color, 1.0);
}
