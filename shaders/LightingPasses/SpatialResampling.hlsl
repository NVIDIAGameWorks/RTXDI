/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#pragma pack_matrix(row_major)

#include "RtxdiApplicationBridge.hlsli"

#include <rtxdi/ResamplingFunctions.hlsli>

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

    const RTXDI_ResamplingRuntimeParameters params = g_Const.runtimeParams.resamplingParams;

    uint2 pixelPosition = RTXDI_ReservoirPosToPixelPos(GlobalIndex, params);

    RAB_RandomSamplerState rng = RAB_InitRandomSampler(pixelPosition, 3);

    RAB_Surface surface = RAB_GetGBufferSurface(pixelPosition, false);

    RTXDI_Reservoir spatialResult = RTXDI_EmptyReservoir();
    
    if (RAB_IsSurfaceValid(surface))
    {
        RTXDI_Reservoir centerSample = RTXDI_LoadReservoir(params,
            GlobalIndex, g_Const.temporalResamplingConstants.temporalOutputBufferIndex);

        RTXDI_SpatialResamplingParameters sparams;
        sparams.sourceBufferIndex = g_Const.spatialResamplingConstants.spatialInputBufferIndex;
        sparams.numSamples = g_Const.spatialResamplingConstants.numSpatialSamples;
        sparams.numDisocclusionBoostSamples = g_Const.spatialResamplingConstants.numDisocclusionBoostSamples;
        sparams.targetHistoryLength = g_Const.temporalResamplingConstants.maxHistoryLength;
        sparams.biasCorrectionMode = g_Const.spatialResamplingConstants.spatialBiasCorrection;
        sparams.samplingRadius = g_Const.spatialResamplingConstants.spatialSamplingRadius;
        sparams.depthThreshold = g_Const.spatialResamplingConstants.spatialDepthThreshold;
        sparams.normalThreshold = g_Const.spatialResamplingConstants.spatialNormalThreshold;
        sparams.enableMaterialSimilarityTest = true;

        RAB_LightSample lightSample = (RAB_LightSample)0;
        spatialResult = RTXDI_SpatialResampling(pixelPosition, surface, centerSample, 
             rng, sparams, params, lightSample);
    }

    RTXDI_StoreReservoir(spatialResult, params, GlobalIndex, g_Const.spatialResamplingConstants.spatialOutputBufferIndex);
}