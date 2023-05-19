/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#ifndef INITIAL_SAMPLING_FUNCTIONS_HLSLI
#define INITIAL_SAMPLING_FUNCTIONS_HLSLI

#include "rtxdi/RtxdiParameters.h"
#include "rtxdi/Reservoir.hlsli"
#include "rtxdi/LocalLightSelection.hlsli"
#ifdef RTXDI_RIS_BUFFER_HLSLI
#include "rtxdi/RISBuffer.hlsli"
#endif // RTXDI_RIS_BUFFER_HLSLI
#include "rtxdi/UniformSampling.hlsli"

#ifndef RTXDI_TILE_SIZE_IN_PIXELS
#define RTXDI_TILE_SIZE_IN_PIXELS 16
#endif

#define RTXDI_LocalLightSamplingMode uint

struct RTXDI_SampleParameters
{    
    uint numLocalLightSamples;
    uint numInfiniteLightSamples;
    uint numEnvironmentMapSamples;
    uint numBrdfSamples;

    uint numMisSamples;
    float localLightMisWeight;
    float environmentMapMisWeight;
    float brdfMisWeight;
    float brdfCutoff;
    float brdfRayMinT;
};

//
// MIS functions
//

// Sample parameters struct
// Defined so that so these can be compile time constants as defined by the user
// brdfCutoff Value in range [0,1] to determine how much to shorten BRDF rays. 0 to disable shortening
RTXDI_SampleParameters RTXDI_InitSampleParameters(
    uint numLocalLightSamples,
    uint numInfiniteLightSamples,
    uint numEnvironmentMapSamples,
    uint numBrdfSamples,
    float brdfCutoff RTXDI_DEFAULT(0.0),
    float brdfRayMinT RTXDI_DEFAULT(0.001f))
{
    RTXDI_SampleParameters result;
    result.numLocalLightSamples = numLocalLightSamples;
    result.numInfiniteLightSamples = numInfiniteLightSamples;
    result.numEnvironmentMapSamples = numEnvironmentMapSamples;
    result.numBrdfSamples = numBrdfSamples;

    result.numMisSamples = numLocalLightSamples + numEnvironmentMapSamples + numBrdfSamples;
    result.localLightMisWeight = float(numLocalLightSamples) / result.numMisSamples;
    result.environmentMapMisWeight = float(numEnvironmentMapSamples) / result.numMisSamples;
    result.brdfMisWeight = float(numBrdfSamples) / result.numMisSamples;
    result.brdfCutoff = brdfCutoff;
    result.brdfRayMinT = brdfRayMinT;

    return result;
}

// Heuristic to determine a max visibility ray length from a PDF wrt. solid angle.
float RTXDI_BrdfMaxDistanceFromPdf(float brdfCutoff, float pdf)
{
    const float kRayTMax = 3.402823466e+38F; // FLT_MAX
    return brdfCutoff > 0.f ? sqrt((1.f / brdfCutoff - 1.f) * pdf) : kRayTMax;
}

// Computes the multi importance sampling pdf for brdf and light sample.
// For light and BRDF PDFs wrt solid angle, blend between the two.
//      lightSelectionPdf is a dimensionless selection pdf
float RTXDI_LightBrdfMisWeight(RAB_Surface surface, RAB_LightSample lightSample,
    float lightSelectionPdf, float lightMisWeight, bool isEnvironmentMap,
    RTXDI_SampleParameters sampleParams)
{
    float lightSolidAnglePdf = RAB_LightSampleSolidAnglePdf(lightSample);
    if (sampleParams.brdfMisWeight == 0 || RAB_IsAnalyticLightSample(lightSample) ||
        lightSolidAnglePdf <= 0 || isinf(lightSolidAnglePdf) || isnan(lightSolidAnglePdf))
    {
        // BRDF samples disabled or we can't trace BRDF rays MIS with analytical lights
        return lightMisWeight * lightSelectionPdf;
    }

    float3 lightDir;
    float lightDistance;
    RAB_GetLightDirDistance(surface, lightSample, lightDir, lightDistance);

    // Compensate for ray shortening due to brdf cutoff, does not apply to environment map sampling
    float brdfPdf = RAB_GetSurfaceBrdfPdf(surface, lightDir);
    float maxDistance = RTXDI_BrdfMaxDistanceFromPdf(sampleParams.brdfCutoff, brdfPdf);
    if (!isEnvironmentMap && lightDistance > maxDistance)
        brdfPdf = 0.f;

    // Convert light selection pdf (unitless) to a solid angle measurement
    float sourcePdfWrtSolidAngle = lightSelectionPdf * lightSolidAnglePdf;

    // MIS blending against solid angle pdfs.
    float blendedPdfWrtSolidangle = lightMisWeight * sourcePdfWrtSolidAngle + sampleParams.brdfMisWeight * brdfPdf;

    // Convert back, RTXDI divides shading again by this term later
    return blendedPdfWrtSolidangle / lightSolidAnglePdf;
}

