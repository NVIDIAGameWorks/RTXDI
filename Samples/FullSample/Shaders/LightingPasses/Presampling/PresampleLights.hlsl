/*
 * SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#include "../RtxdiApplicationBridge/RtxdiApplicationBridge.hlsli"

#include <Rtxdi/LightSampling/PresamplingFunctions.hlsli>

[numthreads(RTXDI_PRESAMPLING_GROUP_SIZE, 1, 1)] 
void main(uint2 globalIndex : SV_DispatchThreadID)
{
    RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(globalIndex.xy, g_Const.runtimeParams.frameIndex, 0);

    RTXDI_PresampleLocalLights(
        rng,
        t_LocalLightPdfTexture,
        g_Const.localLightPdfTextureSize,
        globalIndex.y,
        globalIndex.x,
        g_Const.lightBufferParams.localLightBufferRegion,
        g_Const.localLightsRISBufferSegmentParams);
}