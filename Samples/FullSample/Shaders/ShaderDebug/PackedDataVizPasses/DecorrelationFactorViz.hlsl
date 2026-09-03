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
 * Visualize the final per-pixel PT decorrelation probability written by FinalShading.
 * The value is already in [0, 1], so we display it
 * as-is in (green) grayscale: black = no decorrelation, full green = always decorrelate.
 */

Texture2D<float> t_DecorrelationFactor : register(t0);
RWTexture2D<float4> t_Output : register(u0);

[numthreads(16, 16, 1)]
void main(uint2 pixelPosition : SV_DispatchThreadID)
{
    const float v = saturate(t_DecorrelationFactor[pixelPosition]);
    t_Output[pixelPosition] = float4(0, pow(v,2.2), 0, 1.0);
}
