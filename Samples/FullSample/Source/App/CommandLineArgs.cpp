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

#include "CommandLineArgs.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>

#include <cxxopts.hpp>
#include <donut/core/log.h>
#include <donut/core/math/math.h>

static void toupper(std::string& s)
{
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return std::toupper(c); });
}

namespace donut::math {
    template <typename T, int n>
    std::istream& operator>> (std::istream& is, vector<T, n>& vec)
    {
        for (int i = 0; i < n; ++i)
        {
            is >> vec[i];
        }
        return is;
    }
}

std::istream& operator>> (std::istream& is, AntiAliasingMode& mode)
{
    std::string s;
    is >> s;
    toupper(s);

    if (s == "OFF")
        mode = AntiAliasingMode::None;
    else if (s == "ACC")
        mode = AntiAliasingMode::Accumulation;
    else if (s == "TAA")
        mode = AntiAliasingMode::TAA;
#if DONUT_WITH_DLSS
    else if (s == "DLSS-SR")
        mode = AntiAliasingMode::DLSS_SR;
    // DLSS-RR is no longer an AA mode - it is a denoiser mode (see --denoiser.mode DLSS_RR).
#endif
    else
        throw cxxopts::exceptions::exception("Unrecognized value passed to the --postProcessing.aaMode argument.");
    
    return is;
}

std::istream& operator>> (std::istream& is, DenoiserMode& mode)
{
    std::string s;
    is >> s;
    toupper(s);

    if (s == "OFF")
        mode = DenoiserMode::NONE;
#if WITH_NRD
    else if (s == "NRD_RELAX")
        mode = DenoiserMode::NRD_RELAX;
    else if (s == "NRD_REBLUR")
        mode = DenoiserMode::NRD_REBLUR;
#endif
#if DONUT_WITH_DLSS
    else if (s == "DLSS_RR" || s == "DLSS-RR")
        mode = DenoiserMode::DLSS_RR;
#endif
    else
        throw cxxopts::exceptions::exception("Unrecognized value passed to the --denoiser.mode argument.");
    return is;
}

std::istream& operator>> (std::istream& is, DirectLightingMode& mode)
{
    std::string s;
    is >> s;
    toupper(s);

    if (s == "NONE")
        mode = DirectLightingMode::None;
    else if (s == "BRDF")
        mode = DirectLightingMode::Brdf;
    else if (s == "RESTIR")
        mode = DirectLightingMode::ReStir;
    else
        throw cxxopts::exceptions::exception("Unrecognized value passed to the --lighting.directMode argument.");

    return is;
}

std::istream& operator>> (std::istream& is, IndirectLightingMode& mode)
{
    std::string s;
    is >> s;
    toupper(s);

    if (s == "NONE")
        mode = IndirectLightingMode::None;
    else if (s == "BRDF")
        mode = IndirectLightingMode::Brdf;
    else if (s == "RESTIRGI")
        mode = IndirectLightingMode::ReStirGI;
    else if (s == "RESTIRPT")
        mode = IndirectLightingMode::ReStirPT;
    else
        throw cxxopts::exceptions::exception("Unrecognized value passed to the --lighting.indirectMode argument.");

    return is;
}

namespace rtxdi
{
std::istream& operator>> (std::istream& is, ReSTIRDI_ResamplingMode& mode)
{
    std::string s;
    is >> s;
    toupper(s);

    if (s == "NONE")
        mode = rtxdi::ReSTIRDI_ResamplingMode::None;
    else if (s == "TEMPORAL")
        mode = rtxdi::ReSTIRDI_ResamplingMode::Temporal;
    else if (s == "SPATIAL")
        mode = rtxdi::ReSTIRDI_ResamplingMode::Spatial;
    else if (s == "TEMPORAL_SPATIAL")
        mode = rtxdi::ReSTIRDI_ResamplingMode::TemporalAndSpatial;
    else if (s == "FUSED")
        mode = rtxdi::ReSTIRDI_ResamplingMode::FusedSpatiotemporal;

    else
        throw cxxopts::exceptions::exception("Unrecognized value passed to the --di-mode argument.");

    return is;
}

std::istream& operator>> (std::istream& is, ReSTIRGI_ResamplingMode& mode)
{
    std::string s;
    is >> s;
    toupper(s);

    if (s == "NONE")
        mode = rtxdi::ReSTIRGI_ResamplingMode::None;
    else if (s == "TEMPORAL")
        mode = rtxdi::ReSTIRGI_ResamplingMode::Temporal;
    else if (s == "SPATIAL")
        mode = rtxdi::ReSTIRGI_ResamplingMode::Spatial;
    else if (s == "TEMPORAL_SPATIAL")
        mode = rtxdi::ReSTIRGI_ResamplingMode::TemporalAndSpatial;
    else if (s == "FUSED")
        mode = rtxdi::ReSTIRGI_ResamplingMode::FusedSpatiotemporal;

    else
        throw cxxopts::exceptions::exception("Unrecognized value passed to the --gi-mode argument.");

    return is;
}

std::istream& operator>> (std::istream& is, ReSTIRPT_ResamplingMode& mode)
{
    std::string s;
    is >> s;
    toupper(s);

    if (s == "NONE")
        mode = rtxdi::ReSTIRPT_ResamplingMode::None;
    else if (s == "TEMPORAL")
        mode = rtxdi::ReSTIRPT_ResamplingMode::Temporal;
    else if (s == "SPATIAL")
        mode = rtxdi::ReSTIRPT_ResamplingMode::Spatial;
    else if (s == "TEMPORAL_SPATIAL")
        mode = rtxdi::ReSTIRPT_ResamplingMode::TemporalAndSpatial;

    else
        throw cxxopts::exceptions::exception("Unrecognized value passed to the --pt-mode argument.");

    return is;
}

}

