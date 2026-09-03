/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#if USE_RAY_QUERY
#define RTXDI_ENABLE_BOILING_FILTER
#define RTXDI_BOILING_FILTER_GROUP_SIZE RTXDI_SCREEN_SPACE_GROUP_SIZE
#endif

#include "../RtxdiApplicationBridge/RtxdiApplicationBridge.hlsli"

#include <Rtxdi/GI/BoilingFilter.hlsli>
#include <Rtxdi/GI/SpatioTemporalResampling.hlsli>

#if USE_RAY_QUERY
[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 globalIndex : SV_DispatchThreadID, uint2 localIndex : SV_GroupThreadID)
#else
[shader("raygeneration")]
void RayGen()
#endif
{
#if !USE_RAY_QUERY
    uint2 globalIndex = DispatchRaysIndex().xy;
#endif
    uint2 pixelPosition = RTXDI_ReservoirPosToPixelPos(globalIndex, g_Const.runtimeParams.activeCheckerboardField);

    RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(globalIndex, g_Const.runtimeParams.frameIndex, RTXDI_GI_SPATIOTEMPORAL_RESAMPLING_RANDOM_SEED);
    
    const RAB_Surface primarySurface = RAB_GetGBufferSurface(pixelPosition, false);
    
    const uint2 reservoirPosition = RTXDI_PixelPosToReservoirPos(pixelPosition, g_Const.runtimeParams.activeCheckerboardField);
    RTXDI_GIReservoir reservoir = RTXDI_LoadGIReservoir(g_Const.restirGI.reservoirBufferParams, reservoirPosition, g_Const.restirGI.bufferIndices.secondarySurfaceReSTIRDIOutputBufferIndex);

    float3 motionVector = t_MotionVectors[pixelPosition].xyz;
    motionVector = ConvertMotionVectorToPixelSpace(g_Const.view, g_Const.prevView, pixelPosition, motionVector);

    if (RAB_IsSurfaceValid(primarySurface)) {
        RTXDI_GISpatioTemporalResamplingParameters stParams = g_Const.restirGI.spatioTemporalResamplingParams;

        uint sourceBufferIndex = g_Const.restirGI.bufferIndices.temporalResamplingInputBufferIndex;

        // Age threshold should vary.
        // This is to avoid to die a bunch of GI reservoirs at once at a disoccluded area.
        stParams.maxReservoirAge = g_Const.restirGI.temporalResamplingParams.maxReservoirAge * (0.5 + RTXDI_GetNextRandom(rng) * 0.5);

        // Execute resampling.
        reservoir = RTXDI_GISpatioTemporalResampling(pixelPosition, primarySurface, sourceBufferIndex, motionVector, reservoir, rng, g_Const.runtimeParams, g_Const.restirGI.reservoirBufferParams, stParams);
    }

#ifdef RTXDI_ENABLE_BOILING_FILTER
    if (g_Const.restirGI.boilingFilterParams.enableBoilingFilter)
    {
        RTXDI_GIBoilingFilter(localIndex, g_Const.restirGI.boilingFilterParams.boilingFilterStrength, reservoir);
    }
#endif

    RTXDI_StoreGIReservoir(reservoir, g_Const.restirGI.reservoirBufferParams, reservoirPosition, g_Const.restirGI.bufferIndices.spatialResamplingOutputBufferIndex);
}
