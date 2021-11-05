/***************************************************************************
 # Copyright (c) 2020-2021, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#ifndef RESAMPLING_FUNCTIONS_HLSLI
#define RESAMPLING_FUNCTIONS_HLSLI

#include "Reservoir.hlsli"

// This macro can be defined in the including shader file to reduce code bloat
// and/or remove ray tracing calls from temporal and spatial resampling shaders
// if bias correction is not necessary.
#ifndef RTXDI_ALLOWED_BIAS_CORRECTION
#define RTXDI_ALLOWED_BIAS_CORRECTION RTXDI_BIAS_CORRECTION_RAY_TRACED
#endif

// Adds a new, non-reservoir light sample into the reservoir, returns true if this sample was selected.
// Algorithm (3) from the ReSTIR paper, Streaming RIS using weighted reservoir sampling.
bool RTXDI_StreamSample(
    inout RTXDI_Reservoir reservoir,
    uint lightIndex,
    float2 uv,
    float random,
    float targetPdf,
    float invSourcePdf)
{
    // What's the current weight
    float risWeight = targetPdf * invSourcePdf;

    // Add one sample to the counter
    reservoir.M += 1;

    // Update the weight sum
    reservoir.weightSum += risWeight;

    // Decide if we will randomly pick this sample
    bool selectSample = (random * reservoir.weightSum < risWeight);

    // If we did select this sample, update the relevant data.
    // New samples don't have visibility or age information, we can skip that.
    if (selectSample) 
    {
        reservoir.lightData = lightIndex | RTXDI_Reservoir::c_LightValidBit;
        reservoir.uvData = uint(saturate(uv.x) * 0xffff) | (uint(saturate(uv.y) * 0xffff) << 16);
        reservoir.targetPdf = targetPdf;
    }

    return selectSample;
}

// Adds `newReservoir` into `reservoir`, returns true if the new reservoir's sample was selected.
// Algorithm (4) from the ReSTIR paper, Combining the streams of multiple reservoirs.
// Normalization - Equation (6) - is postponed until all reservoirs are combined.
bool RTXDI_CombineReservoirs(
    inout RTXDI_Reservoir reservoir,
    const RTXDI_Reservoir newReservoir,
    float random,
    float targetPdf)
{
    // What's the current weight (times any prior-step RIS normalization factor)
    float risWeight = targetPdf * newReservoir.weightSum * newReservoir.M;

    // Our *effective* candidate pool is the sum of our candidates plus those of our neighbors
    reservoir.M += newReservoir.M;

    // Update the weight sum
    reservoir.weightSum += risWeight;

    // Decide if we will randomly pick this sample
    bool selectSample = (random * reservoir.weightSum < risWeight);

    // If we did select this sample, update the relevant data
    if (selectSample) 
    {
        reservoir.lightData = newReservoir.lightData;
        reservoir.uvData = newReservoir.uvData;
        reservoir.targetPdf = targetPdf;
        reservoir.packedVisibility = newReservoir.packedVisibility;
        reservoir.spatialDistance = newReservoir.spatialDistance;
        reservoir.age = newReservoir.age;
    }

    return selectSample;
}

// Performs normalization of the reservoir after streaming. Equation (6) from the ReSTIR paper.
void RTXDI_FinalizeResampling(
    inout RTXDI_Reservoir reservoir,
    float normalizationNumerator,
    float normalizationDenominator)
{
    float denominator = reservoir.targetPdf * normalizationDenominator;

    reservoir.weightSum = (denominator == 0.0) ? 0.0 : (reservoir.weightSum * normalizationNumerator) / denominator;
}

void RTXDI_SamplePdfMipmap(
    inout RAB_RandomSamplerState rng, 
    Texture2D<float> pdfTexture, // full mip chain starting from unnormalized sampling pdf in mip 0
    uint2 pdfTextureSize,        // dimensions of pdfTexture at mip 0; must be 16k or less
    out uint2 position,
    out float pdf)
{
    int lastMipLevel = max(0, int(floor(log2(max(pdfTextureSize.x, pdfTextureSize.y)))) - 1);

    position = 0;
    pdf = 1.0;
    for (int mipLevel = lastMipLevel; mipLevel >= 0; mipLevel--)
    {
        position *= 2;

        float4 samples; // there's no version of Gather that supports mipmaps, really?
        samples.x = max(0, pdfTexture.Load(int3(position, mipLevel), int2(0, 0)).x);
        samples.y = max(0, pdfTexture.Load(int3(position, mipLevel), int2(0, 1)).x);
        samples.z = max(0, pdfTexture.Load(int3(position, mipLevel), int2(1, 0)).x);
        samples.w = max(0, pdfTexture.Load(int3(position, mipLevel), int2(1, 1)).x);

        float weightSum = samples.x + samples.y + samples.z + samples.w;
        if (weightSum <= 0)
        {
            pdf = 0;
            return;
        }

        samples /= weightSum;

        float rnd = RAB_GetNextRandom(rng);
        
        int2 selectedOffset;

        if (rnd < samples.x)
        { 
            pdf *= samples.x;
        }
        else
        {
            rnd -= samples.x;
            
            if (rnd < samples.y)
            {
                position += uint2(0, 1);
                pdf *= samples.y;
            }
            else
            {
                rnd -= samples.y;

                if (rnd < samples.z)
                {
                    position += uint2(1, 0);
                    pdf *= samples.z;
                }
                else
                {
                    position += uint2(1, 1);
                    pdf *= samples.w;
                }
            }
        }
    }
}

void RTXDI_PresampleLocalLights(
    inout RAB_RandomSamplerState rng, 
    Texture2D<float> pdfTexture,
    uint2 pdfTextureSize,
    uint tileIndex,
    uint sampleInTile,
    RTXDI_ResamplingRuntimeParameters params,
    RWBuffer<uint2> RisBuffer)
{
    uint2 texelPosition;
    float pdf;
    RTXDI_SamplePdfMipmap(rng, pdfTexture, pdfTextureSize, texelPosition, pdf);

    uint lightIndex = RTXDI_ZCurveToLinearIndex(texelPosition);

    uint risBufferPtr = sampleInTile + tileIndex * params.tileSize;

    bool compact = false;
    float invSourcePdf = 0;

    if (pdf > 0)
    {
        invSourcePdf = 1.0 / pdf;

        RAB_LightInfo lightInfo = RAB_LoadLightInfo(lightIndex + params.firstLocalLight, false);
        compact = RAB_StoreCompactLightInfo(risBufferPtr, lightInfo);
    }

    lightIndex += params.firstLocalLight;

    if(compact) {
        lightIndex |= RTXDI_LIGHT_COMPACT_BIT;
    }

    // Store the index of the light that we found and its inverse pdf.
    // Or zero and zero if we somehow found nothing.
    RisBuffer[risBufferPtr] = uint2(lightIndex, asuint(invSourcePdf));
}

void RTXDI_PresampleEnvironmentMap(
    inout RAB_RandomSamplerState rng, 
    Texture2D<float> pdfTexture,
    uint2 pdfTextureSize,
    uint tileIndex,
    uint sampleInTile,
    RTXDI_ResamplingRuntimeParameters params,
    RWBuffer<uint2> RisBuffer)
{
    uint2 texelPosition;
    float pdf;
    RTXDI_SamplePdfMipmap(rng, pdfTexture, pdfTextureSize, texelPosition, pdf);

    // Uniform sampling inside the pixels
    float2 fPos = float2(texelPosition);
    fPos.x += RAB_GetNextRandom(rng);
    fPos.y += RAB_GetNextRandom(rng);
    
    // Convert texel position to UV and pack it
    float2 uv = fPos / float2(pdfTextureSize);
    uint packedUv = uint(saturate(uv.x) * 0xffff) | (uint(saturate(uv.y) * 0xffff) << 16);

    // Compute the inverse PDF if we found something
    pdf *= pdfTextureSize.x * pdfTextureSize.y;
    float invSourcePdf = (pdf > 0) ? (1.0 / pdf) : 0;

    // Store the result
    uint risBufferPtr = params.environmentRisBufferOffset + sampleInTile + tileIndex * params.environmentTileSize;
    RisBuffer[risBufferPtr] = uint2(packedUv, asuint(invSourcePdf));
}

#ifndef RTXDI_TILE_SIZE_IN_PIXELS
#define RTXDI_TILE_SIZE_IN_PIXELS 16
#endif

// SDK internal function that samples the given set of lights generated by RIS
// or the local light pool. The RIS set can come from local light importance presampling or from ReGIR.
RTXDI_Reservoir RTXDI_SampleLocalLightsInternal(
    inout RAB_RandomSamplerState rng, 
    RAB_Surface surface, 
    uint numSamples,
    bool useRisBuffer,
    uint risBufferBase,
    uint risBufferCount,
    RTXDI_ResamplingRuntimeParameters params,
    RWBuffer<uint2> RisBuffer,
    out RAB_LightSample o_selectedSample)
{
    RTXDI_Reservoir state = RTXDI_EmptyReservoir();
    o_selectedSample = (RAB_LightSample)0;

    if (params.numLocalLights == 0)
        return state;

    if (numSamples == 0)
        return state;

    for (uint i = 0; i < numSamples; i++)
    {
        float rnd = RAB_GetNextRandom(rng);

        uint rndLight;
        RAB_LightInfo lightInfo = (RAB_LightInfo)0;
        float invSourcePdf;
        bool lightLoaded = false;

        if (useRisBuffer)
        {
            uint risSample = min(uint(floor(rnd * risBufferCount)), risBufferCount - 1);
            uint risBufferPtr = risSample + risBufferBase;
            
            uint2 tileData = RisBuffer[risBufferPtr];
            rndLight = tileData.x & RTXDI_LIGHT_INDEX_MASK;
            invSourcePdf = asfloat(tileData.y);

            if (tileData.x & RTXDI_LIGHT_COMPACT_BIT)
            {
                lightInfo = RAB_LoadCompactLightInfo(risBufferPtr);
                lightLoaded = true;
            }
        }
        else
        {
            rndLight = min(uint(floor(rnd * params.numLocalLights)), params.numLocalLights - 1) + params.firstLocalLight;
            invSourcePdf = float(params.numLocalLights);
        }

        if (!lightLoaded) {
            lightInfo = RAB_LoadLightInfo(rndLight, false);
        }

        float2 uv;
        uv.x = RAB_GetNextRandom(rng);
        uv.y = RAB_GetNextRandom(rng);

        RAB_LightSample candidateSample = RAB_SamplePolymorphicLight(lightInfo, surface, uv);

        float targetPdf = RAB_GetLightSampleTargetPdfForSurface(candidateSample, surface);
        float risRnd = RAB_GetNextRandom(rng);

        bool selected = RTXDI_StreamSample(state, rndLight, uv, risRnd, targetPdf, invSourcePdf);

        if (selected) {
            o_selectedSample = candidateSample;
        }
    }

    RTXDI_FinalizeResampling(state, 1.0, state.M);
    state.M = 1;

    return state;
}

// Samples the local light pool for the given surface.
RTXDI_Reservoir RTXDI_SampleLocalLights(
    inout RAB_RandomSamplerState rng, 
    inout RAB_RandomSamplerState coherentRng,
    RAB_Surface surface, 
    uint numSamples,
    RTXDI_ResamplingRuntimeParameters params,
    RWBuffer<uint2> RisBuffer,
    out RAB_LightSample o_selectedSample)
{
    float tileRnd = RAB_GetNextRandom(coherentRng);
    uint tileIndex = uint(tileRnd * params.tileCount);

    uint risBufferBase = tileIndex * params.tileSize;

    return RTXDI_SampleLocalLightsInternal(rng, surface, numSamples,
        params.enableLocalLightImportanceSampling, risBufferBase, params.tileSize,
        params, RisBuffer, o_selectedSample);
}

// Samples the infinite light pool for the given surface.
RTXDI_Reservoir RTXDI_SampleInfiniteLights(
    inout RAB_RandomSamplerState rng, 
    RAB_Surface surface, 
    uint numSamples,
    RTXDI_ResamplingRuntimeParameters params,
    RWBuffer<uint2> RisBuffer,
    out RAB_LightSample o_selectedSample)
{
    RTXDI_Reservoir state = RTXDI_EmptyReservoir();
    o_selectedSample = (RAB_LightSample)0;

    if (params.numInfiniteLights == 0)
        return state;

    if (numSamples == 0)
        return state;

    uint stride = (params.numInfiniteLights + numSamples - 1) / numSamples;
    uint randStart = uint(RAB_GetNextRandom(rng) * params.numInfiniteLights);

    float invSourcePdf = float(params.numInfiniteLights);

    for(uint i = 0; i < numSamples; i++)
    {
        float rnd = RAB_GetNextRandom(rng);

        uint rndLight = params.firstInfiniteLight 
            + min(uint(floor(rnd * params.numInfiniteLights)), params.numInfiniteLights - 1);

        float2 uv;
        uv.x = RAB_GetNextRandom(rng);
        uv.y = RAB_GetNextRandom(rng);

        RAB_LightInfo lightInfo = RAB_LoadLightInfo(rndLight, false);
        
        RAB_LightSample candidateSample = RAB_SamplePolymorphicLight(lightInfo, surface, uv);
        
        float targetPdf = RAB_GetLightSampleTargetPdfForSurface(candidateSample, surface);
        float risRnd = RAB_GetNextRandom(rng);

        bool selected = RTXDI_StreamSample(state, rndLight, uv, risRnd, targetPdf, invSourcePdf);

        if(selected)
        {
            o_selectedSample = candidateSample;
        }
    }

    RTXDI_FinalizeResampling(state, 1.0, state.M);
    state.M = 1;

    return state;
}

RTXDI_Reservoir RTXDI_SampleEnvironmentMap(
    inout RAB_RandomSamplerState rng, 
    inout RAB_RandomSamplerState coherentRng,
    RAB_Surface surface, 
    uint numSamples,
    RTXDI_ResamplingRuntimeParameters params,
    RWBuffer<uint2> RisBuffer,
    out RAB_LightSample o_selectedSample)
{
    RTXDI_Reservoir state = RTXDI_EmptyReservoir();
    o_selectedSample = (RAB_LightSample)0;

    if (!params.environmentLightPresent)
        return state;

    if (numSamples == 0)
        return state;

    float tileRnd = RAB_GetNextRandom(coherentRng);
    uint tileIndex = uint(tileRnd * params.environmentTileCount);

    uint risBufferBase = tileIndex * params.environmentTileSize + params.environmentRisBufferOffset;
    uint risBufferCount = params.environmentTileSize;

    RAB_LightInfo lightInfo = RAB_LoadLightInfo(params.environmentLightIndex, false);

    for (uint i = 0; i < numSamples; i++)
    {
        float rnd = RAB_GetNextRandom(rng);
        uint risSample = min(uint(floor(rnd * risBufferCount)), risBufferCount - 1);
        uint risBufferPtr = risSample + risBufferBase;
        
        uint2 tileData = RisBuffer[risBufferPtr];
        uint packedUv = tileData.x;
        float invSourcePdf = asfloat(tileData.y);

        float2 uv = float2(packedUv & 0xffff, packedUv >> 16) / float(0xffff);        

        RAB_LightSample candidateSample = RAB_SamplePolymorphicLight(lightInfo, surface, uv);

        float targetPdf = RAB_GetLightSampleTargetPdfForSurface(candidateSample, surface);
        float risRnd = RAB_GetNextRandom(rng);

        bool selected = RTXDI_StreamSample(state, params.environmentLightIndex, uv, risRnd, targetPdf, invSourcePdf);

        if (selected) {
            o_selectedSample = candidateSample;
        }
    }

    RTXDI_FinalizeResampling(state, 1.0, state.M);
    state.M = 1;

    return state;
}

#if RTXDI_REGIR_MODE != RTXDI_REGIR_DISABLED

// ReGIR grid build pass.
// Each thread populates one light slot in a grid cell.
void RTXDI_PresampleLocalLightsForReGIR(
    inout RAB_RandomSamplerState rng, 
    inout RAB_RandomSamplerState coherentRng,
    uint lightSlot,
    uint numSamples,
    RTXDI_ResamplingRuntimeParameters params, 
    RWBuffer<uint2> RisBuffer)
{
    uint risBufferPtr = params.regirCommon.risBufferOffset + lightSlot;

    if (numSamples == 0)
    {
        RisBuffer[risBufferPtr] = 0;
        return;
    }

    uint lightInCell = lightSlot % params.regirCommon.lightsPerCell;

    uint cellIndex = lightSlot / params.regirCommon.lightsPerCell;

    float3 cellCenter;
    float cellRadius;
    if (!RTXDI_ReGIR_CellIndexToWorldPos(params, cellIndex, cellCenter, cellRadius))
    {
        RisBuffer[risBufferPtr] = 0;
        return;
    }

    cellRadius *= (params.regirCommon.samplingJitter + 1.0);

    RAB_LightInfo selectedLightInfo = (RAB_LightInfo)0;
    uint selectedLight = 0;
    float selectedTargetPdf = 0;
    float weightSum = 0;


    float rndTileSample = RAB_GetNextRandom(coherentRng);
    uint tileIndex = uint(rndTileSample * params.tileCount);

    float invNumSamples = 1.0 / float(numSamples);
    
    for (uint i = 0; i < numSamples; i++)
    {
        uint rndLight;
        RAB_LightInfo lightInfo = (RAB_LightInfo)0;
        float invSourcePdf;
        float rand = RAB_GetNextRandom(rng);
        bool lightLoaded = false;

        if (params.enableLocalLightImportanceSampling)
        {
            uint tileSample = min(rand * params.tileSize, params.tileSize - 1);
            uint tilePtr = tileSample + tileIndex * params.tileSize;
            
            uint2 tileData = RisBuffer[tilePtr];
            rndLight = tileData.x & RTXDI_LIGHT_INDEX_MASK;
            invSourcePdf = asfloat(tileData.y) * invNumSamples;

            if (tileData.x & RTXDI_LIGHT_COMPACT_BIT)
            {
                lightInfo = RAB_LoadCompactLightInfo(tilePtr);
                lightLoaded = true;
            }
        }
        else
        {
            rndLight = min(rand * params.numLocalLights, params.numLocalLights - 1) + params.firstLocalLight;
            invSourcePdf = float(params.numLocalLights) * invNumSamples;
        }

        if (!lightLoaded) {
            lightInfo = RAB_LoadLightInfo(rndLight, false);
        }

        float targetPdf = RAB_GetLightTargetPdfForVolume(lightInfo, cellCenter, cellRadius);
        float risRnd = RAB_GetNextRandom(rng);

        float risWeight = targetPdf * invSourcePdf;
        weightSum += risWeight;

        if (risRnd * weightSum < risWeight)
        {
            selectedLightInfo = lightInfo;
            selectedLight = rndLight;
            selectedTargetPdf = targetPdf;
        }
    }

    float weight = (selectedTargetPdf > 0) ? weightSum / selectedTargetPdf : 0;

    bool compact = false;

    if (weight > 0) {
        compact = RAB_StoreCompactLightInfo(risBufferPtr, selectedLightInfo);
    }

    if(compact) {
        selectedLight |= RTXDI_LIGHT_COMPACT_BIT;
    }

    RisBuffer[risBufferPtr] = uint2(selectedLight, asuint(weight));
}

// Sampling lights for a surface from the ReGIR structure or the local light pool.
// If the surface is inside the ReGIR structure, and ReGIR is enabled, and
// numRegirSamples is nonzero, then this function will sample the ReGIR structure.
// Otherwise, it samples the local light pool.
RTXDI_Reservoir RTXDI_SampleLocalLightsFromReGIR(
    inout RAB_RandomSamplerState rng,
    inout RAB_RandomSamplerState coherentRng,
    RAB_Surface surface,
    uint numRegirSamples,
    uint numLocalLightSamples,
    RTXDI_ResamplingRuntimeParameters params,
    RWBuffer<uint2> RisBuffer,
    out RAB_LightSample o_selectedSample)
{
    RTXDI_Reservoir reservoir = RTXDI_EmptyReservoir();
    o_selectedSample = (RAB_LightSample)0;

    if (numRegirSamples == 0 && numLocalLightSamples == 0)
        return reservoir;

    int cellIndex = -1;

    if (params.regirCommon.enable && numRegirSamples > 0)
    {
        float3 cellJitter = float3(
            RAB_GetNextRandom(coherentRng),
            RAB_GetNextRandom(coherentRng),
            RAB_GetNextRandom(coherentRng));
        cellJitter -= 0.5;

        float3 samplingPos = RAB_GetSurfaceWorldPos(surface);
        float jitterScale = RTXDI_ReGIR_GetJitterScale(params, samplingPos);
        samplingPos += cellJitter * jitterScale;

        cellIndex = RTXDI_ReGIR_WorldPosToCellIndex(params, samplingPos);
    }

    uint risBufferBase, risBufferCount, numSamples;
    bool useRisBuffer;

    if (cellIndex < 0)
    {
        float tileRnd = RAB_GetNextRandom(coherentRng);
        uint tileIndex = uint(tileRnd * params.tileCount);

        risBufferBase = tileIndex * params.tileSize;
        risBufferCount = params.tileSize;
        numSamples = numLocalLightSamples;
        useRisBuffer = params.enableLocalLightImportanceSampling;
    }
    else
    {
        uint cellBase = uint(cellIndex) * params.regirCommon.lightsPerCell;
        risBufferBase = cellBase + params.regirCommon.risBufferOffset;
        risBufferCount =  params.regirCommon.lightsPerCell;
        numSamples = numRegirSamples;
        useRisBuffer = true;
    }

    reservoir = RTXDI_SampleLocalLightsInternal(rng, surface, numSamples,
        useRisBuffer, risBufferBase, risBufferCount, params, RisBuffer, o_selectedSample);

    return reservoir;
}

#endif // (RTXDI_REGIR_MODE != RTXDI_REGIR_DISABLED)

// Samples ReGIR and the local and infinite light pools for a given surface.
RTXDI_Reservoir RTXDI_SampleLightsForSurface(
    inout RAB_RandomSamplerState rng,
    inout RAB_RandomSamplerState coherentRng,
    RAB_Surface surface,
    uint numRegirSamples,
    uint numLocalLightSamples,
    uint numInfiniteLightSamples,
    uint numEnvironmentMapSamples,
    RTXDI_ResamplingRuntimeParameters params, 
    RWBuffer<uint2> RisBuffer,
    out RAB_LightSample o_lightSample)
{
    o_lightSample = (RAB_LightSample)0;

    RTXDI_Reservoir localReservoir;
    RAB_LightSample localSample = (RAB_LightSample)0;
    
#if RTXDI_REGIR_MODE != RTXDI_REGIR_DISABLED
    // If ReGIR is enabled and the surface is inside the grid, sample the grid.
    // Otherwise, fall back to source pool sampling.
    localReservoir = RTXDI_SampleLocalLightsFromReGIR(rng, coherentRng,
        surface, numRegirSamples, numLocalLightSamples, params, RisBuffer, localSample);
#else
    localReservoir = RTXDI_SampleLocalLights(rng, coherentRng, surface, 
        numLocalLightSamples, params, RisBuffer, localSample);
#endif

    RAB_LightSample infiniteSample = (RAB_LightSample)0;  
    RTXDI_Reservoir infiniteReservoir = RTXDI_SampleInfiniteLights(rng, surface,
        numInfiniteLightSamples, params, RisBuffer, infiniteSample);

    RAB_LightSample environmentSample = (RAB_LightSample)0;
    RTXDI_Reservoir environmentReservoir = RTXDI_SampleEnvironmentMap(rng, coherentRng, surface,
        numEnvironmentMapSamples, params, RisBuffer, environmentSample);

    RTXDI_Reservoir state = RTXDI_EmptyReservoir();
    RTXDI_CombineReservoirs(state, localReservoir, 0.5, localReservoir.targetPdf);
    bool selectInfinite = RTXDI_CombineReservoirs(state, infiniteReservoir, RAB_GetNextRandom(rng), infiniteReservoir.targetPdf);
    bool selectEnvironment = RTXDI_CombineReservoirs(state, environmentReservoir, RAB_GetNextRandom(rng), environmentReservoir.targetPdf);

    RTXDI_FinalizeResampling(state, 1.0, 1.0);
    state.M = 1;

    if (selectEnvironment)
        o_lightSample = environmentSample;
    else if (selectInfinite)
        o_lightSample = infiniteSample;
    else
        o_lightSample = localSample;

    return state;
}

#ifdef RTXDI_ENABLE_BOILING_FILTER
// RTXDI_BOILING_FILTER_GROUP_SIZE must be defined - 16 is a reasonable value
#define RTXDI_BOILING_FILTER_MIN_LANE_COUNT 32

groupshared float s_weights[(RTXDI_BOILING_FILTER_GROUP_SIZE * RTXDI_BOILING_FILTER_GROUP_SIZE + RTXDI_BOILING_FILTER_MIN_LANE_COUNT - 1) / RTXDI_BOILING_FILTER_MIN_LANE_COUNT];
groupshared uint s_count[(RTXDI_BOILING_FILTER_GROUP_SIZE * RTXDI_BOILING_FILTER_GROUP_SIZE + RTXDI_BOILING_FILTER_MIN_LANE_COUNT - 1) / RTXDI_BOILING_FILTER_MIN_LANE_COUNT];

// Boiling filter that should be applied at the end of the temporal resampling pass.
// Can be used inside the same shader that does temporal resampling if it's a compute shader,
// or in a separate pass if temporal resampling is a raygen shader.
// The filter analyzes the weights of all reservoirs in a thread group, and discards
// the reservoirs whose weights are very high, i.e. above a certain threshold.
void RTXDI_BoilingFilter(
    uint2 LocalIndex,
    float filterStrength, // (0..1]
    RTXDI_ResamplingRuntimeParameters params,
    inout RTXDI_Reservoir state)
{
    // Boiling happens when some highly unlikely light is discovered and it is relevant
    // for a large surface area around the pixel that discovered it. Then this light sample
    // starts to propagate to the neighborhood through spatiotemporal reuse, which looks like
    // a flash. We can detect such lights because their weight is significantly higher than 
    // the weight of their neighbors. So, compute the average group weight and apply a threshold.

    float boilingFilterMultiplier = 10.f / clamp(filterStrength, 1e-6, 1.0) - 9.f;

    // Start with average nonzero weight within the wavefront
    float waveWeight = WaveActiveSum(state.weightSum);
    uint waveCount = WaveActiveCountBits(state.weightSum > 0);

    // Store the results of each wavefront into shared memory
    uint linearThreadIndex = LocalIndex.x + LocalIndex.y * RTXDI_BOILING_FILTER_GROUP_SIZE;
    uint waveIndex = linearThreadIndex / WaveGetLaneCount();

    if (WaveIsFirstLane())
    {
        s_weights[waveIndex] = waveWeight;
        s_count[waveIndex] = waveCount;
    }

    GroupMemoryBarrierWithGroupSync();

    // Reduce the per-wavefront averages into a global average using one wavefront
    if (linearThreadIndex < (RTXDI_BOILING_FILTER_GROUP_SIZE * RTXDI_BOILING_FILTER_GROUP_SIZE + WaveGetLaneCount() - 1) / WaveGetLaneCount())
    {
        waveWeight = s_weights[linearThreadIndex];
        waveCount = s_count[linearThreadIndex];

        waveWeight = WaveActiveSum(waveWeight);
        waveCount = WaveActiveSum(waveCount);

        if (linearThreadIndex == 0)
        {
            s_weights[0] = (waveCount > 0) ? (waveWeight / float(waveCount)) : 0.0;
        }
    }

    GroupMemoryBarrierWithGroupSync();

    // Read the per-group average and apply the threshold
    float averageNonzeroWeight = s_weights[0];
    if (state.weightSum > averageNonzeroWeight * boilingFilterMultiplier)
    {
        state = RTXDI_EmptyReservoir();
    }
}
#endif // RTXDI_ENABLE_BOILING_FILTER


// Internal SDK function that permutes the pixels sampled from the previous frame.
void RTXDI_ApplyPermutationSampling(inout int2 prevPixelPos, uint uniformRandomNumber)
{
    int2 offset = int2(uniformRandomNumber & 3, (uniformRandomNumber >> 2) & 3);
    prevPixelPos += offset;
 
    prevPixelPos.x ^= 3;
    prevPixelPos.y ^= 3;
    
    prevPixelPos -= offset;
}

// A structure that groups the application-provided settings for temporal resampling.
struct RTXDI_TemporalResamplingParameters
{
    // Screen-space motion vector, computed as (previousPosition - currentPosition).
    // The X and Y components are measured in pixels.
    // The Z component is in linear depth units.
    float3 screenSpaceMotion;

    // The index of the reservoir buffer to pull the temporal samples from.
    uint sourceBufferIndex;

    // Maximum history length for temporal reuse, measured in frames.
    // Higher values result in more stable and high quality sampling, at the cost of slow reaction to changes.
    uint maxHistoryLength;

    // Controls the bias correction math for temporal reuse. Depending on the setting, it can add
    // some shader cost and one approximate shadow ray per pixel (or per two pixels if checkerboard sampling is enabled).
    // Ideally, these rays should be traced through the previous frame's BVH to get fully unbiased results.
    uint biasCorrectionMode;

    // Surface depth similarity threshold for temporal reuse.
    // If the previous frame surface's depth is within this threshold from the current frame surface's depth,
    // the surfaces are considered similar. The threshold is relative, i.e. 0.1 means 10% of the current depth.
    // Otherwise, the pixel is not reused, and the resampling shader will look for a different one.
    float depthThreshold;

    // Surface normal similarity threshold for temporal reuse.
    // If the dot product of two surfaces' normals is higher than this threshold, the surfaces are considered similar.
    // Otherwise, the pixel is not reused, and the resampling shader will look for a different one.
    float normalThreshold;

    // Allows the temporal resampling logic to skip the bias correction ray trace for light samples
    // reused from the previous frame. Only safe to use when invisible light samples are discarded
    // on the previous frame, then any sample coming from the previous frame can be assumed visible.
    bool enableVisibilityShortcut;

    // Enables permuting the pixels sampled from the previous frame in order to add temporal
    // variation to the output signal and make it more denoiser friendly.
    bool enablePermutationSampling;
};

// Temporal resampling pass.
// Takes the previous G-buffer, motion vectors, and two light reservoir buffers as inputs.
// Tries to match the surfaces in the current frame to surfaces in the previous frame.
// If a match is found for a given pixel, the current and previous reservoirs are 
// combined. An optional visibility ray may be cast if enabled, to reduce the resampling bias.
// That visibility ray should ideally be traced through the previous frame BVH, but
// can also use the current frame BVH if the previous is not available - that will produce more bias.
// The selectedLightSample parameter is used to update and return the selected sample; it's optional,
// and it's safe to pass a null structure there and ignore the result.
RTXDI_Reservoir RTXDI_TemporalResampling(
    uint2 pixelPosition,
    RAB_Surface surface,
    RTXDI_Reservoir curSample,
    RAB_RandomSamplerState rng,
    RTXDI_TemporalResamplingParameters tparams,
    RTXDI_ResamplingRuntimeParameters params,
    RWStructuredBuffer<RTXDI_PackedReservoir> LightReservoirs,
    out int2 temporalSamplePixelPos,
    inout RAB_LightSample selectedLightSample)
{
    int historyLimit = min(RTXDI_Reservoir::c_MaxM, tparams.maxHistoryLength * curSample.M);

    int selectedLightPrevID = -1;

    if (RTXDI_IsValidReservoir(curSample))
    {
        selectedLightPrevID = RAB_TranslateLightIndex(RTXDI_GetReservoirLightIndex(curSample), true);
    }

    temporalSamplePixelPos = -1;

    RTXDI_Reservoir state = RTXDI_EmptyReservoir();
    RTXDI_CombineReservoirs(state, curSample, /* random = */ 0.5, curSample.targetPdf);

    // Backproject this pixel to last frame
    float3 motion = tparams.screenSpaceMotion;
    
    if (!tparams.enablePermutationSampling)
    {
        motion.xy += float2(RAB_GetNextRandom(rng), RAB_GetNextRandom(rng)) - 0.5;
    }

    float2 reprojectedSamplePosition = float2(pixelPosition) + motion.xy;
    int2 prevPos = int2(round(reprojectedSamplePosition));

    float expectedPrevLinearDepth = RAB_GetSurfaceLinearDepth(surface) + motion.z;

    RAB_Surface temporalSurface = (RAB_Surface)0;
    bool foundNeighbor = false;
    const float radius = (params.activeCheckerboardField == 0) ? 4 : 8;
    int2 spatialOffset = 0;

    // Try to find a matching surface in the neighborhood of the reprojected pixel
    for(int i = 0; i < 9; i++)
    {
        int2 offset = 0;
        if(i > 0)
        {
            offset.x = int((RAB_GetNextRandom(rng) - 0.5) * radius);
            offset.y = int((RAB_GetNextRandom(rng) - 0.5) * radius);
        }

        int2 idx = prevPos + offset;
        if (tparams.enablePermutationSampling && i == 0)
        {
            RTXDI_ApplyPermutationSampling(idx, params.uniformRandomNumber);
        }

        if (!RTXDI_IsActiveCheckerboardPixel(idx, true, params))
        {
            idx.x += int(params.activeCheckerboardField) * 2 - 3;
        }

        // Grab shading / g-buffer data from last frame
        temporalSurface = RAB_GetGBufferSurface(idx, true);
        if (!RAB_IsSurfaceValid(temporalSurface))
            continue;
        
        // Test surface similarity, discard the sample if the surface is too different.
        if (!RTXDI_IsValidNeighbor(
            RAB_GetSurfaceNormal(surface), RAB_GetSurfaceNormal(temporalSurface), 
            expectedPrevLinearDepth, RAB_GetSurfaceLinearDepth(temporalSurface), 
            tparams.normalThreshold, tparams.depthThreshold))
            continue;

        spatialOffset = idx - prevPos;
        prevPos = idx;
        foundNeighbor = true;

        break;
    }

    bool selectedPreviousSample = false;
    uint previousM = 0;

    if (foundNeighbor)
    {
        // Resample the previous frame sample into the current reservoir, but reduce the light's weight
        // according to the bilinear weight of the current pixel
        uint2 prevReservoirPos = RTXDI_PixelPosToReservoir(prevPos, params);
        RTXDI_Reservoir prevSample = RTXDI_LoadReservoir(params, LightReservoirs,
            prevReservoirPos, tparams.sourceBufferIndex);
        prevSample.M = min(prevSample.M, historyLimit);
        prevSample.spatialDistance += spatialOffset;
        prevSample.age += 1;

        uint originalPrevLightID = RTXDI_GetReservoirLightIndex(prevSample);

        // Map the light ID from the previous frame into the current frame, if it still exists
        if (RTXDI_IsValidReservoir(prevSample))
        {
            if (prevSample.age <= 1)
            {
                temporalSamplePixelPos = prevPos;
            }

            int mappedLightID = RAB_TranslateLightIndex(RTXDI_GetReservoirLightIndex(prevSample), false);

            if (mappedLightID < 0)
            {
                // Kill the reservoir
                prevSample.weightSum = 0;
                prevSample.lightData = 0;
            }
            else
            {
                // Sample is valid - modify the light ID stored
                prevSample.lightData = mappedLightID | RTXDI_Reservoir::c_LightValidBit;
            }
        }

        previousM = prevSample.M;

        float weightAtCurrent = 0;
        RAB_LightSample candidateLightSample = (RAB_LightSample)0;
        if (RTXDI_IsValidReservoir(prevSample))
        {
            const RAB_LightInfo candidateLight = RAB_LoadLightInfo(RTXDI_GetReservoirLightIndex(prevSample), false);

            candidateLightSample = RAB_SamplePolymorphicLight(
                candidateLight, surface, RTXDI_GetReservoirSampleUV(prevSample));

            weightAtCurrent = RAB_GetLightSampleTargetPdfForSurface(candidateLightSample, surface);
        }

        bool sampleSelected = RTXDI_CombineReservoirs(state, prevSample, RAB_GetNextRandom(rng), weightAtCurrent);
        if(sampleSelected)
        {
            selectedPreviousSample = true;
            selectedLightPrevID = int(originalPrevLightID);
            selectedLightSample = candidateLightSample;
        }
    }

