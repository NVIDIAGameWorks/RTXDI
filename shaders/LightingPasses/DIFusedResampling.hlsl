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

#include <rtxdi/InitialSamplingFunctions.hlsli>
#include <rtxdi/DIResamplingFunctions.hlsli>
#if RTXDI_REGIR_MODE != RTXDI_REGIR_DISABLED
#include "rtxdi/ReGIRSampling.hlsli"
#endif

#ifdef WITH_NRD
#define NRD_HEADER_ONLY
#include <NRDEncoding.hlsli>
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

    const RTXDI_RuntimeParameters params = g_Const.runtimeParams;

    uint2 pixelPosition = RTXDI_ReservoirPosToPixelPos(GlobalIndex, params.activeCheckerboardField);

    RAB_RandomSamplerState rng = RAB_InitRandomSampler(pixelPosition, 1);
    RAB_RandomSamplerState tileRng = RAB_InitRandomSampler(pixelPosition / RTXDI_TILE_SIZE_IN_PIXELS, 1);

    RAB_Surface surface = RAB_GetGBufferSurface(pixelPosition, false);

    RTXDI_SampleParameters sampleParams = RTXDI_InitSampleParameters(
        g_Const.restirDI.initialSamplingParams.numPrimaryLocalLightSamples,
        g_Const.restirDI.initialSamplingParams.numPrimaryInfiniteLightSamples,
        g_Const.restirDI.initialSamplingParams.numPrimaryEnvironmentSamples,
        g_Const.restirDI.initialSamplingParams.numPrimaryBrdfSamples,
        g_Const.restirDI.initialSamplingParams.brdfCutoff,
        0.001f);

    RAB_LightSample lightSample;
    RTXDI_DIReservoir reservoir = RTXDI_SampleLightsForSurface(rng, tileRng, surface,
        sampleParams, g_Const.lightBufferParams, g_Const.restirDI.initialSamplingParams.localLightSamplingMode,
#ifdef RTXDI_ENABLE_PRESAMPLING
        g_Const.localLightsRISBufferSegmentParams, g_Const.environmentLightRISBufferSegmentParams,
#if RTXDI_REGIR_MODE != RTXDI_REGIR_MODE_DISABLED
        g_Const.regir,
#endif
#endif
        lightSample);

    if (g_Const.restirDI.initialSamplingParams.enableInitialVisibility && RTXDI_IsValidDIReservoir(reservoir))
    {
        if (!RAB_GetConservativeVisibility(surface, lightSample))
        {
            RTXDI_StoreVisibilityInDIReservoir(reservoir, 0, true);
        }
    }

    int2 temporalSamplePixelPos = -1;

    float3 motionVector = t_MotionVectors[pixelPosition].xyz;
    motionVector = convertMotionVectorToPixelSpace(g_Const.view, g_Const.prevView, pixelPosition, motionVector);

    bool usePermutationSampling = false;
    if (g_Const.restirDI.temporalResamplingParams.enablePermutationSampling)
    {
        // Permutation sampling makes more noise on thin, high-detail objects.
        usePermutationSampling = !IsComplexSurface(pixelPosition, surface);
    }

    RTXDI_DISpatioTemporalResamplingParameters stparams;
    stparams.screenSpaceMotion = motionVector;
    stparams.sourceBufferIndex = g_Const.restirDI.bufferIndices.temporalResamplingInputBufferIndex;
    stparams.maxHistoryLength = g_Const.restirDI.temporalResamplingParams.maxHistoryLength;
    stparams.biasCorrectionMode = g_Const.restirDI.temporalResamplingParams.temporalBiasCorrection;
    stparams.depthThreshold = g_Const.restirDI.temporalResamplingParams.temporalDepthThreshold;
    stparams.normalThreshold = g_Const.restirDI.temporalResamplingParams.temporalNormalThreshold;
    stparams.numSamples = g_Const.restirDI.spatialResamplingParams.numSpatialSamples + 1;
    stparams.numDisocclusionBoostSamples = g_Const.restirDI.spatialResamplingParams.numDisocclusionBoostSamples;
    stparams.samplingRadius = g_Const.restirDI.spatialResamplingParams.spatialSamplingRadius;
    stparams.enableVisibilityShortcut = g_Const.restirDI.temporalResamplingParams.discardInvisibleSamples;
    stparams.enablePermutationSampling = usePermutationSampling;
    stparams.enableMaterialSimilarityTest = true;
    stparams.uniformRandomNumber = g_Const.restirDI.temporalResamplingParams.uniformRandomNumber;
    stparams.discountNaiveSamples = g_Const.discountNaiveSamples;

    reservoir = RTXDI_DISpatioTemporalResampling(pixelPosition, surface, reservoir,
            rng, params, g_Const.restirDI.reservoirBufferParams, stparams, temporalSamplePixelPos, lightSample);

    u_TemporalSamplePositions[GlobalIndex] = temporalSamplePixelPos;

#ifdef RTXDI_ENABLE_BOILING_FILTER
    if (g_Const.restirDI.temporalResamplingParams.enableBoilingFilter)
    {
        RTXDI_BoilingFilter(LocalIndex, g_Const.restirDI.temporalResamplingParams.boilingFilterStrength, reservoir);
    }
#endif

    float3 diffuse = 0;
    float3 specular = 0;
    float lightDistance = 0;
    float2 currLuminance = 0;

    if (RTXDI_IsValidDIReservoir(reservoir))
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

    RTXDI_StoreDIReservoir(reservoir, g_Const.restirDI.reservoirBufferParams, GlobalIndex, g_Const.restirDI.bufferIndices.shadingInputBufferIndex);

#if RTXDI_REGIR_MODE != RTXDI_REGIR_DISABLED
    if (g_Const.visualizeRegirCells)
    {
        diffuse *= RTXDI_VisualizeReGIRCells(g_Const.regir, RAB_GetSurfaceWorldPos(surface));
    }
#endif

    StoreShadingOutput(GlobalIndex, pixelPosition, 
        surface.viewDepth, surface.roughness,  diffuse, specular, lightDistance, true, g_Const.restirDI.shadingParams.enableDenoiserInputPacking);
}