std::istream& operator>> (std::istream& is, ReSTIRDI_LocalLightSamplingMode& mode)
{
    std::string s;
    is >> s;
    toupper(s);

    if (s == "UNIFORM")
        mode = ReSTIRDI_LocalLightSamplingMode::Uniform;
    else if (s == "POWER_RIS")
        mode = ReSTIRDI_LocalLightSamplingMode::Power_RIS;
    else if (s == "REGIR_RIS")
        mode = ReSTIRDI_LocalLightSamplingMode::ReGIR_RIS;
    else
        throw cxxopts::exceptions::exception("Unrecognized value passed to the --restirDI.nee.localLightSamplingMode argument.");

    return is;
}

std::istream& operator>> (std::istream& is, ReSTIRDI_TemporalBiasCorrectionMode& mode)
{
    std::string s;
    is >> s;
    toupper(s);

    if (s == "OFF")
        mode = ReSTIRDI_TemporalBiasCorrectionMode::Off;
    else if (s == "BASIC")
        mode = ReSTIRDI_TemporalBiasCorrectionMode::Basic;
    else if (s == "RAYTRACED")
        mode = ReSTIRDI_TemporalBiasCorrectionMode::Raytraced;
    else
        throw cxxopts::exceptions::exception("Unrecognized value passed to the --restirDI.temporalResampling.biasCorrectionMode argument.");

    return is;
}

std::istream& operator>> (std::istream& is, ReSTIRDI_SpatialBiasCorrectionMode& mode)
{
    std::string s;
    is >> s;
    toupper(s);

    if (s == "OFF")
        mode = ReSTIRDI_SpatialBiasCorrectionMode::Off;
    else if (s == "BASIC")
        mode = ReSTIRDI_SpatialBiasCorrectionMode::Basic;
    else if (s == "PAIRWISE")
        mode = ReSTIRDI_SpatialBiasCorrectionMode::Pairwise;
    else if (s == "RAYTRACED")
        mode = ReSTIRDI_SpatialBiasCorrectionMode::Raytraced;
    else
        throw cxxopts::exceptions::exception("Unrecognized value passed to the --restirDI.spatialResampling.biasCorrectionMode argument.");

    return is;
}

std::istream& operator>> (std::istream& is, ReSTIRDI_SpatioTemporalBiasCorrectionMode& mode)
{
    std::string s;
    is >> s;
    toupper(s);

    if (s == "OFF")
        mode = ReSTIRDI_SpatioTemporalBiasCorrectionMode::Off;
    else if (s == "BASIC")
        mode = ReSTIRDI_SpatioTemporalBiasCorrectionMode::Basic;
    else if (s == "PAIRWISE")
        mode = ReSTIRDI_SpatioTemporalBiasCorrectionMode::Pairwise;
    else if (s == "RAYTRACED")
        mode = ReSTIRDI_SpatioTemporalBiasCorrectionMode::Raytraced;
    else
        throw cxxopts::exceptions::exception("Unrecognized value passed to a --restirDI.*biasCorrectionMode argument.");

    return is;
}

std::istream& operator>> (std::istream& is, RTXDI_GIBiasCorrectionMode& mode)
{
    std::string s;
    is >> s;
    toupper(s);

    if (s == "OFF")
        mode = RTXDI_GIBiasCorrectionMode::Off;
    else if (s == "BASIC")
        mode = RTXDI_GIBiasCorrectionMode::Basic;
    else if (s == "RAYTRACED")
        mode = RTXDI_GIBiasCorrectionMode::Raytraced;
    else
        throw cxxopts::exceptions::exception("Unrecognized value passed to the --restirGI.temporalResampling.biasCorrectionMode or --restirGI.spatialResampling.biasCorrectionMode argument.");

    return is;
}