//
// Local light UV selection and reservoir streaming
//

float2 RTXDI_RandomlySelectLocalLightUV(inout RAB_RandomSamplerState rng)
{
    float2 uv;
    uv.x = RAB_GetNextRandom(rng);
    uv.y = RAB_GetNextRandom(rng);
    return uv;
}

bool RTXDI_StreamLocalLightAtUVIntoReservoir(
    inout RAB_RandomSamplerState rng,
    RTXDI_SampleParameters sampleParams,
    RAB_Surface surface,
    uint lightIndex,
    float2 uv,
    float invSourcePdf,
    RAB_LightInfo lightInfo,
    inout RTXDI_Reservoir state,
    inout RAB_LightSample o_selectedSample)
{
    RAB_LightSample candidateSample = RAB_SamplePolymorphicLight(lightInfo, surface, uv);
    float blendedSourcePdf = RTXDI_LightBrdfMisWeight(surface, candidateSample, 1.0 / invSourcePdf,
        sampleParams.localLightMisWeight, false, sampleParams);
    float targetPdf = RAB_GetLightSampleTargetPdfForSurface(candidateSample, surface);
    float risRnd = RAB_GetNextRandom(rng);

    if (blendedSourcePdf == 0)
    {
        return false;
    }
    bool selected = RTXDI_StreamSample(state, lightIndex, uv, risRnd, targetPdf, 1.0 / blendedSourcePdf);

    if (selected) {
        o_selectedSample = candidateSample;
    }
    return true;
}

//
// ReGIR-based RIS for local lights. See PresampleReGIR.hlsl
//

#if RTXDI_ENABLE_PRESAMPLING
#if RTXDI_REGIR_MODE != RTXDI_REGIR_DISABLED

int RTXDI_CalculateReGIRCellIndex(
    inout RAB_RandomSamplerState coherentRng,
    RTXDI_RuntimeParameters params,
    RAB_Surface surface)
{
    int cellIndex = -1;
    float3 cellJitter = float3(
        RAB_GetNextRandom(coherentRng),
        RAB_GetNextRandom(coherentRng),
        RAB_GetNextRandom(coherentRng));
    cellJitter -= 0.5;

    float3 samplingPos = RAB_GetSurfaceWorldPos(surface);
    float jitterScale = RTXDI_ReGIR_GetJitterScale(params, samplingPos);
    samplingPos += cellJitter * jitterScale;

    cellIndex = RTXDI_ReGIR_WorldPosToCellIndex(params, samplingPos);
    return cellIndex;
}

RTXDI_RISTileInfo RTXDI_SelectLocalLightReGIRRISTile(
    int cellIndex,
    RTXDI_ReGIRCommonParameters regirCommon)
{
    RTXDI_RISTileInfo tileInfo;
    uint cellBase = uint(cellIndex)*regirCommon.lightsPerCell;
    tileInfo.risTileOffset = cellBase + regirCommon.risBufferOffset;
    tileInfo.risTileSize = regirCommon.lightsPerCell;
    return tileInfo;
}

#endif // RTXDI_REGIR_MODE != RTXDI_REGIR_DISABLED
#endif // RTXDI_ENABLE_PRESAMPLING

#if RTXDI_ENABLE_PRESAMPLING
#if RTXDI_REGIR_MODE != RTXDI_REGIR_DISABLED
RTXDI_LocalLightSelectionContext RTXDI_InitializeLocalLightSelectionContextReGIRRIS(
    inout RAB_RandomSamplerState coherentRng,
    RTXDI_RuntimeParameters params,
    RAB_Surface surface)
{
    RTXDI_LocalLightSelectionContext ctx;
    int cellIndex = RTXDI_CalculateReGIRCellIndex(coherentRng, params, surface);
    if (cellIndex >= 0)
    {
        ctx = RTXDI_InitializeLocalLightSelectionContextRIS(RTXDI_SelectLocalLightReGIRRISTile(cellIndex, params.regirCommon));
    }
    else if (params.regirCommon.localLightSamplingFallbackMode == RTXDI_LocalLightSamplingMode_POWER_RIS)
    {
        ctx = RTXDI_InitializeLocalLightSelectionContextRIS(coherentRng, params.localLightParams.risBufferParams);
    }
    else
    {
        ctx = RTXDI_InitializeLocalLightSelectionContextUniform(params.localLightParams.localLightsBufferRegion);
    }
    return ctx;
}
#endif // RTXDI_REGIR_MODE != RTXDI_REGIR_DISABLED
#endif // RTXDI_ENABLE_PRESAMPLING

