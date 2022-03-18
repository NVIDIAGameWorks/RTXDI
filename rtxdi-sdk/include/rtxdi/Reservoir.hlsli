/***************************************************************************
 # Copyright (c) 2020-2022, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#ifndef RESERVOIR_HLSLI
#define RESERVOIR_HLSLI

#include "RtxdiParameters.h"
#include "RtxdiHelpers.hlsli"

#ifndef RTXDI_LIGHT_RESERVOIR_BUFFER
#error "RTXDI_LIGHT_RESERVOIR_BUFFER must be defined to point to a RWStructuredBuffer<RTXDI_PackedReservoir> type resource"
#endif

// Define this macro to 0 if your shader needs read-only access to the reservoirs, 
// to avoid compile errors in the RTXDI_StoreReservoir function
#ifndef RTXDI_ENABLE_STORE_RESERVOIR
#define RTXDI_ENABLE_STORE_RESERVOIR 1
#endif

// This structure represents a single light reservoir that stores the weights, the sample ref,
// sample count (M), and visibility for reuse. It can be serialized into RTXDI_PackedReservoir for storage.
struct RTXDI_Reservoir
{
    // Light index (bits 0..30) and validity bit (31)
    uint lightData;     

    // Sample UV encoded in 16-bit fixed point format
    uint uvData;        

    // Overloaded: represents RIS weight sum during streaming,
    // then reservoir weight (inverse PDF) after FinalizeResampling
    float weightSum;

    // Target PDF of the selected sample
    float targetPdf;

    // Number of samples considered for this reservoir (pairwise MIS makes this a float)
    float M;

    // Visibility information stored in the reservoir for reuse
    uint packedVisibility;

    // Screen-space distance between the current location of the reservoir
    // and the location where the visibility information was generated,
    // minus the motion vectors applied in temporal resampling
    int2 spatialDistance;

    // How many frames ago the visibility information was generated
    uint age;

    // Cannonical weight when using pairwise MIS (ignored except during pairwise MIS computations)
    float canonicalWeight;
};

// Encoding helper constants for RTXDI_PackedReservoir.mVisibility
static const uint RTXDI_PackedReservoir_VisibilityMask = 0x3ffff;
static const uint RTXDI_PackedReservoir_VisibilityChannelMax = 0x3f;
static const uint RTXDI_PackedReservoir_VisibilityChannelShift = 6;
static const uint RTXDI_PackedReservoir_MShift = 18;
static const uint RTXDI_PackedReservoir_MaxM = 0x3fff;

// Encoding helper constants for RTXDI_PackedReservoir.distanceAge
static const uint RTXDI_PackedReservoir_DistanceChannelBits = 8;
static const uint RTXDI_PackedReservoir_DistanceXShift = 0;
static const uint RTXDI_PackedReservoir_DistanceYShift = 8;
static const uint RTXDI_PackedReservoir_AgeShift = 16;
static const uint RTXDI_PackedReservoir_MaxAge = 0xff;
static const uint RTXDI_PackedReservoir_DistanceMask = (1u << RTXDI_PackedReservoir_DistanceChannelBits) - 1;
static const  int RTXDI_PackedReservoir_MaxDistance = int((1u << (RTXDI_PackedReservoir_DistanceChannelBits - 1)) - 1);

// Light index helpers
static const uint RTXDI_Reservoir_LightValidBit = 0x80000000;
static const uint RTXDI_Reservoir_LightIndexMask = 0x7FFFFFFF;

RTXDI_PackedReservoir RTXDI_PackReservoir(const RTXDI_Reservoir reservoir)
{
    int2 clampedSpatialDistance = clamp(reservoir.spatialDistance, -RTXDI_PackedReservoir_MaxDistance, RTXDI_PackedReservoir_MaxDistance);
    uint clampedAge = clamp(reservoir.age, 0, RTXDI_PackedReservoir_MaxAge);

    RTXDI_PackedReservoir data;
    data.lightData = reservoir.lightData;
    data.uvData = reservoir.uvData;

    data.mVisibility = reservoir.packedVisibility
        | (min(uint(reservoir.M), RTXDI_PackedReservoir_MaxM) << RTXDI_PackedReservoir_MShift);

    data.distanceAge = 
          ((clampedSpatialDistance.x & RTXDI_PackedReservoir_DistanceMask) << RTXDI_PackedReservoir_DistanceXShift) 
        | ((clampedSpatialDistance.y & RTXDI_PackedReservoir_DistanceMask) << RTXDI_PackedReservoir_DistanceYShift) 
        | (clampedAge << RTXDI_PackedReservoir_AgeShift);

    data.targetPdf = reservoir.targetPdf;
    data.weight = reservoir.weightSum;

    return data;
}

#if RTXDI_ENABLE_STORE_RESERVOIR
void RTXDI_StoreReservoir(
    const RTXDI_Reservoir reservoir,
    RTXDI_ResamplingRuntimeParameters params,
    uint2 reservoirPosition,
    uint reservoirArrayIndex)
{
    uint pointer = RTXDI_ReservoirPositionToPointer(params, reservoirPosition, reservoirArrayIndex);
    RTXDI_LIGHT_RESERVOIR_BUFFER[pointer] = RTXDI_PackReservoir(reservoir);
}
#endif // RTXDI_ENABLE_STORE_RESERVOIR

RTXDI_Reservoir RTXDI_EmptyReservoir()
{
    RTXDI_Reservoir s;
    s.lightData = 0;
    s.uvData = 0;
    s.targetPdf = 0;
    s.weightSum = 0;
    s.M = 0;
    s.packedVisibility = 0;
    s.spatialDistance = int2(0, 0);
    s.age = 0;
    s.canonicalWeight = 0;
    return s;
}

RTXDI_Reservoir RTXDI_UnpackReservoir(RTXDI_PackedReservoir data)
{
    RTXDI_Reservoir res;
    res.lightData = data.lightData;
    res.uvData = data.uvData;
    res.targetPdf = data.targetPdf;
    res.weightSum = data.weight;
    res.M = (data.mVisibility >> RTXDI_PackedReservoir_MShift) & RTXDI_PackedReservoir_MaxM;
    res.packedVisibility = data.mVisibility & RTXDI_PackedReservoir_VisibilityMask;
    // Sign extend the shift values
    res.spatialDistance.x = int(data.distanceAge << (32 - RTXDI_PackedReservoir_DistanceXShift - RTXDI_PackedReservoir_DistanceChannelBits)) >> (32 - RTXDI_PackedReservoir_DistanceChannelBits);
    res.spatialDistance.y = int(data.distanceAge << (32 - RTXDI_PackedReservoir_DistanceYShift - RTXDI_PackedReservoir_DistanceChannelBits)) >> (32 - RTXDI_PackedReservoir_DistanceChannelBits);
    res.age = (data.distanceAge >> RTXDI_PackedReservoir_AgeShift) & RTXDI_PackedReservoir_MaxAge;
    res.canonicalWeight = 0.0f;

    // Discard reservoirs that have Inf/NaN
    if (isinf(res.weightSum) || isnan(res.weightSum)) {
        res = RTXDI_EmptyReservoir();
    }

    return res;
}

RTXDI_Reservoir RTXDI_LoadReservoir(
    RTXDI_ResamplingRuntimeParameters params,
    uint2 reservoirPosition,
    uint reservoirArrayIndex)
{
    uint pointer = RTXDI_ReservoirPositionToPointer(params, reservoirPosition, reservoirArrayIndex);
    return RTXDI_UnpackReservoir(RTXDI_LIGHT_RESERVOIR_BUFFER[pointer]);
}

void RTXDI_StoreVisibilityInReservoir(
    inout RTXDI_Reservoir reservoir,
    float3 visibility,
    bool discardIfInvisible)
{
    reservoir.packedVisibility = uint(saturate(visibility.x) * RTXDI_PackedReservoir_VisibilityChannelMax) 
        | (uint(saturate(visibility.y) * RTXDI_PackedReservoir_VisibilityChannelMax)) << RTXDI_PackedReservoir_VisibilityChannelShift
        | (uint(saturate(visibility.z) * RTXDI_PackedReservoir_VisibilityChannelMax)) << (RTXDI_PackedReservoir_VisibilityChannelShift * 2);

    reservoir.spatialDistance = int2(0, 0);
    reservoir.age = 0;

    if (discardIfInvisible && visibility.x == 0 && visibility.y == 0 && visibility.z == 0)
    {
        // Keep M for correct resampling, remove the actual sample
        reservoir.lightData = 0;
        reservoir.weightSum = 0;
    }
}

// Structure that groups the parameters for RTXDI_GetReservoirVisibility(...)
// Reusing final visibility reduces the number of high-quality shadow rays needed to shade
// the scene, at the cost of somewhat softer or laggier shadows.
struct RTXDI_VisibilityReuseParameters
{
    // Controls the maximum age of the final visibility term, measured in frames, that can be reused from the
    // previous frame(s). Higher values result in better performance.
    uint maxAge;

    // Controls the maximum distance in screen space between the current pixel and the pixel that has
    // produced the final visibility term. The distance does not include the motion vectors.
    // Higher values result in better performance and softer shadows.
    float maxDistance;
};

bool RTXDI_GetReservoirVisibility(
    const RTXDI_Reservoir reservoir,
    const RTXDI_VisibilityReuseParameters params,
    out float3 o_visibility)
{
    if (reservoir.age > 0 &&
        reservoir.age <= params.maxAge &&
        length(float2(reservoir.spatialDistance)) < params.maxDistance)
    {
        o_visibility.x = float(reservoir.packedVisibility & RTXDI_PackedReservoir_VisibilityChannelMax) / RTXDI_PackedReservoir_VisibilityChannelMax;
        o_visibility.y = float((reservoir.packedVisibility >> RTXDI_PackedReservoir_VisibilityChannelShift) & RTXDI_PackedReservoir_VisibilityChannelMax) / RTXDI_PackedReservoir_VisibilityChannelMax;
        o_visibility.z = float((reservoir.packedVisibility >> (RTXDI_PackedReservoir_VisibilityChannelShift * 2)) & RTXDI_PackedReservoir_VisibilityChannelMax) / RTXDI_PackedReservoir_VisibilityChannelMax;

        return true;
    }

    o_visibility = float3(0, 0, 0);
    return false;
}

bool RTXDI_IsValidReservoir(const RTXDI_Reservoir reservoir)
{
    return reservoir.lightData != 0;
}

uint RTXDI_GetReservoirLightIndex(const RTXDI_Reservoir reservoir)
{
    return reservoir.lightData & RTXDI_Reservoir_LightIndexMask;
}

float2 RTXDI_GetReservoirSampleUV(const RTXDI_Reservoir reservoir)
{
    return float2(reservoir.uvData & 0xffff, reservoir.uvData >> 16) / float(0xffff);
}

float RTXDI_GetReservoirInvPdf(const RTXDI_Reservoir reservoir)
{
    return reservoir.weightSum;
}

bool RTXDI_IsActiveCheckerboardPixel(
    uint2 pixelPosition,
    bool previousFrame,
    RTXDI_ResamplingRuntimeParameters params)
{
    if (params.activeCheckerboardField == 0)
        return true;

    return ((pixelPosition.x + pixelPosition.y + int(previousFrame)) & 1) == (params.activeCheckerboardField & 1);
}

uint2 RTXDI_PixelPosToReservoir(uint2 pixelPosition, RTXDI_ResamplingRuntimeParameters params)
{
    if (params.activeCheckerboardField == 0)
        return pixelPosition;

    return uint2(pixelPosition.x >> 1, pixelPosition.y);
}

uint2 RTXDI_ReservoirToPixelPos(uint2 reservoirIndex, RTXDI_ResamplingRuntimeParameters params)
{
    if (params.activeCheckerboardField == 0)
        return reservoirIndex;

    uint2 pixelPosition = uint2(reservoirIndex.x << 1, reservoirIndex.y);
    pixelPosition.x += ((pixelPosition.y + params.activeCheckerboardField) & 1);
    return pixelPosition;
}

#endif // RESERVOIR_HLSLI