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

#if USE_RAY_QUERY
#define RTXDI_ENABLE_BOILING_FILTER
#define RTXDI_BOILING_FILTER_GROUP_SIZE RTXDI_SCREEN_SPACE_GROUP_SIZE
#endif

#include "RtxdiApplicationBridge.hlsli"

#include <rtxdi/GIResamplingFunctions.hlsli>

#if USE_RAY_QUERY
[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 GlobalIndex : SV_DispatchThreadID, uint2 LocalIndex : SV_GroupThreadID)
#else
[shader("raygeneration")]
void RayGen()
#endif
{
#if !USE_RAY_QUERY
    uint2 GlobalIndex = DispatchRaysIndex().xy;
#endif
    uint2 pixelPosition = RTXDI_ReservoirPosToPixelPos(GlobalIndex, g_Const.runtimeParams.activeCheckerboardField);

    RAB_RandomSamplerState rng = RAB_InitRandomSampler(GlobalIndex, 7);
    
    const RAB_Surface primarySurface = RAB_GetGBufferSurface(pixelPosition, false);
    
    const uint2 reservoirPosition = RTXDI_PixelPosToReservoirPos(pixelPosition, g_Const.runtimeParams.activeCheckerboardField);
    RTXDI_GIReservoir reservoir = RTXDI_LoadGIReservoir(g_Const.restirGI.reservoirBufferParams, reservoirPosition, g_Const.restirGI.bufferIndices.secondarySurfaceReSTIRDIOutputBufferIndex);

    float3 motionVector = t_MotionVectors[pixelPosition].xyz;
    motionVector = convertMotionVectorToPixelSpace(g_Const.view, g_Const.prevView, pixelPosition, motionVector);

    if (RAB_IsSurfaceValid(primarySurface)) {
        RTXDI_GISpatioTemporalResamplingParameters stParams;

        stParams.screenSpaceMotion = motionVector;
        stParams.sourceBufferIndex = g_Const.restirGI.bufferIndices.temporalResamplingInputBufferIndex;
        stParams.maxHistoryLength = g_Const.restirGI.temporalResamplingParams.maxHistoryLength;
        stParams.biasCorrectionMode = g_Const.restirGI.temporalResamplingParams.temporalBiasCorrectionMode;
        stParams.depthThreshold = g_Const.restirGI.temporalResamplingParams.depthThreshold;
        stParams.normalThreshold = g_Const.restirGI.temporalResamplingParams.normalThreshold;
        stParams.enablePermutationSampling = g_Const.restirGI.temporalResamplingParams.enablePermutationSampling;
        stParams.enableFallbackSampling = g_Const.restirGI.temporalResamplingParams.enableFallbackSampling;
        stParams.numSpatialSamples = g_Const.restirGI.spatialResamplingParams.numSpatialSamples;
        stParams.samplingRadius = g_Const.restirGI.spatialResamplingParams.spatialSamplingRadius;
        stParams.uniformRandomNumber = g_Const.restirGI.temporalResamplingParams.uniformRandomNumber;

        // Age threshold should vary.
        // This is to avoid to die a bunch of GI reservoirs at once at a disoccluded area.
        stParams.maxReservoirAge = g_Const.restirGI.temporalResamplingParams.maxReservoirAge * (0.5 + RAB_GetNextRandom(rng) * 0.5);

        // Execute resampling.
        reservoir = RTXDI_GISpatioTemporalResampling(pixelPosition, primarySurface, reservoir, rng, g_Const.runtimeParams, g_Const.restirGI.reservoirBufferParams, stParams);
    }

#ifdef RTXDI_ENABLE_BOILING_FILTER
    if (g_Const.restirGI.temporalResamplingParams.enableBoilingFilter)
    {
        RTXDI_GIBoilingFilter(LocalIndex, g_Const.restirGI.temporalResamplingParams.boilingFilterStrength, reservoir);
    }
#endif

    RTXDI_StoreGIReservoir(reservoir, g_Const.restirGI.reservoirBufferParams, reservoirPosition, g_Const.restirGI.bufferIndices.spatialResamplingOutputBufferIndex);
}
