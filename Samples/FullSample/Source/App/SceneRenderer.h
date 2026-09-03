/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#pragma once

#include <memory>
#include <optional>
#include <string>

#include <donut/app/ApplicationBase.h>
#include <donut/app/Camera.h>
#include <donut/engine/View.h>
#include <donut/render/DLSS.h>

#include <Rtxdi/ImportanceSamplingContext.h>

#include "UserInterface.h"

class SampleScene;

class RasterizedGBufferPass;
class RaytracedGBufferPass;

class GlassPass;
class FilterGradientsPass;
class DLSSRRInputFormattingPass;
class ConfidencePass;
class CompositingPass;
class AccumulationPass;
class PrepareLightsPass;
class RenderEnvironmentMapPass;
class GenerateMipsPass;
class GenerateMipsPass;
class VisualizationPass;
class DebugVizPasses;
class DIReservoirVizPass;
class GIReservoirVizPass;
class PTReservoirVizPass;
class PTPathVizPass;

namespace donut
{
    namespace engine
    {
        class DescriptorTableManager;
        struct IesProfile;
        class IesProfileLoader;
        class PerspectiveCamera;
    }

    namespace render
    {
        class BloomPass;
        class DLSS;
        class TemporalAntiAliasingPass;
        class ToneMappingPass;
    }
}

namespace nvrhi
{
    class ITexture;
}

#if WITH_NRD
#include "RenderPasses/DenoisingPasses/NrdIntegration.h"
#endif

#if WITH_DLSS
#include "RenderPasses/DenoisingPasses/DLSS.h"
#endif

using namespace donut;
using namespace donut::math;
using namespace std::chrono;
#include "SharedShaderInclude/ShaderParameters.h"

enum class SceneAsset
{
    Arcade,
    Bistro,
    BistroMirror,
    CornellBox,
};

struct SceneRendererStartupSettings
{
    std::optional<float3> cameraDirection;
    std::optional<float> cameraMovementSpeed;
    std::optional<float3> cameraPosition;
    std::optional<float> cameraRotateSpeed;
    uint32_t saveFrameIndex = 0;
    std::string saveFrameFileName;
    bool verbose = false;
    bool benchmark = false;
    int renderWidth = 0;
    int renderHeight = 0;
    SceneAsset sceneAsset = SceneAsset::BistroMirror;
};

class SceneRenderer : public app::ApplicationBase
{
public:
    SceneRenderer(app::DeviceManager* deviceManager, UIData& ui, const SceneRendererStartupSettings& args);

    ~SceneRenderer();

    [[nodiscard]] std::shared_ptr<engine::ShaderFactory> GetShaderFactory() const;

    [[nodiscard]] std::shared_ptr<vfs::IFileSystem> GetRootFs() const;

    bool Init();

    void AssignIesProfiles(nvrhi::ICommandList* commandList);

    virtual void SceneLoaded() override;

    void LoadShaders();

    virtual bool LoadScene(std::shared_ptr<vfs::IFileSystem> fs, const std::filesystem::path& sceneFileName) override;

    bool KeyboardUpdate(int key, int scancode, int action, int mods) override;

    virtual bool MousePosUpdate(double xpos, double ypos) override;

    virtual bool MouseButtonUpdate(int button, int action, int mods) override;

    virtual void Animate(float fElapsedTimeSeconds) override;

    virtual void BackBufferResized(const uint32_t width, const uint32_t height, const uint32_t sampleCount) override;

    void RenderWaitFrame(nvrhi::IFramebuffer* framebuffer);

    void UpdateEnvironmentMap();

    void SetupView(uint32_t renderWidth, uint32_t renderHeight, const engine::PerspectiveCamera* activeCamera);

    void SetupRenderPasses(uint32_t renderWidth, uint32_t renderHeight, bool& exposureResetRequired);

    virtual void RenderSplashScreen(nvrhi::IFramebuffer* framebuffer) override;

    void UpdateReGIRContextFromUI();

