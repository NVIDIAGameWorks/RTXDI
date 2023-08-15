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

#include <rtxdi/ReSTIRDIParameters.h>
#include <rtxdi/ReSTIRGIParameters.h>
#include "../shaders/BRDFPTParameters.h"

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
    class ReSTIRDIContext;
    struct ReGIRStaticParameters;
    class ImportanceSamplingContext;
}

class RenderTargets;
class RtxdiResources;
class Profiler;
class EnvironmentLight;
struct ResamplingConstants;
struct GBufferSettings;

namespace nrd
{
    struct HitDistanceParameters;
}

// A 32-bit bool type to directly use from the command line parser.
typedef int ibool;

BRDFPathTracing_MaterialOverrideParameters getDefaultBRDFPathTracingMaterialOverrideParams();
BRDFPathTracing_SecondarySurfaceReSTIRDIParameters getDefaultBRDFPathTracingSecondarySurfaceReSTIRDIParams();
BRDFPathTracing_Parameters getDefaultBRDFPathTracingParams();

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
    nvrhi::BindingSetHandle m_BindingSet;
    nvrhi::BindingSetHandle m_PrevBindingSet;
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
        uint32_t denoiserMode = 0;

        ibool enablePreviousTLAS = true;
        ibool enableAlphaTestedGeometry = true;
        ibool enableTransparentGeometry = true;
        ibool enableRayCounts = true;
        ibool visualizeRegirCells = false;
        
        ibool enableGradients = true;
        float gradientLogDarknessBias = -12.f;
        float gradientSensitivity = 8.f;
        float confidenceHistoryLength = 0.75f;

        BRDFPathTracing_Parameters brdfptParams = getDefaultBRDFPathTracingParams();
        
#if WITH_NRD
        const nrd::HitDistanceParameters* reblurDiffHitDistanceParams = nullptr;
        const nrd::HitDistanceParameters* reblurSpecHitDistanceParams = nullptr;
#endif
    };

    LightingPasses(
        nvrhi::IDevice* device,
        std::shared_ptr<donut::engine::ShaderFactory> shaderFactory,
        std::shared_ptr<donut::engine::CommonRenderPasses> commonPasses,
        std::shared_ptr<donut::engine::Scene> scene,
        std::shared_ptr<Profiler> profiler,
        nvrhi::IBindingLayout* bindlessLayout);

    void CreatePipelines(const rtxdi::ReGIRStaticParameters& regirStaticParams, bool useRayQuery);

    void CreateBindingSet(
        nvrhi::rt::IAccelStruct* topLevelAS,
        nvrhi::rt::IAccelStruct* prevTopLevelAS,
        const RenderTargets& renderTargets,
        const RtxdiResources& resources);

    void PrepareForLightSampling(
        nvrhi::ICommandList* commandList,
        rtxdi::ImportanceSamplingContext& context,
        const donut::engine::IView& view,
        const donut::engine::IView& previousView,
        const RenderSettings& localSettings,
        bool enableAccumulation);

    void RenderDirectLighting(
        nvrhi::ICommandList* commandList,
        rtxdi::ReSTIRDIContext& context,
        const donut::engine::IView& view,
        const RenderSettings& localSettings);

    void RenderBrdfRays(
        nvrhi::ICommandList* commandList,
        rtxdi::ImportanceSamplingContext& isContext,
        const donut::engine::IView& view,
        const donut::engine::IView& previousView,
        const RenderSettings& localSettings,
        const GBufferSettings& gbufferSettings,
        const EnvironmentLight& environmentLight,
        bool enableIndirect,
        bool enableAdditiveBlend,
        bool enableEmissiveSurfaces,
        bool enableAccumulation,
        bool enableReStirGI
    );

    void NextFrame();

    [[nodiscard]] nvrhi::IBindingLayout* GetBindingLayout() const { return m_BindingLayout; }
    [[nodiscard]] nvrhi::IBindingSet* GetCurrentBindingSet() const { return m_BindingSet; }
    [[nodiscard]] uint32_t GetOutputReservoirBufferIndex() const { return m_CurrentFrameOutputReservoir; }
    [[nodiscard]] uint32_t GetGIOutputReservoirBufferIndex() const { return m_CurrentFrameGIOutputReservoir; }

    static donut::engine::ShaderMacro GetRegirMacro(const rtxdi::ReGIRStaticParameters& regirStaticParams);

private:
    void FillResamplingConstants(
        ResamplingConstants& constants,
        const RenderSettings& lightingSettings,
        const rtxdi::ImportanceSamplingContext& isContext);

    void createPresamplingPipelines();
    void createReGIRPipeline(const rtxdi::ReGIRStaticParameters& regirStaticParams, const std::vector<donut::engine::ShaderMacro>& regirMacros);
    void createReSTIRDIPipelines(const std::vector<donut::engine::ShaderMacro>& regirMacros, bool useRayQuery);
    void createReSTIRGIPipelines(bool useRayQuery);
};