RTXDI_LocalLightSelectionContext RTXDI_InitializeLocalLightSelectionContext(
    inout RAB_RandomSamplerState coherentRng,
    RTXDI_RuntimeParameters params,
    RAB_Surface surface)
{
    RTXDI_LocalLightSelectionContext ctx;
#if RTXDI_ENABLE_PRESAMPLING
#if RTXDI_REGIR_MODE != RTXDI_REGIR_DISABLED
    if (params.localLightParams.localLightSamplingMode == RTXDI_LocalLightSamplingMode_REGIR_RIS)
    {
        ctx = RTXDI_InitializeLocalLightSelectionContextReGIRRIS(coherentRng, params, surface);
    }
    else
#endif // RTXDI_REGIR_MODE != RTXDI_REGIR_DISABLED
    if (params.localLightParams.localLightSamplingMode == RTXDI_LocalLightSamplingMode_POWER_RIS)
    {
        ctx = RTXDI_InitializeLocalLightSelectionContextRIS(coherentRng, params.localLightParams.risBufferParams);
    }
    else
#endif // RTXDI_ENABLE_PRESAMPLING
    {
        ctx = RTXDI_InitializeLocalLightSelectionContextUniform(params.localLightParams.localLightsBufferRegion);
    }
    return ctx;
}

RTXDI_Reservoir RTXDI_SampleLocalLightsInternal(
    inout RAB_RandomSamplerState rng,
    inout RAB_RandomSamplerState coherentRng,
    RAB_Surface surface,
    RTXDI_SampleParameters sampleParams,
    RTXDI_RuntimeParameters params,
    out RAB_LightSample o_selectedSample)
{
    RTXDI_Reservoir state = RTXDI_EmptyReservoir();

    RTXDI_LocalLightSelectionContext lightSelectionContext = RTXDI_InitializeLocalLightSelectionContext(coherentRng, params, surface);
    for (uint i = 0; i < sampleParams.numLocalLightSamples; i++)
    {
        uint lightIndex;
        RAB_LightInfo lightInfo;
        float invSourcePdf;

        RTXDI_SelectNextLocalLight(lightSelectionContext, rng, lightInfo, lightIndex, invSourcePdf);
        float2 uv = RTXDI_RandomlySelectLocalLightUV(rng);
        bool zeroPdf = RTXDI_StreamLocalLightAtUVIntoReservoir(rng, sampleParams, surface, lightIndex, uv, invSourcePdf, lightInfo, state, o_selectedSample);

        if (zeroPdf)
            continue;
    }

    RTXDI_FinalizeResampling(state, 1.0, sampleParams.numMisSamples);
    state.M = 1;

    return state;
}

//
// Local light sampling
//

RTXDI_Reservoir RTXDI_SampleLocalLights(
    inout RAB_RandomSamplerState rng,
    inout RAB_RandomSamplerState coherentRng,
    RAB_Surface surface,
    RTXDI_SampleParameters sampleParams,
    RTXDI_RuntimeParameters params,
    out RAB_LightSample o_selectedSample)
{
    o_selectedSample = RAB_EmptyLightSample();

    if (params.localLightParams.localLightsBufferRegion.numLights == 0)
        return RTXDI_EmptyReservoir();

    if (sampleParams.numLocalLightSamples == 0)
        return RTXDI_EmptyReservoir();

    return RTXDI_SampleLocalLightsInternal(rng, coherentRng, surface, sampleParams, params, o_selectedSample);
}

//
// Uniform sampling for infinite lights
//

float2 RTXDI_RandomlySelectInfiniteLightUV(inout RAB_RandomSamplerState rng)
{
    float2 uv;
    uv.x = RAB_GetNextRandom(rng);
    uv.y = RAB_GetNextRandom(rng);
    return uv;
}

