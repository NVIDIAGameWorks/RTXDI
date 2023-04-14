/***************************************************************************
 # Copyright (c) 2021-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

 /*
 License for Dear ImGui

 Copyright (c) 2014-2019 Omar Cornut

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.
 */

#include "UserInterface.h"
#include "Profiler.h"
#include "SampleScene.h"

#include <donut/engine/IesProfile.h>
#include <donut/app/Camera.h>
#include <donut/app/UserInterfaceUtils.h>
#include <donut/core/json.h>

#include <json/writer.h>

using namespace donut;

UIData::UIData()
{
    rtxdiContextParams.ReGIR.Mode = rtxdi::ReGIRMode::Onion;
    
    taaParams.newFrameWeight = 0.04f;
    taaParams.maxRadiance = 200.f;
    taaParams.clampingFactor = 1.3f;
    
    ApplyPreset();

#ifdef WITH_NRD
    SetDefaultDenoiserSettings();
#endif
}

void UIData::ApplyPreset()
{
    bool enableCheckerboardSampling = (rtxdiContextParams.CheckerboardSamplingMode != rtxdi::CheckerboardMode::Off);

    if (preset != QualityPreset::Custom)
        lightingSettings = LightingPasses::RenderSettings();

    switch (preset)
    {
    case QualityPreset::Fast:
        enableCheckerboardSampling = true;
        lightingSettings.numPrimaryRegirSamples = 0;
        lightingSettings.numPrimaryLocalLightSamples = 4;
        lightingSettings.numPrimaryBrdfSamples = 0;
        lightingSettings.numPrimaryInfiniteLightSamples = 1;
        lightingSettings.enableReGIR = false;
        lightingSettings.temporalBiasCorrection = RTXDI_BIAS_CORRECTION_OFF;
        lightingSettings.spatialBiasCorrection = RTXDI_BIAS_CORRECTION_OFF;
        lightingSettings.numSpatialSamples = 1;
        lightingSettings.numDisocclusionBoostSamples = 2;
        lightingSettings.discardInvisibleSamples = true;
        lightingSettings.reuseFinalVisibility = true;
        lightingSettings.enableBoilingFilter = true;
        lightingSettings.enableSecondaryResampling = false;
        lightingSettings.enableGradients = false;
        break;

    case QualityPreset::Medium:
        enableCheckerboardSampling = false;
        lightingSettings.numPrimaryRegirSamples = 8;
        lightingSettings.numPrimaryLocalLightSamples = 8;
        lightingSettings.numPrimaryBrdfSamples = 1;
        lightingSettings.numPrimaryInfiniteLightSamples = 2;
        lightingSettings.enableReGIR = true;
        lightingSettings.temporalBiasCorrection = RTXDI_BIAS_CORRECTION_RAY_TRACED;
        lightingSettings.spatialBiasCorrection = RTXDI_BIAS_CORRECTION_BASIC;
        lightingSettings.numSpatialSamples = 1;
        lightingSettings.numDisocclusionBoostSamples = 8;
        lightingSettings.discardInvisibleSamples = true;
        lightingSettings.reuseFinalVisibility = true;
        lightingSettings.enableBoilingFilter = true;
        lightingSettings.enableSecondaryResampling = true;
        lightingSettings.secondarySamplingRadius = 1.f;
        lightingSettings.numSecondarySamples = 1;
        lightingSettings.secondaryBiasCorrection = RTXDI_BIAS_CORRECTION_BASIC;
        lightingSettings.enableGradients = true;
        break;

    case QualityPreset::Unbiased:
        enableCheckerboardSampling = false;
        lightingSettings.numPrimaryRegirSamples = 16;
        lightingSettings.numPrimaryLocalLightSamples = 8;
        lightingSettings.numPrimaryBrdfSamples = 1;
        lightingSettings.numPrimaryInfiniteLightSamples = 2;
        lightingSettings.enableReGIR = true;
        lightingSettings.temporalBiasCorrection = RTXDI_BIAS_CORRECTION_RAY_TRACED;
        lightingSettings.spatialBiasCorrection = RTXDI_BIAS_CORRECTION_RAY_TRACED;
        lightingSettings.numSpatialSamples = 1;
        lightingSettings.numDisocclusionBoostSamples = 8;
        lightingSettings.discardInvisibleSamples = false;
        lightingSettings.reuseFinalVisibility = false;
        lightingSettings.enableBoilingFilter = false;
        lightingSettings.enableSecondaryResampling = true;
        lightingSettings.secondarySamplingRadius = 1.f;
        lightingSettings.numSecondarySamples = 1;
        lightingSettings.secondaryBiasCorrection = RTXDI_BIAS_CORRECTION_RAY_TRACED;
        lightingSettings.enableGradients = true;
        break;

    case QualityPreset::Ultra:
        enableCheckerboardSampling = false;
        lightingSettings.numPrimaryRegirSamples = 16;
        lightingSettings.numPrimaryLocalLightSamples = 16;
        lightingSettings.numPrimaryBrdfSamples = 1;
        lightingSettings.numPrimaryInfiniteLightSamples = 16;
        lightingSettings.enableReGIR = true;
        lightingSettings.temporalBiasCorrection = RTXDI_BIAS_CORRECTION_RAY_TRACED;
        lightingSettings.spatialBiasCorrection = RTXDI_BIAS_CORRECTION_RAY_TRACED;
        lightingSettings.numSpatialSamples = 4;
        lightingSettings.numDisocclusionBoostSamples = 16;
        lightingSettings.discardInvisibleSamples = false;
        lightingSettings.reuseFinalVisibility = false;
        lightingSettings.enableBoilingFilter = false;
        lightingSettings.enableSecondaryResampling = true;
        lightingSettings.secondarySamplingRadius = 4.f;
        lightingSettings.numSecondarySamples = 2;
        lightingSettings.secondaryBiasCorrection = RTXDI_BIAS_CORRECTION_RAY_TRACED;
        lightingSettings.enableGradients = true;
        break;

    case QualityPreset::Reference:
        enableCheckerboardSampling = false;
        lightingSettings.numPrimaryRegirSamples = 0;
        lightingSettings.numPrimaryLocalLightSamples = 16;
        lightingSettings.numPrimaryBrdfSamples = 1;
        lightingSettings.numPrimaryInfiniteLightSamples = 16;
        lightingSettings.enableReGIR = false;
        lightingSettings.resamplingMode = ResamplingMode::None;
        lightingSettings.enableBoilingFilter = false;
        lightingSettings.enableSecondaryResampling = false;
        lightingSettings.enableGradients = false;
        break;

    case QualityPreset::Custom:
    default:;
    }

    rtxdi::CheckerboardMode newCheckerboardMode = enableCheckerboardSampling ? rtxdi::CheckerboardMode::Black : rtxdi::CheckerboardMode::Off;
    if (newCheckerboardMode != rtxdiContextParams.CheckerboardSamplingMode)
    {
        rtxdiContextParams.CheckerboardSamplingMode = newCheckerboardMode;
        resetRtxdiContext = true;
    }
}

#ifdef WITH_NRD
void UIData::SetDefaultDenoiserSettings()
{
    reblurSettings = nrd::ReblurSettings();
    reblurSettings.antilagIntensitySettings.thresholdMin = 0.02f;
    reblurSettings.antilagIntensitySettings.thresholdMax = 0.14f;
    reblurSettings.antilagIntensitySettings.sensitivityToDarkness = 0.25f;
    reblurSettings.antilagIntensitySettings.enable = true;
    reblurSettings.enableAdvancedPrepass = false;
    reblurSettings.enableAntiFirefly = true;
    
    relaxSettings = nrd::RelaxDiffuseSpecularSettings();
    relaxSettings.specularPrepassBlurRadius = 0.0f;
    relaxSettings.diffuseMaxFastAccumulatedFrameNum = 1;
    relaxSettings.specularMaxFastAccumulatedFrameNum = 1;
    relaxSettings.diffusePhiLuminance = 1.0f;
    relaxSettings.disocclusionFixEdgeStoppingNormalPower = 1.0f;
    relaxSettings.disocclusionFixNumFramesToFix = 1;
    relaxSettings.spatialVarianceEstimationHistoryThreshold = 1;
    relaxSettings.enableAntiFirefly = true;
}
#endif


