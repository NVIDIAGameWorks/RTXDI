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
#include "../ShaderDebug/ShaderDebugPrint/ShaderDebugPrint.hlsli"

#include <Rtxdi/DI/SpatialResampling.hlsli>

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

    const RTXDI_RuntimeParameters params = g_Const.runtimeParams;

    uint2 pixelPosition = RTXDI_ReservoirPosToPixelPos(globalIndex, params.activeCheckerboardField);

    RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(pixelPosition, g_Const.runtimeParams.frameIndex, RTXDI_DI_SPATIAL_RESAMPLING_RANDOM_SEED);

    RAB_Surface surface = RAB_GetGBufferSurface(pixelPosition, false);

    RTXDI_DIReservoir spatialResult = RTXDI_EmptyDIReservoir();
    
    if (RAB_IsSurfaceValid(surface))
    {
        RTXDI_DIReservoir centerSample = RTXDI_LoadDIReservoir(g_Const.restirDI.reservoirBufferParams,
            globalIndex, g_Const.restirDI.bufferIndices.spatialResamplingInputBufferIndex);

		uint sourceBufferIndex = g_Const.restirDI.bufferIndices.spatialResamplingInputBufferIndex;
        RAB_LightSample lightSample = (RAB_LightSample)0;
        spatialResult = RTXDI_DISpatialResampling(pixelPosition, surface, centerSample, 
             rng, params, g_Const.restirDI.reservoirBufferParams, sourceBufferIndex, g_Const.restirDI.spatialResamplingParams, lightSample);
    }

    RTXDI_StoreDIReservoir(spatialResult, g_Const.restirDI.reservoirBufferParams, globalIndex, g_Const.restirDI.bufferIndices.spatialResamplingOutputBufferIndex);
}