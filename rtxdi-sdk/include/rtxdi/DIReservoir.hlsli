/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#ifndef RTXDI_DIRESERVOIR_HLSLI
#define RTXDI_DIRESERVOIR_HLSLI

#include "RtxdiParameters.h"
#include "RtxdiHelpers.hlsli"

#ifndef RTXDI_LIGHT_RESERVOIR_BUFFER
#error "RTXDI_LIGHT_RESERVOIR_BUFFER must be defined to point to a RWStructuredBuffer<RTXDI_PackedDIReservoir> type resource"
#endif

// Define this macro to 0 if your shader needs read-only access to the reservoirs, 
// to avoid compile errors in the RTXDI_StoreDIReservoir function
#ifndef RTXDI_ENABLE_STORE_RESERVOIR
#define RTXDI_ENABLE_STORE_RESERVOIR 1
#endif

// This structure represents a single light reservoir that stores the weights, the sample ref,
// sample count (M), and visibility for reuse. It can be serialized into RTXDI_PackedDIReservoir for storage.
struct RTXDI_DIReservoir
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

// Encoding helper constants for RTXDI_PackedDIReservoir.mVisibility
static const uint RTXDI_PackedDIReservoir_VisibilityMask = 0x3ffff;
static const uint RTXDI_PackedDIReservoir_VisibilityChannelMax = 0x3f;
static const uint RTXDI_PackedDIReservoir_VisibilityChannelShift = 6;
static const uint RTXDI_PackedDIReservoir_MShift = 18;
static const uint RTXDI_PackedDIReservoir_MaxM = 0x3fff;

// Encoding helper constants for RTXDI_PackedDIReservoir.distanceAge
static const uint RTXDI_PackedDIReservoir_DistanceChannelBits = 8;
static const uint RTXDI_PackedDIReservoir_DistanceXShift = 0;
static const uint RTXDI_PackedDIReservoir_DistanceYShift = 8;
static const uint RTXDI_PackedDIReservoir_AgeShift = 16;
static const uint RTXDI_PackedDIReservoir_MaxAge = 0xff;
static const uint RTXDI_PackedDIReservoir_DistanceMask = (1u << RTXDI_PackedDIReservoir_DistanceChannelBits) - 1;
static const  int RTXDI_PackedDIReservoir_MaxDistance = int((1u << (RTXDI_PackedDIReservoir_DistanceChannelBits - 1)) - 1);

// Light index helpers
static const uint RTXDI_DIReservoir_LightValidBit = 0x80000000;
static const uint RTXDI_DIReservoir_LightIndexMask = 0x7FFFFFFF;

RTXDI_PackedDIReservoir RTXDI_PackDIReservoir(const RTXDI_DIReservoir reservoir)
{
    int2 clampedSpatialDistance = clamp(reservoir.spatialDistance, -RTXDI_PackedDIReservoir_MaxDistance, RTXDI_PackedDIReservoir_MaxDistance);
    uint clampedAge = clamp(reservoir.age, 0, RTXDI_PackedDIReservoir_MaxAge);

    RTXDI_PackedDIReservoir data;
    data.lightData = reservoir.lightData;
    data.uvData = reservoir.uvData;

    data.mVisibility = reservoir.packedVisibility
        | (min(uint(reservoir.M), RTXDI_PackedDIReservoir_MaxM) << RTXDI_PackedDIReservoir_MShift);

    data.distanceAge = 
          ((clampedSpatialDistance.x & RTXDI_PackedDIReservoir_DistanceMask) << RTXDI_PackedDIReservoir_DistanceXShift) 
        | ((clampedSpatialDistance.y & RTXDI_PackedDIReservoir_DistanceMask) << RTXDI_PackedDIReservoir_DistanceYShift) 
        | (clampedAge << RTXDI_PackedDIReservoir_AgeShift);

    data.targetPdf = reservoir.targetPdf;
    data.weight = reservoir.weightSum;

    return data;
}

