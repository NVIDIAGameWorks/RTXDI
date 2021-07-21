/***************************************************************************
 # Copyright (c) 2021, NVIDIA CORPORATION.  All rights reserved.
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

enum class RenderingMode : uint32_t
{
    BrdfDirectOnly,
    ReStirDirectOnly,
    ReStirDirectBrdfMIS,
    ReStirDirectBrdfIndirect
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

struct UIData
{
    bool reloadShaders = false;
    bool resetAccumulation = false;
    bool showUI = true;
    bool isLoading = true;

    float loadingPercentage = 0.f;

    bool enableTextures = true;
    bool enableAccumulation = false;
    uint32_t framesToAccumulate = 0;
    bool enableToneMapping = true;
    bool enablePixelJitter = true;
    bool enableTAA = true;
    bool freezeRandom = false;
    bool rasterizeGBuffer = false;
    bool useRayQuery = true;
    bool enableBloom = true;
    float exposureBias = -1.0f;
    float verticalFov = 60.f;

    uint32_t numAccumulatedFrames = 1;

    RenderingMode renderingMode = RenderingMode::ReStirDirectOnly;
    bool enableAnimations = true;
    float animationSpeed = 1.f;
    int environmentMapDirty = 0; // 1 -> needs to be rendered; 2 -> passes/textures need to be created
    int environmentMapIndex = -1;
    bool environmentMapImportanceSampling = true;
    bool enableLocalLightImportanceSampling = true;
    float environmentIntensityBias = 0.f;
    float environmentRotation = 0.f;

    bool enableDenoiser = true;
#ifdef WITH_NRD
    nrd::Method denoisingMethod = nrd::Method::RELAX_DIFFUSE_SPECULAR;
    nrd::ReblurDiffuseSpecularSettings reblurSettings;
    nrd::RelaxDiffuseSpecularSettings relaxSettings;
    void SetDefaultDenoiserSettings();
#endif

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

    GBufferSettings gbufferSettings;
    LightingPasses::RenderSettings lightingSettings;
    donut::render::TemporalAntiAliasingParameters taaParams;

    donut::render::TemporalAntiAliasingJitter temporalJitter = donut::render::TemporalAntiAliasingJitter::Halton;

    std::shared_ptr<Profiler> profiler;

    std::shared_ptr<SampleScene> scene;
    donut::app::FirstPersonCamera* camera = nullptr;

    std::vector<std::shared_ptr<donut::engine::IesProfile>> iesProfiles;

    std::shared_ptr<donut::engine::Material> selectedMaterial;

    UIData();
};


class UserInterface : public donut::app::ImGui_Renderer
{
private:
    UIData& m_ui;
    ImFont* m_FontOpenSans = nullptr;
    std::shared_ptr<donut::engine::Light> m_SelectedLight;
    QualityPreset m_Preset = QualityPreset::Custom;

    bool m_showAdvancedSamplingSettings = false;
    bool m_showAdvancedDenoisingSettings = false;

    void CopySelectedLight() const;
    void CopyCamera() const;
    void ApplyPreset();

    void GeneralSettingsWindow();
    void PerformanceWindow();
    void SamplingSettingsWindow();
    void SceneSettingsWindow();

#ifdef WITH_NRD
    void DenoiserSettingsWindow();
#endif

protected:
    void buildUI(void) override;

public:
    UserInterface(donut::app::DeviceManager* deviceManager, donut::vfs::IFileSystem& rootFS, UIData& ui);

};
