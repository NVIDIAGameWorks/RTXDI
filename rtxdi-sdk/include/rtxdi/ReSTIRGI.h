/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#pragma once

#include <stdint.h>
#include "rtxdi/ReSTIRGIParameters.h"
#include "rtxdi/RTXDIUtils.h"

namespace rtxdi
{

static constexpr uint32_t c_NumReSTIRGIReservoirBuffers = 2;

struct ReSTIRGIStaticParameters
{
    uint32_t RenderWidth = 0;
    uint32_t RenderHeight = 0;
    CheckerboardMode CheckerboardSamplingMode = CheckerboardMode::Off;
};

enum class ReSTIRGI_ResamplingMode : uint32_t
{
    None = 0,
    Temporal = 1,
    Spatial = 2,
    TemporalAndSpatial = 3,
    FusedSpatiotemporal = 4,
};

constexpr ReSTIRGI_BufferIndices getDefaultReSTIRGIBufferIndices()
{
    ReSTIRGI_BufferIndices bufferIndices = {};
    bufferIndices.secondarySurfaceReSTIRDIOutputBufferIndex = 0;
    bufferIndices.temporalResamplingInputBufferIndex = 0;
    bufferIndices.temporalResamplingOutputBufferIndex = 0;
    bufferIndices.spatialResamplingInputBufferIndex = 0;
    bufferIndices.spatialResamplingOutputBufferIndex = 0;
    return bufferIndices;
}

constexpr ReSTIRGI_TemporalResamplingParameters getDefaultReSTIRGITemporalResamplingParams()
{
    ReSTIRGI_TemporalResamplingParameters params = {};
    params.boilingFilterStrength = 0.2f;
    params.depthThreshold = 0.1f;
    params.enableBoilingFilter = true;
    params.enableFallbackSampling = true;
    params.enablePermutationSampling = false;
    params.maxHistoryLength = 8;
    params.maxReservoirAge = 30;
    params.normalThreshold = 0.6f;
    params.temporalBiasCorrectionMode = ResTIRGI_TemporalBiasCorrectionMode::Basic;
    return params;
}

constexpr ReSTIRGI_SpatialResamplingParameters getDefaultReSTIRGISpatialResamplingParams()
{
    ReSTIRGI_SpatialResamplingParameters params = {};
    params.numSpatialSamples = 2;
    params.spatialBiasCorrectionMode = ResTIRGI_SpatialBiasCorrectionMode::Basic;
    params.spatialDepthThreshold = 0.1f;
    params.spatialNormalThreshold = 0.6f;
    params.spatialSamplingRadius = 32.0f;
    return params;
}

constexpr ReSTIRGI_FinalShadingParameters getDefaultReSTIRGIFinalShadingParams()
{
    ReSTIRGI_FinalShadingParameters params = {};
    params.enableFinalMIS = true;
    params.enableFinalVisibility = true;
    return params;
}

class ReSTIRGIContext
{
public:
    ReSTIRGIContext(const ReSTIRGIStaticParameters& params);

    ReSTIRGIStaticParameters getStaticParams() const;

    uint32_t getFrameIndex() const;
    RTXDI_ReservoirBufferParameters getReservoirBufferParameters() const;
    ReSTIRGI_ResamplingMode getResamplingMode() const;
    ReSTIRGI_BufferIndices getBufferIndices() const;
    ReSTIRGI_TemporalResamplingParameters getTemporalResamplingParameters() const;
    ReSTIRGI_SpatialResamplingParameters getSpatialResamplingParameters() const;
    ReSTIRGI_FinalShadingParameters getFinalShadingParameters() const;

    void setFrameIndex(uint32_t frameIndex);
    void setResamplingMode(ReSTIRGI_ResamplingMode resamplingMode);
    void setTemporalResamplingParameters(const ReSTIRGI_TemporalResamplingParameters& temporalResamplingParams);
    void setSpatialResamplingParameters(const ReSTIRGI_SpatialResamplingParameters& spatialResamplingParams);
    void setFinalShadingParameters(const ReSTIRGI_FinalShadingParameters& finalShadingParams);

    static uint32_t numReservoirBuffers;

private:
    ReSTIRGIStaticParameters m_staticParams;

    uint32_t m_frameIndex;
    RTXDI_ReservoirBufferParameters m_reservoirBufferParams;
    ReSTIRGI_ResamplingMode m_resamplingMode;
    ReSTIRGI_BufferIndices m_bufferIndices;
    ReSTIRGI_TemporalResamplingParameters m_temporalResamplingParams;
    ReSTIRGI_SpatialResamplingParameters m_spatialResamplingParams;
    ReSTIRGI_FinalShadingParameters m_finalShadingParams;

    void updateBufferIndices();
};

}