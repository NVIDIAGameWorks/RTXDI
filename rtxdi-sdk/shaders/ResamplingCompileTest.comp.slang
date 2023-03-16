/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

// This shader file is intended for testing the RTXDI header files to make sure
// that they do not make any undeclared assumptions about the contents of the 
// user-defined structures and about the functions being available.

#include "RtxdiParameters.h"

struct RAB_RandomSamplerState
{
    uint unused;
};

struct RAB_Surface
{
    uint unused;
};

struct RAB_LightSample
{
    uint unused;
};

struct RAB_LightInfo
{
    uint unused;
};

RAB_Surface RAB_EmptySurface()
{
    return (RAB_Surface)0;
}

RAB_LightInfo RAB_EmptyLightInfo()
{
    return (RAB_LightInfo)0;
}

RAB_LightSample RAB_EmptyLightSample()
{
    return (RAB_LightSample)0;
}

float RAB_GetLightSampleSolidAnglePDF(RAB_LightSample lightSample)
{
  return 0.0f;
}

RAB_RandomSamplerState RAB_InitRandomSampler(uint2 index, uint frameIndex, uint pass)
{
    return (RAB_RandomSamplerState)0;
}

void RAB_GetLightDirDistance(RAB_Surface surface, RAB_LightSample lightSample,
    out float3 o_lightDir,
    out float o_lightDistance)
{
}

bool RAB_GetConservativeVisibility(RAB_Surface surface, RAB_LightSample lightSample)
{
    return true;
}

bool RAB_GetTemporalConservativeVisibility(RAB_Surface currentSurface, RAB_Surface previousSurface, RAB_LightSample lightSample)
{
    return true;
}

RAB_Surface RAB_GetGBufferSurface(int2 pixelPosition, bool previousFrame)
{
    return (RAB_Surface)0;
}

struct RAB_VolumeVisibilityContext
{
    uint unused;
};

struct RAB_VolumeInteraction
{
    uint unused;
};

RAB_Surface RAB_GetSurfaceForTemporalBiasCorrection(RAB_Surface currentSurface, int2 prevPixelID, float16_t3 prevNormal, float3 virtualMotionVector)
{   
    return currentSurface;
}

bool RAB_IsSurfaceValid(RAB_Surface surface)
{
    return true;
}

float3 RAB_GetSurfaceWorldPos(RAB_Surface surface)
{
    return 0.0;
}

float16_t3 RAB_GetSurfaceNormal(RAB_Surface surface)
{
    return 0.0;
}

float16_t3 RAB_GetTriangleNormal(RAB_Surface surface)
{
    return 0.0;
}

float16_t3 RAB_GetViewDirection(RAB_Surface surface)
{
    return 0.0;
}

float RAB_GetSurfaceLinearDepth(RAB_Surface surface)
{
    return 0.0;
}

uint RAB_GetSurfacePortalSpace(RAB_Surface surface)
{
    return 0;
}

void RAB_TransformSurface(inout RAB_Surface surface, float4x4 transformMatrix)
{
}

float4x4 RAB_GetPortalTransform(uint8_t portalIdx)
{
    return (float4x4)0;
}

bool RAB_IsPortalActive(uint8_t portalIdx)
{
    return false;
}

uint8_t RAB_GetOppositePortal(uint8_t lightPortal)
{
    return lightPortal ^ 1;
}

float RAB_GetNextRandom(inout RAB_RandomSamplerState rng)
{
    return 0.0;
}

bool RAB_GetSurfaceBrdfSample(RAB_Surface surface, inout RAB_RandomSamplerState rng, out float3 dir)
{
    return true;
}

float RAB_GetSurfaceBrdfPdf(RAB_Surface surface, float3 dir)
{
    return 0.0;
}

float RAB_GetLightSampleTargetPdfForSurface(RAB_LightSample lightSample, RAB_Surface surface)
{
    return 1.0;
}

float RAB_GetGITargetPdfForSurface(float3 radiance, float3 position, RAB_Surface surface)
{
    return 1.0;
}

float RAB_GetLightSampleTargetPdfForVolume(RAB_LightSample lightSample, RAB_VolumeInteraction volumeInteraction)
{
    return 1.0;
}


RAB_LightSample RAB_GetLightSample(uint lightID, float2 lightUV, RAB_Surface surface, bool usePreviousLights = false)
{
    return (RAB_LightSample)0;
}

