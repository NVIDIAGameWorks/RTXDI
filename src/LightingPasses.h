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

#include "RayTracingPass.h"
#include "ProfilerSections.h"

#include <donut/core/math/math.h>
#include <nvrhi/nvrhi.h>
#include <memory>

#include <rtxdi/RtxdiParameters.h>

namespace donut::engine
{
    class Scene;
    class CommonRenderPasses;
    class IView;
    class ShaderFactory;
    struct ShaderMacro;
}

namespace rtxdi
{
    struct FrameParameters;
    class Context;
    struct ResamplingSettings;
    struct ContextParameters;
}

class RenderTargets;
class RtxdiResources;
class Profiler;
class EnvironmentLight;
struct ResamplingConstants;
class RtxgiIntegration;
struct GBufferSettings;

namespace nrd
{
    struct HitDistanceParameters;
}

// A 32-bit bool type to directly use from the command line parser.
typedef int ibool;

enum class ResamplingMode : uint32_t
{
    None                = 0,
    Temporal            = 1,
    Spatial             = 2,
    TemporalAndSpatial  = 3,
    FusedSpatiotemporal = 4,
};

struct ReStirGIParameters
{
    ResamplingMode  resamplingMode = ResamplingMode::TemporalAndSpatial;
    float           depthThreshold = 0.1f;
    float           normalThreshold = 0.6f;
    uint32_t        maxReservoirAge = 30;
    uint32_t        maxHistoryLength = 8;
    float           samplingRadius = 30.f;
    uint32_t        numSpatialSamples = 2;

    ibool           enableBoilingFilter = true;
    float           boilingFilterStrength = 0.2f;
    
    uint32_t        temporalBiasCorrection = RTXDI_BIAS_CORRECTION_BASIC;
    uint32_t        spatialBiasCorrection = RTXDI_BIAS_CORRECTION_BASIC;
    ibool           enablePermutationSampling = false;
    ibool           enableFinalVisibility = true;
    ibool           enableFallbackSampling = true;
    ibool           enableFinalMIS = true;
};

class LightingPasses
{
private:
    struct ComputePass {
        nvrhi::ShaderHandle Shader;
        nvrhi::ComputePipelineHandle Pipeline;
    };

    nvrhi::DeviceHandle m_Device;

    ComputePass m_PresampleLightsPass;
    ComputePass m_PresampleEnvironmentMapPass;
    ComputePass m_PresampleReGIR;
    RayTracingPass m_GenerateInitialSamplesPass;
    RayTracingPass m_TemporalResamplingPass;
    RayTracingPass m_SpatialResamplingPass;
    RayTracingPass m_ShadeSamplesPass;
    RayTracingPass m_BrdfRayTracingPass;
    RayTracingPass m_ShadeSecondarySurfacesPass;
    RayTracingPass m_FusedResamplingPass;
    RayTracingPass m_GradientsPass;
    RayTracingPass m_GITemporalResamplingPass;
    RayTracingPass m_GISpatialResamplingPass;
    RayTracingPass m_GIFusedResamplingPass;
    RayTracingPass m_GIFinalShadingPass;
    nvrhi::BindingLayoutHandle m_BindingLayout;
    nvrhi::BindingLayoutHandle m_BindlessLayout;
    nvrhi::BindingLayoutHandle m_RtxgiBindingLayout;
    nvrhi::BindingSetHandle m_BindingSet;
    nvrhi::BindingSetHandle m_PrevBindingSet;
    nvrhi::BindingSetHandle m_RtxgiBindingSet;
    nvrhi::BufferHandle m_ConstantBuffer;
    nvrhi::BufferHandle m_LightReservoirBuffer;
    nvrhi::BufferHandle m_SecondarySurfaceBuffer;
    nvrhi::BufferHandle m_GIReservoirBuffer;

    dm::uint2 m_EnvironmentPdfTextureSize;
    dm::uint2 m_LocalLightPdfTextureSize;

