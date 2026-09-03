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

#include "../RtxdiApplicationBridge/RtxdiApplicationBridge.hlsli"

#include <Rtxdi/GI/SpatialResampling.hlsli>

#if USE_RAY_QUERY
[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 globalIndex : SV_DispatchThreadID)
#else
[shader("raygeneration")]
void RayGen()
#endif
{
#if !USE_RAY_QUERY
    uint2 globalIndex = DispatchRaysIndex().xy;
#endif
    uint2 pixelPosition = RTXDI_ReservoirPosToPixelPos(globalIndex, g_Const.runtimeParams.activeCheckerboardField);

    if (any(pixelPosition > int2(g_Const.view.viewportSize)))
        return;

    RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(globalIndex, g_Const.runtimeParams.frameIndex, RTXDI_GI_SPATIAL_RESAMPLING_RANDOM_SEED);
    
    const RAB_Surface primarySurface = RAB_GetGBufferSurface(pixelPosition, false);
    
    const uint2 reservoirPosition = RTXDI_PixelPosToReservoirPos(pixelPosition, g_Const.runtimeParams.activeCheckerboardField);
    RTXDI_GIReservoir reservoir = RTXDI_LoadGIReservoir(g_Const.restirGI.reservoirBufferParams, reservoirPosition, g_Const.restirGI.bufferIndices.spatialResamplingInputBufferIndex);

    if (RAB_IsSurfaceValid(primarySurface))
	{
		uint sourceBufferIndex = g_Const.restirGI.bufferIndices.spatialResamplingInputBufferIndex;
        RTXDI_GISpatialResamplingParameters sparams = g_Const.restirGI.spatialResamplingParams;

        reservoir = RTXDI_GISpatialResampling(pixelPosition, primarySurface, sourceBufferIndex, reservoir, rng, g_Const.runtimeParams, g_Const.restirGI.reservoirBufferParams, sparams);
    }

    RTXDI_StoreGIReservoir(reservoir, g_Const.restirGI.reservoirBufferParams, reservoirPosition, g_Const.restirGI.bufferIndices.spatialResamplingOutputBufferIndex);
}
