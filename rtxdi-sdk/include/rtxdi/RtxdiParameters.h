/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#ifndef RTXDI_PARAMETERS_H
#define RTXDI_PARAMETERS_H

#include "RtxdiTypes.h"

// Flag that is used in the RIS buffer to identify that a light is 
// stored in a compact form.
#define RTXDI_LIGHT_COMPACT_BIT 0x80000000u

// Light index mask for the RIS buffer.
#define RTXDI_LIGHT_INDEX_MASK 0x7fffffff

// Reservoirs are stored in a structured buffer in a block-linear layout.
// This constant defines the size of that block, measured in pixels.
#define RTXDI_RESERVOIR_BLOCK_SIZE 16

// Bias correction modes for temporal and spatial resampling:
// Use (1/M) normalization, which is very biased but also very fast.
#define RTXDI_BIAS_CORRECTION_OFF 0
// Use MIS-like normalization but assume that every sample is visible.
#define RTXDI_BIAS_CORRECTION_BASIC 1
// Use pairwise MIS normalization (assuming every sample is visible).  Better perf & specular quality
#define RTXDI_BIAS_CORRECTION_PAIRWISE 2
// Use MIS-like normalization with visibility rays. Unbiased.
#define RTXDI_BIAS_CORRECTION_RAY_TRACED 3

// Select local lights with equal probability from the light buffer during initial sampling
#define RTXDI_LocalLightSamplingMode_UNIFORM 0
// Use power based RIS to select local lights during initial sampling
#define RTXDI_LocalLightSamplingMode_POWER_RIS 1
// Use ReGIR based RIS to select local lights during initial sampling.
#define RTXDI_LocalLightSamplingMode_REGIR_RIS 2

// This macro enables the functions that deal with the RIS buffer and presampling.
#ifndef RTXDI_ENABLE_PRESAMPLING
#define RTXDI_ENABLE_PRESAMPLING 1
#endif

#define RTXDI_INVALID_LIGHT_INDEX (0xffffffffu)

#ifndef __cplusplus
static const uint RTXDI_InvalidLightIndex = RTXDI_INVALID_LIGHT_INDEX;
#endif

#include "ReGIRParameters.h"

struct RTXDI_LightsBufferRegion
{
    uint32_t firstLightIndex;
    uint32_t numLights;
};

struct RTXDI_RISBufferParameters
{
    uint32_t bufferOffset;
    uint32_t tileSize;
    uint32_t tileCount;
    uint32_t pad0;
};

struct RTXDI_LocalLightRuntimeParameters
{
    RTXDI_LightsBufferRegion localLightsBufferRegion;
    uint32_t localLightSamplingMode;
    uint32_t pad1;

    // Presampling parameters
    RTXDI_RISBufferParameters risBufferParams;
};

struct RTXDI_InfiniteLightRuntimeParameters
{
    RTXDI_LightsBufferRegion infiniteLightsBufferRegion;
    uint32_t pad1;
    uint32_t pad2;
};

struct RTXDI_EnvironmentLightRuntimeParameters
{
    uint32_t environmentLightPresent;
    uint32_t environmentLightIndex;
    uint32_t pad1;
    uint32_t pad2;

    // Presampling parameters
    RTXDI_RISBufferParameters risBufferParams;
};

struct RTXDI_ResamplingRuntimeParameters
{
    uint32_t neighborOffsetMask;
    uint32_t uniformRandomNumber;
    uint32_t activeCheckerboardField; // 0 - no checkerboard, 1 - odd pixels, 2 - even pixels
    uint32_t reservoirBlockRowPitch;

    uint32_t reservoirArrayPitch;
    uint32_t pad1;
    uint32_t pad2;
    uint32_t pad3;
};

struct RTXDI_RuntimeParameters
{
    RTXDI_LocalLightRuntimeParameters localLightParams;
    RTXDI_InfiniteLightRuntimeParameters infiniteLightParams;
    RTXDI_EnvironmentLightRuntimeParameters environmentLightParams;

    RTXDI_ResamplingRuntimeParameters resamplingParams;

    RTXDI_ReGIRCommonParameters regirCommon;
    RTXDI_ReGIRGridParameters regirGrid;
    RTXDI_ReGIROnionParameters regirOnion;
};

struct RTXDI_PackedReservoir
{
    uint32_t lightData;
    uint32_t uvData;
    uint32_t mVisibility;
    uint32_t distanceAge;
    float targetPdf;
    float weight;
};

struct RTXDI_PackedGIReservoir
{
#ifdef __cplusplus
    using float3 = float[3];
#endif

    float3      position;
    uint32_t    packed_miscData_age_M; // See Reservoir.hlsli about the detail of the bit field.

    uint32_t    packed_radiance;    // Stored as 32bit LogLUV format.
    float       weight;
    uint32_t    packed_normal;      // Stored as 2x 16-bit snorms in the octahedral mapping
    float       unused;
};

#endif // RTXDI_PARAMETERS_H
