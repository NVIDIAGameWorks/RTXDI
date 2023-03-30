/***************************************************************************
 # Copyright (c) 2021-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#pragma once

#include <rtxdi/RTXDI.h>

#include <donut/engine/Scene.h>
#include <donut/render/TemporalAntiAliasingPass.h>
#include <donut/app/imgui_renderer.h>
#include "GBufferPass.h"
#include "LightingPasses.h"

#if WITH_NRD
#include <NRD.h>
#endif

#include <optional>
#include <string>


class SampleScene;

namespace donut::engine {
    struct IesProfile;
}

namespace donut::app {
    class FirstPersonCamera;
}

enum class DirectLightingMode : uint32_t
{
    None,
    Brdf,
    ReStir
};

enum class IndirectLightingMode : uint32_t
{
    None,
    Brdf,
    ReStirGI
};

enum class QualityPreset : uint32_t
{
    Custom = 0,
    Fast = 1,
    Medium = 2,
    Unbiased = 3,
    Ultra = 4,
    Reference = 5
};

struct RtxgiParameters
{
    ibool enabled = true;
    ibool showProbes = false;
    int selectedVolumeIndex = 0;
    float hysteresis = 0.99f;
    float irradianceThreshold = 0.25f;
    float brightnessThreshold = 0.1f;
    float minFrontFaceDistanceFraction = 0.1f;
    bool probeRelocation = true;
    bool probeClassification = true;
    bool resetRelocation = false;
};

enum class AntiAliasingMode : uint32_t
{
    None,
    Accumulation,
    TAA,
#ifdef WITH_DLSS
    DLSS,
#endif
};

struct UIResources
{
    std::shared_ptr<Profiler> profiler;

    std::shared_ptr<SampleScene> scene;
    donut::app::FirstPersonCamera* camera = nullptr;

    std::vector<std::shared_ptr<donut::engine::IesProfile>> iesProfiles;

    std::shared_ptr<donut::engine::Material> selectedMaterial;
};

enum DebugRenderOutput
{
    LDRColor,
    Depth,
    GBufferDiffuseAlbedo,
    GBufferSpecularRough,
    GBufferNormals,
    GBufferGeoNormals,
    GBufferEmissive,
    DiffuseLighting,
    SpecularLighting,
    DenoisedDiffuseLighting,
    DenoisedSpecularLighting,
    RestirLuminance,
    PrevRestirLuminance,
    DiffuseConfidence,
    SpecularConfidence,
    MotionVectors
};

struct UIData
{
    bool reloadShaders = false;
    bool resetAccumulation = false;
    bool showUI = true;
    bool isLoading = true;

    float loadingPercentage = 0.f;

    ibool enableTextures = true;
    uint32_t framesToAccumulate = 0;
    ibool enableToneMapping = true;
    ibool enablePixelJitter = true;
    ibool rasterizeGBuffer = true;
    ibool useRayQuery = true;
    ibool enableBloom = true;
    float exposureBias = -1.0f;
    float verticalFov = 60.f;

    QualityPreset preset = QualityPreset::Medium;

#ifdef WITH_DLSS
    AntiAliasingMode aaMode = AntiAliasingMode::DLSS;
#else
    AntiAliasingMode aaMode = AntiAliasingMode::TAA;
#endif

    uint32_t numAccumulatedFrames = 1;

    DirectLightingMode directLightingMode = DirectLightingMode::ReStir;
    IndirectLightingMode indirectLightingMode = IndirectLightingMode::None;
    ibool enableAnimations = true;
    float animationSpeed = 1.f;
    int environmentMapDirty = 0; // 1 -> needs to be rendered; 2 -> passes/textures need to be created
    int environmentMapIndex = -1;
    bool environmentMapImportanceSampling = true;
    bool enableLocalLightImportanceSampling = true;
    float environmentIntensityBias = 0.f;
    float environmentRotation = 0.f;
    
    RtxgiParameters rtxgi;
    
    bool enableDenoiser = true;
#ifdef WITH_NRD
    bool usePrePass = false;
    float debug = 0.0f;
    nrd::Method denoisingMethod = nrd::Method::RELAX_DIFFUSE_SPECULAR;
    nrd::ReblurSettings reblurSettings = {};
    nrd::RelaxDiffuseSpecularSettings relaxSettings = {};
    void SetDefaultDenoiserSettings();
#endif
    float noiseMix = 0.33f;
    float noiseClampLow = 0.5f;
    float noiseClampHigh = 2.0f;

#ifdef WITH_DLSS
    bool dlssAvailable = false;
    float dlssExposureScale = 2.f;
    float dlssSharpness = 0.f;
#endif

    float resolutionScale = 1.f;

    bool enableFpsLimit = false;
    uint32_t fpsLimit = 60;

    rtxdi::ContextParameters rtxdiContextParams;
    bool resetRtxdiContext = false;
    uint32_t regirLightSlotCount = 0;
    bool freezeRegirPosition = false;
    float regirCellSize = 1.f;
    float regirSamplingJitter = 1.f;
    std::optional<int> animationFrame;
    std::string benchmarkResults;

    uint32_t visualizationMode = 0; // See the VIS_MODE_XXX constants in ShaderParameters.h
    uint32_t debugRenderOutputBuffer = 0; // See DebugRenderOutput enum above

    bool storeReferenceImage = false;
    bool referenceImageCaptured = false;
    float referenceImageSplit = 0.f;

    GBufferSettings gbufferSettings;
    LightingPasses::RenderSettings lightingSettings;
    donut::render::TemporalAntiAliasingParameters taaParams;

    donut::render::TemporalAntiAliasingJitter temporalJitter = donut::render::TemporalAntiAliasingJitter::Halton;

    std::unique_ptr<UIResources> resources = std::make_unique<UIResources>();

    UIData();

    void ApplyPreset();
};


class UserInterface : public donut::app::ImGui_Renderer
{
private:
    UIData& m_ui;
    ImFont* m_FontOpenSans = nullptr;
    std::shared_ptr<donut::engine::Light> m_SelectedLight;

    bool m_showAdvancedSamplingSettings = false;
    bool m_showAdvancedDenoisingSettings = false;

    void CopySelectedLight() const;
    void CopyCamera() const;
    
    void PerformanceWindow();
    void SceneSettings();
    void GeneralRenderingSettings();
    void SamplingSettings();
    void PostProcessSettings();

#ifdef WITH_NRD
    void DenoiserSettings();
#endif

protected:
    void buildUI(void) override;

public:
    UserInterface(donut::app::DeviceManager* deviceManager, donut::vfs::IFileSystem& rootFS, UIData& ui);

};