    RTXDI_DISpatioTemporalResamplingParameters SpatioTemporalParamsFromIndividualParams(const RTXDI_DITemporalResamplingParameters& tParams,
        const RTXDI_DISpatialResamplingParameters& sParams);

    RTXDI_GISpatioTemporalResamplingParameters SpatioTemporalParamsFromIndividualParams(const RTXDI_GITemporalResamplingParameters& tParams,
        const RTXDI_GISpatialResamplingParameters& sParams);

    void UpdateReSTIRDIContextFromUI();

    void UpdateReSTIRGIContextFromUI();

    void UpdateReSTIRPTContextFromUI();

    bool IsLocalLightPowerRISEnabled();

    void RenderScene(nvrhi::IFramebuffer* framebuffer) override;

private:
    std::shared_ptr<vfs::RootFileSystem> m_rootFs;
    std::shared_ptr<SampleScene> m_scene;

    // Top level rendering objects
    std::shared_ptr<engine::ShaderFactory> m_shaderFactory;
    nvrhi::CommandListHandle m_commandList;

    // Descriptor/binding resources
    nvrhi::BindingLayoutHandle m_bindlessLayout;
    std::shared_ptr<engine::DescriptorTableManager> m_descriptorTableManager;
    engine::BindingCache m_bindingCache;

    // Rendering resources
    std::shared_ptr<RenderTargets> m_renderTargets;
    std::unique_ptr<RtxdiResources> m_rtxdiResources;
    bool m_rtxdiResourcesCreated = false;

    // Camera data
    app::FirstPersonCamera m_camera;
    bool m_prevViewValid = false;
    engine::PlanarView m_view;
    engine::PlanarView m_viewPrevious;
    engine::PlanarView m_viewPreviousPrevious;
    engine::PlanarView m_upscaledView;

    // Lighting data
    std::unique_ptr<engine::IesProfileLoader> m_iesProfileLoader;
    std::vector<std::shared_ptr<engine::IesProfile>> m_iesProfiles;
    std::shared_ptr<engine::DirectionalLight> m_sunLight;
    std::shared_ptr<EnvironmentLight> m_environmentLight;
    std::shared_ptr<engine::LoadedTexture> m_environmentMap;

    // ReSTIR data
    std::unique_ptr<rtxdi::ImportanceSamplingContext> m_isContext;
    dm::float3 m_regirCenter;

    // Render passes in the order in which they execute
    std::unique_ptr<RenderEnvironmentMapPass> m_renderEnvironmentMapPass;
    std::unique_ptr<GenerateMipsPass> m_environmentMapPdfMipmapPass;
    std::unique_ptr<RasterizedGBufferPass> m_rasterizedGBufferPass;
    std::unique_ptr<RaytracedGBufferPass> m_gBufferPass;
    std::unique_ptr<PostprocessGBufferPass> m_postprocessGBufferPass;
    std::unique_ptr<PrepareLightsPass> m_prepareLightsPass;
    std::unique_ptr<GenerateMipsPass> m_localLightPdfMipmapPass;
    std::unique_ptr<LightingPasses> m_lightingPasses;
    std::unique_ptr<FilterGradientsPass> m_filterGradientsPass;
    std::unique_ptr<DLSSRRInputFormattingPass> m_DLSSRRInputFormattingPass;
    std::unique_ptr<ConfidencePass> m_confidencePass;
#if WITH_NRD
    std::unique_ptr<NrdIntegration> m_nrd;
#endif
    std::unique_ptr<CompositingPass> m_compositingPass;
    std::unique_ptr<GlassPass> m_glassPass;
    std::unique_ptr<AccumulationPass> m_accumulationPass;
    std::unique_ptr<render::TemporalAntiAliasingPass> m_temporalAntiAliasingPass;
#if DONUT_WITH_DLSS
    std::unique_ptr<donut::render::DLSS> m_dlssSR;
    std::unique_ptr<donut::render::DLSS> m_dlssRR;
#endif
    std::unique_ptr<render::BloomPass> m_bloomPass;
    std::unique_ptr<render::ToneMappingPass> m_toneMappingPass;

