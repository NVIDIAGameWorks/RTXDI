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

#ifndef RTXDI_RAB_NEIGHBOR_SELECTION_HLSLI
#define RTXDI_RAB_NEIGHBOR_SELECTION_HLSLI

#include "../../GBufferHelpers.hlsli"
#include "RAB_Buffers.hlsli"

// Reads the dedicated neighbor-selection G-buffer (world normal + view depth) and reconstructs the
// world position. Returns false for background pixels. Used by the spatial neighbor-selection heuristic.
bool RAB_GetNeighborSelectionSurface(int2 pixel, out float3 worldNormal, out float3 worldPos, out float viewDepth)
{
    float4 data = u_NeighborSelectionGBuffer[pixel];
    worldNormal = data.xyz;
    viewDepth = data.w;
    worldPos = float3(0.0f, 0.0f, 0.0f);
    if (viewDepth == BACKGROUND_DEPTH)
        return false;
    worldPos = ViewDepthToWorldPos(g_Const.view, pixel, viewDepth);
    return true;
}

#endif // RTXDI_RAB_NEIGHBOR_SELECTION_HLSLI
