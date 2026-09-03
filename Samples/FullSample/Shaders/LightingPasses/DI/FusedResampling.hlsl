/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#if USE_RAY_QUERY
#define RTXDI_ENABLE_BOILING_FILTER
#define RTXDI_BOILING_FILTER_GROUP_SIZE RTXDI_SCREEN_SPACE_GROUP_SIZE
#endif

#include "../RtxdiApplicationBridge/RtxdiApplicationBridge.hlsli"

#include <Rtxdi/DI/BoilingFilter.hlsli>
#include <Rtxdi/DI/InitialSampling.hlsli>
#include <Rtxdi/DI/SpatioTemporalResampling.hlsli>
#if RTXDI_REGIR_MODE != RTXDI_REGIR_DISABLED
#include "Rtxdi/ReGIR/ReGIRSampling.hlsli"
#endif

#ifdef WITH_NRD
#define NRD_HEADER_ONLY
#include <NRD.hlsli>
#endif

#include "../ShadingHelpers.hlsli"

#if USE_RAY_QUERY
[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 globalIndex : SV_DispatchThreadID, uint2 localIndex : SV_GroupThreadID)
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

    RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(pixelPosition, g_Const.runtimeParams.frameIndex, RTXDI_DI_GENERATE_INITIAL_SAMPLES_RANDOM_SEED);
    RTXDI_RandomSamplerState tileRng = RTXDI_InitRandomSampler(pixelPosition / RTXDI_TILE_SIZE_IN_PIXELS, g_Const.runtimeParams.frameIndex, RTXDI_DI_GENERATE_INITIAL_SAMPLES_RANDOM_SEED);

    RAB_Surface surface = RAB_GetGBufferSurface(pixelPosition, false);

    RAB_LightSample lightSample;
    RTXDI_DIReservoir reservoir = RTXDI_SampleLightsForSurface(rng, tileRng, surface,
        g_Const.restirDI.initialSamplingParams, g_Const.lightBufferParams,
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
    motionVector = ConvertMotionVectorToPixelSpace(g_Const.view, g_Const.prevView, pixelPosition, motionVector);

    bool usePermutationSampling = false;
    if (g_Const.restirDI.temporalResamplingParams.enablePermutationSampling)
    {
        // Permutation sampling makes more noise on thin, high-detail objects.
        usePermutationSampling = !IsComplexSurface(pixelPosition, surface);
    }

    reservoir = RTXDI_DISpatioTemporalResampling(pixelPosition, surface, reservoir,
            rng, motionVector, g_Const.restirDI.bufferIndices.temporalResamplingInputBufferIndex, params, g_Const.restirDI.reservoirBufferParams, g_Const.restirDI.spatioTemporalResamplingParams, temporalSamplePixelPos, lightSample);

    u_TemporalSamplePositions[globalIndex] = temporalSamplePixelPos;

#ifdef RTXDI_ENABLE_BOILING_FILTER
    if (g_Const.restirDI.boilingFilterParams.enableBoilingFilter)
    {
        RTXDI_BoilingFilter(localIndex, g_Const.restirDI.boilingFilterParams.boilingFilterStrength, reservoir);
    }
#endif

    float3 diffuse = 0;
    float3 specular = 0;
    float lightDistance = 0;
    float2 currLuminance = 0;

    if (RTXDI_IsValidDIReservoir(reservoir))
    {
        // lightSample is produced by the RTXDI_SampleLightsForSurface and RTXDI_SpatioTemporalResampling calls above
        ShadeSurfaceWithLightSample(reservoir, surface, g_Const.restirDI.shadingParams, lightSample,
            /* prevFrameTLAS = */ false, /* enableVisibilityReuse = */ true, g_Const.restirDI.temporalResamplingParams.enableVisibilityShortcut, diffuse, specular, lightDistance);

        currLuminance = float2(CalcLuminance(diffuse * surface.material.diffuseAlbedo), CalcLuminance(specular));

        specular = DemodulateSpecular(surface.material.specularF0, specular);
    }

    // Store the sampled lighting luminance for the gradient pass.
    // Discard the pixels where the visibility was reused, as gradients need actual visibility.
    u_RestirLuminance[globalIndex] = currLuminance * (reservoir.age > 0 ? 0 : 1);

    RTXDI_StoreDIReservoir(reservoir, g_Const.restirDI.reservoirBufferParams, globalIndex, g_Const.restirDI.bufferIndices.shadingInputBufferIndex);

#if RTXDI_REGIR_MODE != RTXDI_REGIR_DISABLED
    if (g_Const.visualizeRegirCells)
    {
        diffuse *= RTXDI_VisualizeReGIRCells(g_Const.regir, RAB_GetSurfaceWorldPos(surface));
    }
#endif

    StoreShadingOutput(globalIndex, pixelPosition,
        surface.viewDepth, surface.material.roughness,  diffuse, specular, lightDistance, true, g_Const.restirDI.shadingParams.enableDenoiserInputPacking);
}