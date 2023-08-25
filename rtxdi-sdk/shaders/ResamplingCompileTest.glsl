/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

// This shader file is intended for testing the RTXDI header files 
// for GLSL compatibility.

#version 460
#extension GL_GOOGLE_include_directive : enable

// Subgroup arithmetic is used in the boiling filter
#extension GL_KHR_shader_subgroup_arithmetic : enable
#extension GL_KHR_shader_subgroup_ballot : enable

#define RTXDI_GLSL
#define RTXDI_REGIR_MODE RTXDI_REGIR_ONION

#include "rtxdi/ReSTIRDIParameters.h"
#include "rtxdi/ReSTIRGIParameters.h"

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
    return RAB_Surface(0);
}

RAB_LightInfo RAB_EmptyLightInfo()
{
    return RAB_LightInfo(0);
}

RAB_LightSample RAB_EmptyLightSample()
{
    return RAB_LightSample(0);
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

int2 RAB_ClampSamplePositionIntoView(int2 pixelPosition, bool previousFrame)
{
    return pixelPosition;
}

RAB_Surface RAB_GetGBufferSurface(int2 pixelPosition, bool previousFrame)
{
    return RAB_Surface(0);
}

bool RAB_IsSurfaceValid(RAB_Surface surface)
{
    return true;
}

float3 RAB_GetSurfaceWorldPos(RAB_Surface surface)
{
    return float3(0.0);
}

float3 RAB_GetSurfaceNormal(RAB_Surface surface)
{
    return float3(0.0);
}

float RAB_GetSurfaceLinearDepth(RAB_Surface surface)
{
    return 0.0;
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

float RAB_GetLightTargetPdfForVolume(RAB_LightInfo light, float3 volumeCenter, float volumeRadius)
{
    return 1.0;
}

RAB_LightSample RAB_SamplePolymorphicLight(RAB_LightInfo lightInfo, RAB_Surface surface, float2 uv)
{
    return RAB_LightSample(0);
}

RAB_LightInfo RAB_LoadLightInfo(uint index, bool previousFrame)
{
    return RAB_LightInfo(0);
}

RAB_LightInfo RAB_LoadCompactLightInfo(uint linearIndex)
{
    return RAB_LightInfo(0);
}

bool RAB_StoreCompactLightInfo(uint linearIndex, RAB_LightInfo lightInfo)
{
    return true;
}

int RAB_TranslateLightIndex(uint lightIndex, bool currentToPrevious)
{
    return -1;
}

float RAB_EvaluateLocalLightSourcePdf(uint lightIndex)
{
    return 0.0;
}

bool RAB_IsAnalyticLightSample(RAB_LightSample lightSample)
{
    return false;
}

float RAB_LightSampleSolidAnglePdf(RAB_LightSample lightSample)
{
    return 0.0;
}

float RAB_EvaluateEnvironmentMapSamplingPdf(float3 L)
{
    return 0.0;
}

float2 RAB_GetEnvironmentMapRandXYFromDir(float3 worldDir)
{
    return float2(0.0);
}

bool RAB_TraceRayForLocalLight(float3 origin, float3 direction, float tMin, float tMax,
    out uint o_lightIndex, out float2 o_randXY)
{
    return false;
}

bool RAB_AreMaterialsSimilar(RAB_Surface a, RAB_Surface b)
{
    return true;
}

bool RAB_ValidateGISampleWithJacobian(inout float jacobian)
{
    return true;
}

float RAB_GetGISampleTargetPdfForSurface(float3 samplePosition, float3 sampleRadiance, RAB_Surface surface)
{
    return 1.0;
}

bool RAB_GetConservativeVisibility(RAB_Surface surface, float3 samplePosition)
{
    return true;
}

bool RAB_GetTemporalConservativeVisibility(RAB_Surface currentSurface, RAB_Surface previousSurface, float3 samplePosition)
{
    return true;
}

#define RTXDI_ENABLE_BOILING_FILTER
#define RTXDI_BOILING_FILTER_GROUP_SIZE 16

layout(set = 0, binding = 0) buffer RIS_BUFFER {
    uvec2 u_RisBuffer[];
};

layout(set = 0, binding = 1) buffer LIGHT_RESERVOIR_BUFFER {
    RTXDI_PackedDIReservoir u_LightReservoirs[];
};

layout(set = 0, binding = 2) readonly buffer NEIGHBOR_OFFSET_BUFFER {
    vec2 t_NeighborOffsets[];
};

layout(set = 0, binding = 3) buffer GI_RESERVOIR_BUFFER {
    RTXDI_PackedGIReservoir u_GIReservoirs[];
};

#define RTXDI_RIS_BUFFER u_RisBuffer
#define RTXDI_LIGHT_RESERVOIR_BUFFER u_LightReservoirs
#define RTXDI_NEIGHBOR_OFFSETS_BUFFER t_NeighborOffsets
#define RTXDI_GI_RESERVOIR_BUFFER u_GIReservoirs

#include "rtxdi/PresamplingFunctions.hlsli"
#include "rtxdi/InitialSamplingFunctions.hlsli"
#include "rtxdi/DIResamplingFunctions.hlsli"

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

void main()
{
}