#if RTXDI_ALLOWED_BIAS_CORRECTION >= RTXDI_BIAS_CORRECTION_BASIC
    if (tparams.biasCorrectionMode >= RTXDI_BIAS_CORRECTION_BASIC)
    {
        // Compute the unbiased normalization term (instead of using 1/M)
        float pi = state.targetPdf;
        float piSum = state.targetPdf * curSample.M;
        
        if (RTXDI_IsValidReservoir(state) && selectedLightPrevID >= 0 && previousM > 0)
        {
            float temporalP = 0;

            const RAB_LightInfo selectedLightPrev = RAB_LoadLightInfo(selectedLightPrevID, true);

            // Get the PDF of the sample RIS selected in the first loop, above, *at this neighbor* 
            const RAB_LightSample selectedSampleAtTemporal = RAB_SamplePolymorphicLight(
                selectedLightPrev, temporalSurface, RTXDI_GetReservoirSampleUV(state));
        
            temporalP = RAB_GetLightSampleTargetPdfForSurface(selectedSampleAtTemporal, temporalSurface);

#if RTXDI_ALLOWED_BIAS_CORRECTION >= RTXDI_BIAS_CORRECTION_RAY_TRACED
            if (tparams.biasCorrectionMode == RTXDI_BIAS_CORRECTION_RAY_TRACED && temporalP > 0 && (!selectedPreviousSample || !tparams.enableVisibilityShortcut))
            {
                if (!RAB_GetTemporalConservativeVisibility(surface, temporalSurface, selectedSampleAtTemporal))
                {
                    temporalP = 0;
                }
            }
#endif

            pi = selectedPreviousSample ? temporalP : pi;
            piSum += temporalP * previousM;
        }

        RTXDI_FinalizeResampling(state, pi, piSum);
    }
    else
