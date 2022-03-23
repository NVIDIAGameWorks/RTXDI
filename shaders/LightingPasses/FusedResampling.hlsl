/***************************************************************************
 # Copyright (c) 2021-2022, NVIDIA CORPORATION.  All rights reserved.
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
#define NRD_HEADER_ONLY
#include <NRD.hlsli>
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

    RTXDI_SampleParameters sampleParams = RTXDI_InitSampleParameters(
        g_Const.numPrimaryRegirSamples,
        g_Const.numPrimaryLocalLightSamples,
        g_Const.numPrimaryInfiniteLightSamples,
        g_Const.numPrimaryEnvironmentSamples,
        g_Const.numPrimaryBrdfSamples,
        g_Const.brdfCutoff,
        0.001f);

    RAB_LightSample lightSample;
    RTXDI_Reservoir reservoir = RTXDI_SampleLightsForSurface(rng, tileRng, surface, 
        sampleParams, params, lightSample);

    if (g_Const.enableInitialVisibility && RTXDI_IsValidReservoir(reservoir))
    {
        if (!RAB_GetConservativeVisibility(surface, lightSample))
        {
            RTXDI_StoreVisibilityInReservoir(reservoir, 0, true);
        }
    }

    int2 temporalSamplePixelPos = -1;

    float3 motionVector = t_MotionVectors[pixelPosition].xyz;
    motionVector = convertMotionVectorToPixelSpace(g_Const.view, g_Const.prevView, pixelPosition, motionVector);

    bool usePermutationSampling = false;
    if (g_Const.enablePermutationSampling)
    {
        // Permutation sampling makes more noise on thin, high-detail objects.
        usePermutationSampling = !IsComplexSurface(pixelPosition, surface);
    }

    RTXDI_SpatioTemporalResamplingParameters stparams;
    stparams.screenSpaceMotion = motionVector;
    stparams.sourceBufferIndex = g_Const.temporalInputBufferIndex;
    stparams.maxHistoryLength = g_Const.maxHistoryLength;
    stparams.biasCorrectionMode = g_Const.temporalBiasCorrection;
    stparams.depthThreshold = g_Const.temporalDepthThreshold;
    stparams.normalThreshold = g_Const.temporalNormalThreshold;
    stparams.numSamples = g_Const.numSpatialSamples + 1;
    stparams.numDisocclusionBoostSamples = g_Const.numDisocclusionBoostSamples;
    stparams.samplingRadius = g_Const.spatialSamplingRadius;
    stparams.enableVisibilityShortcut = g_Const.discardInvisibleSamples;
    stparams.enablePermutationSampling = usePermutationSampling;

    reservoir = RTXDI_SpatioTemporalResampling(pixelPosition, surface, reservoir,
            rng, stparams, params, temporalSamplePixelPos, lightSample);

    u_TemporalSamplePositions[GlobalIndex] = temporalSamplePixelPos;

#ifdef RTXDI_ENABLE_BOILING_FILTER
    if (g_Const.boilingFilterStrength > 0)
    {
        RTXDI_BoilingFilter(LocalIndex, g_Const.boilingFilterStrength, params, reservoir);
    }
#endif

    float3 diffuse = 0;
    float3 specular = 0;
    float lightDistance = 0;
    float2 currLuminance = 0;

    if (RTXDI_IsValidReservoir(reservoir))
    {
        // lightSample is produced by the RTXDI_SampleLightsForSurface and RTXDI_SpatioTemporalResampling calls above
        ShadeSurfaceWithLightSample(reservoir, surface, lightSample,
            /* previousFrameTLAS = */ false, /* enableVisibilityReuse = */ true, diffuse, specular, lightDistance);

        currLuminance = float2(calcLuminance(diffuse * surface.diffuseAlbedo), calcLuminance(specular));
        
        specular = DemodulateSpecular(surface.specularF0, specular);
    }

    // Store the sampled lighting luminance for the gradient pass.
    // Discard the pixels where the visibility was reused, as gradients need actual visibility.
    u_RestirLuminance[GlobalIndex] = currLuminance * (reservoir.age > 0 ? 0 : 1);

    RTXDI_StoreReservoir(reservoir, params, GlobalIndex, g_Const.shadeInputBufferIndex);

#if RTXDI_REGIR_MODE != RTXDI_REGIR_DISABLED
    if (g_Const.visualizeRegirCells)
    {
        diffuse *= RTXDI_VisualizeReGIRCells(g_Const.runtimeParams, RAB_GetSurfaceWorldPos(surface));
    }
#endif

    StoreShadingOutput(GlobalIndex, pixelPosition, 
        surface.viewDepth, surface.roughness,  diffuse, specular, lightDistance, true, g_Const.enableDenoiserInputPacking);
}