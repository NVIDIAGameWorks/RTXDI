/***************************************************************************
 # Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#pragma pack_matrix(row_major)

#include "RtxdiApplicationBridge.hlsli"

#include <rtxdi/GIResamplingFunctions.hlsli>


#if USE_RAY_QUERY
[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 GlobalIndex : SV_DispatchThreadID)
#else
[shader("raygeneration")]
void RayGen()
#endif
{
#if !USE_RAY_QUERY
    uint2 GlobalIndex = DispatchRaysIndex().xy;
#endif
    uint2 pixelPosition = RTXDI_ReservoirPosToPixelPos(GlobalIndex, g_Const.runtimeParams.resamplingParams);

    if (any(pixelPosition > int2(g_Const.view.viewportSize)))
        return;

    RAB_RandomSamplerState rng = RAB_InitRandomSampler(GlobalIndex, 8);
    
    const RAB_Surface primarySurface = RAB_GetGBufferSurface(pixelPosition, false);
    
    const uint2 reservoirPosition = RTXDI_PixelPosToReservoirPos(pixelPosition, g_Const.runtimeParams.resamplingParams);
    RTXDI_GIReservoir reservoir = RTXDI_LoadGIReservoir(g_Const.runtimeParams.resamplingParams, reservoirPosition, g_Const.spatialResamplingConstants.spatialInputBufferIndex);

    if (RAB_IsSurfaceValid(primarySurface)) {
        RTXDI_GISpatialResamplingParameters sparams;

        sparams.sourceBufferIndex = g_Const.spatialResamplingConstants.spatialInputBufferIndex;
        sparams.biasCorrectionMode = g_Const.spatialResamplingConstants.spatialBiasCorrection;
        sparams.depthThreshold = g_Const.spatialResamplingConstants.spatialDepthThreshold;
        sparams.normalThreshold = g_Const.spatialResamplingConstants.spatialNormalThreshold;
        sparams.numSamples = g_Const.spatialResamplingConstants.numSpatialSamples;
        sparams.samplingRadius = g_Const.spatialResamplingConstants.spatialSamplingRadius;

        // Execute resampling.
        reservoir = RTXDI_GISpatialResampling(pixelPosition, primarySurface, reservoir, rng, sparams, g_Const.runtimeParams.resamplingParams);
    }

    RTXDI_StoreGIReservoir(reservoir, g_Const.runtimeParams.resamplingParams, reservoirPosition, g_Const.spatialResamplingConstants.spatialOutputBufferIndex);
}
