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

// Only enable the boiling filter for RayQuery (compute shader) mode because it requires shared memory
#if USE_RAY_QUERY
#define RTXDI_ENABLE_BOILING_FILTER
#define RTXDI_BOILING_FILTER_GROUP_SIZE RTXDI_SCREEN_SPACE_GROUP_SIZE
#endif

#include "RtxdiApplicationBridge.hlsli"

#include <rtxdi/DIResamplingFunctions.hlsli>

#if USE_RAY_QUERY
[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)] 
void main(uint2 GlobalIndex : SV_DispatchThreadID, uint2 LocalIndex : SV_GroupThreadID, uint2 GroupIdx : SV_GroupID)
#else
[shader("raygeneration")]
void RayGen()
#endif
{
#if !USE_RAY_QUERY
    uint2 GlobalIndex = DispatchRaysIndex().xy;
#endif

    const RTXDI_RuntimeParameters params = g_Const.runtimeParams;

    uint2 pixelPosition = RTXDI_ReservoirPosToPixelPos(GlobalIndex, params.activeCheckerboardField);

    RAB_RandomSamplerState rng = RAB_InitRandomSampler(pixelPosition, 2);

    RAB_Surface surface = RAB_GetGBufferSurface(pixelPosition, false);

    bool usePermutationSampling = false;
    if (g_Const.restirDI.temporalResamplingParams.enablePermutationSampling)
    {
        // Permutation sampling makes more noise on thin, high-detail objects.
        usePermutationSampling = !IsComplexSurface(pixelPosition, surface);
    }

    RTXDI_DIReservoir temporalResult = RTXDI_EmptyDIReservoir();
    int2 temporalSamplePixelPos = -1;
    
    if (RAB_IsSurfaceValid(surface))
    {
        RTXDI_DIReservoir curSample = RTXDI_LoadDIReservoir(g_Const.restirDI.reservoirBufferParams,
            GlobalIndex, g_Const.restirDI.bufferIndices.initialSamplingOutputBufferIndex);

        float3 motionVector = t_MotionVectors[pixelPosition].xyz;
        motionVector = convertMotionVectorToPixelSpace(g_Const.view, g_Const.prevView, pixelPosition, motionVector);

        RTXDI_DITemporalResamplingParameters tparams;
        tparams.screenSpaceMotion = motionVector;
        tparams.sourceBufferIndex = g_Const.restirDI.bufferIndices.temporalResamplingInputBufferIndex;
        tparams.maxHistoryLength = g_Const.restirDI.temporalResamplingParams.maxHistoryLength;
        tparams.biasCorrectionMode = g_Const.restirDI.temporalResamplingParams.temporalBiasCorrection;
        tparams.depthThreshold = g_Const.restirDI.temporalResamplingParams.temporalDepthThreshold;
        tparams.normalThreshold = g_Const.restirDI.temporalResamplingParams.temporalNormalThreshold;
        tparams.enableVisibilityShortcut = g_Const.restirDI.temporalResamplingParams.discardInvisibleSamples;
        tparams.enablePermutationSampling = usePermutationSampling;
        tparams.uniformRandomNumber = g_Const.restirDI.temporalResamplingParams.uniformRandomNumber;

        RAB_LightSample selectedLightSample = (RAB_LightSample)0;
        
        temporalResult = RTXDI_DITemporalResampling(pixelPosition, surface, curSample,
            rng, params, g_Const.restirDI.reservoirBufferParams, tparams, temporalSamplePixelPos, selectedLightSample);
    }

#ifdef RTXDI_ENABLE_BOILING_FILTER
    if (g_Const.restirDI.temporalResamplingParams.enableBoilingFilter)
    {
        RTXDI_BoilingFilter(LocalIndex, g_Const.restirDI.temporalResamplingParams.boilingFilterStrength, temporalResult);
    }
#endif

    u_TemporalSamplePositions[GlobalIndex] = temporalSamplePixelPos;
    
    RTXDI_StoreDIReservoir(temporalResult, g_Const.restirDI.reservoirBufferParams, GlobalIndex, g_Const.restirDI.bufferIndices.temporalResamplingOutputBufferIndex);
}