#endif
    {
        RTXDI_FinalizeResampling(state, 1.0, state.M);
    }

    return state;
}

// A structure that groups the application-provided settings for spatial resampling.
struct RTXDI_SpatialResamplingParameters
{
    // The index of the reservoir buffer to pull the spatial samples from.
    uint sourceBufferIndex;
    
    // Number of neighbor pixels considered for resampling (1-32)
    // Some of the may be skipped if they fail the surface similarity test.
    uint numSamples;

    // Number of neighbor pixels considered when there is not enough history data (1-32)
    // Setting this parameter equal or lower than `numSpatialSamples` effectively
    // disables the disocclusion boost.
    uint numDisocclusionBoostSamples;

    // Disocclusion boost is activated when the current reservoir's M value
    // is less than targetHistoryLength.
    uint targetHistoryLength;


    // Controls the bias correction math for spatial reuse. Depending on the setting, it can add
    // some shader cost and one approximate shadow ray *per every spatial sample* per pixel 
    // (or per two pixels if checkerboard sampling is enabled).
    uint biasCorrectionMode;

    // Screen-space radius for spatial resampling, measured in pixels.
    float samplingRadius;

    // Surface depth similarity threshold for spatial reuse.
    // See 'RTXDI_TemporalResamplingParameters::depthThreshold' for more information.
    float depthThreshold;

