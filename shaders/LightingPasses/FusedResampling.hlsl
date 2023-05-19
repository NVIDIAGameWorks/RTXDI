/***************************************************************************
 # Copyright (c) 2021-2023, NVIDIA CORPORATION.  All rights reserved.
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

    const RTXDI_ResamplingRuntimeParameters params = g_Const.runtimeParams.resamplingParams;

    uint2 pixelPosition = RTXDI_ReservoirPosToPixelPos(GlobalIndex, params);

    RAB_RandomSamplerState rng = RAB_InitRandomSampler(pixelPosition, 2);

    RAB_Surface surface = RAB_GetGBufferSurface(pixelPosition, false);

    RTXDI_Reservoir reservoir = RTXDI_LoadReservoir(params, GlobalIndex, g_Const.initialSamplingConstants.initialOutputBufferIndex);

    int2 temporalSamplePixelPos = -1;

    float3 motionVector = t_MotionVectors[pixelPosition].xyz;
    motionVector = convertMotionVectorToPixelSpace(g_Const.view, g_Const.prevView, pixelPosition, motionVector);

    bool usePermutationSampling = false;
    if (g_Const.temporalResamplingConstants.enablePermutationSampling)
    {
        // Permutation sampling makes more noise on thin, high-detail objects.
        usePermutationSampling = !IsComplexSurface(pixelPosition, surface);
    }

    RTXDI_SpatioTemporalResamplingParameters stparams;
    stparams.screenSpaceMotion = motionVector;
    stparams.sourceBufferIndex = g_Const.temporalResamplingConstants.temporalInputBufferIndex;
    stparams.maxHistoryLength = g_Const.temporalResamplingConstants.maxHistoryLength;
    stparams.biasCorrectionMode = g_Const.temporalResamplingConstants.temporalBiasCorrection;
    stparams.depthThreshold = g_Const.temporalResamplingConstants.temporalDepthThreshold;
    stparams.normalThreshold = g_Const.temporalResamplingConstants.temporalNormalThreshold;
    stparams.numSamples = g_Const.spatialResamplingConstants.numSpatialSamples + 1;
    stparams.numDisocclusionBoostSamples = g_Const.spatialResamplingConstants.numDisocclusionBoostSamples;
    stparams.samplingRadius = g_Const.spatialResamplingConstants.spatialSamplingRadius;
    stparams.enableVisibilityShortcut = g_Const.temporalResamplingConstants.discardInvisibleSamples;
    stparams.enablePermutationSampling = usePermutationSampling;
    stparams.enableMaterialSimilarityTest = true;

    RAB_LightSample lightSample;
    reservoir = RTXDI_SpatioTemporalResampling(pixelPosition, surface, reservoir,
            rng, stparams, params, temporalSamplePixelPos, lightSample);

    u_TemporalSamplePositions[GlobalIndex] = temporalSamplePixelPos;

#ifdef RTXDI_ENABLE_BOILING_FILTER
    if (g_Const.boilingFilterStrength > 0)
    {
        RTXDI_BoilingFilter(LocalIndex, g_Const.boilingFilterStrength, reservoir);
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

    RTXDI_StoreReservoir(reservoir, params, GlobalIndex, g_Const.shadingConstants.shadeInputBufferIndex);

#if RTXDI_REGIR_MODE != RTXDI_REGIR_DISABLED
    if (g_Const.visualizeRegirCells)
    {
        diffuse *= RTXDI_VisualizeReGIRCells(g_Const.runtimeParams, RAB_GetSurfaceWorldPos(surface));
    }
#endif

    StoreShadingOutput(GlobalIndex, pixelPosition, 
        surface.viewDepth, surface.roughness,  diffuse, specular, lightDistance, true, g_Const.shadingConstants.enableDenoiserInputPacking);
}