/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include <Rtxdi/DI/ReSTIRDI.h>
#include <Rtxdi/GI/ReSTIRGI.h>
#include <Rtxdi/PT/ReSTIRPT.h>
#include <Rtxdi/ReGIR/ReGIR.h>
#include <Rtxdi/ReGIR/ReGIRParameters.h>

#include <donut/engine/Scene.h>
#include <donut/render/TemporalAntiAliasingPass.h>
#include <donut/app/imgui_renderer.h>
#include "RenderPasses/GBufferPass.h"
#include "RenderPasses/LightingPasses/LightingPasses.h"
#include "RenderPasses/DenoisingPasses/NrdIntegrationDebugSettings.h"

#include "SharedShaderInclude/PTParameters.h"
#include "SharedShaderInclude/ShaderDebug/PTPathViz/PTPathVizConstants.h"

#include <donut/core/math/math.h>
using namespace donut;
using namespace donut::math;

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

enum class DenoiserMode
{
    NONE,
    NRD_RELAX,
    NRD_REBLUR,
    DLSS_RR
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

enum class ReSTIRPTQualityPreset : uint32_t
{
    Custom = 0,
    Fast = 1,
    Medium = 2,
    Ultra = 3
};

enum class AntiAliasingMode : uint32_t
{
    None,
    Accumulation,
    TAA,
#if DONUT_WITH_DLSS
    DLSS_SR
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

// TODO: Keep track of this
//       If None, just output LDR color
//       Otherwise, run the correct subroutine.
enum DebugRenderOutputMode
{
    Off,
    TextureBlit,
    ReservoirSubfield,
    VisualizationOverlay,
    NrdValidation
};

enum DebugTextureBlitMode
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
    MotionVectors,
    DirectLightingRaw,
    IndirectLightingRaw,
    PSRDepth,
    PSRNormalRoughness,
    PSRMotionVectors,
    PSRHitT,
    PSRDiffuseAlbedo,
    PSRSpecularF0,
    PTDuplicationMap,
    SmoothedPTDuplicationMap,
    PTDecorrelationFactor,
    PTSampleID,
    NeighborSelectionGBuffer
};

#include "SharedShaderInclude/ShaderDebug/VisualizationOverlayMode.h"
#include "SharedShaderInclude/ShaderDebug/StructuredBufferVisualizationParameters.h"
#include "SharedShaderInclude/ShaderDebug/ReservoirSubfieldVizPasses/DIReservoirVizParameters.h"
#include "SharedShaderInclude/ShaderDebug/ReservoirSubfieldVizPasses/GIReservoirVizParameters.h"
#include "SharedShaderInclude/ShaderDebug/ReservoirSubfieldVizPasses/PTReservoirVizParameters.h"

struct UIDebugOutputSettings
{
    bool enableShaderDebugPrint = true;
    bool shaderDebugPrintOnClickOrAlways = true; // true = on click, false = always

    DebugRenderOutputMode renderOutputMode = DebugRenderOutputMode::Off;
    DebugTextureBlitMode textureBlitMode = DebugTextureBlitMode::LDRColor;
    ReservoirSubfieldVizMode reservoirSubfieldVizMode = ReservoirSubfieldVizMode::PTReservoir;
        DIReservoirField diReservoirVizField = DIReservoirField::DI_RESERVOIR_FIELD_UV_DATA;
        GIReservoirField giReservoirVizField = GIReservoirField::GI_RESERVOIR_FIELD_POSITION;
        PTReservoirField ptReservoirVizField = PTReservoirField::PT_RESERVOIR_FIELD_TRANSLATED_WORLD_POSITION;
    VisualizationOverlayMode visualizationOverlayMode = VisualizationOverlayMode::VISUALIZATION_OVERLAY_MODE_NONE;

    struct
    {
        bool enabled = true;
        bool cameraVertexEnabled = false;
        bool updateOnClickOrAlways = true; // true = on click, false = always
        bool enableDepth = true;

        PTPathVizPaths paths;
    } ptPathViz;

};

struct LocalLightSamplingUIData
{
    ReSTIRDI_LocalLightSamplingMode localLightSamplingMode = ReSTIRDI_LocalLightSamplingMode::ReGIR_RIS;
    uint32_t numLocalLightUniformSamples = 8;
    uint32_t numLocalLightPowerRISSamples = 8;
    uint32_t numLocalLightReGIRRISSamples = 8;

    uint32_t GetSelectedLocalLightSampleCount();
};

struct UIData
{
    bool reloadShaders = false;
    bool resetAccumulation = false;
    bool showUI = true;
    bool isLoading = true;

    nvrhi::GraphicsAPI graphicsAPI = nvrhi::GraphicsAPI::D3D12;

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
    ReSTIRPTQualityPreset restirPtQualityPreset = ReSTIRPTQualityPreset::Medium;

#if DONUT_WITH_DLSS
    AntiAliasingMode aaMode = AntiAliasingMode::DLSS_SR;
#else
    AntiAliasingMode aaMode = AntiAliasingMode::TAA;
#endif

    uint32_t numAccumulatedFrames = 1;