    // Debug passes & state
    std::unique_ptr<DebugVizPasses> m_debugVizPasses;
    std::unique_ptr<DIReservoirVizPass> m_diReservoirVizPass;
    std::unique_ptr<GIReservoirVizPass> m_giReservoirVizPass;
    std::unique_ptr<PTReservoirVizPass> m_ptReservoirVizPass;
    std::unique_ptr<PTPathVizPass> m_ptPathVizPass;
    std::unique_ptr<ShaderPrintReadbackPass> m_shaderDebugPrintPass;
    std::unique_ptr<VisualizationPass> m_visualizationPass;
    bool m_shouldUpdatePtViz = true;
    bool m_shouldRunShaderDebugPrint = true;
    enum class FrameStepMode
    {
        Disabled,
        Wait,
        Step
    };
    FrameStepMode m_frameStepMode = FrameStepMode::Disabled;

    // Profiling
    std::shared_ptr<Profiler> m_profiler;
    time_point<steady_clock> m_prevFrameTimeStamp;
    uint32_t m_renderFrameIndex = 0;

    // Settings
    UIData& m_ui;
    SceneRendererStartupSettings m_args;

    // Acceleration structure state
    uint m_framesSinceAnimation = 0;

#if DONUT_WITH_DLSS
    uint m_dlssInitAttempted = false;
#endif

    nvrhi::ITexture* GetEnvironmentMapTexture();
    void UpdateRtxdiResources();
#if DONUT_WITH_DLSS
    void InitDLSS();
    donut::render::DLSS::EvaluateParameters GetDLSSSRParams() const;
    donut::render::DLSS::EvaluateParameters GetDLSSRRParams() const;
#endif
    void LoadInitialCameraSettings();

    // Render passes in the order in which they execute
    bool ProcessFrameStepMode(nvrhi::IFramebuffer* framebuffer);
    void RunBenchmarkAnimation(const engine::PerspectiveCamera*& activeCamera, uint& effectiveFrameIndex);
    void AdvanceFrameForRenderPasses();
    void ProcessAccumulationModeLogic(bool cameraIsStatic);
    void UpdateSelectedMaterialInUI();
    void UpdateEnvironmentLightAndSunLight();
    void ProcessLocalLightPdfMipMap();
    void EnforceFpsLimit();
    void UpdateAccelerationStructure();
    void EnvironmentMap();
    void GBuffer();
    void CopyPSRBuffers();
    void UpdateRtxdiFrameIndex(uint effectiveFrameIndex);
    void PrepareLights();
    void ApplyNrdCheckerboardSettings();
    LightingPasses::RenderSettings GetFullyProcessedLightingSettings(uint32_t denoiserMode, bool enableDirectReStirPass);
    void PresampleLights(bool enableDirectReStirPass, bool enableIndirect, const LightingPasses::RenderSettings& lightingSettings);
    void RenderDirectLighting(bool enableDirectReStirPass, bool checkerboard, const LightingPasses::RenderSettings& lightingSettings);
    void RenderIndirectLighting(bool enableBrdfAndIndirectPass, bool enableDirectReStirPass, const LightingPasses::RenderSettings& lightingSettings);
    void HandleNoLightingCase(bool enableDirectReStirPass, bool enableBrdfAndIndirectPass);
    void Denoiser(const LightingPasses::RenderSettings& lightingSettings);
    void TransparentGeometry();
    void ResolveAA(nvrhi::ICommandList* commandList, float accumulationWeight) const;
    void Bloom();
    void ReferenceImage(bool cameraIsStatic);
    void ToneMapping(bool exposureResetRequired);
    void DebugPathViz();
    void DebugVisualizationOverlayPass(nvrhi::IFramebuffer* framebuffer);
    void DebugReservoirSubfieldVizPass(nvrhi::IFramebuffer* framebuffer);
    void DebugTextureBlit(nvrhi::IFramebuffer* framebuffer);
    void DebugOutputPass(nvrhi::IFramebuffer* framebuffer);
    void FinalFramebufferOutput(nvrhi::IFramebuffer* framebuffer);
    void ProcessScreenshotFrame();
    void OutputShaderMessages();
};