    // Surface normal similarity threshold for spatial reuse.
    // See 'RTXDI_TemporalResamplingParameters::normalThreshold' for more information.
    float normalThreshold;
};

// Spatial resampling pass.
// Operates on the current frame G-buffer and its reservoirs.
// For each pixel, considers a number of its neighbors and, if their surfaces are 
// similar enough to the current pixel, combines their light reservoirs.
// Optionally, one visibility ray is traced for each neighbor being considered, to reduce bias.
// The selectedLightSample parameter is used to update and return the selected sample; it's optional,
// and it's safe to pass a null structure there and ignore the result.
RTXDI_Reservoir RTXDI_SpatialResampling(
    uint2 pixelPosition,
    RAB_Surface centerSurface,
    RTXDI_Reservoir centerSample,
    RAB_RandomSamplerState rng,
    RTXDI_SpatialResamplingParameters sparams,
    RTXDI_ResamplingRuntimeParameters params,
    RWStructuredBuffer<RTXDI_PackedReservoir> LightReservoirs,
    Buffer<float2> NeighborOffsets,
    inout RAB_LightSample selectedLightSample)
{
    RTXDI_Reservoir state = RTXDI_EmptyReservoir();

    // This is the weight we'll use (instead of 1/M) to make our estimate unbaised (see paper).
    float normalizationWeight = 1.0f;

    // Since we're using our bias correction scheme, we need to remember which light selection we made
    int selected = -1;

    RAB_LightInfo selectedLight = (RAB_LightInfo)0;

    if (RTXDI_IsValidReservoir(centerSample))
    {
        selectedLight = RAB_LoadLightInfo(RTXDI_GetReservoirLightIndex(centerSample), false);
    }

    RTXDI_CombineReservoirs(state, centerSample, /* random = */ 0.5f, centerSample.targetPdf);

    uint startIdx = RAB_GetNextRandom(rng) * params.neighborOffsetMask;
    
    int i;
    int numSpatialSamples = sparams.numSamples;
    if(centerSample.M < sparams.targetHistoryLength)
        numSpatialSamples = max(sparams.numDisocclusionBoostSamples, numSpatialSamples);

    // Clamp the sample count at 32 to make sure we can keep the neighbor mask in an uint (cachedResult)
    numSpatialSamples = min(numSpatialSamples, 32);

    // We loop through neighbors twice.  Cache the validity / edge-stopping function
    //   results for the 2nd time through.
    uint cachedResult = 0;

    // Walk the specified number of neighbors, resampling using RIS
    for (i = 0; i < numSpatialSamples; ++i)
    {
        // Get screen-space location of neighbor
        uint sampleIdx = (startIdx + i) & params.neighborOffsetMask;
        int2 spatialOffset = int2(float2(NeighborOffsets[sampleIdx].xy) * sparams.samplingRadius);
        int2 idx = pixelPosition + spatialOffset;

        if (!RTXDI_IsActiveCheckerboardPixel(idx, false, params))
            idx.x += (idx.y & 1) ? 1 : -1;

        RAB_Surface neighborSurface = RAB_GetGBufferSurface(idx, false);

        if (!RAB_IsSurfaceValid(neighborSurface))
            continue;

        if (!RTXDI_IsValidNeighbor(RAB_GetSurfaceNormal(centerSurface), RAB_GetSurfaceNormal(neighborSurface), 
            RAB_GetSurfaceLinearDepth(centerSurface), RAB_GetSurfaceLinearDepth(neighborSurface), 
            sparams.normalThreshold, sparams.depthThreshold))
            continue;

        if (!RAB_AreMaterialsSimilar(centerSurface, neighborSurface))
            continue;

        uint2 neighborReservoirPos = RTXDI_PixelPosToReservoir(idx, params);

        RTXDI_Reservoir neighborSample = RTXDI_LoadReservoir(params, LightReservoirs,
            neighborReservoirPos, sparams.sourceBufferIndex);
        neighborSample.spatialDistance += spatialOffset;

        cachedResult |= (1u << uint(i));

        RAB_LightInfo candidateLight;

        // Load that neighbor's RIS state, do resampling
        float neighborWeight = 0;
        RAB_LightSample candidateLightSample = (RAB_LightSample)0;
        if (RTXDI_IsValidReservoir(neighborSample))
        {   
            candidateLight = RAB_LoadLightInfo(RTXDI_GetReservoirLightIndex(neighborSample), false);
            
            candidateLightSample = RAB_SamplePolymorphicLight(
                candidateLight, centerSurface, RTXDI_GetReservoirSampleUV(neighborSample));
            
            neighborWeight = RAB_GetLightSampleTargetPdfForSurface(candidateLightSample, centerSurface);
        }
        
        if (RTXDI_CombineReservoirs(state, neighborSample, RAB_GetNextRandom(rng), neighborWeight))
        {
            selected = i;
            selectedLight = candidateLight;
            selectedLightSample = candidateLightSample;
        }
    }

    if (RTXDI_IsValidReservoir(state))
    {
#if RTXDI_ALLOWED_BIAS_CORRECTION >= RTXDI_BIAS_CORRECTION_BASIC
        if (sparams.biasCorrectionMode >= RTXDI_BIAS_CORRECTION_BASIC)
        {
            // Compute the unbiased normalization term (instead of using 1/M)
            float pi = state.targetPdf;
            float piSum = state.targetPdf * centerSample.M;

            // To do this, we need to walk our neighbors again
            for (i = 0; i < numSpatialSamples; ++i)
            {
                // If we skipped this neighbor above, do so again.
                if ((cachedResult & (1u << uint(i))) == 0) continue;

                uint sampleIdx = (startIdx + i) & params.neighborOffsetMask;

                // Get the screen-space location of our neighbor
                int2 idx = pixelPosition + int2(float2(NeighborOffsets[sampleIdx].xy) * sparams.samplingRadius);

                if (!RTXDI_IsActiveCheckerboardPixel(idx, false, params))
                    idx.x += (idx.y & 1) ? 1 : -1;

                // Load our neighbor's G-buffer
                RAB_Surface neighborSurface = RAB_GetGBufferSurface(idx, false);
                
                // Get the PDF of the sample RIS selected in the first loop, above, *at this neighbor* 
                const RAB_LightSample selectedSampleAtNeighbor = RAB_SamplePolymorphicLight(
                    selectedLight, neighborSurface, RTXDI_GetReservoirSampleUV(state));

                float ps = RAB_GetLightSampleTargetPdfForSurface(selectedSampleAtNeighbor, neighborSurface);

#if RTXDI_ALLOWED_BIAS_CORRECTION >= RTXDI_BIAS_CORRECTION_RAY_TRACED
                if (sparams.biasCorrectionMode == RTXDI_BIAS_CORRECTION_RAY_TRACED && ps > 0)
                {
                    if (!RAB_GetConservativeVisibility(neighborSurface, selectedSampleAtNeighbor))
                    {
                        ps = 0;
                    }
                }
#endif

                uint2 neighborReservoirPos = RTXDI_PixelPosToReservoir(idx, params);

                RTXDI_Reservoir neighborSample = RTXDI_LoadReservoir(params, LightReservoirs,
                    neighborReservoirPos, sparams.sourceBufferIndex);

                // Select this sample for the (normalization) numerator if this particular neighbor pixel
                //     was the one we selected via RIS in the first loop, above.
                pi = selected == i ? ps : pi;

                // Add to the sums of weights for the (normalization) denominator
                piSum += ps * neighborSample.M;
            }

            // Use "MIS-like" normalization
            RTXDI_FinalizeResampling(state, pi, piSum);
        }
        else
#endif
        {
            RTXDI_FinalizeResampling(state, 1.0, state.M);
        }
    }

    return state;
}