std::istream& operator>> (std::istream& is, PTInitialSamplingLightSamplingMode& mode)
{
    std::string s;
    is >> s;
    toupper(s);

    if (s == "EMISSIVE")
        mode = PTInitialSamplingLightSamplingMode::EmissiveOnly;
    else if (s == "NEE")
        mode = PTInitialSamplingLightSamplingMode::NeeOnly;
    else if (s == "MIS")
        mode = PTInitialSamplingLightSamplingMode::Mis;
    else
        throw cxxopts::exceptions::exception("Unrecognized value passed to the --pt.lightSamplingMode argument.");

    return is;
}

std::istream& operator>> (std::istream& is, RTXDI_PTReconnectionMode& mode)
{
    std::string s;
    is >> s;
    toupper(s);

    if (s == "FIXED")
        mode = RTXDI_PTReconnectionMode::FixedThreshold;
    else if (s == "FOOTPRINT")
        mode = RTXDI_PTReconnectionMode::Footprint;
    else
        throw cxxopts::exceptions::exception("Unrecognized value passed to the --restirpt.reconnection.reconnectionMode argument.");

    return is;
}

std::istream& operator>> (std::istream& is, SceneAsset& sceneAsset)
{
    std::string s;
    is >> s;
    toupper(s);

    if (s == "ARCADE")
        sceneAsset = SceneAsset::Arcade;
    else if (s == "BISTRO")
        sceneAsset = SceneAsset::Bistro;
    else if (s == "BISTROMIRROR")
        sceneAsset = SceneAsset::BistroMirror;
    else if (s == "CORNELLBOX")
        sceneAsset = SceneAsset::CornellBox;

    else
        throw cxxopts::exceptions::exception("Unrecognized value passed to the --scene argument.");

    return is;
}

// A hacky operator to allow selecting the preset
std::istream& operator>> (std::istream& is, UIData& ui)
{
    std::string s;
    is >> s;
    toupper(s);

    if (s == "FAST")
        ui.preset = QualityPreset::Fast;
    else if (s == "MEDIUM")
        ui.preset = QualityPreset::Medium;
    else if (s == "UNBIASED")
        ui.preset = QualityPreset::Unbiased;
    else if (s == "ULTRA")
        ui.preset = QualityPreset::Ultra;
    else if (s == "REFERENCE")
        ui.preset = QualityPreset::Reference;
    else
        throw cxxopts::exceptions::exception("Unrecognized value passed to the --preset argument.");

    ui.ApplyPreset();

    return is;
}

struct ReSTIRPTPresetCommand
{
    UIData* ui = nullptr;
};

std::istream& operator>> (std::istream& is, ReSTIRPTPresetCommand& command)
{
    std::string s;
    is >> s;
    toupper(s);

    if (s == "FAST")
        command.ui->restirPtQualityPreset = ReSTIRPTQualityPreset::Fast;
    else if (s == "MEDIUM")
        command.ui->restirPtQualityPreset = ReSTIRPTQualityPreset::Medium;
    else if (s == "ULTRA")
        command.ui->restirPtQualityPreset = ReSTIRPTQualityPreset::Ultra;
    else if (s == "CUSTOM")
        command.ui->restirPtQualityPreset = ReSTIRPTQualityPreset::Custom;
    else
        throw cxxopts::exceptions::exception("Unrecognized value passed to the --restirPT.preset argument.");

    command.ui->ApplyReSTIRPTPreset();

    return is;
}

template<typename T>
std::istream& operator>> (std::istream& is, std::optional<T>& data)
{
    T output;
    is >> output;
    data = output;
    return is;
}

