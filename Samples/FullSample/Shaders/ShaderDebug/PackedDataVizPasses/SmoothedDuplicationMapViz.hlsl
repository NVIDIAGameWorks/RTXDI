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
 * Visualize temporally-smoothed PT duplication map (single-channel, age/40 EMA).
 */

Texture2D<float> t_SmoothedDuplicationMap : register(t0);
RWTexture2D<float4> t_Output : register(u0);

[numthreads(16, 16, 1)]
void main(uint2 pixelPosition : SV_DispatchThreadID)
{
    const float v = saturate(t_SmoothedDuplicationMap[pixelPosition]);
    t_Output[pixelPosition] = float4(0, v, 0, 1.0);
}
