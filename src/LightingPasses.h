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

#include "RayTracingPass.h"
#include "ProfilerSections.h"

#include <donut/core/math/math.h>
#include <nvrhi/nvrhi.h>
#include <memory>

#include <rtxdi/RtxdiParameters.h>

namespace donut::engine
{
    class BindlessScene;
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

namespace nrd
{
    struct HitDistanceParameters;
}

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
    RayTracingPass m_FusedResamplingPass;
    nvrhi::BindingLayoutHandle m_BindingLayout;
    nvrhi::BindingLayoutHandle m_BindlessLayout;
    nvrhi::BindingSetHandle m_BindingSet;
    nvrhi::BindingSetHandle m_PrevBindingSet;
    nvrhi::BufferHandle m_ConstantBuffer;
    dm::uint2 m_EnvironmentPdfTextureSize;
    dm::uint2 m_LocalLightPdfTextureSize;

    uint32_t m_LastFrameOutputReservoir = 0;
    uint32_t m_CurrentFrameOutputReservoir = 0;

    std::shared_ptr<donut::engine::ShaderFactory> m_ShaderFactory;
    std::shared_ptr<donut::engine::CommonRenderPasses> m_CommonPasses;
    std::shared_ptr<donut::engine::BindlessScene> m_BindlessScene;
    std::shared_ptr<Profiler> m_Profiler;

    void CreateComputePass(ComputePass& pass, const char* shaderName, const std::vector<donut::engine::ShaderMacro>& macros);
    void ExecuteComputePass(nvrhi::ICommandList* commandList, ComputePass& pass, const char* passName, dm::int2 dispatchSize, ProfilerSection::Enum profilerSection);
    void ExecuteRayTracingPass(nvrhi::ICommandList* commandList, RayTracingPass& pass, bool enableRayCounts, const char* passName, dm::int2 dispatchSize, ProfilerSection::Enum profilerSection);

public:
    struct RenderSettings
    {
        uint32_t denoiserMode = 0;
        bool enableDenoiserInputPacking = false;

        bool enablePreviousTLAS = true;
        bool enableAlphaTestedGeometry = true;
        bool enableTransparentGeometry = true;
        bool enableInitialVisibility = true;
        bool enableFinalVisibility = true;
        bool enableRayCounts = true;
        bool visualizeRegirCells = false;

        uint32_t numPrimaryRegirSamples = 8;
        uint32_t numPrimaryLocalLightSamples = 8;
        uint32_t numPrimaryInfiniteLightSamples = 1;
        uint32_t numPrimaryEnvironmentSamples = 2;
        uint32_t numIndirectRegirSamples = 8;
        uint32_t numIndirectLocalLightSamples = 8;
        uint32_t numIndirectInfiniteLightSamples = 1;
        uint32_t numIndirectEnvironmentSamples = 2;

        bool enableTemporalResampling = true;
        float temporalNormalThreshold = 0.5f;
        float temporalDepthThreshold = 0.1f;
        uint32_t maxHistoryLength = 20;
        uint32_t temporalBiasCorrection = RTXDI_BIAS_CORRECTION_BASIC;

        bool enableBoilingFilter = true;
        float boilingFilterStrength = 0.2f;

        bool enableSpatialResampling = true;
        uint32_t numSpatialSamples = 1;
        uint32_t numDisocclusionBoostSamples = 8;
        float spatialSamplingRadius = 32.f;
        float spatialNormalThreshold = 0.5f;
        float spatialDepthThreshold = 0.1f;
        uint32_t spatialBiasCorrection = RTXDI_BIAS_CORRECTION_BASIC;

        bool reuseFinalVisibility = true;
        uint32_t finalVisibilityMaxAge = 4;
        float finalVisibilityMaxDistance = 16.f;

        // Enables discarding the reservoirs if their lights turn out to be occluded in the final pass.
        // This mode significantly reduces the noise in the penumbra but introduces bias. That bias can be 
        // corrected by setting 'enableSpatialBiasCorrection' and 'enableTemporalBiasCorrection' to true.
        bool discardInvisibleSamples = false;

        bool enableReGIR = true;
        uint32_t numRegirBuildSamples = 8;

        bool useFusedKernel = false;
        
#if WITH_NRD
        const nrd::HitDistanceParameters* reblurDiffHitDistanceParams = nullptr;
        const nrd::HitDistanceParameters* reblurSpecHitDistanceParams = nullptr;
#endif
    };

    LightingPasses(
        nvrhi::IDevice* device,
        std::shared_ptr<donut::engine::ShaderFactory> shaderFactory,
        std::shared_ptr<donut::engine::CommonRenderPasses> commonPasses,
        std::shared_ptr<donut::engine::BindlessScene> bindlessScene,
        std::shared_ptr<Profiler> profiler,
        nvrhi::IBindingLayout* bindlessLayout);

    void CreatePipelines(const rtxdi::ContextParameters& contextParameters, bool useRayQuery);

    void CreateBindingSet(
        nvrhi::rt::IAccelStruct* topLevelAS,
        nvrhi::rt::IAccelStruct* prevTopLevelAS,
        const RenderTargets& renderTargets,
        const RtxdiResources& resources);

    void Render(
        nvrhi::ICommandList* commandList,
        rtxdi::Context& context,
        const donut::engine::IView& view,
        const donut::engine::IView& previousView,
        const RenderSettings& localSettings,
        const rtxdi::FrameParameters& frameParameters,
        bool enableSpecularMis);

    void RenderBrdfRays(
        nvrhi::ICommandList* commandList,
        rtxdi::Context& context,
        const donut::engine::IView& view,
        const RenderSettings& localSettings,
        const rtxdi::FrameParameters& frameParameters,
        const EnvironmentLight& environmentLight,
        bool enableIndirect,
        bool enableAdditiveBlend,
        bool enableSpecularMis);

    void NextFrame();

private:
    void FillResamplingConstants(
        ResamplingConstants& constants,
        const RenderSettings& lightingSettings,
        const rtxdi::FrameParameters& frameParameters);
};
