/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#ifndef RTXDI_RESTIRGI_PARAMETERS_H
#define RTXDI_RESTIRGI_PARAMETERS_H

#include "RtxdiTypes.h"
#include "RtxdiParameters.h"

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

struct ReSTIRGI_ReservoirBufferParameters
{
    uint32_t reservoirBlockRowPitch;
    uint32_t reservoirArrayPitch;
    uint32_t pad1;
    uint32_t pad2;
};

#ifdef __cplusplus
enum class ResTIRGI_TemporalBiasCorrectionMode : uint32_t
{
    Off = RTXDI_BIAS_CORRECTION_OFF,
    Basic = RTXDI_BIAS_CORRECTION_BASIC,
    // Pairwise is not supported
    Raytraced = RTXDI_BIAS_CORRECTION_RAY_TRACED
};

enum class ResTIRGI_SpatialBiasCorrectionMode : uint32_t
{
    Off = RTXDI_BIAS_CORRECTION_OFF,
    Basic = RTXDI_BIAS_CORRECTION_BASIC,
    // Pairwise is not supported
    Raytraced = RTXDI_BIAS_CORRECTION_RAY_TRACED
};
#else
#define ResTIRGI_TemporalBiasCorrectionMode uint32_t
#define ResTIRGI_SpatialBiasCorrectionMode uint32_t
#endif

// Very similar to RTXDI_TemporalResamplingParameters but it has an extra field
// It's also not the same algo, and we don't want the two to be coupled
struct ReSTIRGI_TemporalResamplingParameters
{
    float           depthThreshold;
    float           normalThreshold;
    uint32_t        enablePermutationSampling;
    uint32_t        maxHistoryLength;

    uint32_t        maxReservoirAge;
    uint32_t        enableBoilingFilter;
    float           boilingFilterStrength;
    uint32_t        enableFallbackSampling;

    ResTIRGI_TemporalBiasCorrectionMode temporalBiasCorrectionMode;// = ResTIRGI_TemporalBiasCorrectionMode::Basic;
    uint32_t uniformRandomNumber;
    uint32_t pad2;
    uint32_t pad3;
};

// See note for ReSTIRGI_TemporalResamplingParameters
struct ReSTIRGI_SpatialResamplingParameters
{
    float       spatialDepthThreshold;
    float       spatialNormalThreshold;
    uint32_t    numSpatialSamples;
    float       spatialSamplingRadius;

    ResTIRGI_SpatialBiasCorrectionMode  spatialBiasCorrectionMode;// = ResTIRGI_SpatialBiasCorrectionMode::Basic;
    uint32_t pad1;
    uint32_t pad2;
    uint32_t pad3;
};

struct ReSTIRGI_FinalShadingParameters
{
    uint32_t enableFinalVisibility;// = true;
    uint32_t enableFinalMIS;// = true;
    uint32_t pad1;
    uint32_t pad2;
};

struct ReSTIRGI_BufferIndices
{
    uint32_t secondarySurfaceReSTIRDIOutputBufferIndex;
    uint32_t temporalResamplingInputBufferIndex;
    uint32_t temporalResamplingOutputBufferIndex;
    uint32_t spatialResamplingInputBufferIndex;

    uint32_t spatialResamplingOutputBufferIndex;
    uint32_t finalShadingInputBufferIndex;
    uint32_t pad1;
    uint32_t pad2;
};

struct ReSTIRGI_Parameters
{
    RTXDI_DIReservoirBufferParameters reservoirBufferParams;
    ReSTIRGI_BufferIndices bufferIndices;
    ReSTIRGI_TemporalResamplingParameters temporalResamplingParams;
    ReSTIRGI_SpatialResamplingParameters spatialResamplingParams;
    ReSTIRGI_FinalShadingParameters finalShadingParams;
};

#endif // RTXDI_RESTIRGI_PARAMETERS_H
