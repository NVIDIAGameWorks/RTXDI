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

#pragma pack_matrix(row_major)

#include "../RtxdiApplicationBridge/RtxdiApplicationBridge.hlsli"

#include <Rtxdi/LightSampling/PresamplingFunctions.hlsli>

[numthreads(256, 1, 1)]
void main(uint globalIndex : SV_DispatchThreadID)
{
    RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(uint2(globalIndex & 0xfff, globalIndex >> 12), g_Const.runtimeParams.frameIndex, 1);
    RTXDI_RandomSamplerState coherentRng = RTXDI_InitRandomSampler(uint2(globalIndex >> 8, 0), g_Const.runtimeParams.frameIndex, 1);

    RTXDI_PresampleLocalLightsForReGIR(rng, coherentRng, globalIndex, g_Const.lightBufferParams.localLightBufferRegion, g_Const.localLightsRISBufferSegmentParams, g_Const.regir);
}