void ProcessCommandLine(int argc, char** argv, const std::string& appTitle, donut::app::DeviceCreationParameters& deviceParams, UIData& ui, CommandLineArguments& args)
{
    using namespace cxxopts;

    Options options(argv[0], appTitle.c_str());

    ibool checkerboard = false;
    std::string denoiserMode;
    bool help = false;
    bool useVk = false;
    ibool dlssRRPreset = false;
    ReSTIRPTPresetCommand restirPTPreset{ &ui };

    options.add_options()
        ("h,help", "Display this help message", value(help))

        // Camera
        ("camera.direction", "Camera direction", value(args.appStartupSettings.cameraDirection))
        ("camera.moveSpeed", "Camera movement speed", value(args.appStartupSettings.cameraMovementSpeed))
        ("camera.position", "Camera position", value(args.appStartupSettings.cameraPosition))
        ("camera.rotationSpeed", "Camera rotation speed", value(args.appStartupSettings.cameraRotateSpeed))

        // Debug
        ("d", "Enable the DX12 or Vulkan validation layers (0/1)", value(deviceParams.enableDebugRuntime)) // Periods in names preclude single letter special case
        ("debug.validation", "Enable the DX12 or Vulkan validation layers (0/1)", value(deviceParams.enableDebugRuntime))
        ("debug.verbose", "Enable debug log messages (0/1)", value(args.appStartupSettings.verbose))

        // Device
        ("device.backBufferWidth", "Window width", value(deviceParams.backBufferWidth))
        ("device.backBufferHeight", "Window height", value(deviceParams.backBufferHeight))
        ("device.borderless", "Run in borderless mode (0/1)", value(deviceParams.startBorderless))
        ("device.disableBgOpt", "Disable DX12 driver background optimization (0/1)", value(args.disableBackgroundOptimization))
        ("device.fullscreen", "Run in full screen (0/1)", value(deviceParams.startFullscreen))
        ("device.maximized", "Run in borderless mode (0/1)", value(deviceParams.startMaximized))
        ("device.vk", "Run the application using Vulkan (otherwise D3D12 if supported) (0/1)", value(useVk))

        // Denosier
        ("denoiser.mode", "Denoiser mode", value(ui.denoiserMode))

        // GBuffer
        ("gbuffer.alphaTested", "Alpha-tested materials toggle (0/1)", value(ui.gbufferSettings.enableAlphaTestedGeometry))
        ("gbuffer.rasterizeGBuffer", "G-buffer rasterization toggle (0/1)", value(ui.rasterizeGBuffer))
        ("gbuffer.transparent", "Transparent materials toggle (0/1)", value(ui.gbufferSettings.enableTransparentGeometry))

        // Lighting
        ("lighting.directMode", "Direct lighting mode: NONE, BRDF, RESTIR", value(ui.lightingSettings.directLightingMode))
        ("lighting.indirectMode", "Indirect lighting mode: NONE, BRDF, RESTIRGI, RESTIRPT", value(ui.indirectLightingMode))

        // Path tracer (for ReSTIR PT)
        ("pt.extraMirrorBounceBudget", "Path tracer: Extra mirror bounce budget", value(ui.lightingSettings.ptParameters.extraMirrorBounceBudget))
        ("pt.minimumPathThroughput", "Path tracer: Minimum path throughput", value(ui.lightingSettings.ptParameters.minimumPathThroughput))
        ("pt.russianRoulette", "Path tracer: Enable Russian roulette (0/1)", value(ui.lightingSettings.ptParameters.enableRussianRoulette))
        ("pt.russianRouletteContinueChance", "Path tracer: Russian roulette continuation chance", value(ui.lightingSettings.ptParameters.russianRouletteContinueChance))
        ("pt.enableSecondaryDISpatialResampling", "Path tracer: Enable ReSTIR DI spatial resampling for NEE (0/1)", value(ui.lightingSettings.ptParameters.enableSecondaryDISpatialResampling))
        ("pt.sampleEnvMapOnSecondaryMiss", "Path tracer: Sample the environment map if the secondary bounce misses (i.e. perform direct env lighting for the primary surface) (0/1)", value(ui.lightingSettings.ptParameters.sampleEnvMapOnSecondaryMiss))
        ("pt.shouldSampleEmissivesOnSecondaryHit", "Path tracer: Sample emissive light on the secondary surface (i.e. perform direct local lighting for the primary surface) (0/1)", value(ui.lightingSettings.ptParameters.sampleEmissivesOnSecondaryHit))
        ("pt.lightSamplingMode", "Path tracer: Light sampling mode: EMISSIVE, NEE, MIS", value(ui.lightingSettings.ptParameters.lightSamplingMode))

        // Preset (independent of quality presets; applied after parsing)
        ("preset.dlssRR", "Apply the DLSS-RR input-noise mode (0/1): selects the DLSS-RR denoiser mode, enables PT dupmap history reduction, stagnancy decorrelation, and final-shading firefly replacement, and disables the PT temporal boiling filter. Applied after quality presets.", value(dlssRRPreset))

        // Post processing
        ("postProcessing.aaMode", "Anti-aliasing mode: OFF, ACC, TAA, DLSS-SR (ignored if DLSS-RR is enabled)", value(ui.aaMode))
        ("postProcessing.bloom", "Bloom effect toggle (0/1)", value(ui.enableBloom))
        ("postProcessing.textures", "Textures toggle (0/1)", value(ui.enableTextures))
        ("postProcessing.toneMapping", "Tone mapping toggle (0/1)", value(ui.enableToneMapping))

        // Profiling
        ("profiling.saveFile", "Save frame to file and exit", value(args.appStartupSettings.saveFrameFileName))
        ("profiling.saveFrame", "Index of the frame to save, default is 0", value(args.appStartupSettings.saveFrameIndex))
        ("profiling.benchmark", "Run the benchmark (0/1)", value(args.appStartupSettings.benchmark))

        // Rendering
        ("rendering.height", "Internal render target height, overrides window size", value(args.appStartupSettings.renderHeight))
        ("rendering.pixelJitter", "Pixel jitter toggle (0/1)", value(ui.enablePixelJitter))
        ("rendering.rayQuery", "Ray Query toggle (0/1)", value(ui.useRayQuery))
        ("rendering.width", "Internal render target width, overrides window size", value(args.appStartupSettings.renderWidth))

        // ReSTIR DI (dynamic + static used at context creation)
        ("restirDI.checkerboard", "Use checkerboard rendering (0/1)", value(checkerboard))
        ("restirDI.diMode", "ReSTIR DI resampling mode: OFF, TEMPORAL, SPATIAL, TEMPORAL_SPATIAL, FUSED", value(ui.restirDI.resamplingMode))
        ("restirDI.preset", "Rendering settings preset: FAST, MEDIUM, UNBIASED, ULTRA, REFERENCE", value(ui))

        ("restirDI.initialSampling.numLocalLightSamples", "ReSTIR DI: initial sampling num local light samples (overridden by NEE counts for active mode unless you match them)", value(ui.restirDI.initialSamplingParams.numLocalLightSamples))
        ("restirDI.initialSampling.numInfiniteLightSamples", "ReSTIR DI: initial sampling infinite light samples", value(ui.restirDI.initialSamplingParams.numInfiniteLightSamples))
        ("restirDI.initialSampling.numEnvironmentSamples", "ReSTIR DI: initial sampling environment samples", value(ui.restirDI.initialSamplingParams.numEnvironmentSamples))
        ("restirDI.initialSampling.numBrdfSamples", "ReSTIR DI: initial sampling BRDF samples", value(ui.restirDI.initialSamplingParams.numBrdfSamples))
        ("restirDI.initialSampling.brdfCutoff", "ReSTIR DI: BRDF cutoff", value(ui.restirDI.initialSamplingParams.brdfCutoff))
        ("restirDI.initialSampling.brdfRayMinT", "ReSTIR DI: BRDF ray minimum t", value(ui.restirDI.initialSamplingParams.brdfRayMinT))
        ("restirDI.initialSampling.localLightSamplingMode", "ReSTIR DI: local light mode UNIFORM, POWER_RIS, REGIR_RIS (also applied to NEE UI)", value(ui.restirDI.initialSamplingParams.localLightSamplingMode))
        ("restirDI.initialSampling.enableInitialVisibility", "ReSTIR DI: enable initial visibility (0/1)", value(ui.restirDI.initialSamplingParams.enableInitialVisibility))
        ("restirDI.initialSampling.environmentMapImportanceSampling", "ReSTIR DI: environment map importance sampling (0/1)", value(ui.restirDI.initialSamplingParams.environmentMapImportanceSampling))

        ("restirDI.neeLocalLightSampling.numLocalLightUniformSamples", "ReSTIR DI: NEE uniform local light sample count", value(ui.restirDI.neeLocalLightSampling.numLocalLightUniformSamples))
        ("restirDI.neeLocalLightSampling.numLocalLightPowerRISSamples", "ReSTIR DI: NEE power RIS local light sample count", value(ui.restirDI.neeLocalLightSampling.numLocalLightPowerRISSamples))
        ("restirDI.neeLocalLightSampling.numLocalLightReGIRRISSamples", "ReSTIR DI: NEE ReGIR RIS local light sample count", value(ui.restirDI.neeLocalLightSampling.numLocalLightReGIRRISSamples))

        ("restirDI.temporalResampling.maxHistoryLength", "ReSTIR DI: temporal max history length", value(ui.restirDI.temporalResamplingParams.maxHistoryLength))
        ("restirDI.temporalResampling.biasCorrectionMode", "ReSTIR DI: temporal bias correction OFF, BASIC, RAYTRACED", value(ui.restirDI.temporalResamplingParams.biasCorrectionMode))
        ("restirDI.temporalResampling.depthThreshold", "ReSTIR DI: temporal depth threshold", value(ui.restirDI.temporalResamplingParams.depthThreshold))
        ("restirDI.temporalResampling.normalThreshold", "ReSTIR DI: temporal normal threshold", value(ui.restirDI.temporalResamplingParams.normalThreshold))
        ("restirDI.temporalResampling.enableVisibilityShortcut", "ReSTIR DI: temporal visibility shortcut (0/1)", value(ui.restirDI.temporalResamplingParams.enableVisibilityShortcut))
        ("restirDI.temporalResampling.enablePermutationSampling", "ReSTIR DI: temporal permutation sampling (0/1)", value(ui.restirDI.temporalResamplingParams.enablePermutationSampling))
        ("restirDI.temporalResampling.uniformRandomNumber", "ReSTIR DI: permutation uniform random seed", value(ui.restirDI.temporalResamplingParams.uniformRandomNumber))
        ("restirDI.temporalResampling.permutationSamplingThreshold", "ReSTIR DI: permutation sampling threshold", value(ui.restirDI.temporalResamplingParams.permutationSamplingThreshold))

        ("restirDI.spatialResampling.numSamples", "ReSTIR DI: spatial neighbor sample count", value(ui.restirDI.spatialResamplingParams.numSamples))
        ("restirDI.spatialResampling.numDisocclusionBoostSamples", "ReSTIR DI: spatial disocclusion boost sample count", value(ui.restirDI.spatialResamplingParams.numDisocclusionBoostSamples))
        ("restirDI.spatialResampling.samplingRadius", "ReSTIR DI: spatial sampling radius (pixels)", value(ui.restirDI.spatialResamplingParams.samplingRadius))
        ("restirDI.spatialResampling.biasCorrectionMode", "ReSTIR DI: spatial bias correction OFF, BASIC, PAIRWISE, RAYTRACED", value(ui.restirDI.spatialResamplingParams.biasCorrectionMode))
        ("restirDI.spatialResampling.depthThreshold", "ReSTIR DI: spatial depth threshold", value(ui.restirDI.spatialResamplingParams.depthThreshold))
        ("restirDI.spatialResampling.normalThreshold", "ReSTIR DI: spatial normal threshold", value(ui.restirDI.spatialResamplingParams.normalThreshold))
        ("restirDI.spatialResampling.targetHistoryLength", "ReSTIR DI: spatial target history length for disocclusion boost", value(ui.restirDI.spatialResamplingParams.targetHistoryLength))
        ("restirDI.spatialResampling.enableMaterialSimilarityTest", "ReSTIR DI: spatial material similarity test (0/1)", value(ui.restirDI.spatialResamplingParams.enableMaterialSimilarityTest))
        ("restirDI.spatialResampling.discountNaiveSamples", "ReSTIR DI: spatial discount naive samples (0/1)", value(ui.restirDI.spatialResamplingParams.discountNaiveSamples))

        ("restirDI.boilingFilter.enableBoilingFilter", "ReSTIR DI: enable boiling filter (0/1)", value(ui.restirDI.boilingFilter.enableBoilingFilter))
        ("restirDI.boilingFilter.boilingFilterStrength", "ReSTIR DI: boiling filter strength", value(ui.restirDI.boilingFilter.boilingFilterStrength))

        ("restirDI.shading.enableFinalVisibility", "ReSTIR DI: shading enable final visibility (0/1)", value(ui.restirDI.shadingParams.enableFinalVisibility))
        ("restirDI.shading.reuseFinalVisibility", "ReSTIR DI: shading reuse final visibility (0/1)", value(ui.restirDI.shadingParams.reuseFinalVisibility))
        ("restirDI.shading.finalVisibilityMaxAge", "ReSTIR DI: shading final visibility max age", value(ui.restirDI.shadingParams.finalVisibilityMaxAge))
        ("restirDI.shading.finalVisibilityMaxDistance", "ReSTIR DI: shading final visibility max distance", value(ui.restirDI.shadingParams.finalVisibilityMaxDistance))
        ("restirDI.shading.enableDenoiserInputPacking", "ReSTIR DI: shading denoiser input packing (0/1)", value(ui.restirDI.shadingParams.enableDenoiserInputPacking))

        // ReSTIR GI
        ("restirGI.mode", "ReSTIR GI resampling mode: OFF, TEMPORAL, SPATIAL, TEMPORAL_SPATIAL, FUSED", value(ui.restirGI.resamplingMode))

        ("restirGI.temporalResampling.depthThreshold", "ReSTIR GI: temporal depth threshold", value(ui.restirGI.temporalResamplingParams.depthThreshold))
        ("restirGI.temporalResampling.normalThreshold", "ReSTIR GI: temporal normal threshold", value(ui.restirGI.temporalResamplingParams.normalThreshold))
        ("restirGI.temporalResampling.maxHistoryLength", "ReSTIR GI: temporal max history length", value(ui.restirGI.temporalResamplingParams.maxHistoryLength))
        ("restirGI.temporalResampling.enableFallbackSampling", "ReSTIR GI: temporal fallback sampling (0/1)", value(ui.restirGI.temporalResamplingParams.enableFallbackSampling))
        ("restirGI.temporalResampling.biasCorrectionMode", "ReSTIR GI: temporal bias correction OFF, BASIC, RAYTRACED", value(ui.restirGI.temporalResamplingParams.biasCorrectionMode))
        ("restirGI.temporalResampling.maxReservoirAge", "ReSTIR GI: temporal max reservoir age", value(ui.restirGI.temporalResamplingParams.maxReservoirAge))
        ("restirGI.temporalResampling.enablePermutationSampling", "ReSTIR GI: temporal permutation sampling (0/1)", value(ui.restirGI.temporalResamplingParams.enablePermutationSampling))
        ("restirGI.temporalResampling.uniformRandomNumber", "ReSTIR GI: permutation uniform random seed", value(ui.restirGI.temporalResamplingParams.uniformRandomNumber))

        ("restirGI.spatialResampling.depthThreshold", "ReSTIR GI: spatial depth threshold", value(ui.restirGI.spatialResamplingParams.depthThreshold))
        ("restirGI.spatialResampling.normalThreshold", "ReSTIR GI: spatial normal threshold", value(ui.restirGI.spatialResamplingParams.normalThreshold))
        ("restirGI.spatialResampling.numSamples", "ReSTIR GI: spatial neighbor sample count", value(ui.restirGI.spatialResamplingParams.numSamples))
        ("restirGI.spatialResampling.samplingRadius", "ReSTIR GI: spatial sampling radius (pixels)", value(ui.restirGI.spatialResamplingParams.samplingRadius))
        ("restirGI.spatialResampling.biasCorrectionMode", "ReSTIR GI: spatial bias correction OFF, BASIC, RAYTRACED", value(ui.restirGI.spatialResamplingParams.biasCorrectionMode))

        ("restirGI.boilingFilter.enableBoilingFilter", "ReSTIR GI: enable boiling filter (0/1)", value(ui.restirGI.boilingFilter.enableBoilingFilter))
        ("restirGI.boilingFilter.boilingFilterStrength", "ReSTIR GI: boiling filter strength", value(ui.restirGI.boilingFilter.boilingFilterStrength))

        ("restirGI.finalShading.enableFinalVisibility", "ReSTIR GI: final shading enable final visibility (0/1)", value(ui.restirGI.finalShadingParams.enableFinalVisibility))
        ("restirGI.finalShading.enableFinalMIS", "ReSTIR GI: final shading enable final MIS (0/1)", value(ui.restirGI.finalShadingParams.enableFinalMIS))

        // ReSTIR PT
        ("restirPT.preset", "ReSTIR PT quality preset: FAST, MEDIUM, ULTRA, CUSTOM", value(restirPTPreset))
        ("restirPT.resamplingMode", "ReSTIR PT: resampling mode: OFF, TEMPORAL, SPATIAL, TEMPORAL_SPATIAL", value(ui.restirPT.resamplingMode))
        ("restirPT.initialSampling.numInitialSamples", "ReSTIR PT: initial sampling number of samples", value(ui.restirPT.initialSampling.numInitialSamples))
        ("restirPT.initialSampling.maxBounceDepth", "ReSTIR PT: max bounce depth", value(ui.restirPT.initialSampling.maxBounceDepth))
        ("restirPT.initialSampling.maxRcVertexLength", "ReSTIR PT: max reconnection vertex length", value(ui.restirPT.initialSampling.maxRcVertexLength))
        ("restirPT.reconnection.minConnectionFootprint", "ReSTIR PT: minimum reconnection footprint", value(ui.restirPT.reconnection.minConnectionFootprint))
        ("restirPT.reconnection.minConnectionFootprintSigma", "ReSTIR PT: minimum reconnection footprint sigma", value(ui.restirPT.reconnection.minConnectionFootprintSigma))
        ("restirPT.reconnection.minPdfRoughness", "ReSTIR PT: minimum reconnection pdf roughness", value(ui.restirPT.reconnection.minPdfRoughness))
        ("restirPT.reconnection.minPdfRoughnessSigma", "ReSTIR PT: minimum reconnection pdf roughness sigma", value(ui.restirPT.reconnection.minPdfRoughnessSigma))
        ("restirPT.reconnection.roughnessThreshold", "ReSTIR PT: reconnection roughness threshold", value(ui.restirPT.reconnection.roughnessThreshold))
        ("restirPT.reconnection.distanceThreshold", "ReSTIR PT: reconnection distance threshold", value(ui.restirPT.reconnection.distanceThreshold))
        ("restirPT.reconnection.reconnectionMode", "ReSTIR PT: reconnection mode: FIXED, FOOTPRINT", value(ui.restirPT.reconnection.reconnectionMode))
        ("restirPT.hybridShift.maxBounceDepth", "ReSTIR PT: hybrid shift max bounce depth", value(ui.restirPT.hybridShift.maxBounceDepth))
        ("restirPT.hybridShift.maxRcVertexLength", "ReSTIR PT: hybrid shift max reconnection vertex length", value(ui.restirPT.hybridShift.maxRcVertexLength))
        ("restirPT.temporalResampling.depthThreshold", "ReSTIR PT: temporal depth threshold", value(ui.restirPT.temporalResampling.depthThreshold))
        ("restirPT.temporalResampling.normalThreshold", "ReSTIR PT: temporal normal  threshold", value(ui.restirPT.temporalResampling.normalThreshold))
        ("restirPT.temporalResampling.enablePermutationSampling", "ReSTIR PT: enable permutation sampling (0/1)", value(ui.restirPT.temporalResampling.enablePermutationSampling))
        ("restirPT.temporalResampling.maxHistoryLength", "ReSTIR PT: maximum history length", value(ui.restirPT.temporalResampling.maxHistoryLength))
        ("restirPT.temporalResampling.duplicationBasedHistoryReduction", "ReSTIR PT: Duplication based history reduction (0/1)", value(ui.restirPT.temporalResampling.duplicationBasedHistoryReduction))
        ("restirPT.temporalResampling.maxReservoirAge", "ReSTIR PT: maximum reservoir age", value(ui.restirPT.temporalResampling.maxReservoirAge))
        ("restirPT.temporalResampling.enableFallbackSampling", "ReSTIR PT: add an additional temporal sample that uses 0 motion vector (0/1)", value(ui.restirPT.temporalResampling.enableFallbackSampling))
        ("restirPT.temporalResampling.enableVisibilityBeforeCombine", "ReSTIR PT: check visibility before combining temporally resampled reservoir (0/1)", value(ui.restirPT.temporalResampling.enableVisibilityBeforeCombine))
        ("restirPT.temporalResampling.enableAgeBasedRejection", "ReSTIR PT: reject temporal samples whose Age exceeds maxReservoirAge (0/1)", value(ui.restirPT.temporalResampling.enableAgeBasedRejection))
        ("restirPT.boilingFilter.enableBoilingFilter", "ReSTIR PT: enable boiling filter (0/1)", value(ui.restirPT.boilingFilter.enableBoilingFilter))
        ("restirPT.boilingFilter.boilingFilterStrength", "ReSTIR PT: boiling filter strength", value(ui.restirPT.boilingFilter.boilingFilterStrength))
        ("restirPT.spatialResampling.numSpatialSamples", "ReSTIR PT: number of spatial samples to take", value(ui.restirPT.spatialResampling.numSpatialSamples))
        ("restirPT.spatialResampling.numDisocclusionBoostSamples", "ReSTIR PT: Number of spatial samples to take if a sample is freshly disoccluded. Only meaningful if higher than numSpatialSamples.", value(ui.restirPT.spatialResampling.numDisocclusionBoostSamples))
        ("restirPT.spatialResampling.samplingRadius", "ReSTIR PT: Sampling radius in pixels", value(ui.restirPT.spatialResampling.samplingRadius))
        ("restirPT.spatialResampling.normalThreshold", "ReSTIR PT: Spatial normal threshold", value(ui.restirPT.spatialResampling.normalThreshold))
        ("restirPT.spatialResampling.depthThreshold", "ReSTIR PT: Spatial depth threshold", value(ui.restirPT.spatialResampling.depthThreshold))

        // Scene
        ("scene.animation", "Enable animation (0/1)", value(ui.enableAnimations))
        ("scene.asset", "Scene to load: Arcade, Bistro, BistroMirror, CornellBox", value(args.appStartupSettings.sceneAsset))
    ;

    try
    {
        options.parse(argc, argv);

        ui.restirDI.neeLocalLightSampling.localLightSamplingMode = ui.restirDI.initialSamplingParams.localLightSamplingMode;

        if (help)
        {
#if defined(_WIN32) && !defined(IS_CONSOLE_APP)
            MessageBoxA(nullptr, options.help().c_str(), appTitle.c_str(), MB_ICONINFORMATION);
#else
            printf("%s", options.help().c_str());
#endif
            exit(0);
        }
        
        if (!denoiserMode.empty())
        {
#if WITH_NRD
            std::transform(denoiserMode.begin(), denoiserMode.end(), denoiserMode.begin(),
                [](unsigned char c) { return std::toupper(c); });

            if (denoiserMode == "REBLUR")
                ui.nrdDenoisingMethod = nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR;
            else if (denoiserMode == "RELAX")
                ui.nrdDenoisingMethod = nrd::Denoiser::RELAX_DIFFUSE_SPECULAR;
            else
                throw cxxopts::exceptions::exception("Unrecognized value passed to the --denoiser argument.");
#endif
        }
    }
    catch (const std::exception& e)
    {
        donut::log::error("%s", e.what());
        exit(1);
    }

    if (args.appStartupSettings.saveFrameIndex != 0 && args.appStartupSettings.saveFrameFileName.empty())
    {
        log::warning("The --save-frame argument is used without --save-file. It will be ignored.");
    }

#if DONUT_WITH_DX12 && DONUT_WITH_VULKAN
    args.graphicsApi = useVk ? nvrhi::GraphicsAPI::VULKAN : nvrhi::GraphicsAPI::D3D12;
#elif DONUT_WITH_DX12
    args.graphicsApi = nvrhi::GraphicsAPI::D3D12;
#elif DONUT_WITH_VULKAN
    args.graphicsApi = nvrhi::GraphicsAPI::VULKAN;
#else
#error "At least one of USE_DX12 and USE_VK macros needs to be defined"
#endif
    
    deviceParams.enableNvrhiValidationLayer = deviceParams.enableDebugRuntime;

    if (args.appStartupSettings.benchmark)
        ui.animationFrame = 0;

    if (checkerboard)
        ui.restirDIStaticParams.CheckerboardSamplingMode = rtxdi::CheckerboardMode::Black;

    // Apply the DLSS-RR mode last so it stays independent of quality presets
    // and explicit per-setting command line overrides.
    if (dlssRRPreset)
        ui.ApplyDLSSRRPreset();
}
