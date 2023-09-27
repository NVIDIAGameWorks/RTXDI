/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#ifndef PRESAMPLING_FUNCTIONS_HLSLI
#define PRESAMPLING_FUNCTIONS_HLSLI

#include "rtxdi/RtxdiParameters.h"
#include "rtxdi/RtxdiMath.hlsli"
#include "rtxdi/RtxdiHelpers.hlsli"
#include "rtxdi/LocalLightSelection.hlsli"
#if RTXDI_REGIR_MODE != RTXDI_REGIR_DISABLED
#include "rtxdi/ReGIRSampling.hlsli"
#endif

#if RTXDI_ENABLE_PRESAMPLING && !defined(RTXDI_RIS_BUFFER)
#error "RTXDI_RIS_BUFFER must be defined to point to a RWBuffer<uint2> type resource"
#endif

#if !RTXDI_ENABLE_PRESAMPLING && (RTXDI_REGIR_MODE != RTXDI_REGIR_DISABLED)
#error "ReGIR requires presampling to be enabled"
#endif

void RTXDI_SamplePdfMipmap(
    inout RAB_RandomSamplerState rng,
    RTXDI_TEX2D pdfTexture, // full mip chain starting from unnormalized sampling pdf in mip 0
    uint2 pdfTextureSize,   // dimensions of pdfTexture at mip 0; must be 16k or less
    out uint2 position,
    out float pdf)
{
    int lastMipLevel = max(0, int(floor(log2(max(pdfTextureSize.x, pdfTextureSize.y)))) - 1);

    position = uint2(0, 0);
    pdf = 1.0;
    for (int mipLevel = lastMipLevel; mipLevel >= 0; mipLevel--)
    {
        position *= 2;

        float4 samples;
        samples.x = max(0, RTXDI_TEX2D_LOAD(pdfTexture, int2(position.x + 0, position.y + 0), mipLevel).x);
        samples.y = max(0, RTXDI_TEX2D_LOAD(pdfTexture, int2(position.x + 0, position.y + 1), mipLevel).x);
        samples.z = max(0, RTXDI_TEX2D_LOAD(pdfTexture, int2(position.x + 1, position.y + 0), mipLevel).x);
        samples.w = max(0, RTXDI_TEX2D_LOAD(pdfTexture, int2(position.x + 1, position.y + 1), mipLevel).x);

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
    RTXDI_TEX2D pdfTexture,
    uint2 pdfTextureSize,
    uint tileIndex,
    uint sampleInTile,
    RTXDI_LightBufferRegion localLightBufferRegion,
    RTXDI_RISBufferSegmentParameters localLightsRISBufferSegmentParams)
{
    uint2 texelPosition;
    float pdf;
    RTXDI_SamplePdfMipmap(rng, pdfTexture, pdfTextureSize, texelPosition, pdf);

    uint lightIndex = RTXDI_ZCurveToLinearIndex(texelPosition);

    uint risBufferPtr = sampleInTile + tileIndex * localLightsRISBufferSegmentParams.tileSize;

    bool compact = false;
    float invSourcePdf = 0;

    if (pdf > 0)
    {
        invSourcePdf = 1.0 / pdf;

        RAB_LightInfo lightInfo = RAB_LoadLightInfo(lightIndex + localLightBufferRegion.firstLightIndex, false);
        compact = RAB_StoreCompactLightInfo(risBufferPtr, lightInfo);
    }

    lightIndex += localLightBufferRegion.firstLightIndex;

    if (compact) {
        lightIndex |= RTXDI_LIGHT_COMPACT_BIT;
    }

    // Store the index of the light that we found and its inverse pdf.
    // Or zero and zero if we somehow found nothing.
    RTXDI_RIS_BUFFER[risBufferPtr] = uint2(lightIndex, asuint(invSourcePdf));
}

void RTXDI_PresampleEnvironmentMap(
    inout RAB_RandomSamplerState rng,
    RTXDI_TEX2D pdfTexture,
    uint2 pdfTextureSize,
    uint tileIndex,
    uint sampleInTile,
    RTXDI_RISBufferSegmentParameters risBufferSegmentParams)
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
    float invSourcePdf = (pdf > 0) ? (1.0 / pdf) : 0;

    // Store the result
    uint risBufferPtr = risBufferSegmentParams.bufferOffset + sampleInTile + tileIndex * risBufferSegmentParams.tileSize;
    RTXDI_RIS_BUFFER[risBufferPtr] = uint2(packedUv, asuint(invSourcePdf));
}

#if RTXDI_REGIR_MODE != RTXDI_REGIR_DISABLED

// ReGIR grid build pass.
// Each thread populates one light slot in a grid cell.
void RTXDI_PresampleLocalLightsForReGIR(
    inout RAB_RandomSamplerState rng,
    inout RAB_RandomSamplerState coherentRng,
    uint lightSlot,
    RTXDI_LightBufferRegion localLightBufferRegion,
    RTXDI_RISBufferSegmentParameters localLightRISBufferSegmentParams,
    ReGIR_Parameters regirParams)
{
    uint risBufferPtr = regirParams.commonParams.risBufferOffset + lightSlot;

    if (regirParams.commonParams.numRegirBuildSamples == 0)
    {
        RTXDI_RIS_BUFFER[risBufferPtr] = uint2(0, 0);
        return;
    }

    uint lightInCell = lightSlot % regirParams.commonParams.lightsPerCell;

    uint cellIndex = lightSlot / regirParams.commonParams.lightsPerCell;

    float3 cellCenter;
    float cellRadius;
    if (!RTXDI_ReGIR_CellIndexToWorldPos(regirParams, int(cellIndex), cellCenter, cellRadius))
    {
        RTXDI_RIS_BUFFER[risBufferPtr] = uint2(0, 0);
        return;
    }

    cellRadius *= (regirParams.commonParams.samplingJitter + 1.0);

    RAB_LightInfo selectedLightInfo = RAB_EmptyLightInfo();
    uint selectedLight = 0;
    float selectedTargetPdf = 0;
    float weightSum = 0;

    float invNumSamples = 1.0 / float(regirParams.commonParams.numRegirBuildSamples);

    RTXDI_LocalLightSelectionContext ctx;
    if (regirParams.commonParams.localLightPresamplingMode == REGIR_LOCAL_LIGHT_PRESAMPLING_MODE_POWER_RIS)
        ctx = RTXDI_InitializeLocalLightSelectionContextRIS(coherentRng, localLightRISBufferSegmentParams);
    else
        ctx = RTXDI_InitializeLocalLightSelectionContextUniform(localLightBufferRegion);

    for (uint i = 0; i < regirParams.commonParams.numRegirBuildSamples; i++)
    {
        uint rndLight;
        RAB_LightInfo lightInfo = RAB_EmptyLightInfo();
        float invSourcePdf;
        float rand = RAB_GetNextRandom(rng);
        bool lightLoaded = false;

        RTXDI_SelectNextLocalLight(ctx, rng, lightInfo, rndLight, invSourcePdf);
        invSourcePdf *= invNumSamples;

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

    if (compact) {
        selectedLight |= RTXDI_LIGHT_COMPACT_BIT;
    }

    RTXDI_RIS_BUFFER[risBufferPtr] = uint2(selectedLight, asuint(weight));
}

#endif // (RTXDI_REGIR_MODE != RTXDI_REGIR_DISABLED)

#endif // PRESAMPLING_FUNCTIONS_HLSLI