RAB_LightSample RAB_GetLightSample(uint lightID, float2 lightUV, RAB_VolumeInteraction volumeInteraction, bool usePreviousLights = false)
{
    return (RAB_LightSample)0;
}

RAB_LightSample RAB_GetLightSampleWithTypeHint(uint lightID, float2 lightUV, uint lightType, RAB_Surface surface)
{
    return (RAB_LightSample)0;
}

RAB_LightSample RAB_GetLightSampleWithTypeHint(uint lightID, float2 lightUV, uint lightType, RAB_VolumeInteraction volumeInteraction)
{
    return (RAB_LightSample)0;
}

RAB_LightSample RAB_GetPortalSpaceLightSample(uint lightIdx, float2 lightUV, uint8_t portalIdx, RAB_Surface surface, bool usePreviousLights = false)
{
    return (RAB_LightSample)0;
}

RTXDI_Reservoir RAB_LoadReservoir(int2 pixel, int page)
{
    return (RTXDI_Reservoir)0;
}

RAB_LightInfo RAB_LoadLightInfo(uint index, bool previousFrame)
{
    return (RAB_LightInfo)0;
}

RAB_LightInfo RAB_LoadCompactLightInfo(uint linearIndex)
{
    return (RAB_LightInfo)0;
}

bool RAB_StoreCompactLightInfo(uint linearIndex, RAB_LightInfo lightInfo)
{
    return true;
}

int RAB_TranslateLightIndex(uint lightIndex, bool currentToPrevious)
{
    return -1;
}

bool RAB_ShouldDiscardEnlargedPixel(inout RAB_RandomSamplerState rng, RAB_Surface surface, float3 virtualMotionVector)
{
    return false;
}

bool RAB_TraceLightSampleVisibility(RAB_Surface surface, RAB_LightSample lightSample, uint expectedRayPortalIndex,
    bool lightSampleIsPreTransformed, bool includeTranslucentGeometry, bool usePreviousTLAS, out float3 attenuation)
{
    attenuation = 1.0;
    return true;
}


bool RAB_TraceGISampleVisibility(RAB_Surface surface, RAB_Surface neighborSurface, ReSTIRGI_Reservoir reservoir, int numPortals)
{
    return true;
}

bool RAB_VolumeReSTIR_TraceNEEVisibility(
    RAB_LightSample lightSample, uint8_t sampledTransportPortalIndex,
    RAB_VolumeVisibilityContext volumeVisibilityContext, RAB_VolumeInteraction volumeInteraction,
    out float16_t3 attenuation)
{
    attenuation = float16_t3(1.0);
    return true;
}

void RAB_GetPortalSamplingProbablity(RAB_Surface surface, out float16_t totalPortalPdf, out float16_t4 sampleThreshold)
{
    totalPortalPdf = 0;
    sampleThreshold = 0;
}

uint RAB_GetLightCount()
{
  return 1;
}

uint RAB_GetLightTypeCount()
{
    return 1;
}

RTXDI_LightRange RAB_GetLightRange(uint lightType)
{
    return (RTXDI_LightRange)0;
}

float RAB_GetSurfaceIlluminance(int2 pixelPos, bool previousFrame)
{
    return 0;
}

int2 RAB_GetTemporalSamplePosition(int2 pixelPos)
{
    return 0;
}

uint32_t RAB_EncodeNormal(float3 normal)
{
    return 0;
}

float3 RAB_DecodeNormal(uint encodedNormal)
{
    return 0;
}

ReSTIRGI_Reservoir RAB_LoadGIReservoir(int2 pixel, int page)
{
    return (RTXDI_Reservoir)0;
}

bool RAB_IsSurfaceInCombinedPortalSpace(RAB_Surface surface)
{
    return false;
}

#define RTXDI_ENABLE_BOILING_FILTER
#define RTXDI_BOILING_FILTER_GROUP_SIZE 16

RWBuffer<uint2> u_RisBuffer;
RWStructuredBuffer<RTXDI_PackedReservoir> u_LightReservoirs;
Buffer<float2> t_NeighborOffsets;

#define RTXDI_RIS_BUFFER u_RisBuffer
#define RTXDI_LIGHT_RESERVOIR_BUFFER u_LightReservoirs
#define RTXDI_NEIGHBOR_OFFSETS_BUFFER t_NeighborOffsets


#include "ResamplingFunctions.slangh"

[numthreads(1, 1, 1)]
[shader("compute")]
void main()
{
}
