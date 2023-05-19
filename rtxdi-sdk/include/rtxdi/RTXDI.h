/***************************************************************************
 # Copyright (c) 2020-2021, NVIDIA CORPORATION.  All rights reserved.
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

#include "RtxdiParameters.h"
#include "ReGIRParameters.h"
#include "ReGIR.h"

namespace rtxdi
{
    // Checkerboard sampling modes match those used in NRD, based on frameIndex:
    // Even frame(0)  Odd frame(1)   ...
    //     B W             W B
    //     W B             B W
    // BLACK and WHITE modes define cells with VALID data
    enum class CheckerboardMode : uint32_t
    {
        Off = 0,
        Black = 1,
        White = 2
    };
        
    enum class LocalLightSamplingMode : uint32_t
    {
        Uniform = RTXDI_LocalLightSamplingMode_UNIFORM,
        Power_RIS = RTXDI_LocalLightSamplingMode_POWER_RIS,
        ReGIR_RIS = RTXDI_LocalLightSamplingMode_REGIR_RIS
    };

    struct RISBufferSegmentParameters
    {
        uint32_t tileSize;
        uint32_t tileCount;
    };

    // Parameters used to initialize the RTXDIContext
    // Changing any of these requires recreating the context.
    struct RTXDIStaticParameters
    {
        RISBufferSegmentParameters localLightPowerRISBufferSegmentParams = {1024, 128};
        RISBufferSegmentParameters environmentLightRISBufferSegmentParams = {1024, 128};
        uint32_t NeighborOffsetCount = 8192;
        uint32_t RenderWidth = 0;
        uint32_t RenderHeight = 0;

        CheckerboardMode CheckerboardSamplingMode = CheckerboardMode::Off;
        
        ReGIRStaticParameters ReGIR;
    };

    // Description of what lights are stored and how in the light buffer
    // Infinite lights must be indexed immediately after local lights
    // Buffer layout: [local lights][infinite lights][environment light]
    struct LightBufferParameters
    {
        uint32_t firstLocalLight = 0;
        uint32_t numLocalLights = 0;
        uint32_t firstInfiniteLight = 0;
        uint32_t numInfiniteLights = 0;
        bool environmentLightPresent = false;
        uint32_t environmentLightIndex = RTXDI_INVALID_LIGHT_INDEX;
    };

    struct InitialSamplingSettings
    {
        LocalLightSamplingMode localLightInitialSamplingMode = LocalLightSamplingMode::ReGIR_RIS;
        uint32_t numPrimaryLocalLightUniformSamples = 8;
        uint32_t numPrimaryLocalLightPowerRISSamples = 8;
        uint32_t numPrimaryLocalLightReGIRRISSamples = 8;
        uint32_t numPrimaryBrdfSamples = 1;
        float brdfCutoff = 0;
        uint32_t numPrimaryInfiniteLightSamples = 1;
        uint32_t numPrimaryEnvironmentSamples = 1;
        bool enableInitialVisibility = true;
    };

    // Currently shared between Temporal + GI
    struct BoilingFilterSettings
    {
        bool enableBoilingFilter = true;
        float boilingFilterStrength = 0.2f;
    };

    struct TemporalResamplingSettings
    {
        float temporalNormalThreshold = 0.5f;
        float temporalDepthThreshold = 0.1f;
        uint32_t maxHistoryLength = 20;
        uint32_t temporalBiasCorrection = RTXDI_BIAS_CORRECTION_BASIC;
        float permutationSamplingThreshold = 0.9f;
        // Enables discarding the reservoirs if their lights turn out to be occluded in the final pass.
        // This mode significantly reduces the noise in the penumbra but introduces bias. That bias can be 
        // corrected by setting 'enableSpatialBiasCorrection' and 'enableTemporalBiasCorrection' to true.
        bool discardInvisibleSamples = false;
    };

    struct SpatialResamplingSettings
    {
        uint32_t numSpatialSamples = 1;
        uint32_t numDisocclusionBoostSamples = 8;
        float spatialSamplingRadius = 32.f;
        float spatialNormalThreshold = 0.5f;
        float spatialDepthThreshold = 0.1f;
        uint32_t spatialBiasCorrection = RTXDI_BIAS_CORRECTION_BASIC;
    };

    struct ShadingSettings
    {
        bool reuseFinalVisibility = true;
        uint32_t finalVisibilityMaxAge = 4;
        float finalVisibilityMaxDistance = 16.f;
    };

    // Make this constructor take static RTXDI params, update its dynamic ones
    class RTXDIContext
    {
    private:
        uint32_t m_ReservoirBlockRowPitch = 0;
        uint32_t m_ReservoirArrayPitch = 0;
        
        std::unique_ptr<ReGIRContext> m_regirContext;

        uint32_t m_frameIndex;
        LightBufferParameters m_lightBufferParams;
        RTXDIStaticParameters m_staticParams;

        InitialSamplingSettings m_initialSamplingSettings;
        TemporalResamplingSettings m_temporalResamplingSettings;
        BoilingFilterSettings m_boilingFilterSettings;
        SpatialResamplingSettings m_spatialResamplingSettings;
        ShadingSettings m_shadingSettings;

    public:
        RTXDIContext(const RTXDIStaticParameters& params);

        InitialSamplingSettings getInitialSamplingSettings() const;
        TemporalResamplingSettings getTemporalResamplingSettings() const;
        BoilingFilterSettings getBoilingFilterSettings() const;
        SpatialResamplingSettings getSpatialResamplingSettings() const;
        ShadingSettings getShadingSettings() const;

        uint32_t getFrameIndex() const;
        const LightBufferParameters& getLightBufferParameters() const;
        const RTXDIStaticParameters& getStaticParameters() const;
        uint32_t GetRisBufferElementCount() const;
        ReGIRContext& getReGIRContext();
        uint32_t GetReservoirBufferElementCount() const;

        void FillNeighborOffsetBuffer(uint8_t* buffer) const;
        void FillRuntimeParameters(RTXDI_RuntimeParameters& runtimeParams) const;

        bool isLocalLightPowerRISEnabled() const;

        void setFrameIndex(uint32_t frameIndex);
        void setLightBufferParameters(const LightBufferParameters& lightBufferParams);

        void setInitialSamplingSettings(const InitialSamplingSettings& initialSamplingSettings);
        void setTemporalResamplingSettings(const TemporalResamplingSettings& temporalResamplingSettings);
        void setBoilingFilterSettings(const BoilingFilterSettings& boilingFilterSettings);
        void setSpatialResamplingSettings(const SpatialResamplingSettings& spatialResamplingSettings);
        void setShadingSettings(const ShadingSettings& shadingSettings);
    };

    void ComputePdfTextureSize(uint32_t maxItems, uint32_t& outWidth, uint32_t& outHeight, uint32_t& outMipLevels);
}
