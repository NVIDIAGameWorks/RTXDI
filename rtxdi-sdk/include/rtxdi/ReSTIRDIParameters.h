/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#ifndef RTXDI_RESTIRDI_PARAMETERS_H
#define RTXDI_RESTIRDI_PARAMETERS_H

#include "RtxdiTypes.h"
#include "RtxdiParameters.h"

#ifdef __cplusplus
enum class ReSTIRDI_LocalLightSamplingMode : uint32_t
{
    Uniform = ReSTIRDI_LocalLightSamplingMode_UNIFORM,
    Power_RIS = ReSTIRDI_LocalLightSamplingMode_POWER_RIS,
    ReGIR_RIS = ReSTIRDI_LocalLightSamplingMode_REGIR_RIS
};

enum class ReSTIRDI_TemporalBiasCorrectionMode : uint32_t
{
    Off = RTXDI_BIAS_CORRECTION_OFF,
    Basic = RTXDI_BIAS_CORRECTION_BASIC,
    Pairwise = RTXDI_BIAS_CORRECTION_PAIRWISE,
    Raytraced = RTXDI_BIAS_CORRECTION_RAY_TRACED
};

enum class ReSTIRDI_SpatialBiasCorrectionMode : uint32_t
{
    Off = RTXDI_BIAS_CORRECTION_OFF,
    Basic = RTXDI_BIAS_CORRECTION_BASIC,
    Pairwise = RTXDI_BIAS_CORRECTION_PAIRWISE,
    Raytraced = RTXDI_BIAS_CORRECTION_RAY_TRACED
};
#else
#define ReSTIRDI_LocalLightSamplingMode uint32_t
#define ReSTIRDI_TemporalBiasCorrectionMode uint32_t
#define ReSTIRDI_SpatialBiasCorrectionMode uint32_t
#endif

struct ReSTIRDI_BufferIndices
{
    uint32_t initialSamplingOutputBufferIndex;
    uint32_t temporalResamplingInputBufferIndex;
    uint32_t temporalResamplingOutputBufferIndex;
    uint32_t spatialResamplingInputBufferIndex;

    uint32_t spatialResamplingOutputBufferIndex;
    uint32_t shadingInputBufferIndex;
    uint32_t pad1;
    uint32_t pad2;
};

struct ReSTIRDI_InitialSamplingParameters
{
    uint32_t numPrimaryLocalLightSamples;
    uint32_t numPrimaryInfiniteLightSamples;
    uint32_t numPrimaryEnvironmentSamples;
    uint32_t numPrimaryBrdfSamples;

    float brdfCutoff;
    uint32_t enableInitialVisibility;
    uint32_t environmentMapImportanceSampling; // Only used in InitialSamplingFunctions.hlsli via RAB_EvaluateEnvironmentMapSamplingPdf
    ReSTIRDI_LocalLightSamplingMode localLightSamplingMode;
};

struct ReSTIRDI_TemporalResamplingParameters
{
    float temporalDepthThreshold;
    float temporalNormalThreshold;
    uint32_t maxHistoryLength;
    ReSTIRDI_TemporalBiasCorrectionMode temporalBiasCorrection;

    uint32_t enablePermutationSampling;
    float permutationSamplingThreshold;
    uint32_t enableBoilingFilter;
    float boilingFilterStrength;

    uint32_t discardInvisibleSamples;
    uint32_t uniformRandomNumber;
    uint32_t pad2;
    uint32_t pad3;
};

struct ReSTIRDI_SpatialResamplingParameters
{
    float spatialDepthThreshold;
    float spatialNormalThreshold;
    ReSTIRDI_SpatialBiasCorrectionMode spatialBiasCorrection;
    uint32_t numSpatialSamples;

    uint32_t numDisocclusionBoostSamples;
    float spatialSamplingRadius;
    uint32_t neighborOffsetMask;
    uint32_t discountNaiveSamples;
};

struct ReSTIRDI_ShadingParameters
{
    uint32_t enableFinalVisibility;
    uint32_t reuseFinalVisibility;
    uint32_t finalVisibilityMaxAge;
    float finalVisibilityMaxDistance;

    uint32_t enableDenoiserInputPacking;
    uint32_t pad1;
    uint32_t pad2;
    uint32_t pad3;
};

struct ReSTIRDI_Parameters
{
    RTXDI_DIReservoirBufferParameters reservoirBufferParams;
    ReSTIRDI_BufferIndices bufferIndices;
    ReSTIRDI_InitialSamplingParameters initialSamplingParams;
    ReSTIRDI_TemporalResamplingParameters temporalResamplingParams;
    ReSTIRDI_SpatialResamplingParameters spatialResamplingParams;
    ReSTIRDI_ShadingParameters shadingParams;
};

#endif // RTXDI_RESTIRDI_PARAMETERS_H