    IndirectLightingMode indirectLightingMode = IndirectLightingMode::ReStirPT;
    ibool enableAnimations = true;
    float animationSpeed = 1.f;
    int environmentMapDirty = 0; // 1 -> needs to be rendered; 2 -> passes/textures need to be created
    int environmentMapIndex = -1;
    bool environmentMapImportanceSampling = true;
    float environmentIntensityBias = 0.f;
    float environmentRotation = 0.f;

#ifdef WITH_NRD
    NrdIntegrationDebugSettings nrdDebugSettings = {};
    float debug = 0.0f;
    float accumulationTime = 0.334f; // (sec) 20 frames @ 60 FPS
    nrd::Denoiser nrdDenoisingMethod = nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR;
    nrd::ReblurSettings reblurSettings = {};
    nrd::RelaxSettings relaxSettings = {};
    void SetDefaultDenoiserSettings();
#endif

#if DONUT_WITH_DLSS
    bool dlssAvailable = false;
    float dlssExposureScale = 2.f;
    float dlssSharpness = 0.f;

    bool dlssSRSupported = false;
    bool dlssRRSupported = false;

    // When true, ApplyDLSSRRPreset is auto-invoked on the transition into DLSS-RR
    // AA mode and on quality-preset changes while DLSS-RR is the active AA mode.
    bool autoApplyDLSSRRPreset = true;
#endif

    DenoiserMode denoiserMode = DenoiserMode::DLSS_RR;

    float resolutionScale = 1.f;

    bool enableFpsLimit = false;
    uint32_t fpsLimit = 60;

    rtxdi::ReSTIRDIStaticParameters restirDIStaticParams;
    rtxdi::ReGIRStaticParameters regirStaticParams;
    rtxdi::ReSTIRGIStaticParameters restirGIStaticParams;
    rtxdi::ReGIRDynamicParameters regirDynamicParameters;
    bool resetISContext = false;
    uint32_t regirLightSlotCount = 0;
    bool freezeRegirPosition = false;
    std::optional<int> animationFrame;
    std::string benchmarkResults;


    UIDebugOutputSettings debugOutputSettings;

    bool storeReferenceImage = false;
    bool referenceImageCaptured = false;
    float referenceImageSplit = 0.f;

    GBufferSettings gbufferSettings;
    LightingPasses::RenderSettings lightingSettings;

    struct
    {
        rtxdi::ReSTIRDI_ResamplingMode resamplingMode;
        LocalLightSamplingUIData neeLocalLightSampling;
        RTXDI_DIInitialSamplingParameters initialSamplingParams;
        RTXDI_DITemporalResamplingParameters temporalResamplingParams;
        RTXDI_BoilingFilterParameters boilingFilter;
        RTXDI_DISpatialResamplingParameters spatialResamplingParams;
        RTXDI_DISpatioTemporalResamplingParameters spatioTemporalResamplingParams;
        RTXDI_ShadingParameters shadingParams;
    } restirDI;

    struct
    {
        rtxdi::ReSTIRGI_ResamplingMode resamplingMode;
        RTXDI_GITemporalResamplingParameters temporalResamplingParams;
        RTXDI_GISpatialResamplingParameters spatialResamplingParams;
        RTXDI_BoilingFilterParameters boilingFilter;
        RTXDI_GIFinalShadingParameters finalShadingParams;
    } restirGI;

    struct
    {
        rtxdi::ReSTIRPT_ResamplingMode resamplingMode;
        LocalLightSamplingUIData neeLocalLightSampling;
        RTXDI_PTInitialSamplingParameters initialSampling;
        RTXDI_PTDecorrelationParameters decorrelation;
        RTXDI_PTReconnectionParameters reconnection;
        RTXDI_PTTemporalResamplingParameters temporalResampling;
        RTXDI_PTHybridShiftPerFrameParameters hybridShift;
        RTXDI_BoilingFilterParameters boilingFilter;
        RTXDI_PTSpatialResamplingParameters spatialResampling;
        bool hybridMirrorInitial;
    } restirPT;

    donut::render::TemporalAntiAliasingParameters taaParams;

    donut::render::TemporalAntiAliasingJitter temporalJitter = donut::render::TemporalAntiAliasingJitter::Halton;

    std::unique_ptr<UIResources> resources = std::make_unique<UIResources>();

    UIData();

    void ApplyPreset();
    void ApplyReSTIRPTPreset();

    // Applies a quality-preset-independent DLSS-RR mode with robust input-noise
    // settings, without changing the selected ReSTIR PT quality preset.
    void ApplyDLSSRRPreset();
    void ApplyNonDLSSRRPreset();
    void ApplyDenoiserCompatDefaults(bool dlssRR);
};


class UserInterface : public donut::app::ImGui_Renderer
{
public:
    UserInterface(donut::app::DeviceManager* deviceManager, donut::vfs::IFileSystem& rootFS, UIData& ui);

protected:
    void buildUI(void) override;

private:
    void CopySelectedLight() const;
    void CopyCamera() const;

    void PerformanceWindow();
    void SceneSettings();
    void GeneralRenderingSettings();
    void SamplingSettings();
    void PostProcessSettings();
    void DebugSettings();

#ifdef WITH_NRD
    void DenoiserSettings();
#endif

    UIData& m_ui;
    std::shared_ptr<donut::app::RegisteredFont> m_fontOpenSans;
    std::shared_ptr<donut::engine::Light> m_selectedLight;

    bool m_showAdvancedSamplingSettings;
    bool m_showAdvancedDenoisingSettings;
};
