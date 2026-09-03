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
 * Compute a temporally-smoothed version of the PT duplication map's temporal duplication value (stagnancy/40).
 *
 * Current-frame value is derived from the final resampled reservoir's stagnancy.
 * We reproject the previous frame's smoothed value using motion vectors and blend with
 * an exponential-moving-average factor exposed to the user. The result drives the
 * FinalShading decorrelation probability, producing a lower-bias
 * and less noisy signal than raw per-frame stagnancy.
 */

#pragma pack_matrix(row_major)

#include "../RtxdiApplicationBridge/RtxdiApplicationBridge.hlsli"

#include "Rtxdi/Utils/ReservoirAddressing.hlsli"
#include <Rtxdi/PT/DuplicationMap.hlsli>

#define SMOOTHED_DUPMAP_GROUP_SIZE 16

#if USE_RAY_QUERY
[numthreads(SMOOTHED_DUPMAP_GROUP_SIZE, SMOOTHED_DUPMAP_GROUP_SIZE, 1)]
void main(uint2 GlobalIndex : SV_DispatchThreadID)
#else
[shader("raygeneration")]
void RayGen()
#endif
{
#if !USE_RAY_QUERY
    const uint2 GlobalIndex = DispatchRaysIndex().xy;
#endif
    const uint2 pixelPosition = RTXDI_ReservoirPosToPixelPos(GlobalIndex, g_Const.runtimeParams.activeCheckerboardField);
    if (any(int2(pixelPosition) >= int2(g_Const.view.viewportSize)))
        return;

    // Reproject into the previous frame using the same motion-vector source as temporal resampling.
    float3 motionVector = g_Const.usePSRMvecForResampling
        ? u_PSRMotionVectors[pixelPosition].xyz
        : t_MotionVectors[pixelPosition].xyz;
    motionVector = ConvertMotionVectorToPixelSpace(g_Const.view, g_Const.prevView, pixelPosition, motionVector);
    const int2 prevPixel = int2(round(float2(pixelPosition) + motionVector.xy));
    const bool prevValid = all(prevPixel >= 0) && all(prevPixel < int2(g_Const.view.viewportSize));

    RTXDI_PTComputeSmoothedDuplicationMap(pixelPosition, g_Const.view.viewportSize,
        g_Const.restirPT.decorrelation.decorrelationEmaFactor, prevPixel, prevValid);
}