void RTXDI_StreamInfiniteLightAtUVIntoReservoir(
    inout RAB_RandomSamplerState rng,
    RAB_LightInfo lightInfo,
    RAB_Surface surface,
    uint lightIndex,
    float2 uv,
    float invSourcePdf,
    inout RTXDI_Reservoir state,
    inout RAB_LightSample o_selectedSample)
{
    RAB_LightSample candidateSample = RAB_SamplePolymorphicLight(lightInfo, surface, uv);
    float targetPdf = RAB_GetLightSampleTargetPdfForSurface(candidateSample, surface);
    float risRnd = RAB_GetNextRandom(rng);
    bool selected = RTXDI_StreamSample(state, lightIndex, uv, risRnd, targetPdf, invSourcePdf);

    if (selected)
    {
        o_selectedSample = candidateSample;
    }
}

RTXDI_Reservoir RTXDI_SampleInfiniteLights(
    inout RAB_RandomSamplerState rng,
    RAB_Surface surface,
    uint numSamples,
    RTXDI_InfiniteLightRuntimeParameters params,
    inout RAB_LightSample o_selectedSample)
{
    RTXDI_Reservoir state = RTXDI_EmptyReservoir();
    o_selectedSample = RAB_EmptyLightSample();

    if (params.infiniteLightsBufferRegion.numLights == 0)
        return state;

    if (numSamples == 0)
        return state;

    for (uint i = 0; i < numSamples; i++)
    {
        float invSourcePdf;
        uint lightIndex;
        RAB_LightInfo lightInfo;

        RTXDI_RandomlySelectLightUniformly(rng, params.infiniteLightsBufferRegion, lightInfo, lightIndex, invSourcePdf);
        float2 uv = RTXDI_RandomlySelectInfiniteLightUV(rng);
        RTXDI_StreamInfiniteLightAtUVIntoReservoir(rng, lightInfo, surface, lightIndex, uv, invSourcePdf, state, o_selectedSample);
    }

    RTXDI_FinalizeResampling(state, 1.0, state.M);
    state.M = 1;

    return state;
}

#if RTXDI_ENABLE_PRESAMPLING

//
// Power based RIS for the environment map. See PresampleEnvironmentMap.hlsl
//

RTXDI_RISTileInfo RTXDI_RandomlySelectEnvironmentLightRISTile(
    inout RAB_RandomSamplerState coherentRng,
    RTXDI_EnvironmentLightRuntimeParameters params)
{
    RTXDI_RISTileInfo risTileInfo;
    float tileRnd = RAB_GetNextRandom(coherentRng);
    uint tileIndex = uint(tileRnd * params.risBufferParams.tileCount);
    risTileInfo.risTileOffset = tileIndex * params.risBufferParams.tileSize + params.risBufferParams.bufferOffset;
    risTileInfo.risTileSize = params.risBufferParams.tileSize;
    return risTileInfo;
}

void RTXDI_UnpackEnvironmentLightDataFromRISData(
    uint2 tileData,
    out float2 uv,
    out float invSourcePdf
)
{
    uint packedUv = tileData.x;
    invSourcePdf = asfloat(tileData.y);
    uv = float2(packedUv & 0xffff, packedUv >> 16) / float(0xffff);
}

void RTXDI_RandomlySelectEnvironmentLightUVFromRISTile(
    inout RAB_RandomSamplerState rng,
    RTXDI_RISTileInfo risTileInfo,
    out float2 uv,
    out float invSourcePdf
)
{
    uint2 tileData;
    uint risBufferPtr;
    RTXDI_RandomlySelectLightDataFromRISTile(rng, risTileInfo, tileData, risBufferPtr);
    RTXDI_UnpackEnvironmentLightDataFromRISData(tileData, uv, invSourcePdf);
}

void RTXDI_StreamEnvironmentLightAtUVIntoReservoir(
    inout RAB_RandomSamplerState rng,
    RTXDI_SampleParameters sampleParams,
    RAB_Surface surface,
    RAB_LightInfo lightInfo,
    uint environmentLightIndex,
    float2 uv,
    float invSourcePdf,
    inout RTXDI_Reservoir state,
    inout RAB_LightSample o_selectedSample)
{
    RAB_LightSample candidateSample = RAB_SamplePolymorphicLight(lightInfo, surface, uv);
    float blendedSourcePdf = RTXDI_LightBrdfMisWeight(surface, candidateSample, 1.0 / invSourcePdf,
        sampleParams.environmentMapMisWeight, true, sampleParams);
    float targetPdf = RAB_GetLightSampleTargetPdfForSurface(candidateSample, surface);
    float risRnd = RAB_GetNextRandom(rng);

    bool selected = RTXDI_StreamSample(state, environmentLightIndex, uv, risRnd, targetPdf, 1.0 / blendedSourcePdf);

    if (selected) {
        o_selectedSample = candidateSample;
    }
}

