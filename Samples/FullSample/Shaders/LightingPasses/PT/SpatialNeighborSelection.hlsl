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

#pragma pack_matrix(row_major)

#include "../RtxdiApplicationBridge/RtxdiApplicationBridge.hlsli"
#include <Rtxdi/PT/SpatialNeighborSelection.hlsli>

[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 GlobalIndex : SV_DispatchThreadID)
{
    const uint2 pixelPosition = GlobalIndex;
    const uint2 viewportSize = g_Const.view.viewportSize;

    if (any(pixelPosition >= viewportSize))
        return;

    RTXDI_PTSpatialNeighborSelection(pixelPosition, viewportSize, g_Const.runtimeParams.frameIndex,
        g_Const.restirPT.spatialResampling.numSpatialSamples);
}