    uint32_t m_LastFrameOutputReservoir = 0;
    uint32_t m_CurrentFrameOutputReservoir = 0;
    uint32_t m_CurrentFrameGIOutputReservoir = 0;

    std::shared_ptr<donut::engine::ShaderFactory> m_ShaderFactory;
    std::shared_ptr<donut::engine::CommonRenderPasses> m_CommonPasses;
    std::shared_ptr<donut::engine::Scene> m_Scene;
    std::shared_ptr<Profiler> m_Profiler;

    void CreateComputePass(ComputePass& pass, const char* shaderName, const std::vector<donut::engine::ShaderMacro>& macros);
    void ExecuteComputePass(nvrhi::ICommandList* commandList, ComputePass& pass, const char* passName, dm::int2 dispatchSize, ProfilerSection::Enum profilerSection);
    void ExecuteRayTracingPass(nvrhi::ICommandList* commandList, RayTracingPass& pass, bool enableRayCounts, const char* passName, dm::int2 dispatchSize, ProfilerSection::Enum profilerSection, nvrhi::IBindingSet* extraBindingSet = nullptr);

public:
    struct RenderSettings
    {
        ResamplingMode resamplingMode = ResamplingMode::TemporalAndSpatial;
        uint32_t denoiserMode = 0;
        bool enableDenoiserInputPacking = false;

        ibool enablePreviousTLAS = true;
        ibool enableAlphaTestedGeometry = true;
        ibool enableTransparentGeometry = true;
        ibool enableInitialVisibility = true;
        ibool enableFinalVisibility = true;
        ibool enableRayCounts = true;
        ibool enablePermutationSampling = true;
        ibool visualizeRegirCells = false;

        uint32_t numPrimaryRegirSamples = 8;
        uint32_t numPrimaryLocalLightSamples = 8;
        uint32_t numPrimaryBrdfSamples = 1;
        float brdfCutoff = 0;
        uint32_t numPrimaryInfiniteLightSamples = 1;
        uint32_t numPrimaryEnvironmentSamples = 1;
        uint32_t numIndirectRegirSamples = 2;
        uint32_t numIndirectLocalLightSamples = 2;
        uint32_t numIndirectInfiniteLightSamples = 1;
        uint32_t numIndirectEnvironmentSamples = 1;
        uint32_t numRtxgiRegirSamples = 8;
        uint32_t numRtxgiLocalLightSamples = 8;
        uint32_t numRtxgiInfiniteLightSamples = 1;
        uint32_t numRtxgiEnvironmentSamples = 1;
        
        float temporalNormalThreshold = 0.5f;
        float temporalDepthThreshold = 0.1f;
        uint32_t maxHistoryLength = 20;
        uint32_t temporalBiasCorrection = RTXDI_BIAS_CORRECTION_BASIC;
        float permutationSamplingThreshold = 0.9f;

        ibool enableBoilingFilter = true;
        float boilingFilterStrength = 0.2f;
        
        uint32_t numSpatialSamples = 1;
        uint32_t numDisocclusionBoostSamples = 8;
        float spatialSamplingRadius = 32.f;
        float spatialNormalThreshold = 0.5f;
        float spatialDepthThreshold = 0.1f;
        uint32_t spatialBiasCorrection = RTXDI_BIAS_CORRECTION_BASIC;

        ibool reuseFinalVisibility = true;
        uint32_t finalVisibilityMaxAge = 4;
        float finalVisibilityMaxDistance = 16.f;

        ibool enableSecondaryResampling = true;
        uint32_t numSecondarySamples = 1;
        float secondarySamplingRadius = 4.f;
        float secondaryNormalThreshold = 0.9f;
        float secondaryDepthThreshold = 0.1f;
        uint32_t secondaryBiasCorrection = RTXDI_BIAS_CORRECTION_BASIC;