RTXDI_Reservoir RTXDI_SampleEnvironmentMap(
    inout RAB_RandomSamplerState rng,
    inout RAB_RandomSamplerState coherentRng,
    RAB_Surface surface,
    RTXDI_SampleParameters sampleParams,
    RTXDI_EnvironmentLightRuntimeParameters params,
    out RAB_LightSample o_selectedSample)
{
    RTXDI_Reservoir state = RTXDI_EmptyReservoir();
    o_selectedSample = RAB_EmptyLightSample();

    if (params.environmentLightPresent == 0)
        return state;

    if (sampleParams.numEnvironmentMapSamples == 0)
        return state;

    RTXDI_RISTileInfo risTileInfo = RTXDI_RandomlySelectRISTile(coherentRng, params.risBufferParams);

    RAB_LightInfo lightInfo = RAB_LoadLightInfo(params.environmentLightIndex, false);

    for (uint i = 0; i < sampleParams.numEnvironmentMapSamples; i++)
    {
        float2 uv;
        float invSourcePdf;
        RTXDI_RandomlySelectEnvironmentLightUVFromRISTile(rng, risTileInfo, uv, invSourcePdf);
        RTXDI_StreamEnvironmentLightAtUVIntoReservoir(rng, sampleParams, surface, lightInfo, params.environmentLightIndex, uv, invSourcePdf, state, o_selectedSample);
    }

    RTXDI_FinalizeResampling(state, 1.0, sampleParams.numMisSamples);
    state.M = 1;

    return state;
}

#endif // RTXDI_ENABLE_PRESAMPLING

//
// BRDF sampling: Samples from the BRDF defined by the given surface
//

RTXDI_Reservoir RTXDI_SampleBrdf(
    inout RAB_RandomSamplerState rng,
    RAB_Surface surface,
    RTXDI_SampleParameters sampleParams,
    RTXDI_RuntimeParameters params,
    out RAB_LightSample o_selectedSample)
{
    RTXDI_Reservoir state = RTXDI_EmptyReservoir();
    
    for (uint i = 0; i < sampleParams.numBrdfSamples; ++i)
    {
        float lightSourcePdf = 0;
        float3 sampleDir;
        uint lightIndex = RTXDI_InvalidLightIndex;
        float2 randXY = float2(0, 0);
        RAB_LightSample candidateSample = RAB_EmptyLightSample();

        if (RAB_GetSurfaceBrdfSample(surface, rng, sampleDir))
        {
            float brdfPdf = RAB_GetSurfaceBrdfPdf(surface, sampleDir);
            float maxDistance = RTXDI_BrdfMaxDistanceFromPdf(sampleParams.brdfCutoff, brdfPdf);
            
            bool hitAnything = RAB_TraceRayForLocalLight(RAB_GetSurfaceWorldPos(surface), sampleDir,
                sampleParams.brdfRayMinT, maxDistance, lightIndex, randXY);

            if (lightIndex != RTXDI_InvalidLightIndex)
            {
                RAB_LightInfo lightInfo = RAB_LoadLightInfo(lightIndex, false);
                candidateSample = RAB_SamplePolymorphicLight(lightInfo, surface, randXY);
                    
                if (sampleParams.brdfCutoff > 0.f)
                {
                    // If Mis cutoff is used, we need to evaluate the sample and make sure it actually could have been
                    // generated by the area sampling technique. This is due to numerical precision.
                    float3 lightDir;
                    float lightDistance;
                    RAB_GetLightDirDistance(surface, candidateSample, lightDir, lightDistance);

                    float brdfPdf = RAB_GetSurfaceBrdfPdf(surface, lightDir);
                    float maxDistance = RTXDI_BrdfMaxDistanceFromPdf(sampleParams.brdfCutoff, brdfPdf);
                    if (lightDistance > maxDistance)
                        lightIndex = RTXDI_InvalidLightIndex;
                }

                if (lightIndex != RTXDI_InvalidLightIndex)
                {
                    lightSourcePdf = RAB_EvaluateLocalLightSourcePdf(params.localLightParams, lightIndex);
                }
            }
            else if (!hitAnything && params.environmentLightParams.environmentLightPresent != 0)
            {
                // sample environment light
                lightIndex = params.environmentLightParams.environmentLightIndex;
                RAB_LightInfo lightInfo = RAB_LoadLightInfo(lightIndex, false);
                randXY = RAB_GetEnvironmentMapRandXYFromDir(sampleDir);
                candidateSample = RAB_SamplePolymorphicLight(lightInfo, surface, randXY);
                lightSourcePdf = RAB_EvaluateEnvironmentMapSamplingPdf(sampleDir);
            }
        }

        if (lightSourcePdf == 0)
        {
            // Did not hit a visible light
            continue;
        }

        bool isEnvMapSample = lightIndex == params.environmentLightParams.environmentLightIndex;
        float targetPdf = RAB_GetLightSampleTargetPdfForSurface(candidateSample, surface);
        float blendedSourcePdf = RTXDI_LightBrdfMisWeight(surface, candidateSample, lightSourcePdf,
            isEnvMapSample ? sampleParams.environmentMapMisWeight : sampleParams.localLightMisWeight, 
            isEnvMapSample,
            sampleParams);
        float risRnd = RAB_GetNextRandom(rng);

        bool selected = RTXDI_StreamSample(state, lightIndex, randXY, risRnd, targetPdf, 1.0f / blendedSourcePdf);
        if (selected) {
            o_selectedSample = candidateSample;
        }
    }

    RTXDI_FinalizeResampling(state, 1.0, sampleParams.numMisSamples);
    state.M = 1;

    return state;
}

