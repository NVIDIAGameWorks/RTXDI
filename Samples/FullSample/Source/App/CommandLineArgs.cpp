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

#include <cxxopts.hpp>

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
    else if (s == "DLSS")
        mode = AntiAliasingMode::DLSS;
#endif
    else
        throw cxxopts::exceptions::exception("Unrecognized value passed to the --aa-mode argument.");
    
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
        throw cxxopts::exceptions::exception("Unrecognized value passed to the --direct-mode argument.");

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
        throw cxxopts::exceptions::exception("Unrecognized value passed to the --indirect-mode argument.");

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

    options.add_options()
        ("h,help", "Display this help message", value(help))

        // Camera
        ("camera.direction", "Camera direction", value(args.appStartupSettings.cameraDirection))
        ("camera.moveSpeed", "Camera movement speed", value(args.appStartupSettings.cameraMovementSpeed))
        ("camera.position", "Camera position", value(args.appStartupSettings.cameraPosition))
        ("camera.rotationSpeed", "Camera rotation speed", value(args.appStartupSettings.cameraRotateSpeed))

        // Debug
        ("d", "Enable the DX12 or Vulkan validation layers", value(deviceParams.enableDebugRuntime)) // Periods in names preclude single letter special case
        ("debug.validation", "Enable the DX12 or Vulkan validation layers", value(deviceParams.enableDebugRuntime))
        ("debug.verbose", "Enable debug log messages", value(args.appStartupSettings.verbose))

        // Device
        ("device.backBufferWidth", "Window width", value(deviceParams.backBufferWidth))
        ("device.backBufferHeight", "Window height", value(deviceParams.backBufferHeight))
        ("device.borderless", "Run in borderless mode", value(deviceParams.startBorderless))
        ("device.disableBgOpt", "Disable DX12 driver background optimization", value(args.disableBackgroundOptimization))
        ("device.fullscreen", "Run in full screen", value(deviceParams.startFullscreen))
        ("device.maximized", "Run in borderless mode", value(deviceParams.startMaximized))
        ("device.vk", "Run the application using Vulkan (otherwise D3D12 if supported)", value(useVk))

        // GBuffer
        ("gbuffer.alphaTested", "Alpha-tested materials toggle", value(ui.gbufferSettings.enableAlphaTestedGeometry))
        ("gbuffer.rasterizeGBuffer", "G-buffer rasterization toggle", value(ui.rasterizeGBuffer))
        ("gbuffer.transparent", "Transparent materials toggle", value(ui.gbufferSettings.enableTransparentGeometry))

        // Lighting
        ("lighting.directMode", "Direct lighting mode: NONE, BRDF, RESTIR", value(ui.lightingSettings.directLightingMode))
        ("lighting.indirectMode", "Indirect lighting mode: NONE, BRDF, RESTIRGI, RESTIRPT", value(ui.indirectLightingMode))

        // Path tracer (for ReSTIR PT)
        ("pt.extraMirrorBounceBudget", "Path tracer: Extra mirror bounce budget", value(ui.lightingSettings.ptParameters.extraMirrorBounceBudget))
        ("pt.minimumPathThroughput", "Path tracer: Minimum path throughput", value(ui.lightingSettings.ptParameters.minimumPathThroughput))
        ("pt.russianRoulette", "Path tracer: Enable Russian roulette", value(ui.lightingSettings.ptParameters.enableRussianRoulette))
        ("pt.russianRouletteContinueChance", "Path tracer: Russian roulette continuation chance", value(ui.lightingSettings.ptParameters.russianRouletteContinueChance))
        ("pt.enableSecondaryDISpatialResampling", "Path tracer: Enable ReSTIR DI spatial resampling for NEE", value(ui.lightingSettings.ptParameters.enableSecondaryDISpatialResampling))
        ("pt.sampleEnvMapOnSecondaryMiss", "Path tracer: Sample the environment map if the secondary bounce misses (i.e. perform direct env lighting for the primary surface)", value(ui.lightingSettings.ptParameters.sampleEnvMapOnSecondaryMiss))
        ("pt.shouldSampleEmissivesOnSecondaryHit", "Path tracer: Sample emissive light on the secondary surface (i.e. perform direct local lighting for the primary surface)", value(ui.lightingSettings.ptParameters.sampleEmissivesOnSecondaryHit))
        ("pt.lightSamplingMode", "Path tracer: Light sampling mode: EMISSIVE, NEE, MIS", value(ui.lightingSettings.ptParameters.lightSamplingMode))

        // Post processing
        ("postProcessing.aaMode", "Anti-aliasing mode: OFF, ACC, TAA, DLSS (if supported)", value(ui.aaMode))
        ("postProcessing.bloom", "Bloom effect toggle", value(ui.enableBloom))
        ("postProcessing.textures", "Textures toggle", value(ui.enableTextures))
        ("postProcessing.toneMapping", "Tone mapping toggle", value(ui.enableToneMapping))

        // Profiling
        ("profiling.saveFile", "Save frame to file and exit", value(args.appStartupSettings.saveFrameFileName))
        ("profiling.saveFrame", "Index of the frame to save, default is 0", value(args.appStartupSettings.saveFrameIndex))
        ("profiling.benchmark", "Run the benchmark", value(args.appStartupSettings.benchmark))

        // Rendering
        ("rendering.height", "Internal render target height, overrides window size", value(args.appStartupSettings.renderHeight))
        ("rendering.pixelJitter", "Pixel jitter toggle", value(ui.enablePixelJitter))
        ("rendering.rayQuery", "Ray Query toggle", value(ui.useRayQuery))
        ("rendering.width", "Internal render target width, overrides window size", value(args.appStartupSettings.renderWidth))

        // ReSTIR DI
        ("restirDI.checkerboard", "Use checkerboard rendering", value(checkerboard))
        ("restirDI.diMode", "ReSTIR DI resampling mode: OFF, TEMPORAL, SPATIAL, TEMPORAL_SPATIAL, FUSED", value(ui.restirDI.resamplingMode))
        ("restirDI.preset", "Rendering settings preset: FAST, MEDIUM, UNBIASED, ULTRA, REFERENCE", value(ui))

        // ReSTIR GI
        ("restirGI.mode", "ReSTIR GI resampling mode: OFF, TEMPORAL, SPATIAL, TEMPORAL_SPATIAL, FUSED", value(ui.restirGI.resamplingMode))

        // ReSTIR PT
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
        ("restirPT.temporalResampling.enablePermutationSampling", "ReSTIR PT: enable permutation sampling", value(ui.restirPT.temporalResampling.enablePermutationSampling))
        ("restirPT.temporalResampling.maxHistoryLength", "ReSTIR PT: maximum history length", value(ui.restirPT.temporalResampling.maxHistoryLength))
        ("restirPT.temporalResampling.duplicationBasedHistoryReduction", "ReSTIR PT: Duplication based history reduction", value(ui.restirPT.temporalResampling.duplicationBasedHistoryReduction))
        ("restirPT.temporalResampling.maxReservoirAge", "ReSTIR PT: maximum reservoir age", value(ui.restirPT.temporalResampling.maxReservoirAge))
        ("restirPT.temporalResampling.enableFallbackSampling", "ReSTIR PT: add an additional temporal sample that uses 0 motion vector", value(ui.restirPT.temporalResampling.enableFallbackSampling))
        ("restirPT.temporalResampling.enableVisibilityBeforeCombine", "ReSTIR PT: check visibility before combining temporally resampled reservoir", value(ui.restirPT.temporalResampling.enableVisibilityBeforeCombine))
        ("restirPT.boilingFilter.enableBoilingFilter", "ReSTIR PT: enable boiling filter", value(ui.restirPT.boilingFilter.enableBoilingFilter))
        ("restirPT.boilingFilter.boilingFilterStrength", "ReSTIR PT: boiling filter strength", value(ui.restirPT.boilingFilter.boilingFilterStrength))
        ("restirPT.spatialResampling.numSpatialSamples", "ReSTIR PT: number of spatial samples to take", value(ui.restirPT.spatialResampling.numSpatialSamples))
        ("restirPT.spatialResampling.numDisocclusionBoostSamples", "ReSTIR PT: Number of spatial samples to take if a sample is freshly disoccluded. Only meaningful if higher than numSpatialSamples.", value(ui.restirPT.spatialResampling.numDisocclusionBoostSamples))
        ("restirPT.spatialResampling.samplingRadius", "ReSTIR PT: Sampling radius in pixels", value(ui.restirPT.spatialResampling.samplingRadius))
        ("restirPT.spatialResampling.normalThreshold", "ReSTIR PT: Spatial normal threshold", value(ui.restirPT.spatialResampling.normalThreshold))
        ("restirPT.spatialResampling.depthThreshold", "ReSTIR PT: Spatial depth threshold", value(ui.restirPT.spatialResampling.depthThreshold))

        // Scene
        ("scene.animation", "Animations toggle", value(ui.enableAnimations))
        ("scene.asset", "Scene to load: Arcade, Bistro, BistroMirror, CornellBox", value(args.appStartupSettings.sceneAsset))
    ;

#if WITH_NRD
    options.add_options()
        ("denoiser", "Denoiser: OFF, REBLUR, RELAX", value(denoiserMode))
    ;
#endif

    try
    {
        options.parse(argc, argv);

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

            if (denoiserMode == "OFF")
                ui.enableDenoiser = false;
            else if (denoiserMode == "REBLUR")
                ui.denoisingMethod = nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR;
            else if (denoiserMode == "RELAX")
                ui.denoisingMethod = nrd::Denoiser::RELAX_DIFFUSE_SPECULAR;
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
}