#if RTXDI_ENABLE_STORE_RESERVOIR
void RTXDI_StoreDIReservoir(
    const RTXDI_DIReservoir reservoir,
    RTXDI_ReservoirBufferParameters reservoirParams,
    uint2 reservoirPosition,
    uint reservoirArrayIndex)
{
    uint pointer = RTXDI_DIReservoirPositionToPointer(reservoirParams, reservoirPosition, reservoirArrayIndex);
    RTXDI_LIGHT_RESERVOIR_BUFFER[pointer] = RTXDI_PackDIReservoir(reservoir);
}
#endif // RTXDI_ENABLE_STORE_RESERVOIR

RTXDI_DIReservoir RTXDI_EmptyDIReservoir()
{
    RTXDI_DIReservoir s;
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

RTXDI_DIReservoir RTXDI_UnpackDIReservoir(RTXDI_PackedDIReservoir data)
{
    RTXDI_DIReservoir res;
    res.lightData = data.lightData;
    res.uvData = data.uvData;
    res.targetPdf = data.targetPdf;
    res.weightSum = data.weight;
    res.M = (data.mVisibility >> RTXDI_PackedDIReservoir_MShift) & RTXDI_PackedDIReservoir_MaxM;
    res.packedVisibility = data.mVisibility & RTXDI_PackedDIReservoir_VisibilityMask;
    // Sign extend the shift values
    res.spatialDistance.x = int(data.distanceAge << (32 - RTXDI_PackedDIReservoir_DistanceXShift - RTXDI_PackedDIReservoir_DistanceChannelBits)) >> (32 - RTXDI_PackedDIReservoir_DistanceChannelBits);
    res.spatialDistance.y = int(data.distanceAge << (32 - RTXDI_PackedDIReservoir_DistanceYShift - RTXDI_PackedDIReservoir_DistanceChannelBits)) >> (32 - RTXDI_PackedDIReservoir_DistanceChannelBits);
    res.age = (data.distanceAge >> RTXDI_PackedDIReservoir_AgeShift) & RTXDI_PackedDIReservoir_MaxAge;
    res.canonicalWeight = 0.0f;

    // Discard reservoirs that have Inf/NaN
    if (isinf(res.weightSum) || isnan(res.weightSum)) {
        res = RTXDI_EmptyDIReservoir();
    }

    return res;
}

RTXDI_DIReservoir RTXDI_LoadDIReservoir(
    RTXDI_ReservoirBufferParameters reservoirParams,
    uint2 reservoirPosition,
    uint reservoirArrayIndex)
{
    uint pointer = RTXDI_DIReservoirPositionToPointer(reservoirParams, reservoirPosition, reservoirArrayIndex);
    return RTXDI_UnpackDIReservoir(RTXDI_LIGHT_RESERVOIR_BUFFER[pointer]);
}

void RTXDI_StoreVisibilityInDIReservoir(
    inout RTXDI_DIReservoir reservoir,
    float3 visibility,
    bool discardIfInvisible)
{
    reservoir.packedVisibility = uint(saturate(visibility.x) * RTXDI_PackedDIReservoir_VisibilityChannelMax) 
        | (uint(saturate(visibility.y) * RTXDI_PackedDIReservoir_VisibilityChannelMax)) << RTXDI_PackedDIReservoir_VisibilityChannelShift
        | (uint(saturate(visibility.z) * RTXDI_PackedDIReservoir_VisibilityChannelMax)) << (RTXDI_PackedDIReservoir_VisibilityChannelShift * 2);

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

bool RTXDI_GetDIReservoirVisibility(
    const RTXDI_DIReservoir reservoir,
    const RTXDI_VisibilityReuseParameters params,
    out float3 o_visibility)
{
    if (reservoir.age > 0 &&
        reservoir.age <= params.maxAge &&
        length(float2(reservoir.spatialDistance)) < params.maxDistance)
    {
        o_visibility.x = float(reservoir.packedVisibility & RTXDI_PackedDIReservoir_VisibilityChannelMax) / RTXDI_PackedDIReservoir_VisibilityChannelMax;
        o_visibility.y = float((reservoir.packedVisibility >> RTXDI_PackedDIReservoir_VisibilityChannelShift) & RTXDI_PackedDIReservoir_VisibilityChannelMax) / RTXDI_PackedDIReservoir_VisibilityChannelMax;
        o_visibility.z = float((reservoir.packedVisibility >> (RTXDI_PackedDIReservoir_VisibilityChannelShift * 2)) & RTXDI_PackedDIReservoir_VisibilityChannelMax) / RTXDI_PackedDIReservoir_VisibilityChannelMax;

        return true;
    }

    o_visibility = float3(0, 0, 0);
    return false;
}

bool RTXDI_IsValidDIReservoir(const RTXDI_DIReservoir reservoir)
{
    return reservoir.lightData != 0;
}

uint RTXDI_GetDIReservoirLightIndex(const RTXDI_DIReservoir reservoir)
{
    return reservoir.lightData & RTXDI_DIReservoir_LightIndexMask;
}

float2 RTXDI_GetDIReservoirSampleUV(const RTXDI_DIReservoir reservoir)
{
    return float2(reservoir.uvData & 0xffff, reservoir.uvData >> 16) / float(0xffff);
}

float RTXDI_GetDIReservoirInvPdf(const RTXDI_DIReservoir reservoir)
{
    return reservoir.weightSum;
}

// Adds a new, non-reservoir light sample into the reservoir, returns true if this sample was selected.
// Algorithm (3) from the ReSTIR paper, Streaming RIS using weighted reservoir sampling.
bool RTXDI_StreamSample(
    inout RTXDI_DIReservoir reservoir,
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
        reservoir.lightData = lightIndex | RTXDI_DIReservoir_LightValidBit;
        reservoir.uvData = uint(saturate(uv.x) * 0xffff) | (uint(saturate(uv.y) * 0xffff) << 16);
        reservoir.targetPdf = targetPdf;
    }

    return selectSample;
}

// Adds `newReservoir` into `reservoir`, returns true if the new reservoir's sample was selected.
// This is a very general form, allowing input parameters to specfiy normalization and targetPdf
// rather than computing them from `newReservoir`.  Named "internal" since these parameters take
// different meanings (e.g., in RTXDI_CombineDIReservoirs() or RTXDI_StreamNeighborWithPairwiseMIS())
bool RTXDI_InternalSimpleResample(
    inout RTXDI_DIReservoir reservoir,
    const RTXDI_DIReservoir newReservoir,
    float random,
    float targetPdf RTXDI_DEFAULT(1.0f),            // Usually closely related to the sample normalization, 
    float sampleNormalization RTXDI_DEFAULT(1.0f),  //     typically off by some multiplicative factor 
    float sampleM RTXDI_DEFAULT(1.0f)               // In its most basic form, should be newReservoir.M
)
{
    // What's the current weight (times any prior-step RIS normalization factor)
    float risWeight = targetPdf * sampleNormalization;

    // Our *effective* candidate pool is the sum of our candidates plus those of our neighbors
    reservoir.M += sampleM;

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

// Adds `newReservoir` into `reservoir`, returns true if the new reservoir's sample was selected.
// Algorithm (4) from the ReSTIR paper, Combining the streams of multiple reservoirs.
// Normalization - Equation (6) - is postponed until all reservoirs are combined.
bool RTXDI_CombineDIReservoirs(
    inout RTXDI_DIReservoir reservoir,
    const RTXDI_DIReservoir newReservoir,
    float random,
    float targetPdf)
{
    return RTXDI_InternalSimpleResample(
        reservoir,
        newReservoir,
        random,
        targetPdf,
        newReservoir.weightSum * newReservoir.M,
        newReservoir.M
    );
}

// Performs normalization of the reservoir after streaming. Equation (6) from the ReSTIR paper.
void RTXDI_FinalizeResampling(
    inout RTXDI_DIReservoir reservoir,
    float normalizationNumerator,
    float normalizationDenominator)
{
    float denominator = reservoir.targetPdf * normalizationDenominator;

    reservoir.weightSum = (denominator == 0.0) ? 0.0 : (reservoir.weightSum * normalizationNumerator) / denominator;
}

#endif // RTXDI_DIRESERVOIR_HLSLI