// Samples the local, infinite, and environment lights for a given surface
RTXDI_Reservoir RTXDI_SampleLightsForSurface(
    inout RAB_RandomSamplerState rng,
    inout RAB_RandomSamplerState coherentRng,
    RAB_Surface surface,
    RTXDI_SampleParameters sampleParams,
    RTXDI_RuntimeParameters params, 
    out RAB_LightSample o_lightSample)
{
    o_lightSample = RAB_EmptyLightSample();

    RTXDI_Reservoir localReservoir;
    RAB_LightSample localSample = RAB_EmptyLightSample();

    localReservoir = RTXDI_SampleLocalLights(rng, coherentRng, surface, 
        sampleParams, params, localSample);

    RAB_LightSample infiniteSample = RAB_EmptyLightSample();  
    RTXDI_Reservoir infiniteReservoir = RTXDI_SampleInfiniteLights(rng, surface,
        sampleParams.numInfiniteLightSamples, params.infiniteLightParams, infiniteSample);

#if RTXDI_ENABLE_PRESAMPLING
    RAB_LightSample environmentSample = RAB_EmptyLightSample();
    RTXDI_Reservoir environmentReservoir = RTXDI_SampleEnvironmentMap(rng, coherentRng, surface,
        sampleParams, params.environmentLightParams, environmentSample);
#endif // RTXDI_ENABLE_PRESAMPLING

    RAB_LightSample brdfSample = RAB_EmptyLightSample();
    RTXDI_Reservoir brdfReservoir = RTXDI_SampleBrdf(rng, surface, sampleParams, params, brdfSample);

    RTXDI_Reservoir state = RTXDI_EmptyReservoir();
    RTXDI_CombineReservoirs(state, localReservoir, 0.5, localReservoir.targetPdf);
    bool selectInfinite = RTXDI_CombineReservoirs(state, infiniteReservoir, RAB_GetNextRandom(rng), infiniteReservoir.targetPdf);
#if RTXDI_ENABLE_PRESAMPLING
    bool selectEnvironment = RTXDI_CombineReservoirs(state, environmentReservoir, RAB_GetNextRandom(rng), environmentReservoir.targetPdf);
#endif // RTXDI_ENABLE_PRESAMPLING
    bool selectBrdf = RTXDI_CombineReservoirs(state, brdfReservoir, RAB_GetNextRandom(rng), brdfReservoir.targetPdf);
    
    RTXDI_FinalizeResampling(state, 1.0, 1.0);
    state.M = 1;

    if (selectBrdf)
        o_lightSample = brdfSample;
    else
#if RTXDI_ENABLE_PRESAMPLING
    if (selectEnvironment)
        o_lightSample = environmentSample;
    else
#endif // RTXDI_ENABLE_PRESAMPLING
    if (selectInfinite)
        o_lightSample = infiniteSample;
    else
        o_lightSample = localSample;

    return state;
}

#endif