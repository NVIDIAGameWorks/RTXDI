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
 * Visualize PT duplication map RG channels as normalized values.
 */

Texture2D<float2> t_DuplicationMap : register(t0);
RWTexture2D<float4> t_Output : register(u0);

// red and green channels visualize spatial and temporal duplication respectively

[numthreads(16, 16, 1)]
void main(uint2 pixelPosition : SV_DispatchThreadID)
{
    t_Output[pixelPosition] = float4(t_DuplicationMap[pixelPosition].xy, 0.f, 1.0);
}
