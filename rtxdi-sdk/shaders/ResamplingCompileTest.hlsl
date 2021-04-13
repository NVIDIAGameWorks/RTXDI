/***************************************************************************
 # Copyright (c) 2020-2021, NVIDIA CORPORATION.  All rights reserved.
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

struct RAB_RandomSamplerState
{

};

struct RAB_Surface
{

};

struct RAB_LightSample
{

};

struct RAB_LightInfo 
{

};

bool RAB_GetConservativeVisibility(RAB_Surface surface, RAB_LightSample lightSample, bool usePreviousFrameScene)
{
    return true;
}

RAB_Surface RAB_GetGBufferSurface(int2 pixelPosition, bool previousFrame)
{
    return (RAB_Surface)0;
}

bool RAB_IsSurfaceValid(RAB_Surface surface)
{
    return true;
}

float3 RAB_GetSurfaceWorldPos(RAB_Surface surface)
{
    return 0.0;
}

float3 RAB_GetSurfaceNormal(RAB_Surface surface)
{
    return 0.0;
}

float RAB_GetSurfaceLinearDepth(RAB_Surface surface)
{
    return 0.0;
}

float RAB_GetNextRandom(inout RAB_RandomSamplerState rng)
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
    return (RAB_LightSample)0;
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

bool RAB_AreMaterialsSimilar(RAB_Surface a, RAB_Surface b)
{
    return true;
}

#define RTXDI_ENABLE_BOILING_FILTER
#define RTXDI_BOILING_FILTER_GROUP_SIZE 16

#include <rtxdi/ResamplingFunctions.hlsli>

[numthreads(1, 1, 1)]
void main()
{
}
