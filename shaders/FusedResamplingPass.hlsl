/***************************************************************************
 # Copyright (c) 2021, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#pragma pack_matrix(row_major)

#include "RtxdiApplicationBridge.hlsli"

#if USE_RAY_QUERY
#define RTXDI_ENABLE_BOILING_FILTER
#define RTXDI_BOILING_FILTER_GROUP_SIZE RTXDI_SCREEN_SPACE_GROUP_SIZE
#endif

#include <rtxdi/ResamplingFunctions.hlsli>

#ifdef WITH_NRD
#include <NRD.hlsl>
#endif

#include "ShadingHelpers.hlsli"

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

    const RTXDI_ResamplingRuntimeParameters params = g_Const.runtimeParams;

    uint2 pixelPosition = RTXDI_ReservoirToPixelPos(GlobalIndex, params);

    RAB_RandomSamplerState rng = RAB_InitRandomSampler(pixelPosition, 1);
    RAB_RandomSamplerState tileRng = RAB_InitRandomSampler(pixelPosition / RTXDI_TILE_SIZE_IN_PIXELS, 1);

    RAB_Surface surface = RAB_GetGBufferSurface(pixelPosition, false);

    RAB_LightSample lightSample;
    RTXDI_Reservoir reservoir = RTXDI_SampleLightsForSurface(rng, tileRng, surface, 
        g_Const.numPrimaryRegirSamples, 
        g_Const.numPrimaryLocalLightSamples, 
        g_Const.numPrimaryInfiniteLightSamples, 
        g_Const.numPrimaryEnvironmentSamples,
        params, u_RisBuffer, lightSample);

    if (g_Const.enableInitialVisibility && RTXDI_IsValidReservoir(reservoir))
    {
        if (!RAB_GetConservativeVisibility(surface, lightSample, false))
        {
            RTXDI_StoreVisibilityInReservoir(reservoir, 0, true);
        }
    }


    RTXDI_SpatioTemporalResamplingParameters stparams;
    stparams.screenSpaceMotion = t_MotionVectors[pixelPosition].xyz;
    stparams.sourceBufferIndex = g_Const.temporalInputBufferIndex;
    stparams.maxHistoryLength = g_Const.maxHistoryLength;
    stparams.biasCorrectionMode = g_Const.temporalBiasCorrection;
    stparams.depthThreshold = g_Const.temporalDepthThreshold;
    stparams.normalThreshold = g_Const.temporalNormalThreshold;
    stparams.numSamples = g_Const.numSpatialSamples + 1;
    stparams.numDisocclusionBoostSamples = g_Const.numDisocclusionBoostSamples;
    stparams.samplingRadius = g_Const.spatialSamplingRadius;

    reservoir = RTXDI_SpatioTemporalResampling(pixelPosition, surface, reservoir,
            rng, stparams, params, u_LightReservoirs, t_NeighborOffsets);

#ifdef RTXDI_ENABLE_BOILING_FILTER
    if (g_Const.boilingFilterStrength > 0)
    {
        RTXDI_BoilingFilter(LocalIndex, g_Const.boilingFilterStrength, params, reservoir);
    }
#endif

    float3 diffuse = 0;
    float3 specular = 0;
    float lightDistance = 0;

    if (RTXDI_IsValidReservoir(reservoir))
    {
        RAB_LightInfo lightInfo = RAB_LoadLightInfo(RTXDI_GetReservoirLightIndex(reservoir), false);

        RAB_LightSample lightSample = RAB_SamplePolymorphicLight(lightInfo, surface,
            RTXDI_GetReservoirSampleUV(reservoir));

        ShadeSurfaceWithLightSample(reservoir, surface, lightSample, diffuse, specular, lightDistance);
    }

    RTXDI_StoreReservoir(reservoir, params, u_LightReservoirs, GlobalIndex, g_Const.shadeInputBufferIndex);

    StoreRestirShadingOutput(GlobalIndex, pixelPosition, params.activeCheckerboardField, 
        surface, diffuse, specular, lightDistance);
}