        // Roughness of secondary surfaces is clamped to suppress caustics.
        float minSecondaryRoughness = 0.5f;
        
        // Enables discarding the reservoirs if their lights turn out to be occluded in the final pass.
        // This mode significantly reduces the noise in the penumbra but introduces bias. That bias can be 
        // corrected by setting 'enableSpatialBiasCorrection' and 'enableTemporalBiasCorrection' to true.
        ibool discardInvisibleSamples = false;
        
        ibool enableReGIR = true;
        uint32_t numRegirBuildSamples = 8;
        
        ibool enableGradients = true;
        float gradientLogDarknessBias = -12.f;
        float gradientSensitivity = 8.f;
        float confidenceHistoryLength = 0.75f;
        
#if WITH_NRD
        const nrd::HitDistanceParameters* reblurDiffHitDistanceParams = nullptr;
        const nrd::HitDistanceParameters* reblurSpecHitDistanceParams = nullptr;
#endif
        
        ReStirGIParameters reStirGI;
    };

    LightingPasses(
        nvrhi::IDevice* device,
        std::shared_ptr<donut::engine::ShaderFactory> shaderFactory,
        std::shared_ptr<donut::engine::CommonRenderPasses> commonPasses,
        std::shared_ptr<donut::engine::Scene> scene,
        std::shared_ptr<Profiler> profiler,
        nvrhi::IBindingLayout* bindlessLayout);

    void CreatePipelines(const rtxdi::ContextParameters& contextParameters, bool useRayQuery);

    void CreateBindingSet(
        nvrhi::rt::IAccelStruct* topLevelAS,
        nvrhi::rt::IAccelStruct* prevTopLevelAS,
        const RenderTargets& renderTargets,
        const RtxdiResources& resources,
        const RtxgiIntegration* rtxgi);

    void PrepareForLightSampling(
        nvrhi::ICommandList* commandList,
        rtxdi::Context& context,
        const donut::engine::IView& view,
        const donut::engine::IView& previousView,
        const RenderSettings& localSettings,
        const rtxdi::FrameParameters& frameParameters,
        bool enableAccumulation);

    void RenderDirectLighting(
        nvrhi::ICommandList* commandList,
        rtxdi::Context& context,
        const donut::engine::IView& view,
        const RenderSettings& localSettings);

    void RenderBrdfRays(
        nvrhi::ICommandList* commandList,
        rtxdi::Context& context,
        const donut::engine::IView& view,
        const donut::engine::IView& previousView,
        const RenderSettings& localSettings,
        const GBufferSettings& gbufferSettings,
        const rtxdi::FrameParameters& frameParameters,
        const EnvironmentLight& environmentLight,
        bool enableIndirect,
        bool enableAdditiveBlend,
        bool enableEmissiveSurfaces,
        uint32_t numRtxgiVolumes,
        bool enableAccumulation,
        bool enableReStirGI
    );

    void NextFrame();

    [[nodiscard]] nvrhi::IBindingLayout* GetBindingLayout() const { return m_BindingLayout; }
    [[nodiscard]] nvrhi::IBindingSet* GetCurrentBindingSet() const { return m_BindingSet; }
    [[nodiscard]] uint32_t GetOutputReservoirBufferIndex() const { return m_CurrentFrameOutputReservoir; }
    [[nodiscard]] uint32_t GetGIOutputReservoirBufferIndex() const { return m_CurrentFrameGIOutputReservoir; }

    void FillConstantBufferForProbeTracing(
        nvrhi::ICommandList* commandList,
        rtxdi::Context& context,
        const RenderSettings& localSettings,
        const rtxdi::FrameParameters& frameParameters);

    static donut::engine::ShaderMacro GetRegirMacro(const rtxdi::ContextParameters& contextParameters);

private:
    void FillResamplingConstants(
        ResamplingConstants& constants,
        const RenderSettings& lightingSettings,
        const rtxdi::FrameParameters& frameParameters);
};