// A structure that groups the application-provided settings for spatio-temporal resampling.
struct RTXDI_SpatioTemporalResamplingParameters
{
    // Screen-space motion vector, computed as (previousPosition - currentPosition).
    // The X and Y components are measured in pixels.
    // The Z component is in linear depth units.
    float3 screenSpaceMotion;

    // The index of the reservoir buffer to pull the temporal samples from.
    uint sourceBufferIndex;

    // Maximum history length for temporal reuse, measured in frames.
    // Higher values result in more stable and high quality sampling, at the cost of slow reaction to changes.
    uint maxHistoryLength;

    // Controls the bias correction math for temporal reuse. Depending on the setting, it can add
    // some shader cost and one approximate shadow ray per pixel (or per two pixels if checkerboard sampling is enabled).
    // Ideally, these rays should be traced through the previous frame's BVH to get fully unbiased results.
    uint biasCorrectionMode;

    // Surface depth similarity threshold for temporal reuse.
    // If the previous frame surface's depth is within this threshold from the current frame surface's depth,
    // the surfaces are considered similar. The threshold is relative, i.e. 0.1 means 10% of the current depth.
    // Otherwise, the pixel is not reused, and the resampling shader will look for a different one.
    float depthThreshold;

    // Surface normal similarity threshold for temporal reuse.
    // If the dot product of two surfaces' normals is higher than this threshold, the surfaces are considered similar.
    // Otherwise, the pixel is not reused, and the resampling shader will look for a different one.
    float normalThreshold;

