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
#include <memory>
#include <vector>

#include "ReSTIRDIParameters.h"
#include "RTXDIUtils.h"

namespace rtxdi
{
    static constexpr uint32_t c_NumReSTIRDIReservoirBuffers = 3;

    enum class ReSTIRDI_ResamplingMode : uint32_t
    {
        None,
        Temporal,
        Spatial,
        TemporalAndSpatial,
        FusedSpatiotemporal
    };

    struct RISBufferSegmentParameters
    {
        uint32_t tileSize;
        uint32_t tileCount;
    };

    // Parameters used to initialize the ReSTIRDIContext
    // Changing any of these requires recreating the context.
    struct ReSTIRDIStaticParameters
    {
        uint32_t NeighborOffsetCount = 8192;
        uint32_t RenderWidth = 0;
        uint32_t RenderHeight = 0;

        CheckerboardMode CheckerboardSamplingMode = CheckerboardMode::Off;
    };

    constexpr ReSTIRDI_BufferIndices getDefaultReSTIRDIBufferIndices()
    {
        ReSTIRDI_BufferIndices bufferIndices = {};
        bufferIndices.initialSamplingOutputBufferIndex = 0;
        bufferIndices.temporalResamplingInputBufferIndex = 0;
        bufferIndices.temporalResamplingOutputBufferIndex = 0;
        bufferIndices.spatialResamplingInputBufferIndex = 0;
        bufferIndices.spatialResamplingOutputBufferIndex = 0;
        bufferIndices.shadingInputBufferIndex = 0;
        return bufferIndices;
    }

    constexpr ReSTIRDI_InitialSamplingParameters getDefaultReSTIRDIInitialSamplingParams()
    {
        ReSTIRDI_InitialSamplingParameters params = {};
        params.brdfCutoff = 0.0001f;
        params.enableInitialVisibility = true;
        params.environmentMapImportanceSampling = 1;
        params.localLightSamplingMode = ReSTIRDI_LocalLightSamplingMode::Uniform;
        params.numPrimaryBrdfSamples = 1;
        params.numPrimaryEnvironmentSamples = 1;
        params.numPrimaryInfiniteLightSamples = 1;
        params.numPrimaryLocalLightSamples = 8;
        return params;
    }

    constexpr ReSTIRDI_TemporalResamplingParameters getDefaultReSTIRDITemporalResamplingParams()
    {
        ReSTIRDI_TemporalResamplingParameters params = {};
        params.boilingFilterStrength = 0.2f;
        params.discardInvisibleSamples = false;
        params.enableBoilingFilter = true;
        params.enablePermutationSampling = true;
        params.maxHistoryLength = 20;
        params.permutationSamplingThreshold = 0.9f;
        params.temporalBiasCorrection = ReSTIRDI_TemporalBiasCorrectionMode::Basic;
        params.temporalDepthThreshold = 0.1f;
        params.temporalNormalThreshold = 0.5f;
        return params;
    }

    constexpr ReSTIRDI_SpatialResamplingParameters getDefaultReSTIRDISpatialResamplingParams()
    {
        ReSTIRDI_SpatialResamplingParameters params = {};
        params.numDisocclusionBoostSamples = 8;
        params.numSpatialSamples = 1;
        params.spatialBiasCorrection = ReSTIRDI_SpatialBiasCorrectionMode::Basic;
        params.spatialDepthThreshold = 0.1f;
        params.spatialNormalThreshold = 0.5f;
        params.spatialSamplingRadius = 32.0f;
        return params;
    }

    constexpr ReSTIRDI_ShadingParameters getDefaultReSTIRDIShadingParams()
    {
        ReSTIRDI_ShadingParameters params = {};
        params.enableDenoiserInputPacking = false;
        params.enableFinalVisibility = true;
        params.finalVisibilityMaxAge = 4;
        params.finalVisibilityMaxDistance = 16.f;
        params.reuseFinalVisibility = true;
        return params;
    }

    // Make this constructor take static RTXDI params, update its dynamic ones
    class ReSTIRDIContext
    {
    public:
        ReSTIRDIContext(const ReSTIRDIStaticParameters& params);

        RTXDI_ReservoirBufferParameters getReservoirBufferParameters() const;
        ReSTIRDI_ResamplingMode getResamplingMode() const;
        RTXDI_RuntimeParameters getRuntimeParams() const;
        ReSTIRDI_BufferIndices getBufferIndices() const;
        ReSTIRDI_InitialSamplingParameters getInitialSamplingParameters() const;
        ReSTIRDI_TemporalResamplingParameters getTemporalResamplingParameters() const;
        ReSTIRDI_SpatialResamplingParameters getSpatialResamplingParameters() const;
        ReSTIRDI_ShadingParameters getShadingParameters() const;

        uint32_t getFrameIndex() const;
        const ReSTIRDIStaticParameters& getStaticParameters() const;

        void setFrameIndex(uint32_t frameIndex);
        void setResamplingMode(ReSTIRDI_ResamplingMode resamplingMode);
        void setInitialSamplingParameters(const ReSTIRDI_InitialSamplingParameters& initialSamplingParams);
        void setTemporalResamplingParameters(const ReSTIRDI_TemporalResamplingParameters& temporalResamplingParams);
        void setSpatialResamplingParameters(const ReSTIRDI_SpatialResamplingParameters& spatialResamplingParams);
        void setShadingParameters(const ReSTIRDI_ShadingParameters& shadingParams);

        static const uint32_t NumReservoirBuffers;

    private:
        uint32_t m_LastFrameOutputReservoir = 0;
        uint32_t m_CurrentFrameOutputReservoir = 0;

        uint32_t m_frameIndex;

        ReSTIRDIStaticParameters m_staticParams;

        ReSTIRDI_ResamplingMode m_resamplingMode;
        RTXDI_ReservoirBufferParameters m_reservoirBufferParams;
        RTXDI_RuntimeParameters m_runtimeParams;
        ReSTIRDI_BufferIndices m_bufferIndices;
        
        ReSTIRDI_InitialSamplingParameters m_initialSamplingParams;
        ReSTIRDI_TemporalResamplingParameters m_temporalResamplingParams;
        ReSTIRDI_SpatialResamplingParameters m_spatialResamplingParams;
        ReSTIRDI_ShadingParameters m_shadingParams;

        void updateBufferIndices();
        void updateCheckerboardField();
    };
}