UserInterface::UserInterface(app::DeviceManager* deviceManager, vfs::IFileSystem& rootFS, UIData& ui)
    : ImGui_Renderer(deviceManager)
    , m_ui(ui)
{
    m_FontOpenSans = LoadFont(rootFS, "/media/fonts/OpenSans/OpenSans-Regular.ttf", 17.f);
}

static void ShowHelpMarker(const char* desc)
{
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(500.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void UserInterface::PerformanceWindow()
{
    double frameTime = GetDeviceManager()->GetAverageFrameTimeSeconds();
    ImGui::Text("%05.2f ms/frame (%05.1f FPS)", frameTime * 1e3f, (frameTime > 0.0) ? 1.0 / frameTime : 0.0);

    bool enableProfiler = m_ui.resources->profiler->IsEnabled();
    ImGui::Checkbox("Enable Profiler", &enableProfiler);
    m_ui.resources->profiler->EnableProfiler(enableProfiler);

    if (enableProfiler)
    {
        ImGui::SameLine();
        ImGui::Checkbox("Count Rays", (bool*)&m_ui.lightingSettings.enableRayCounts);

        m_ui.resources->profiler->BuildUI(m_ui.lightingSettings.enableRayCounts);
    }
}

constexpr uint32_t c_ColorRegularHeader   = 0xffff8080;
constexpr uint32_t c_ColorAttentionHeader = 0xff80ffff;

static bool ImGui_ColoredTreeNode(const char* text, uint32_t color)
{
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    bool expanded = ImGui::TreeNode(text);
    ImGui::PopStyleColor();
    return expanded;
}

void UserInterface::GeneralRenderingSettings()
{
    if (ImGui_ColoredTreeNode("General Rendering", c_ColorRegularHeader))
    {
        if (ImGui::Button("Reload Shaders (Ctrl+R)"))
        {
            m_ui.reloadShaders = true;
            m_ui.resetAccumulation = true;
        }

        if (GetDevice()->queryFeatureSupport(nvrhi::Feature::RayQuery))
        {
            if (GetDevice()->queryFeatureSupport(nvrhi::Feature::RayTracingPipeline))
            {
                m_ui.reloadShaders |= ImGui::Checkbox("Use RayQuery", (bool*)&m_ui.useRayQuery);
            }
            else
            {
                ImGui::Checkbox("Use RayQuery (No other options)", (bool*)&m_ui.useRayQuery);
                m_ui.useRayQuery = true;
            }
        }
        else
        {
            ImGui::Checkbox("Use RayQuery (Not available)", (bool*)&m_ui.useRayQuery);
            m_ui.useRayQuery = false;
        }

        ImGui::Checkbox("Rasterize G-Buffer", (bool*)&m_ui.rasterizeGBuffer);

        int resolutionScalePercents = int(m_ui.resolutionScale * 100.f);
        ImGui::SliderInt("Resolution Scale (%)", &resolutionScalePercents, 50, 100);
        m_ui.resolutionScale = float(resolutionScalePercents) * 0.01f;
        m_ui.resolutionScale = dm::clamp(m_ui.resolutionScale, 0.5f, 1.0f);

        ImGui::Checkbox("##enableFpsLimit", &m_ui.enableFpsLimit);
        ImGui::SameLine();
        ImGui::PushItemWidth(69.f);
        ImGui::SliderInt("FPS Limit", (int*)&m_ui.fpsLimit, 10, 60);
        ImGui::PopItemWidth();

        m_ui.resetAccumulation |= ImGui::Checkbox("##enablePixelJitter", (bool*)&m_ui.enablePixelJitter);
        ImGui::SameLine();
        ImGui::PushItemWidth(69.f);
        m_ui.resetAccumulation |= ImGui::Combo("Pixel Jitter", (int*)&m_ui.temporalJitter, "MSAA\0Halton\0R2\0White Noise\0");
        ImGui::PopItemWidth();

        ImGui::TreePop();
    }

    ImGui::Separator();
}

void UserInterface::SamplingSettings()
{
    if (ImGui_ColoredTreeNode("Shared ReSTIR Settings", c_ColorRegularHeader))
    {
        m_ui.resetAccumulation |= ImGui::Checkbox("Importance Sample Local Lights", &m_ui.enableLocalLightImportanceSampling);
        m_ui.resetAccumulation |= ImGui::Checkbox("Importance Sample Env. Map", &m_ui.environmentMapImportanceSampling);

        if (ImGui::TreeNode("RTXDI Context"))
        {
            if (ImGui::Button("Apply Settings"))
                m_ui.resetRtxdiContext = true;

            bool enableCheckerboardSampling = (m_ui.rtxdiContextParams.CheckerboardSamplingMode != rtxdi::CheckerboardMode::Off);
            ImGui::Checkbox("Checkerboard Rendering", &enableCheckerboardSampling);
            m_ui.rtxdiContextParams.CheckerboardSamplingMode = enableCheckerboardSampling ? rtxdi::CheckerboardMode::Black : rtxdi::CheckerboardMode::Off;

            ImGui::Combo("ReGIR Mode", (int*)&m_ui.rtxdiContextParams.ReGIR.Mode, "Disabled\0Grid\0Onion\0");
            ImGui::DragInt("Lights per Cell", (int*)&m_ui.rtxdiContextParams.ReGIR.LightsPerCell, 1, 32, 8192);
            if (m_ui.rtxdiContextParams.ReGIR.Mode == rtxdi::ReGIRMode::Grid)
            {
                ImGui::DragInt3("Grid Resolution", (int*)&m_ui.rtxdiContextParams.ReGIR.GridSize.x, 1, 1, 64);
            }
            else if (m_ui.rtxdiContextParams.ReGIR.Mode == rtxdi::ReGIRMode::Onion)
            {
                ImGui::SliderInt("Onion Layers - Detail", (int*)&m_ui.rtxdiContextParams.ReGIR.OnionDetailLayers, 0, 8);
                ImGui::SliderInt("Onion Layers - Coverage", (int*)&m_ui.rtxdiContextParams.ReGIR.OnionCoverageLayers, 0, 20);
            }

            ImGui::Text("Total ReGIR Cells: %d", m_ui.regirLightSlotCount / m_ui.rtxdiContextParams.ReGIR.LightsPerCell);

            ImGui::TreePop();
        }

        if (ImGui::TreeNode("ReGIR"))
        {
            m_ui.resetAccumulation |= ImGui::Checkbox("Use ReGIR", (bool*)&m_ui.lightingSettings.enableReGIR);
            m_ui.resetAccumulation |= ImGui::SliderFloat("Cell Size", &m_ui.regirCellSize, 0.1f, 4.f);
            m_ui.resetAccumulation |= ImGui::SliderInt("Grid Build Samples", (int*)&m_ui.lightingSettings.numRegirBuildSamples, 0, 32);
            m_ui.resetAccumulation |= ImGui::SliderFloat("Sampling Jitter", &m_ui.regirSamplingJitter, 0.0f, 2.f);

            ImGui::Checkbox("Freeze Position", &m_ui.freezeRegirPosition);
            ImGui::SameLine(0.f, 10.f);
            ImGui::Checkbox("Visualize Cells", (bool*)&m_ui.lightingSettings.visualizeRegirCells);

            ImGui::TreePop();
        }

        ImGui::TreePop();
    }
    ImGui::Separator();
    
    if (ImGui_ColoredTreeNode("Direct Lighting", c_ColorAttentionHeader))
    {
        bool samplingSettingsChanged = false;

        m_ui.resetAccumulation |= ImGui::Combo("Direct Lighting Mode", (int*)&m_ui.directLightingMode,
            "None\0"
            "BRDF\0"
            "ReSTIR\0"
        );
        switch(m_ui.directLightingMode)
        {
        case DirectLightingMode::None:
            ShowHelpMarker(
                "No direct lighting is applied to primary surfaces.");
            break;
        case DirectLightingMode::Brdf:
            ShowHelpMarker(
                "Trace BRDF rays from primary surfaces and collect emissive objects found by such rays. "
                "No light sampling is performed for primary surfaces. Produces very noisy results unless "
                "Indirect Lighting Mode is set to ReSTIR GI, in which case resampling is applied to BRDF rays.");
            break;
        case DirectLightingMode::ReStir:
            ShowHelpMarker(
                "Sample the direct lighting using ReSTIR.");
            break;
        }

        if (ImGui::Combo("Preset", (int*)&m_ui.preset, "(Custom)\0Fast\0Medium\0Unbiased\0Ultra\0Reference\0"))
        {
            m_ui.ApplyPreset();
            m_ui.resetAccumulation = true;
        }

        ImGui::Checkbox("Show Advanced Settings", &m_showAdvancedSamplingSettings);
        
        bool isUsingReStir = m_ui.directLightingMode == DirectLightingMode::ReStir;
        
        if (isUsingReStir)
        {
            ImGui::PushItemWidth(180.f);
            m_ui.resetAccumulation |= ImGui::Combo("Resampling Mode", (int*)&m_ui.lightingSettings.resamplingMode,
                "None\0"
                "Temporal\0"
                "Spatial\0"
                "Temporal + Spatial\0"
                "Fused Spatiotemporal\0");
            ImGui::PopItemWidth();
            ImGui::Separator();

            if (ImGui::TreeNode("Initial Sampling"))
            {
                samplingSettingsChanged |= ImGui::SliderInt("Initial BRDF Samples", (int*)&m_ui.lightingSettings.numPrimaryBrdfSamples, 0, 8);
                ShowHelpMarker(
                    "Number of rays traced from the surface using BRDF importance sampling to find mesh lights or environment map samples. Helps glossy reflections.");

                samplingSettingsChanged |= ImGui::SliderInt("Initial ReGIR Samples", (int*)&m_ui.lightingSettings.numPrimaryRegirSamples, 0, 32);
                ShowHelpMarker(
                    "Number of samples drawn from the local ReGIR cell, if it's available.");

                samplingSettingsChanged |= ImGui::SliderInt("Initial Local Light Samples", (int*)&m_ui.lightingSettings.numPrimaryLocalLightSamples, 0, 32);
                ShowHelpMarker(
                    "Number of samples drawn from the local light pool when or where ReGIR is not available.");

                samplingSettingsChanged |= ImGui::SliderInt("Initial Infinite Light Samples", (int*)&m_ui.lightingSettings.numPrimaryInfiniteLightSamples, 0, 32);
                ShowHelpMarker(
                    "Number of samples drawn from the infinite light pool, i.e. the sun light when using "
                    "the procedural environment, and the environment map when it's not importance sampled.");

                samplingSettingsChanged |= ImGui::SliderInt("Initial Environment Samples", (int*)&m_ui.lightingSettings.numPrimaryEnvironmentSamples, 0, 32);
                ShowHelpMarker(
                    "Number of samples drawn from the environment map when it is importance sampled.");

                samplingSettingsChanged |= ImGui::Checkbox("Enable Initial Visibility", (bool*)&m_ui.lightingSettings.enableInitialVisibility);

                samplingSettingsChanged |= ImGui::SliderFloat("BRDF Sample Cutoff", (float*)&m_ui.lightingSettings.brdfCutoff, 0.0f, 0.1f);
                ShowHelpMarker(
                    "Determine how much to shorten BRDF rays. 0 to disable shortening");

                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Temporal Resampling"))
            {
                samplingSettingsChanged |= ImGui::Checkbox("Enable Previous Frame TLAS/BLAS", (bool*)&m_ui.lightingSettings.enablePreviousTLAS);
                ShowHelpMarker(
                    "Use the previous frame TLAS for bias correction rays during temporal resampling and gradient computation. "
                    "Resutls in less biased results under motion and brighter, more complete gradients.");

                samplingSettingsChanged |= ImGui::Checkbox("Enable Permutation Sampling", (bool*)&m_ui.lightingSettings.enablePermutationSampling);
                ShowHelpMarker(
                    "Shuffle the pixels from the previous frame when resampling from them. This makes pixel colors less correllated "
                    "temporally and therefore better suited for temporal accumulation and denoising. Also results in a higher positive "
                    "bias when the Reuse Final Visibility setting is on, which somewhat counteracts the negative bias from spatial resampling.");

                samplingSettingsChanged |= ImGui::Combo("Temporal Bias Correction", (int*)&m_ui.lightingSettings.temporalBiasCorrection, "Off\0Basic\0Pairwise\0Ray Traced\0");
                ShowHelpMarker(
                    "Off = use the 1/M normalization.\n"
                    "Basic = use the MIS normalization but assume that every sample is visible.\n"
                    "Pairwise = pairwise MIS improves perf and specular quality (assumes every sample is visible).\n"
                    "Ray Traced = use the MIS normalization and verify visibility.");

                if (m_showAdvancedSamplingSettings)
                {
                    samplingSettingsChanged |= ImGui::SliderFloat("Temporal Depth Threshold", &m_ui.lightingSettings.temporalDepthThreshold, 0.f, 1.f);
                    ShowHelpMarker("Higher values result in accepting temporal samples with depths more different from the current pixel.");

                    samplingSettingsChanged |= ImGui::SliderFloat("Temporal Normal Threshold", &m_ui.lightingSettings.temporalNormalThreshold, 0.f, 1.f);
                    ShowHelpMarker("Lower values result in accepting temporal samples with normals more different from the current pixel.");

                    ImGui::SliderFloat("Permutation Sampling Threshold", &m_ui.lightingSettings.permutationSamplingThreshold, 0.8f, 1.f);
                    ShowHelpMarker("Higher values result in disabling permutation sampling on less complex surfaces.");
                }
                samplingSettingsChanged |= ImGui::SliderInt("Max History Length", (int*)&m_ui.lightingSettings.maxHistoryLength, 1, 100);

                samplingSettingsChanged |= ImGui::Checkbox("##enableBoilingFilter", (bool*)&m_ui.lightingSettings.enableBoilingFilter);
                ImGui::SameLine();
                ImGui::PushItemWidth(69.f);
                samplingSettingsChanged |= ImGui::SliderFloat("Boiling Filter", &m_ui.lightingSettings.boilingFilterStrength, 0.f, 1.f);
                ImGui::PopItemWidth();
                ShowHelpMarker(
                    "The boiling filter analyzes the neighborhood of each pixel and discards the pixel's reservoir "
                    "if it has a significantly higher weight than the other pixels.");

                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Spatial Resampling"))
            {
                if (m_ui.lightingSettings.resamplingMode != ResamplingMode::FusedSpatiotemporal)
                {
                    samplingSettingsChanged |= ImGui::Combo("Spatial Bias Correction", (int*)&m_ui.lightingSettings.spatialBiasCorrection, "Off\0Basic\0Pairwise\0Ray Traced\0");
                    ShowHelpMarker(
                        "Off = use the 1/M normalization.\n"
                        "Basic = use the MIS normalization but assume that every sample is visible.\n"
                        "Pairwise = pairwise MIS improves perf and specular quality (assumes every sample is visible).\n"
                        "Ray Traced = use the MIS normalization and verify visibility.");
                }
                samplingSettingsChanged |= ImGui::SliderInt("Spatial Samples", (int*)&m_ui.lightingSettings.numSpatialSamples, 1, 32);

                if (m_ui.lightingSettings.resamplingMode == ResamplingMode::TemporalAndSpatial || m_ui.lightingSettings.resamplingMode == ResamplingMode::FusedSpatiotemporal)
                {
                    samplingSettingsChanged |= ImGui::SliderInt("Disocclusion Boost Samples", (int*)&m_ui.lightingSettings.numDisocclusionBoostSamples, 1, 32);
                    ShowHelpMarker(
                        "The number of spatial samples to take on surfaces which don't have sufficient accumulated history length. "
                        "More samples result in faster convergence in disoccluded regions but increase processing time.");
                }

                samplingSettingsChanged |= ImGui::SliderFloat("Spatial Sampling Radius", &m_ui.lightingSettings.spatialSamplingRadius, 1.f, 32.f);
                if (m_showAdvancedSamplingSettings && m_ui.lightingSettings.resamplingMode != ResamplingMode::FusedSpatiotemporal)
                {
                    samplingSettingsChanged |= ImGui::SliderFloat("Spatial Depth Threshold", &m_ui.lightingSettings.spatialDepthThreshold, 0.f, 1.f);
                    ShowHelpMarker("Higher values result in accepting samples with depths more different from the center pixel.");

                    samplingSettingsChanged |= ImGui::SliderFloat("Spatial Normal Threshold", &m_ui.lightingSettings.spatialNormalThreshold, 0.f, 1.f);
                    ShowHelpMarker("Lower values result in accepting samples with normals more different from the center pixel.");
                }

                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Final Shading"))
            {
                samplingSettingsChanged |= ImGui::Checkbox("Enable Final Visibility", (bool*)&m_ui.lightingSettings.enableFinalVisibility);

                samplingSettingsChanged |= ImGui::Checkbox("Discard Invisible Samples", (bool*)&m_ui.lightingSettings.discardInvisibleSamples);
                ShowHelpMarker(
                    "When a sample is determined to be occluded during final shading, its reservoir is discarded. "
                    "This can significantly reduce noise, but also introduce some bias near shadow boundaries beacuse the reservoirs' M values are kept. "
                    "Also, enabling this option speeds up temporal resampling with Ray Traced bias correction by skipping most of the bias correction rays.");

                samplingSettingsChanged |= ImGui::Checkbox("Reuse Final Visibility", (bool*)&m_ui.lightingSettings.reuseFinalVisibility);
                ShowHelpMarker(
                    "Store the fractional final visibility term in the reservoirs and reuse it later if the reservoir is not too old and has not "
                    "moved too far away from its original location. Enable the Advanced Settings option to control the thresholds.");

                if (m_ui.lightingSettings.reuseFinalVisibility && m_showAdvancedSamplingSettings)
                {
                    samplingSettingsChanged |= ImGui::SliderFloat("Final Visibility - Max Distance", &m_ui.lightingSettings.finalVisibilityMaxDistance, 0.f, 32.f);
                    samplingSettingsChanged |= ImGui::SliderInt("Final Visibility - Max Age", (int*)&m_ui.lightingSettings.finalVisibilityMaxAge, 0, 16);
                }

                ImGui::TreePop();
            }
        }

        if (samplingSettingsChanged)
        {
            m_ui.preset = QualityPreset::Custom;
            m_ui.resetAccumulation = true;
        }

        ImGui::TreePop();
    }
    ImGui::Separator();

    if (ImGui_ColoredTreeNode("Indirect Lighting", c_ColorAttentionHeader))
    {
        m_ui.resetAccumulation |= ImGui::Combo("Indirect Lighting Mode", (int*)&m_ui.indirectLightingMode,
            "None\0"
            "BRDF\0"
            "ReSTIR GI\0"
        );
        switch (m_ui.indirectLightingMode)
        {
        case IndirectLightingMode::Brdf:
            ShowHelpMarker(
                "Trace BRDF rays from primary surfaces. "
                "Shade the surfaces found with BRDF rays using direct light sampling.");
            break;
        case IndirectLightingMode::ReStirGI:
            ShowHelpMarker(
                "Trace diffuse and specular BRDF rays and resample results with ReSTIR GI. "
                "Shade the surfaces found with BRDF rays using direct light sampling.");
            break;
        default:;
        }

        bool isUsingIndirect = m_ui.indirectLightingMode != IndirectLightingMode::None;

        m_ui.resetAccumulation |= ImGui::SliderFloat("Min Secondary Roughness", &m_ui.lightingSettings.minSecondaryRoughness, 0.f, 1.f);

        if (isUsingIndirect && ImGui::TreeNode("Secondary Surface Light Sampling"))
        {
            m_ui.resetAccumulation |= ImGui::SliderInt("Indirect ReGIR Samples", (int*)&m_ui.lightingSettings.numIndirectRegirSamples, 0, 32);
            m_ui.resetAccumulation |= ImGui::SliderInt("Indirect Local Light Samples", (int*)&m_ui.lightingSettings.numIndirectLocalLightSamples, 0, 32);
            m_ui.resetAccumulation |= ImGui::SliderInt("Indirect Inifinite Light Samples", (int*)&m_ui.lightingSettings.numIndirectInfiniteLightSamples, 0, 32);
            m_ui.resetAccumulation |= ImGui::SliderInt("Indirect Environment Samples", (int*)&m_ui.lightingSettings.numIndirectEnvironmentSamples, 0, 32);

            ImGui::TreePop();
        }

        if (isUsingIndirect && m_ui.directLightingMode == DirectLightingMode::ReStir && ImGui::TreeNode("Reuse Primary Samples"))
        {
            m_ui.resetAccumulation |= ImGui::Checkbox("Reuse RTXDI samples for secondary surf.", (bool*)&m_ui.lightingSettings.enableSecondaryResampling);
            ShowHelpMarker(
                "When shading a secondary surface, try to find a matching surface in screen space and reuse its light reservoir. "
                "This feature uses the Spatial Resampling function and has similar controls.");

            m_ui.resetAccumulation |= ImGui::Combo("Secondary Bias Correction", (int*)&m_ui.lightingSettings.secondaryBiasCorrection, "Off\0Basic\0Pairwise\0Ray Traced\0");
            m_ui.resetAccumulation |= ImGui::SliderInt("Secondary Samples", (int*)&m_ui.lightingSettings.numSecondarySamples, 1, 4);
            m_ui.resetAccumulation |= ImGui::SliderFloat("Secondary Sampling Radius", &m_ui.lightingSettings.secondarySamplingRadius, 0.f, 32.f);
            m_ui.resetAccumulation |= ImGui::SliderFloat("Secondary Depth Threshold", &m_ui.lightingSettings.secondaryDepthThreshold, 0.f, 1.f);
            m_ui.resetAccumulation |= ImGui::SliderFloat("Secondary Normal Threshold", &m_ui.lightingSettings.secondaryNormalThreshold, 0.f, 1.f);

            ImGui::TreePop();
        }
        
        if (m_ui.indirectLightingMode == IndirectLightingMode::ReStirGI)
        {
            ImGui::PushItemWidth(180.f);
            m_ui.resetAccumulation |= ImGui::Combo("Resampling Mode", (int*)&m_ui.lightingSettings.reStirGI.resamplingMode,
                "None\0"
                "Temporal\0"
                "Spatial\0"
                "Temporal + Spatial\0"
                "Fused Spatiotemporal\0");
            ImGui::PopItemWidth();
            ImGui::Separator();
            
            if (m_ui.lightingSettings.reStirGI.resamplingMode == ResamplingMode::Temporal ||
                m_ui.lightingSettings.reStirGI.resamplingMode == ResamplingMode::TemporalAndSpatial ||
                m_ui.lightingSettings.reStirGI.resamplingMode == ResamplingMode::FusedSpatiotemporal)
            {
                m_ui.resetAccumulation |= ImGui::SliderFloat("Depth Threshold", &m_ui.lightingSettings.reStirGI.depthThreshold, 0.001f, 1.f);
                m_ui.resetAccumulation |= ImGui::SliderFloat("Normal Threshold", &m_ui.lightingSettings.reStirGI.normalThreshold, 0.001f, 1.f);

                m_ui.resetAccumulation |= ImGui::SliderInt("Max reservoir age", (int*)&m_ui.lightingSettings.reStirGI.maxReservoirAge, 1, 100);
                m_ui.resetAccumulation |= ImGui::SliderInt("Max history length", (int*)&m_ui.lightingSettings.reStirGI.maxHistoryLength, 1, 100);
                m_ui.resetAccumulation |= ImGui::Checkbox("Enable permutation sampling", (bool*)&m_ui.lightingSettings.reStirGI.enablePermutationSampling);
                m_ui.resetAccumulation |= ImGui::Checkbox("Enable fallback sampling", (bool*)&m_ui.lightingSettings.reStirGI.enableFallbackSampling);

                const char* biasCorrectionText = (m_ui.lightingSettings.reStirGI.resamplingMode == ResamplingMode::FusedSpatiotemporal)
                    ? "Fused bias correction"
                    : "Temporal bias correction";
                m_ui.resetAccumulation |= ImGui::Combo(biasCorrectionText, (int*)&m_ui.lightingSettings.reStirGI.temporalBiasCorrection,
                    "Off\0"
                    "Basic MIS\0"
                    "RayTraced\0");

                m_ui.resetAccumulation |= ImGui::Checkbox("##enableGIBoilingFilter", (bool*)&m_ui.lightingSettings.reStirGI.enableBoilingFilter);
                ImGui::SameLine();
                ImGui::PushItemWidth(69.f);
                m_ui.resetAccumulation |= ImGui::SliderFloat("Boiling Filter##GIBoilingFilter", &m_ui.lightingSettings.reStirGI.boilingFilterStrength, 0.f, 1.f);
                ImGui::PopItemWidth();

                ImGui::Separator();
            }

            if (m_ui.lightingSettings.reStirGI.resamplingMode == ResamplingMode::Spatial || 
                m_ui.lightingSettings.reStirGI.resamplingMode == ResamplingMode::TemporalAndSpatial || 
                m_ui.lightingSettings.reStirGI.resamplingMode == ResamplingMode::FusedSpatiotemporal)
            {
                m_ui.resetAccumulation |= ImGui::SliderInt("Num spatial samples", (int*)&m_ui.lightingSettings.reStirGI.numSpatialSamples, 1, 7);
                m_ui.resetAccumulation |= ImGui::SliderFloat("Sampling Radius", (float*)&m_ui.lightingSettings.reStirGI.samplingRadius, 0.01f, 60.0f);

                if (m_ui.lightingSettings.reStirGI.resamplingMode != ResamplingMode::FusedSpatiotemporal)
                {
                    m_ui.resetAccumulation |= ImGui::Combo("Spatial bias correction", (int*)&m_ui.lightingSettings.reStirGI.spatialBiasCorrection,
                        "Off\0"
                        "Basic MIS\0"
                        "RayTraced\0");
                }

                ImGui::Separator();
            }

            m_ui.resetAccumulation |= ImGui::Checkbox("Final visibility", (bool*)&m_ui.lightingSettings.reStirGI.enableFinalVisibility);
            m_ui.resetAccumulation |= ImGui::Checkbox("Final MIS", (bool*)&m_ui.lightingSettings.reStirGI.enableFinalMIS);
        }

        ImGui::TreePop();
    }

    ImGui::Separator();
}

void UserInterface::PostProcessSettings()
{
    if (ImGui_ColoredTreeNode("Post-Processing", c_ColorRegularHeader))
    {
        AntiAliasingMode previousAAMode = m_ui.aaMode;
        ImGui::RadioButton("No AA", (int*)&m_ui.aaMode, (int)AntiAliasingMode::None);
        ImGui::SameLine();
        ImGui::RadioButton("Accumulation", (int*)&m_ui.aaMode, (int)AntiAliasingMode::Accumulation);
        ImGui::SameLine();
        ImGui::RadioButton("TAAU", (int*)&m_ui.aaMode, (int)AntiAliasingMode::TAA);
#ifdef WITH_DLSS
        if (m_ui.dlssAvailable)
        {
            ImGui::SameLine();
            ImGui::RadioButton("DLSS", (int*)&m_ui.aaMode, (int)AntiAliasingMode::DLSS);
        }
#endif
        if (m_ui.aaMode != previousAAMode)
            m_ui.resetAccumulation = true;

        ImGui::PushItemWidth(50.f);
        m_ui.resetAccumulation |= ImGui::DragInt("Accum. Frame Limit", (int*)&m_ui.framesToAccumulate, 1.f, 0, 1024);
        ImGui::PopItemWidth();

        if (m_ui.aaMode == AntiAliasingMode::Accumulation)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("// %d frame(s)", m_ui.numAccumulatedFrames);
        }

        // Reference image UI
        {
            ImGui::Separator();
            ImGui::TextUnformatted("Reference Image:");
            ShowHelpMarker(
                "Allows you to store the current rendering output into a texture, "
                "and later show this texture side-by-side with new rendering output "
                "or toggle between the two for comparison. Most useful with the "
                "Accumulation mode above."
            );
            if (ImGui::Button("Store"))
                m_ui.storeReferenceImage = true;
            if (m_ui.referenceImageCaptured)
            {
                ImGui::SameLine();
                if (ImGui::Button("Toggle"))
                {
                    if (m_ui.referenceImageSplit == 0.f)
                        m_ui.referenceImageSplit = 1.f;
                    else
                        m_ui.referenceImageSplit = 0.f;
                }
                ImGui::SameLine(160.f);
                ImGui::SliderFloat("Split Display", &m_ui.referenceImageSplit, 0.f, 1.f, "%.2f");
            }
            ImGui::Separator();
        }

#ifdef WITH_DLSS
        if (m_ui.dlssAvailable)
        {
            // ImGui::SliderFloat("DLSS Exposure Scale", &m_ui.dlssExposureScale, 0.125f, 16.f, "%.3f", ImGuiSliderFlags_Logarithmic);
            // ImGui::SliderFloat("DLSS Sharpness", &m_ui.dlssSharpness, 0.f, 1.f);
        }
#endif
        m_ui.resetAccumulation |= ImGui::Checkbox("Apply Textures in Compositing", (bool*)&m_ui.enableTextures);
        
        ImGui::Checkbox("Tone mapping", (bool*)&m_ui.enableToneMapping);
        ImGui::SameLine(160.f);
        ImGui::SliderFloat("Exposure bias", &m_ui.exposureBias, -4.f, 2.f);

        ImGui::Checkbox("Bloom", (bool*)&m_ui.enableBloom);

        ImGui::Separator();
        ImGui::PushItemWidth(150.f);
        ImGui::Combo("Visualization", (int*)&m_ui.visualizationMode,
            "None\0"
            "Composited Color\0"
            "Resolved Color\0"
            "Diffuse\0"
            "Specular\0"
            "Diffuse (Denoised)\0"
            "Specular (Denoised)\0"
            "Reservoir Weight\0"
            "Reservoir M\0"
            "Diffuse Gradients\0"
            "Specular Gradients\0"
            "Diffuse Confidence\0"
            "Specular Confidence\0"
            "GI Reservoir Weight\0"
            "GI Reservoir M\0"
        );
        ShowHelpMarker(
            "For HDR signals, displays a horizontal cross-section of the specified channel.\n"
            "The cross-section is taken in the middle of the screen, at the yellow line.\n"
            "Horizontal lines show the values in log scale: the yellow line in the middle is 1.0,\n"
            "above it are 10, 100, etc., and below it are 0.1, 0.01, etc.\n"
            "The yellow \"fire\" at the bottom is shown where the displayed value is 0.\n"
            "For confidence, shows a heat map with blue at full confidence and red at zero."
        );
        ImGui::Combo("Debug Render Target", (int*)&m_ui.debugRenderOutputBuffer,
            "LDR Color\0"
            "Depth\0"
            "GBufferDiffuseAlbedo\0"
            "GBufferSpecularRough\0"
            "GBufferNormals\0"
            "GBufferGeoNormals\0"
            "GBufferEmissive\0"
            "DiffuseLighting\0"
            "SpecularLighting\0"
            "DenoisedDiffuseLighting\0"
            "DenoisedSpecularLighting\0"
            "RestirLuminance\0"
            "PrevRestirLuminance\0"
            "DiffuseConfidence\0"
            "SpecularConfidence\0"
            "MotionVectors\0"
        );
        ImGui::PopItemWidth();

        ImGui::TreePop();
    }
}

#ifdef WITH_NRD
void UserInterface::DenoiserSettings()
{
    const nrd::LibraryDesc& nrdLibraryDesc = nrd::GetLibraryDesc();

    char s[128];
    snprintf(s, sizeof(s) - 1, "Denoising (NRD v%u.%u.%u)", nrdLibraryDesc.versionMajor, nrdLibraryDesc.versionMinor, nrdLibraryDesc.versionBuild);

    if (ImGui_ColoredTreeNode(s, c_ColorAttentionHeader))
    {
        ImGui::Checkbox("Enable Denoiser", &m_ui.enableDenoiser);

        if (m_ui.enableDenoiser)
        {
            ImGui::SameLine();
            ImGui::Checkbox("Advanced Settings", &m_showAdvancedDenoisingSettings);

            int useReLAX = (m_ui.denoisingMethod == nrd::Method::RELAX_DIFFUSE_SPECULAR) ? 1 : 0;
            ImGui::Combo("Denoiser", &useReLAX, "ReBLUR\0ReLAX\0");
            m_ui.denoisingMethod = useReLAX ? nrd::Method::RELAX_DIFFUSE_SPECULAR : nrd::Method::REBLUR_DIFFUSE_SPECULAR;
            
            ImGui::SameLine();
            if (ImGui::Button("Reset Settings"))
                m_ui.SetDefaultDenoiserSettings();

            ImGui::Separator();
            ImGui::PushItemWidth(160.f);
            ImGui::SliderFloat("Noise Mix-in", &m_ui.noiseMix, 0.f, 1.f);
            ImGui::PopItemWidth();
            ImGui::PushItemWidth(76.f);
            ImGui::SliderFloat("##noiseClampLow", &m_ui.noiseClampLow, 0.f, 1.f);
            ImGui::SameLine();
            ImGui::SliderFloat("Noise Clamp", &m_ui.noiseClampHigh, 1.f, 4.f);
            ImGui::PopItemWidth();

            ImGui::Separator();
            ImGui::Checkbox("Use Confidence Input", (bool*)&m_ui.lightingSettings.enableGradients);
            if (m_ui.lightingSettings.enableGradients && m_showAdvancedDenoisingSettings)
            {
                ImGui::SliderFloat("Gradient Sensitivity", &m_ui.lightingSettings.gradientSensitivity, 1.f, 20.f);
                ImGui::SliderFloat("Darkness Bias (EV)", &m_ui.lightingSettings.gradientLogDarknessBias, -16.f, -4.f);
                ImGui::SliderFloat("Confidence History Length", &m_ui.lightingSettings.confidenceHistoryLength, 0.f, 3.f);
            }

            if (m_showAdvancedDenoisingSettings)
            {
                ImGui::Separator();
                ImGui::PushItemWidth(160.f);
                if (useReLAX)
                {
                    ImGui::SliderInt("History length (frames)", (int*)&m_ui.relaxSettings.diffuseMaxAccumulatedFrameNum, 0, nrd::RELAX_MAX_HISTORY_FRAME_NUM);
                    ImGui::SliderInt("Fast history length (frames)", (int*)&m_ui.relaxSettings.diffuseMaxFastAccumulatedFrameNum, 0, nrd::RELAX_MAX_HISTORY_FRAME_NUM);
                    ImGui::Checkbox("Anti-firefly", &m_ui.relaxSettings.enableAntiFirefly);
                    ImGui::SameLine();
                    ImGui::Checkbox("Roughness edge stopping", &m_ui.relaxSettings.enableRoughnessEdgeStopping);
                    ImGui::Checkbox("Pre-pass", &m_ui.usePrePass);
                    ImGui::SameLine();
                    ImGui::Checkbox("Virtual history clamping", &m_ui.relaxSettings.enableSpecularVirtualHistoryClamping);

                    ImGui::Text("Reprojection:");
                    ImGui::SliderFloat("Spec variance boost", &m_ui.relaxSettings.specularVarianceBoost, 0.0f, 8.0f, "%.2f");
                    ImGui::SliderFloat("Clamping sigma scale", &m_ui.relaxSettings.historyClampingColorBoxSigmaScale, 0.0f, 10.0f, "%.1f");

                    ImGui::Text("Spatial filering:");
                    ImGui::SliderInt("A-trous iterations", (int32_t*)&m_ui.relaxSettings.atrousIterationNum, 2, 8);
                    ImGui::SliderFloat2("Diff-Spec luma weight", &m_ui.relaxSettings.diffusePhiLuminance, 0.0f, 10.0f, "%.1f");
                    ImGui::SetNextItemWidth( ImGui::CalcItemWidth() * 0.9f );
                    ImGui::SliderFloat3("Diff-Spec-Rough fraction", &m_ui.relaxSettings.diffuseLobeAngleFraction, 0.0f, 1.0f, "%.2f");
                    ImGui::SetNextItemWidth( ImGui::CalcItemWidth() * 0.9f );
                    ImGui::SliderFloat3("Luma-Normal-Rough relaxation", &m_ui.relaxSettings.luminanceEdgeStoppingRelaxation, 0.0f, 1.0f, "%.2f");
                    ImGui::SliderFloat("Spec lobe angle slack", &m_ui.relaxSettings.specularLobeAngleSlack, 0.0f, 89.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
                    ImGui::SliderFloat2("Diff-Spec min luma weight", &m_ui.relaxSettings.diffuseMinLuminanceWeight, 0.0f, 1.0f, "%.2f");
                    ImGui::SliderFloat("Depth threshold", &m_ui.relaxSettings.depthThreshold, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic);

                    ImGui::Text("History fix:");
                    ImGui::SliderFloat("Edge-stop normal power", &m_ui.relaxSettings.disocclusionFixEdgeStoppingNormalPower, 0.0f, 128.0f, "%.1f");
                    ImGui::SliderFloat("Max radius", &m_ui.relaxSettings.disocclusionFixMaxRadius, 0.0f, 100.0f, "%.1f");
                    ImGui::SliderInt("Frames to fix", (int32_t*)&m_ui.relaxSettings.disocclusionFixNumFramesToFix, 0, 10);

                    ImGui::Text("Spatial variance estimation:");
                    ImGui::SliderInt("History threshold", (int32_t*)&m_ui.relaxSettings.spatialVarianceEstimationHistoryThreshold, 0, 10);

                    m_ui.relaxSettings.diffusePrepassBlurRadius = m_ui.usePrePass ? 50.0f : 0.0f;
                    m_ui.relaxSettings.specularPrepassBlurRadius = m_ui.usePrePass ? 50.0f : 0.0f;
                }
                else
                {
                    ImGui::SliderInt("History length (frames)", (int*)&m_ui.reblurSettings.maxAccumulatedFrameNum, 0, nrd::REBLUR_MAX_HISTORY_FRAME_NUM);
                    ImGui::Checkbox("Anti-firefly", &m_ui.reblurSettings.enableAntiFirefly);
                    ImGui::SameLine();
                    ImGui::Checkbox("Performance mode", &m_ui.reblurSettings.enablePerformanceMode);
                    ImGui::Checkbox("Pre-pass", &m_ui.usePrePass);
                    ImGui::SameLine();
                    ImGui::Checkbox("Reference accum", &m_ui.reblurSettings.enableReferenceAccumulation);

                    ImGui::Text("Spatial filering:");
                    ImGui::SliderFloat("Input mix", &m_ui.reblurSettings.inputMix, 0.0f, 1.0f, "%.3f");
                    ImGui::SliderFloat("Blur base radius (px)", &m_ui.reblurSettings.blurRadius, 0.0f, 60.0f, "%.1f");
                    ImGui::SliderFloat("Min radius scale", &m_ui.reblurSettings.minConvergedStateBaseRadiusScale, 0.0f, 1.0f, "%.2f");
                    ImGui::SliderFloat("Max radius scale", &m_ui.reblurSettings.maxAdaptiveRadiusScale, 0.0f, 10.0f, "%.2f");
                    ImGui::SliderFloat("Lobe fraction", &m_ui.reblurSettings.lobeAngleFraction, 0.0f, 1.0f, "%.2f");
                    ImGui::SliderFloat("Roughness fraction", &m_ui.reblurSettings.roughnessFraction, 0.0f, 1.0f, "%.2f");
                    ImGui::SliderFloat("Stabilization strength", &m_ui.reblurSettings.stabilizationStrength, 0.0f, 1.0f, "%.2f");
                    ImGui::SliderFloat("History fix strength", &m_ui.reblurSettings.historyFixStrength, 0.0f, 1.0f, "%.2f");
                    ImGui::SetNextItemWidth( ImGui::CalcItemWidth() * 0.6f );
                    ImGui::SliderFloat("Responsive accum roughness threshold", &m_ui.reblurSettings.responsiveAccumulationRoughnessThreshold, 0.0f, 1.0f, "%.2f");

                    ImGui::Text("Anti-lag:");
                    ImGui::Checkbox("Intensity", &m_ui.reblurSettings.antilagIntensitySettings.enable);
                    ImGui::SameLine();
                    ImGui::Text("[%.1f%%; %.1f%%]", m_ui.reblurSettings.antilagIntensitySettings.thresholdMin * 100.0, m_ui.reblurSettings.antilagIntensitySettings.thresholdMax * 100.0);

                    ImGui::SameLine();
                    ImGui::Checkbox("Hit dist", &m_ui.reblurSettings.antilagHitDistanceSettings.enable);
                    ImGui::SameLine();
                    ImGui::Text("[%.1f%%; %.1f%%]", m_ui.reblurSettings.antilagHitDistanceSettings.thresholdMin * 100.0, m_ui.reblurSettings.antilagHitDistanceSettings.thresholdMax * 100.0);
                }
                ImGui::SliderFloat("Debug", &m_ui.debug, 0.0f, 1.0f, "%.5f");
                ImGui::PopItemWidth();
            }
        }

        ImGui::TreePop();
    }

    ImGui::Separator();
}
#endif

void UserInterface::CopySelectedLight() const
{
    Json::Value root(Json::objectValue);

    m_SelectedLight->Store(root);

    {
        auto n = m_SelectedLight->GetNode();
        auto trn = n->GetLocalToWorldTransform();
        donut::math::dquat rotation;
        donut::math::double3 scaling;
        donut::math::decomposeAffine<double>(trn, nullptr, &rotation, &scaling);
        root["translation"] << donut::math::float3(trn.m_translation);
        root["rotation"] << donut::math::double4(rotation.x, rotation.y, rotation.z, rotation.w);
    }

    Json::StreamWriterBuilder builder;
    builder.settings_["precision"] = 4;
    auto* writer = builder.newStreamWriter();

    std::stringstream ss;
    writer->write(root, &ss);

    glfwSetClipboardString(GetDeviceManager()->GetWindow(), ss.str().c_str());
}

void UserInterface::CopyCamera() const
{
    dm::float3 cameraPos = m_ui.resources->camera->GetPosition();
    dm::float3 cameraDir = m_ui.resources->camera->GetDir();

    std::stringstream text;
    text.precision(4);
    text << "\"position\": [" << cameraPos.x << ", " << cameraPos.y << ", " << cameraPos.z << "], ";
    text << "\"direction\": [" << cameraDir.x << ", " << cameraDir.y << ", " << cameraDir.z << "]";

    glfwSetClipboardString(GetDeviceManager()->GetWindow(), text.str().c_str());
}

static std::string getEnvironmentMapName(SampleScene& scene, const int index)
{
    if (index < 0 || index >= scene.GetEnvironmentMaps().size())
        return "None";

    const auto& environmentMapPath = scene.GetEnvironmentMaps()[index];

    if (environmentMapPath.empty())
        return "Procedural";

    return std::filesystem::path(environmentMapPath).stem().generic_string();
}

void UserInterface::SceneSettings()
{
    if (ImGui_ColoredTreeNode("Scene", c_ColorRegularHeader))
    {
        ImGui::Checkbox("##enableAnimations", (bool*)&m_ui.enableAnimations);
        ImGui::SameLine();
        ImGui::PushItemWidth(89.f);
        ImGui::SliderFloat("Animation Speed", &m_ui.animationSpeed, 0.f, 2.f);
        ImGui::PopItemWidth();

        m_ui.resetAccumulation |= ImGui::Checkbox("Alpha-Tested Geometry", (bool*)&m_ui.gbufferSettings.enableAlphaTestedGeometry);
        m_ui.resetAccumulation |= ImGui::Checkbox("Transparent Geometry", (bool*)&m_ui.gbufferSettings.enableTransparentGeometry);

        const auto& environmentMaps = m_ui.resources->scene->GetEnvironmentMaps();

        const std::string selectedEnvironmentMap = getEnvironmentMapName(*m_ui.resources->scene, m_ui.environmentMapIndex);

        ImGui::PushItemWidth(120.f);
        if (ImGui::BeginCombo("Environment", selectedEnvironmentMap.c_str()))
        {
            for (int index = -1; index < int(environmentMaps.size()); index++)
            {
                bool selected = (index == m_ui.environmentMapIndex);
                ImGui::Selectable(getEnvironmentMapName(*m_ui.resources->scene, index).c_str(), &selected);
                if (selected)
                {
                    if (index != m_ui.environmentMapIndex)
                    {
                        m_ui.environmentMapIndex = index;
                        m_ui.environmentMapDirty = 2;
                    }
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopItemWidth();
        m_ui.resetAccumulation |= ImGui::SliderFloat("Environment Bias (EV)", &m_ui.environmentIntensityBias, -8.f, 4.f);
        m_ui.resetAccumulation |= ImGui::SliderFloat("Environment Rotation (deg)", &m_ui.environmentRotation, -180.f, 180.f);

        {
            static float globalEmissiveFactor = 1.0f;
            bool changed;
            changed = ImGui::SliderFloat("Global Emissive Factor", &globalEmissiveFactor, 0.0f, 1.5f);
            m_ui.resetAccumulation |= changed;

            if (changed) {
                auto sceneGraph = m_ui.resources->scene->GetSceneGraph();

                for (auto& material : sceneGraph->GetMaterials())
                {
                    material->emissiveIntensity = globalEmissiveFactor;
                    material->dirty = true;
                }
            }
        }

        ImGui::TreePop();
    }

    ImGui::Separator();
    
    if (ImGui_ColoredTreeNode("Material Editor", c_ColorRegularHeader))
    {
        ImGui::Checkbox("##enableRoughnessOverride", &m_ui.gbufferSettings.enableRoughnessOverride);
        ImGui::SameLine();
        ImGui::PushItemWidth(89.f);
        ImGui::SliderFloat("Roughness Override", &m_ui.gbufferSettings.roughnessOverride, 0.f, 1.f);
        ImGui::PopItemWidth();

        ImGui::Checkbox("##enableMetalnessOverride", &m_ui.gbufferSettings.enableMetalnessOverride);
        ImGui::SameLine();
        ImGui::PushItemWidth(89.f);
        ImGui::SliderFloat("Metalness Override", &m_ui.gbufferSettings.metalnessOverride, 0.f, 1.f);
        ImGui::PopItemWidth();

        ImGui::SliderFloat("Normal Map Scale", &m_ui.gbufferSettings.normalMapScale, 0.f, 1.f);
        ImGui::SliderFloat("Texture LOD Bias", &m_ui.gbufferSettings.textureLodBias, -2.f, 2.f);

        bool resetSelection = false;
        if (m_ui.resources->selectedMaterial)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", m_ui.resources->selectedMaterial->name.c_str());
            ImGui::SameLine(0.f, 10.f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.f);
            if (ImGui::Button(" X "))
                resetSelection = true;
            ImGui::PopStyleVar();

            ImGui::PushItemWidth(200.f);
            bool materialChanged = donut::app::MaterialEditor(m_ui.resources->selectedMaterial.get(), false);
            ImGui::PopItemWidth();

            if (materialChanged)
                m_ui.resources->selectedMaterial->dirty = true;

            if (resetSelection)
                m_ui.resources->selectedMaterial.reset();
        }
        else
            ImGui::Text("Use RMB to select materials");

        ImGui::TreePop();
    }
    
    ImGui::Separator();

    if (ImGui_ColoredTreeNode("Light Editor", c_ColorRegularHeader))
    {
        if (ImGui::BeginCombo("Select Light", m_SelectedLight ? m_SelectedLight->GetName().c_str() : "(None)"))
        {
            for (const auto& light : m_ui.resources->scene->GetSceneGraph()->GetLights())
            {
                if (light->GetLightType() == LightType_Environment)
                    continue;

                bool selected = m_SelectedLight == light;
                ImGui::Selectable(light->GetName().c_str(), &selected);
                if (selected)
                {
                    m_SelectedLight = light;
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (m_SelectedLight)
        {
            ImGui::PushItemWidth(200.f);
            switch (m_SelectedLight->GetLightType())
            {
            case LightType_Directional:
            {
                engine::DirectionalLight& dirLight = static_cast<engine::DirectionalLight&>(*m_SelectedLight);
                if (app::LightEditor_Directional(dirLight))
                    m_ui.environmentMapDirty = 1;
                break;
            }
            case LightType_Spot:
            {
                SpotLightWithProfile& spotLight = static_cast<SpotLightWithProfile&>(*m_SelectedLight);
                app::LightEditor_Spot(spotLight);

                ImGui::PushItemWidth(150.f);
                if (ImGui::BeginCombo("IES Profile", spotLight.profileName.empty() ? "(none)" : spotLight.profileName.c_str()))
                {
                    bool selected = spotLight.profileName.empty();
                    if (ImGui::Selectable("(none)", &selected) && selected)
                    {
                        spotLight.profileName = "";
                        spotLight.profileTextureIndex = -1;
                    }

                    for (auto profile : m_ui.resources->iesProfiles)
                    {
                        selected = profile->name == spotLight.profileName;
                        if (ImGui::Selectable(profile->name.c_str(), &selected) && selected)
                        {
                            spotLight.profileName = profile->name;
                            spotLight.profileTextureIndex = -1;
                        }

                        if (selected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }

                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();

                if (ImGui::Button("Place Here"))
                {
                    spotLight.SetPosition(dm::double3(m_ui.resources->camera->GetPosition()));
                    spotLight.SetDirection(dm::double3(m_ui.resources->camera->GetDir()));
                }
                ImGui::SameLine();
                if (ImGui::Button("Camera to Light"))
                {
                    m_ui.resources->camera->LookAt(dm::float3(spotLight.GetPosition()), dm::float3(spotLight.GetPosition() + spotLight.GetDirection()));
                }
                break;
            }
            case LightType_Point:
            {
                engine::PointLight& pointLight = static_cast<engine::PointLight&>(*m_SelectedLight);
                ImGui::SliderFloat("Radius", &pointLight.radius, 0.f, 1.f, "%.2f");
                ImGui::ColorEdit3("Color", &pointLight.color.x, ImGuiColorEditFlags_Float);
                ImGui::SliderFloat("Intensity", &pointLight.intensity, 0.f, 100.f, "%.2f", ImGuiSliderFlags_Logarithmic);
                if (ImGui::Button("Place Here"))
                {
                    pointLight.SetPosition(dm::double3(m_ui.resources->camera->GetPosition()));
                }
                break;
            }
            case LightType_Cylinder:
            {
                CylinderLight& cylinderLight = static_cast<CylinderLight&>(*m_SelectedLight);
                ImGui::PushItemWidth(150.f);
                dm::float3 position = dm::float3(cylinderLight.GetPosition());
                if (ImGui::DragFloat3("Center", &position.x, 0.01f))
                    cylinderLight.SetPosition(dm::double3(position));
                ImGui::PopItemWidth();
                dm::double3 direction = cylinderLight.GetDirection();
                if (app::AzimuthElevationSliders(direction, true))
                    cylinderLight.SetDirection(direction);
                ImGui::SliderFloat("Radius", &cylinderLight.radius, 0.01f, 1.f, "%.3f", ImGuiSliderFlags_Logarithmic);
                ImGui::SliderFloat("Length", &cylinderLight.length, 0.01f, 10.f, "%.3f", ImGuiSliderFlags_Logarithmic);
                ImGui::SliderFloat("Flux", &cylinderLight.flux, 0.f, 100.f);
                ImGui::ColorEdit3("Color", &cylinderLight.color.x);
                if (ImGui::Button("Place Here"))
                {
                    cylinderLight.SetPosition(dm::double3(m_ui.resources->camera->GetPosition()));
                }
                break;
            }
            case LightType_Disk:
            {
                DiskLight& diskLight = static_cast<DiskLight&>(*m_SelectedLight);
                ImGui::PushItemWidth(150.f);
                dm::float3 position = dm::float3(diskLight.GetPosition());
                if (ImGui::DragFloat3("Center", &position.x, 0.01f))
                    diskLight.SetPosition(dm::double3(position));
                ImGui::PopItemWidth();
                dm::double3 direction = diskLight.GetDirection();
                if (app::AzimuthElevationSliders(direction, true))
                    diskLight.SetDirection(direction);
                ImGui::SliderFloat("Radius", &diskLight.radius, 0.01f, 1.f, "%.3f", ImGuiSliderFlags_Logarithmic);
                ImGui::SliderFloat("Flux", &diskLight.flux, 0.f, 100.f);
                ImGui::ColorEdit3("Color", &diskLight.color.x);
                if (ImGui::Button("Place Here"))
                {
                    diskLight.SetPosition(dm::double3(m_ui.resources->camera->GetPosition()));
                    diskLight.SetDirection(dm::double3(m_ui.resources->camera->GetDir()));
                }
                break;
            }
            case LightType_Rect:
            {
                RectLight& rectLight = static_cast<RectLight&>(*m_SelectedLight);
                ImGui::PushItemWidth(150.f);
                dm::float3 position = dm::float3(rectLight.GetPosition());
                if (ImGui::DragFloat3("Center", &position.x, 0.01f))
                    rectLight.SetPosition(dm::double3(position));
                ImGui::PopItemWidth();
                dm::double3 direction = rectLight.GetDirection();
                if (app::AzimuthElevationSliders(direction, true))
                    rectLight.SetDirection(direction);
                ImGui::SliderFloat("Width", &rectLight.width, 0.01f, 1.f, "%.3f", ImGuiSliderFlags_Logarithmic);
                ImGui::SliderFloat("Height", &rectLight.height, 0.01f, 1.f, "%.3f", ImGuiSliderFlags_Logarithmic);
                ImGui::SliderFloat("Flux", &rectLight.flux, 0.f, 100.f);
                ImGui::ColorEdit3("Color", &rectLight.color.x);
                if (ImGui::Button("Place Here"))
                {
                    rectLight.SetPosition(dm::double3(m_ui.resources->camera->GetPosition()));
                    rectLight.SetDirection(dm::double3(m_ui.resources->camera->GetDir()));
                }
                break;
            }
            }

            if (ImGui::Button("Copy as JSON"))
            {
                CopySelectedLight();
            }
            ImGui::PopItemWidth();
        }

        ImGui::TreePop();
    }

    ImGui::Separator();

    if (ImGui_ColoredTreeNode("Camera and Benchmark", c_ColorRegularHeader))
    {
        ImGui::SliderFloat("Camera vFOV", &m_ui.verticalFov, 10.f, 110.f);

        dm::float3 cameraPos = m_ui.resources->camera->GetPosition();
        ImGui::Text("Camera: %.2f %.2f %.2f", cameraPos.x, cameraPos.y, cameraPos.z);
        if (ImGui::Button("Copy Camera to Clipboard"))
        {
            CopyCamera();
        }

        if (m_ui.animationFrame.has_value())
        {
            if (ImGui::Button("Stop Benchmark"))
            {
                m_ui.animationFrame.reset();
            }
            else
            {
                ImGui::SameLine();
                ImGui::Text("Frame %d", m_ui.animationFrame.value());
            }
        }
        else
        {
            if (ImGui::Button("Start Benchmark"))
            {
                m_ui.animationFrame = std::optional<int>(0);
            }
        }

        ImGui::TreePop();
    }

    ImGui::Separator();

}

void UserInterface::buildUI()
{
    if (!m_ui.showUI)
        return;
    
    int width, height;
    GetDeviceManager()->GetWindowDimensions(width, height);

    if (m_ui.isLoading)
    {
        BeginFullScreenWindow();

        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        ImColor barColor = ImColor(1.f, 1.f, 1.f);
        ImVec2 frameTopLeft = ImVec2(200.f, float(height) * 0.5f - 30.f);
        ImVec2 frameBottomRight = ImVec2(float(width) - 200.f, float(height) * 0.5f + 30.f);
        draw_list->AddRect(frameTopLeft, frameBottomRight, barColor, 0.f, 15, 3.f);

        float frameMargin = 5.f;
        float barFullWidth = frameBottomRight.x - frameTopLeft.x - frameMargin * 2.f;
        float barWidth = barFullWidth * dm::saturate(m_ui.loadingPercentage);
        ImVec2 barTopLeft = ImVec2(frameTopLeft.x + frameMargin, frameTopLeft.y + frameMargin);
        ImVec2 barBottomRight = ImVec2(frameTopLeft.x + frameMargin + barWidth, frameBottomRight.y - frameMargin);
        draw_list->AddRectFilled(barTopLeft, barBottomRight, barColor);

        EndFullScreenWindow();

        return;
    }

    if (!m_ui.benchmarkResults.empty())
    {
        ImGui::SetNextWindowPos(ImVec2(float(width) * 0.5f, float(height) * 0.5f), 0, ImVec2(0.5f, 0.5f));
        ImGui::Begin("Benchmark Results", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("%s", m_ui.benchmarkResults.c_str());
        if (ImGui::Button("OK", ImVec2(130.f, 0.f)))
        {
            m_ui.benchmarkResults = "";
        }
        ImGui::SameLine();
        if (ImGui::Button("Copy", ImVec2(130.f, 0.f)))
        {
            glfwSetClipboardString(GetDeviceManager()->GetWindow(), m_ui.benchmarkResults.c_str());
        }
        ImGui::End();

        return;
    }

    ImGui::SetNextWindowPos(ImVec2(10.f, 10.f), 0, ImVec2(0.f, 0.f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(375.f, 0.f), ImVec2(float(width) - 20.f, float(height) - 20.f));
    if (ImGui::Begin("Settings (Tilde key to hide)", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::PushItemWidth(100.f);
        SceneSettings();
        GeneralRenderingSettings();
        SamplingSettings();
#ifdef WITH_NRD
        DenoiserSettings();
#endif
        PostProcessSettings();
        ImGui::PopItemWidth();
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(float(width) - 10.f, 10.f), 0, ImVec2(1.f, 0.f));
    if (ImGui::Begin("Performance", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::PushItemWidth(100.f);
        PerformanceWindow();
        ImGui::PopItemWidth();
    }
    ImGui::End();
}