    // Number of neighbor pixels considered for resampling (1-32)
    // Some of the may be skipped if they fail the surface similarity test.
    uint numSamples;

    // Number of neighbor pixels considered when there is no temporal surface (1-32)
    // Setting this parameter equal or lower than `numSpatialSamples` effectively
    // disables the disocclusion boost.
    uint numDisocclusionBoostSamples;

    // Screen-space radius for spatial resampling, measured in pixels.
    float samplingRadius;

    // Allows the temporal resampling logic to skip the bias correction ray trace for light samples
    // reused from the previous frame. Only safe to use when invisible light samples are discarded
    // on the previous frame, then any sample coming from the previous frame can be assumed visible.
    bool enableVisibilityShortcut;

    // Enables permuting the pixels sampled from the previous frame in order to add temporal
    // variation to the output signal and make it more denoiser friendly.
    bool enablePermutationSampling;
};


// Spatio-temporal resampling pass.
// A combination of the temporal and spatial passes that operates only on the previous frame reservoirs.
// The selectedLightSample parameter is used to update and return the selected sample; it's optional,
// and it's safe to pass a null structure there and ignore the result.
RTXDI_Reservoir RTXDI_SpatioTemporalResampling(
    uint2 pixelPosition,
    RAB_Surface surface,
    RTXDI_Reservoir curSample,
    RAB_RandomSamplerState rng,
    RTXDI_SpatioTemporalResamplingParameters stparams,
    RTXDI_ResamplingRuntimeParameters params,
    RWStructuredBuffer<RTXDI_PackedReservoir> LightReservoirs,
    Buffer<float2> NeighborOffsets,
    out int2 temporalSamplePixelPos,
    inout RAB_LightSample selectedLightSample)
{
    int historyLimit = min(RTXDI_Reservoir::c_MaxM, stparams.maxHistoryLength * curSample.M);

    int selectedLightPrevID = -1;

    if (RTXDI_IsValidReservoir(curSample))
    {
        selectedLightPrevID = RAB_TranslateLightIndex(RTXDI_GetReservoirLightIndex(curSample), true);
    }

    temporalSamplePixelPos = -1;

    RTXDI_Reservoir state = RTXDI_EmptyReservoir();
    RTXDI_CombineReservoirs(state, curSample, /* random = */ 0.5, curSample.targetPdf);

    uint startIdx = RAB_GetNextRandom(rng) * params.neighborOffsetMask;

    // Backproject this pixel to last frame
    float3 motion = stparams.screenSpaceMotion;

    if (!stparams.enablePermutationSampling)
    {
        motion.xy += float2(RAB_GetNextRandom(rng), RAB_GetNextRandom(rng)) - 0.5;
    }

    float2 reprojectedSamplePosition = float2(pixelPosition) + motion.xy;
    int2 prevPos = int2(round(reprojectedSamplePosition));

    float expectedPrevLinearDepth = RAB_GetSurfaceLinearDepth(surface) + motion.z;

    int i;

    RAB_Surface temporalSurface = (RAB_Surface)0;
    bool foundTemporalSurface = false;
    const float temporalSearchRadius = (params.activeCheckerboardField == 0) ? 4 : 8;
    int2 temporalSpatialOffset = 0;

    // Try to find a matching surface in the neighborhood of the reprojected pixel
    for (i = 0; i < 9; i++)
    {
        int2 offset = 0;
        if(i > 0)
        {
            offset.x = int((RAB_GetNextRandom(rng) - 0.5) * temporalSearchRadius);
            offset.y = int((RAB_GetNextRandom(rng) - 0.5) * temporalSearchRadius);
        }

        int2 idx = prevPos + offset;

        if (stparams.enablePermutationSampling && i == 0)
        {
            RTXDI_ApplyPermutationSampling(idx, params.uniformRandomNumber);
        }

        if (!RTXDI_IsActiveCheckerboardPixel(idx, true, params))
        {
            idx.x += int(params.activeCheckerboardField) * 2 - 3;
        }

        // Grab shading / g-buffer data from last frame
        temporalSurface = RAB_GetGBufferSurface(idx, true);
        if (!RAB_IsSurfaceValid(temporalSurface))
            continue;
        
        // Test surface similarity, discard the sample if the surface is too different.
        if (!RTXDI_IsValidNeighbor(
            RAB_GetSurfaceNormal(surface), RAB_GetSurfaceNormal(temporalSurface), 
            expectedPrevLinearDepth, RAB_GetSurfaceLinearDepth(temporalSurface), 
            stparams.normalThreshold, stparams.depthThreshold))
            continue;

        temporalSpatialOffset = idx - prevPos;
        foundTemporalSurface = true;
        break;
    }

    // Clamp the sample count at 32 to make sure we can keep the neighbor mask in an uint (cachedResult)
    uint numSamples = clamp(stparams.numSamples, 1, 32);

    // Apply disocclusion boost if there is no temporal surface
    if (!foundTemporalSurface)
        numSamples = clamp(stparams.numDisocclusionBoostSamples, numSamples, 32);

    // We loop through neighbors twice.  Cache the validity / edge-stopping function
    //   results for the 2nd time through.
    uint cachedResult = 0;

    // Since we're using our bias correction scheme, we need to remember which light selection we made
    int selected = -1;

    // Walk the specified number of neighbors, resampling using RIS
    for (i = 0; i < numSamples; ++i)
    {
        int2 spatialOffset, idx;

        // Get screen-space location of neighbor
        if (i == 0 && foundTemporalSurface)
        {
            spatialOffset = temporalSpatialOffset;
            idx = prevPos + spatialOffset;
        }
        else
        {
            uint sampleIdx = (startIdx + i) & params.neighborOffsetMask;
            spatialOffset = (i == 0 && foundTemporalSurface) 
                ? temporalSpatialOffset 
                : int2(float2(NeighborOffsets[sampleIdx].xy) * stparams.samplingRadius);

            idx = prevPos + spatialOffset;

            if (!RTXDI_IsActiveCheckerboardPixel(idx, true, params))
            {
                idx.x += int(params.activeCheckerboardField) * 2 - 3;
            }

            temporalSurface = RAB_GetGBufferSurface(idx, true);

            if (!RAB_IsSurfaceValid(temporalSurface))
                continue;

            if (!RTXDI_IsValidNeighbor(RAB_GetSurfaceNormal(surface), RAB_GetSurfaceNormal(temporalSurface), 
                RAB_GetSurfaceLinearDepth(surface), RAB_GetSurfaceLinearDepth(temporalSurface), 
                stparams.normalThreshold, stparams.depthThreshold))
                continue;

            if (!RAB_AreMaterialsSimilar(surface, temporalSurface))
                continue;
        }
        
        cachedResult |= (1u << uint(i));

        uint2 neighborReservoirPos = RTXDI_PixelPosToReservoir(idx, params);

        RTXDI_Reservoir prevSample = RTXDI_LoadReservoir(params, LightReservoirs,
            neighborReservoirPos, stparams.sourceBufferIndex);

        prevSample.M = min(prevSample.M, historyLimit);
        prevSample.spatialDistance += spatialOffset;
        prevSample.age += 1;

        uint originalPrevLightID = RTXDI_GetReservoirLightIndex(prevSample);

        // Map the light ID from the previous frame into the current frame, if it still exists
        if (RTXDI_IsValidReservoir(prevSample))
        {   
            if (i == 0 && foundTemporalSurface && prevSample.age <= 1)
            {
                temporalSamplePixelPos = idx;
            }

            int mappedLightID = RAB_TranslateLightIndex(RTXDI_GetReservoirLightIndex(prevSample), false);

            if (mappedLightID < 0)
            {
                // Kill the reservoir
                prevSample.weightSum = 0;
                prevSample.lightData = 0;
            }
            else
            {
                // Sample is valid - modify the light ID stored
                prevSample.lightData = mappedLightID | RTXDI_Reservoir::c_LightValidBit;
            }
        }

        RAB_LightInfo candidateLight;

        // Load that neighbor's RIS state, do resampling
        float neighborWeight = 0;
        RAB_LightSample candidateLightSample = (RAB_LightSample)0;
        if (RTXDI_IsValidReservoir(prevSample))
        {   
            candidateLight = RAB_LoadLightInfo(RTXDI_GetReservoirLightIndex(prevSample), false);
            
            candidateLightSample = RAB_SamplePolymorphicLight(
                candidateLight, surface, RTXDI_GetReservoirSampleUV(prevSample));
            
            neighborWeight = RAB_GetLightSampleTargetPdfForSurface(candidateLightSample, surface);
        }

        if (RTXDI_CombineReservoirs(state, prevSample, RAB_GetNextRandom(rng), neighborWeight))
        {
            selected = i;
            selectedLightPrevID = int(originalPrevLightID);
            selectedLightSample = candidateLightSample;
        }
    }

    if (RTXDI_IsValidReservoir(state))
    {
#if RTXDI_ALLOWED_BIAS_CORRECTION >= RTXDI_BIAS_CORRECTION_BASIC
        if (stparams.biasCorrectionMode >= RTXDI_BIAS_CORRECTION_BASIC)
        {
            // Compute the unbiased normalization term (instead of using 1/M)
            float pi = state.targetPdf;
            float piSum = state.targetPdf * curSample.M;

            if (selectedLightPrevID >= 0)
            {
                const RAB_LightInfo selectedLightPrev = RAB_LoadLightInfo(selectedLightPrevID, true);

                // To do this, we need to walk our neighbors again
                for (i = 0; i < numSamples; ++i)
                {
                    // If we skipped this neighbor above, do so again.
                    if ((cachedResult & (1u << uint(i))) == 0) continue;

                    uint sampleIdx = (startIdx + i) & params.neighborOffsetMask;

                    // Get the screen-space location of our neighbor
                    int2 spatialOffset = (i == 0 && foundTemporalSurface) 
                        ? temporalSpatialOffset 
                        : int2(float2(NeighborOffsets[sampleIdx].xy) * stparams.samplingRadius);
                    int2 idx = prevPos + spatialOffset;

                    if (!RTXDI_IsActiveCheckerboardPixel(idx, true, params))
                    {
                        idx.x += params.activeCheckerboardField * 2 - 3;
                    }

                    // Load our neighbor's G-buffer
                    RAB_Surface neighborSurface = RAB_GetGBufferSurface(idx, true);
                    
                    // Get the PDF of the sample RIS selected in the first loop, above, *at this neighbor* 
                    const RAB_LightSample selectedSampleAtNeighbor = RAB_SamplePolymorphicLight(
                        selectedLightPrev, neighborSurface, RTXDI_GetReservoirSampleUV(state));

                    float ps = RAB_GetLightSampleTargetPdfForSurface(selectedSampleAtNeighbor, neighborSurface);

#if RTXDI_ALLOWED_BIAS_CORRECTION >= RTXDI_BIAS_CORRECTION_RAY_TRACED
                                                                                                              // TODO:  WHY?
                    if (stparams.biasCorrectionMode == RTXDI_BIAS_CORRECTION_RAY_TRACED && ps > 0 && (selected != i || i != 0 || !stparams.enableVisibilityShortcut))
                    {
                        RAB_Surface fallbackSurface;
                        if (i == 0 && foundTemporalSurface)
                            fallbackSurface = surface;
                        else
                            fallbackSurface = neighborSurface;

                        if (!RAB_GetTemporalConservativeVisibility(fallbackSurface, neighborSurface, selectedSampleAtNeighbor))
                        {
                            ps = 0;
                        }
                    }
#endif

                    uint2 neighborReservoirPos = RTXDI_PixelPosToReservoir(idx, params);

                    RTXDI_Reservoir prevSample = RTXDI_LoadReservoir(params, LightReservoirs,
                        neighborReservoirPos, stparams.sourceBufferIndex);
                    prevSample.M = min(prevSample.M, historyLimit);

                    // Select this sample for the (normalization) numerator if this particular neighbor pixel
                    //     was the one we selected via RIS in the first loop, above.
                    pi = selected == i ? ps : pi;

                    // Add to the sums of weights for the (normalization) denominator
                    piSum += ps * prevSample.M;
                }
            }

            // Use "MIS-like" normalization
            RTXDI_FinalizeResampling(state, pi, piSum);
        }
        else
#endif
        {
            RTXDI_FinalizeResampling(state, 1.0, state.M);
        }
    }

    return state;
}

#endif // RESAMPLING_FUNCTIONS_HLSLI
