/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#ifndef RTXDI_LOCAL_LIGHT_SELECTION_CONTEXT_HLSLI
#define RTXDI_LOCAL_LIGHT_SELECTION_CONTEXT_HLSLI

#if RTXDI_ENABLE_PRESAMPLING
#include "rtxdi/RISBuffer.hlsli"
#endif
#include "rtxdi/UniformSampling.hlsli"

#define RTXDI_LocalLightContextSamplingMode uint
#define RTXDI_LocalLightContextSamplingMode_UNIFORM 0
#if RTXDI_ENABLE_PRESAMPLING
#define RTXDI_LocalLightContextSamplingMode_RIS 1
#endif

struct RTXDI_LocalLightSelectionContext
{
    RTXDI_LocalLightContextSamplingMode mode;

#if RTXDI_ENABLE_PRESAMPLING
    RTXDI_RISTileInfo risTileInfo;
#endif // RTXDI_ENABLE_PRESAMPLING
    RTXDI_LightBufferRegion lightBufferRegion;
};

RTXDI_LocalLightSelectionContext RTXDI_InitializeLocalLightSelectionContextUniform(RTXDI_LightBufferRegion lightBufferRegion)
{
    RTXDI_LocalLightSelectionContext ctx;
    ctx.mode = RTXDI_LocalLightContextSamplingMode_UNIFORM;
    ctx.lightBufferRegion = lightBufferRegion;
    return ctx;
}

#if RTXDI_ENABLE_PRESAMPLING
RTXDI_LocalLightSelectionContext RTXDI_InitializeLocalLightSelectionContextRIS(RTXDI_RISTileInfo risTileInfo)
{
    RTXDI_LocalLightSelectionContext ctx;
    ctx.mode = RTXDI_LocalLightContextSamplingMode_RIS;
    ctx.risTileInfo = risTileInfo;
    return ctx;
}

RTXDI_LocalLightSelectionContext RTXDI_InitializeLocalLightSelectionContextRIS(
    inout RAB_RandomSamplerState coherentRng,
    RTXDI_RISBufferSegmentParameters risBufferSegmentParams)
{
    RTXDI_LocalLightSelectionContext ctx;
    ctx.mode = RTXDI_LocalLightContextSamplingMode_RIS;
    ctx.risTileInfo = RTXDI_RandomlySelectRISTile(coherentRng, risBufferSegmentParams);
    return ctx;
}

void RTXDI_UnpackLocalLightFromRISLightData(
    uint2 tileData,
    uint risBufferPtr,
    out RAB_LightInfo lightInfo,
    out uint lightIndex,
    out float invSourcePdf)
{
    lightIndex = tileData.x & RTXDI_LIGHT_INDEX_MASK;
    invSourcePdf = asfloat(tileData.y);

    if ((tileData.x & RTXDI_LIGHT_COMPACT_BIT) != 0)
    {
        lightInfo = RAB_LoadCompactLightInfo(risBufferPtr);
    }
    else
    {
        lightInfo = RAB_LoadLightInfo(lightIndex, false);
    }
}

void RTXDI_RandomlySelectLocalLightFromRISTile(
    inout RAB_RandomSamplerState rng,
    const RTXDI_RISTileInfo risTileInfo,
    out RAB_LightInfo lightInfo,
    out uint lightIndex,
    out float invSourcePdf)
{
    uint2 risTileData;
    uint risBufferPtr;
    RTXDI_RandomlySelectLightDataFromRISTile(rng, risTileInfo, risTileData, risBufferPtr);
    RTXDI_UnpackLocalLightFromRISLightData(risTileData, risBufferPtr, lightInfo, lightIndex, invSourcePdf);
}
#endif

void RTXDI_SelectNextLocalLight(
    RTXDI_LocalLightSelectionContext ctx,
    inout RAB_RandomSamplerState rng,
    out RAB_LightInfo lightInfo,
    out uint lightIndex,
    out float invSourcePdf)
{
    switch (ctx.mode)
    {
#if RTXDI_ENABLE_PRESAMPLING
    case RTXDI_LocalLightContextSamplingMode_RIS:
        RTXDI_RandomlySelectLocalLightFromRISTile(rng, ctx.risTileInfo, lightInfo, lightIndex, invSourcePdf);
        break;
#endif // RTXDI_ENABLE_PRESAMPLING
    default:
    case RTXDI_LocalLightContextSamplingMode_UNIFORM:
        RTXDI_RandomlySelectLightUniformly(rng, ctx.lightBufferRegion, lightInfo, lightIndex, invSourcePdf);
        break;
    }
}

#endif // RTXDI_LOCAL_LIGHT_SELECTION_CONTEXT_HLSLI