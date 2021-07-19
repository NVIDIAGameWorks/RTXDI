/***************************************************************************
 # Copyright (c) 2021, NVIDIA CORPORATION.  All rights reserved.
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

#include <json/writer.h>

using namespace donut;

UIData::UIData()
{
    rtxdiContextParams.ReGIR.Mode = rtxdi::ReGIRMode::Onion;
    
    taaParams.newFrameWeight = 0.04f;
    taaParams.maxRadiance = 200.f;
    taaParams.clampingFactor = 1.3f;

#ifdef WITH_NRD
    SetDefaultDenoiserSettings();
#endif
}

#ifdef WITH_NRD
void UIData::SetDefaultDenoiserSettings()
{
    reblurSettings = nrd::ReblurDiffuseSpecularSettings();
    relaxSettings = nrd::RelaxDiffuseSpecularSettings();

    reblurSettings.diffuseSettings.hitDistanceParameters.A = 1.0f;
    reblurSettings.diffuseSettings.antilagIntensitySettings.thresholdMin = 0.01f;
    reblurSettings.diffuseSettings.antilagIntensitySettings.thresholdMax = 0.1f;
    reblurSettings.diffuseSettings.antilagIntensitySettings.sigmaScale = 1.5f;
    reblurSettings.diffuseSettings.antilagIntensitySettings.sensitivityToDarkness = 0.25f;
    reblurSettings.diffuseSettings.antilagIntensitySettings.enable = true;
    reblurSettings.diffuseSettings.antilagHitDistanceSettings.thresholdMin = 0.01f;
    reblurSettings.diffuseSettings.antilagHitDistanceSettings.thresholdMax = 0.1f;
    reblurSettings.diffuseSettings.antilagHitDistanceSettings.sigmaScale = 1.5f;
    reblurSettings.diffuseSettings.antilagHitDistanceSettings.sensitivityToDarkness = 0.5f;
    reblurSettings.diffuseSettings.antilagHitDistanceSettings.enable = true;
    reblurSettings.diffuseSettings.maxAccumulatedFrameNum = 31;
    reblurSettings.diffuseSettings.maxFastAccumulatedFrameNum = 1;
    reblurSettings.diffuseSettings.blurRadius = 10.0f;
    reblurSettings.diffuseSettings.maxAdaptiveRadiusScale = 5.0f;
    reblurSettings.diffuseSettings.historyClampingColorBoxSigmaScale = 1.0f;
    reblurSettings.diffuseSettings.stabilizationStrength = 0.2f;
    reblurSettings.diffuseSettings.normalWeightStrictness = 0.33f;
    reblurSettings.diffuseSettings.antifirefly = true;
    reblurSettings.diffuseSettings.usePrePass = false;

    reblurSettings.specularSettings.hitDistanceParameters.C = 10.0f;
    reblurSettings.specularSettings.hitDistanceParameters.D = -25.0f;
    reblurSettings.specularSettings.antilagIntensitySettings = reblurSettings.diffuseSettings.antilagIntensitySettings;
    reblurSettings.specularSettings.antilagHitDistanceSettings = reblurSettings.diffuseSettings.antilagHitDistanceSettings;
    reblurSettings.specularSettings.maxAccumulatedFrameNum = reblurSettings.diffuseSettings.maxAccumulatedFrameNum;
    reblurSettings.specularSettings.maxFastAccumulatedFrameNum = reblurSettings.diffuseSettings.maxFastAccumulatedFrameNum;
    reblurSettings.specularSettings.blurRadius = reblurSettings.diffuseSettings.blurRadius;
    reblurSettings.specularSettings.maxAdaptiveRadiusScale = reblurSettings.diffuseSettings.maxAdaptiveRadiusScale;
    reblurSettings.specularSettings.historyClampingColorBoxSigmaScale = reblurSettings.diffuseSettings.historyClampingColorBoxSigmaScale;
    reblurSettings.specularSettings.stabilizationStrength = reblurSettings.diffuseSettings.stabilizationStrength;
    reblurSettings.specularSettings.normalWeightStrictness = reblurSettings.diffuseSettings.normalWeightStrictness;
    reblurSettings.specularSettings.antifirefly = reblurSettings.diffuseSettings.antifirefly;
    reblurSettings.specularSettings.usePrePass = reblurSettings.diffuseSettings.usePrePass;

    relaxSettings.diffuseMaxAccumulatedFrameNum = 31;
    relaxSettings.specularMaxAccumulatedFrameNum = 31;
    relaxSettings.diffuseMaxFastAccumulatedFrameNum = 1;
    relaxSettings.specularMaxFastAccumulatedFrameNum = 1;
    relaxSettings.historyClampingColorBoxSigmaScale = 1.0f;
    relaxSettings.disocclusionFixEdgeStoppingNormalPower = 1.0f;
    relaxSettings.disocclusionFixNumFramesToFix = 1;
    relaxSettings.diffusePhiLuminance = 1.0f;
    relaxSettings.specularPhiLuminance = 1.0f;
    relaxSettings.antifirefly = true;
}
#endif


UserInterface::UserInterface(app::DeviceManager* deviceManager, vfs::IFileSystem& rootFS, UIData& ui)
    : ImGui_Renderer(deviceManager)
    , m_ui(ui)
{
    m_FontOpenSans = LoadFont(rootFS, "/media/fonts/OpenSans/OpenSans-Regular.ttf", 17.f);

    m_Preset = QualityPreset::Medium;
    ApplyPreset();
}

void UserInterface::GeneralSettingsWindow()
{
    m_ui.resetAccumulation |= ImGui::Checkbox("Accumulation", &m_ui.enableAccumulation);

    if (m_ui.enableAccumulation)
    {
        ImGui::SameLine();
        ImGui::Text("%d frame(s)", m_ui.numAccumulatedFrames);
    }
    ImGui::PushItemWidth(50.f);
    m_ui.resetAccumulation |= ImGui::DragInt("Frames to Accumulate", (int*)&m_ui.framesToAccumulate, 1.f, 0, 1024);
    ImGui::PopItemWidth();

    m_ui.resetAccumulation |= ImGui::Checkbox("Freeze Random", &m_ui.freezeRandom);
    m_ui.resetAccumulation |= ImGui::Checkbox("Enable Textures", &m_ui.enableTextures);

    m_ui.resetAccumulation |= ImGui::Checkbox("##enablePixelJitter", &m_ui.enablePixelJitter);
    ImGui::SameLine();
    ImGui::PushItemWidth(69.f);
    m_ui.resetAccumulation |= ImGui::Combo("Pixel Jitter", (int*)&m_ui.temporalJitter, "MSAA\0Halton\0R2\0White Noise\0");
    ImGui::PopItemWidth();

    ImGui::Checkbox("Tone mapping", &m_ui.enableToneMapping);
    ImGui::Checkbox("Temporal AA", &m_ui.enableTAA);
    ImGui::Checkbox("Bloom", &m_ui.enableBloom);

    if (GetDevice()->queryFeatureSupport(nvrhi::Feature::RayQuery))
    {
        if (GetDevice()->queryFeatureSupport(nvrhi::Feature::RayTracingPipeline))
        {
            m_ui.reloadShaders |= ImGui::Checkbox("RayQuery", &m_ui.useRayQuery);
        }
        else
        {
            ImGui::Checkbox("RayQuery (No other options)", &m_ui.useRayQuery);
            m_ui.useRayQuery = true;
        }
    }
    else
    {
        ImGui::Checkbox("RayQuery (Not available)", &m_ui.useRayQuery);
        m_ui.useRayQuery = false;
    }

    ImGui::Checkbox("Rasterize G-Buffer", &m_ui.rasterizeGBuffer);

    ImGui::SliderFloat("Exposure bias", &m_ui.exposureBias, -4.f, 2.f);

    ImGui::Checkbox("##enableFpsLimit", &m_ui.enableFpsLimit);
    ImGui::SameLine();
    ImGui::PushItemWidth(69.f);
    ImGui::SliderInt("FPS Limit", (int*)&m_ui.fpsLimit, 1, 120);
    ImGui::PopItemWidth();

    if (ImGui::Button("Reload Shaders"))
    {
        m_ui.reloadShaders = true;
        m_ui.resetAccumulation = true;
    }
}

void UserInterface::PerformanceWindow()
{
    double frameTime = GetDeviceManager()->GetAverageFrameTimeSeconds();
    ImGui::Text("%.2f ms/frame (%.1f FPS)", m_ui.profiler->GetTimer(ProfilerSection::Frame), (frameTime > 0.0) ? 1.0 / frameTime : 0.0);

    ImGui::Checkbox("Count Rays", &m_ui.lightingSettings.enableRayCounts);

    m_ui.profiler->BuildUI(m_ui.lightingSettings.enableRayCounts);
}

void UserInterface::ApplyPreset()
{
    m_ui.lightingSettings = LightingPasses::RenderSettings();
    bool enableCheckerboardSampling = (m_ui.rtxdiContextParams.CheckerboardSamplingMode != rtxdi::CheckerboardMode::Off);

    switch (m_Preset)
    {
    case QualityPreset::Fast:
        enableCheckerboardSampling = true;
        m_ui.lightingSettings.numPrimaryRegirSamples = 0;
        m_ui.lightingSettings.numPrimaryLocalLightSamples = 4;
        m_ui.lightingSettings.numPrimaryInfiniteLightSamples = 1;
        m_ui.lightingSettings.enableReGIR = false;
        m_ui.lightingSettings.temporalBiasCorrection = RTXDI_BIAS_CORRECTION_OFF;
        m_ui.lightingSettings.spatialBiasCorrection = RTXDI_BIAS_CORRECTION_OFF;
        m_ui.lightingSettings.numSpatialSamples = 1;
        m_ui.lightingSettings.numDisocclusionBoostSamples = 2;
        m_ui.lightingSettings.discardInvisibleSamples = true;
        m_ui.lightingSettings.reuseFinalVisibility = true;
        m_ui.lightingSettings.enableBoilingFilter = true;
        break;

    case QualityPreset::Medium:
        enableCheckerboardSampling = false;
        m_ui.lightingSettings.numPrimaryRegirSamples = 8;
        m_ui.lightingSettings.numPrimaryLocalLightSamples = 8;
        m_ui.lightingSettings.numPrimaryInfiniteLightSamples = 2;
        m_ui.lightingSettings.enableReGIR = true;
        m_ui.lightingSettings.temporalBiasCorrection = RTXDI_BIAS_CORRECTION_BASIC;
        m_ui.lightingSettings.spatialBiasCorrection = RTXDI_BIAS_CORRECTION_BASIC;
        m_ui.lightingSettings.numSpatialSamples = 1;
        m_ui.lightingSettings.numDisocclusionBoostSamples = 8;
        m_ui.lightingSettings.discardInvisibleSamples = true;
        m_ui.lightingSettings.reuseFinalVisibility = true;
        m_ui.lightingSettings.enableBoilingFilter = true;
        break;

    case QualityPreset::Unbiased:
        enableCheckerboardSampling = false;
        m_ui.lightingSettings.numPrimaryRegirSamples = 16;
        m_ui.lightingSettings.numPrimaryLocalLightSamples = 8;
        m_ui.lightingSettings.numPrimaryInfiniteLightSamples = 2;
        m_ui.lightingSettings.enableReGIR = true;
        m_ui.lightingSettings.temporalBiasCorrection = RTXDI_BIAS_CORRECTION_RAY_TRACED;
        m_ui.lightingSettings.spatialBiasCorrection = RTXDI_BIAS_CORRECTION_RAY_TRACED;
        m_ui.lightingSettings.numSpatialSamples = 1;
        m_ui.lightingSettings.numDisocclusionBoostSamples = 8;
        m_ui.lightingSettings.discardInvisibleSamples = false;
        m_ui.lightingSettings.reuseFinalVisibility = false;
        m_ui.lightingSettings.enableBoilingFilter = false;
        break;

    case QualityPreset::Ultra:
        enableCheckerboardSampling = false;
        m_ui.lightingSettings.numPrimaryRegirSamples = 16;
        m_ui.lightingSettings.numPrimaryLocalLightSamples = 16;
        m_ui.lightingSettings.numPrimaryInfiniteLightSamples = 16;
        m_ui.lightingSettings.enableReGIR = true;
        m_ui.lightingSettings.temporalBiasCorrection = RTXDI_BIAS_CORRECTION_RAY_TRACED;
        m_ui.lightingSettings.spatialBiasCorrection = RTXDI_BIAS_CORRECTION_RAY_TRACED;
        m_ui.lightingSettings.numSpatialSamples = 4;
        m_ui.lightingSettings.numDisocclusionBoostSamples = 16;
        m_ui.lightingSettings.discardInvisibleSamples = false;
        m_ui.lightingSettings.reuseFinalVisibility = false;
        m_ui.lightingSettings.enableBoilingFilter = false;
        break;

    case QualityPreset::Reference:
        enableCheckerboardSampling = false;
        m_ui.lightingSettings.numPrimaryRegirSamples = 0;
        m_ui.lightingSettings.numPrimaryLocalLightSamples = 16;
        m_ui.lightingSettings.numPrimaryInfiniteLightSamples = 16;
        m_ui.lightingSettings.enableReGIR = false;
        m_ui.lightingSettings.enableTemporalResampling = false;
        m_ui.lightingSettings.enableSpatialResampling = false;
        m_ui.lightingSettings.enableBoilingFilter = false;
        break;

    default: ;
    }

    rtxdi::CheckerboardMode newCheckerboardMode = enableCheckerboardSampling ? rtxdi::CheckerboardMode::Black : rtxdi::CheckerboardMode::Off;
    if (newCheckerboardMode != m_ui.rtxdiContextParams.CheckerboardSamplingMode)
    {
        m_ui.rtxdiContextParams.CheckerboardSamplingMode = newCheckerboardMode;
        m_ui.resetRtxdiContext = true;
    }
}

void UserInterface::SamplingSettingsWindow()
{
    ImGui::PushItemWidth(200.f);
    m_ui.resetAccumulation |= ImGui::Combo("Render Mode", (int*)&m_ui.renderingMode, 
        "BRDF Direct Lighting\0"
        "ReSTIR Direct Lighting\0"
        "ReSTIR Direct + BRDF MIS\0"
        "ReSTIR Direct + BRDF Indirect\0");
    ImGui::PopItemWidth();

    if (ImGui::Combo("Preset", (int*)&m_Preset, "(Custom)\0Fast\0Medium\0Unbiased\0Ultra\0Reference\0"))
    {
        ApplyPreset();
        m_ui.resetAccumulation = true;
    }

    bool samplingSettingsChanged = false;

    ImGui::Checkbox("Show Advanced Settings", &m_showAdvancedSamplingSettings);
    
    if (m_ui.renderingMode == RenderingMode::ReStirDirectOnly || m_ui.renderingMode == RenderingMode::ReStirDirectBrdfMIS || m_ui.renderingMode == RenderingMode::ReStirDirectBrdfIndirect)
    {
        if (m_showAdvancedSamplingSettings)
        {
            samplingSettingsChanged |= ImGui::Checkbox("Importance Sample Local Lights", &m_ui.enableLocalLightImportanceSampling);
            samplingSettingsChanged |= ImGui::Checkbox("Importance Sample Env. Map", &m_ui.environmentMapImportanceSampling);
        }

        ImGui::Separator();

        if (ImGui::TreeNode("Screen Space Sampling"))
        {
            samplingSettingsChanged |= ImGui::Checkbox("Use Fused Kernel", &m_ui.lightingSettings.useFusedKernel);
            samplingSettingsChanged |= ImGui::SliderInt("Initial ReGIR Samples", (int*)&m_ui.lightingSettings.numPrimaryRegirSamples, 0, 32);
            samplingSettingsChanged |= ImGui::SliderInt("Initial Local Light Samples", (int*)&m_ui.lightingSettings.numPrimaryLocalLightSamples, 0, 32);
            samplingSettingsChanged |= ImGui::SliderInt("Initial Infinite Light Samples", (int*)&m_ui.lightingSettings.numPrimaryInfiniteLightSamples, 0, 32);
            samplingSettingsChanged |= ImGui::SliderInt("Initial Environment Samples", (int*)&m_ui.lightingSettings.numPrimaryEnvironmentSamples, 0, 32);
            samplingSettingsChanged |= ImGui::Checkbox("Enable Initial Visibility", &m_ui.lightingSettings.enableInitialVisibility);

            ImGui::Separator();
            if (!m_ui.lightingSettings.useFusedKernel)
            {
                samplingSettingsChanged |= ImGui::Checkbox("Enable Temporal Resampling", &m_ui.lightingSettings.enableTemporalResampling);
            }
            samplingSettingsChanged |= ImGui::Checkbox("Enable Previous Frame TLAS/BLAS", &m_ui.lightingSettings.enablePreviousTLAS);
            samplingSettingsChanged |= ImGui::Combo("Temporal Bias Correction", (int*)&m_ui.lightingSettings.temporalBiasCorrection, "Off\0Basic\0Ray Traced\0");
            if (m_showAdvancedSamplingSettings)
            {
                samplingSettingsChanged |= ImGui::SliderFloat("Temporal Depth Threshold", &m_ui.lightingSettings.temporalDepthThreshold, 0.f, 1.f);
                samplingSettingsChanged |= ImGui::SliderFloat("Temporal Normal Threshold", &m_ui.lightingSettings.temporalNormalThreshold, 0.f, 1.f);
            }
            samplingSettingsChanged |= ImGui::SliderInt("Max History Length", (int*)&m_ui.lightingSettings.maxHistoryLength, 1, 100);
            samplingSettingsChanged |= ImGui::Checkbox("##enableBoilingFilter", &m_ui.lightingSettings.enableBoilingFilter);
            ImGui::SameLine();
            ImGui::PushItemWidth(69.f);
            samplingSettingsChanged |= ImGui::SliderFloat("Boiling Filter", &m_ui.lightingSettings.boilingFilterStrength, 0.f, 1.f);
            ImGui::PopItemWidth();

            ImGui::Separator();
            samplingSettingsChanged |= ImGui::Checkbox("Enable Spatial Resampling", &m_ui.lightingSettings.enableSpatialResampling);
            if (!m_ui.lightingSettings.useFusedKernel)
            {
                samplingSettingsChanged |= ImGui::Combo("Spatial Bias Correction", (int*)&m_ui.lightingSettings.spatialBiasCorrection, "Off\0Basic\0Ray Traced\0");
            }
            samplingSettingsChanged |= ImGui::SliderInt("Spatial Samples", (int*)&m_ui.lightingSettings.numSpatialSamples, 1, 32);
            if (m_ui.lightingSettings.enableTemporalResampling || m_ui.lightingSettings.useFusedKernel)
            {
                samplingSettingsChanged |= ImGui::SliderInt("Disocclusion Boost Samples", (int*)&m_ui.lightingSettings.numDisocclusionBoostSamples, 1, 32);
            }
            samplingSettingsChanged |= ImGui::SliderFloat("Spatial Sampling Radius", &m_ui.lightingSettings.spatialSamplingRadius, 1.f, 32.f);
            if (m_showAdvancedSamplingSettings && !m_ui.lightingSettings.useFusedKernel)
            {
                samplingSettingsChanged |= ImGui::SliderFloat("Spatial Depth Threshold", &m_ui.lightingSettings.spatialDepthThreshold, 0.f, 1.f);
                samplingSettingsChanged |= ImGui::SliderFloat("Spatial Normal Threshold", &m_ui.lightingSettings.spatialNormalThreshold, 0.f, 1.f);
            }

            ImGui::Separator();
            samplingSettingsChanged |= ImGui::Checkbox("Enable Final Visibility", &m_ui.lightingSettings.enableFinalVisibility);
            samplingSettingsChanged |= ImGui::Checkbox("Discard Invisible Samples", &m_ui.lightingSettings.discardInvisibleSamples);
            samplingSettingsChanged |= ImGui::Checkbox("Reuse Final Visibility", &m_ui.lightingSettings.reuseFinalVisibility);

            if (m_ui.lightingSettings.reuseFinalVisibility && m_showAdvancedSamplingSettings)
            {
                samplingSettingsChanged |= ImGui::SliderFloat("Final Visibility - Max Distance", &m_ui.lightingSettings.finalVisibilityMaxDistance, 0.f, 32.f);
                samplingSettingsChanged |= ImGui::SliderInt("Final Visibility - Max Age", (int*)&m_ui.lightingSettings.finalVisibilityMaxAge, 0, 16);
            }

            if (m_ui.renderingMode == RenderingMode::ReStirDirectBrdfIndirect)
            {
                ImGui::Separator();
                samplingSettingsChanged |= ImGui::SliderInt("Indirect ReGIR Samples", (int*)&m_ui.lightingSettings.numIndirectRegirSamples, 0, 32);
                samplingSettingsChanged |= ImGui::SliderInt("Indirect Local Light Samples", (int*)&m_ui.lightingSettings.numIndirectLocalLightSamples, 0, 32);
                samplingSettingsChanged |= ImGui::SliderInt("Indirect Inifinite Light Samples", (int*)&m_ui.lightingSettings.numIndirectInfiniteLightSamples, 0, 32);
                samplingSettingsChanged |= ImGui::SliderInt("Indirect Environment Samples", (int*)&m_ui.lightingSettings.numIndirectEnvironmentSamples, 0, 32);
            }

            ImGui::TreePop();
        }

        if (ImGui::TreeNode("ReGIR Settings"))
        {
            samplingSettingsChanged |= ImGui::Checkbox("Use ReGIR", &m_ui.lightingSettings.enableReGIR);
            samplingSettingsChanged |= ImGui::SliderFloat("Cell Size", &m_ui.regirCellSize, 0.1f, 4.f);
            samplingSettingsChanged |= ImGui::SliderInt("Grid Build Samples", (int*)&m_ui.lightingSettings.numRegirBuildSamples, 0, 32);
            samplingSettingsChanged |= ImGui::SliderFloat("Sampling Jitter", &m_ui.regirSamplingJitter, 0.0f, 2.f);

            ImGui::Checkbox("Freeze Position", &m_ui.freezeRegirPosition);
            ImGui::SameLine(0.f, 10.f);
            ImGui::Checkbox("Visualize Cells", &m_ui.lightingSettings.visualizeRegirCells);

            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Context Settings"))
        {
            bool enableCheckerboardSampling = (m_ui.rtxdiContextParams.CheckerboardSamplingMode != rtxdi::CheckerboardMode::Off);
            ImGui::Checkbox("Checkerboard Rendering", &enableCheckerboardSampling);
            m_ui.rtxdiContextParams.CheckerboardSamplingMode = enableCheckerboardSampling ? rtxdi::CheckerboardMode::Black : rtxdi::CheckerboardMode::Off;
            
            ImGui::Separator();
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

            ImGui::Text("Total Cells: %d", m_ui.regirLightSlotCount / m_ui.rtxdiContextParams.ReGIR.LightsPerCell);

            if (ImGui::Button("Apply"))
                m_ui.resetRtxdiContext = true;

            ImGui::TreePop();
        }
    }

    if (samplingSettingsChanged)
    {
        m_Preset = QualityPreset::Custom;
        m_ui.resetAccumulation = true;
    }
}

#ifdef WITH_NRD
void UserInterface::DenoiserSettingsWindow()
{
    ImGui::Checkbox("Enable Denoiser", &m_ui.enableDenoiser);

    if (!m_ui.enableDenoiser)
        return;

    int useReLAX = (m_ui.denoisingMethod == nrd::Method::RELAX_DIFFUSE_SPECULAR) ? 1 : 0;
    ImGui::Combo("Denoising Method", &useReLAX, "ReBLUR\0ReLAX\0");
    m_ui.denoisingMethod = useReLAX ? nrd::Method::RELAX_DIFFUSE_SPECULAR : nrd::Method::REBLUR_DIFFUSE_SPECULAR;
    ImGui::Checkbox("Show Advanced Settings", &m_showAdvancedDenoisingSettings);
    ImGui::SameLine(0.f, 20.f);
    if (ImGui::Button("Reset"))
    {
        m_ui.SetDefaultDenoiserSettings();
    }
    ImGui::Separator();

    if (useReLAX)
    {
        ImGui::SliderInt("Max Accumulated Frame Num", (int*)&m_ui.relaxSettings.specularMaxAccumulatedFrameNum, 0, 63);
        ImGui::SliderInt("Max Fast Accumulated Frame Num", (int*)&m_ui.relaxSettings.specularMaxFastAccumulatedFrameNum, 0, 63);
        m_ui.relaxSettings.diffuseMaxAccumulatedFrameNum = m_ui.relaxSettings.specularMaxAccumulatedFrameNum;
        m_ui.relaxSettings.diffuseMaxFastAccumulatedFrameNum = m_ui.relaxSettings.specularMaxFastAccumulatedFrameNum;
        if (m_showAdvancedDenoisingSettings)
        {
            ImGui::Checkbox("Bicubic Reprojection Filter", &m_ui.relaxSettings.bicubicFilterForReprojectionEnabled);
            ImGui::SliderFloat("Specular Variance Boost", &m_ui.relaxSettings.specularVarianceBoost, 0.f, 1.f);
        }
        ImGui::Separator();
        ImGui::SliderInt("Disocclusion Frames To Fix", (int*)&m_ui.relaxSettings.disocclusionFixNumFramesToFix, 0, 10);
        ImGui::SliderFloat("Disocclusion Fix Radius", &m_ui.relaxSettings.disocclusionFixMaxRadius, 0.f, 64.f);
        if (m_showAdvancedDenoisingSettings)
        {
            ImGui::SliderFloat("Disocclusion Edge Normal Power", &m_ui.relaxSettings.disocclusionFixEdgeStoppingNormalPower, 0.f, 32.f);
        }
        ImGui::Separator();
        ImGui::Checkbox("Firefly Filter", &m_ui.relaxSettings.antifirefly);
        if (m_showAdvancedDenoisingSettings)
        {
            ImGui::SliderFloat("History Clamping Box Scale", &m_ui.relaxSettings.historyClampingColorBoxSigmaScale, 0.f, 3.f);
            ImGui::SliderInt("Spatial Variance History Threshold", (int*)&m_ui.relaxSettings.spatialVarianceEstimationHistoryThreshold, 0, 10);
        }
        ImGui::Separator();
        ImGui::SliderInt("A-Trous Filter Iterations", (int*)&m_ui.relaxSettings.atrousIterationNum, 0, 10);
        ImGui::SliderFloat("Specular Phi Luminance", &m_ui.relaxSettings.specularPhiLuminance, 0.f, 10.f);
        ImGui::SliderFloat("Diffuse Phi Luminance", &m_ui.relaxSettings.diffusePhiLuminance, 0.f, 10.f);
        if (m_showAdvancedDenoisingSettings)
        {
            ImGui::SliderFloat("Phi Normal", &m_ui.relaxSettings.phiNormal, 0.f, 128.f);
            ImGui::SliderFloat("Phi Depth", &m_ui.relaxSettings.phiDepth, 0.f, 1.f);
            ImGui::SliderFloat("Edge Relaxation - Roughness", &m_ui.relaxSettings.roughnessEdgeStoppingRelaxation, 0.f, 1.f);
            ImGui::SliderFloat("Edge Relaxation - Normal", &m_ui.relaxSettings.normalEdgeStoppingRelaxation, 0.f, 1.f);
            ImGui::SliderFloat("Edge Relaxation - Luminance", &m_ui.relaxSettings.luminanceEdgeStoppingRelaxation, 0.f, 1.f);
        }
    }
    else
    {
        ImGui::SliderInt("Max Accumulated Frames", (int*)&m_ui.reblurSettings.diffuseSettings.maxAccumulatedFrameNum, 0, 63);
        ImGui::SliderInt("Max Fast Accumulated Frames", (int*)&m_ui.reblurSettings.diffuseSettings.maxFastAccumulatedFrameNum, 0, 63);
        ImGui::SliderFloat("Blur Radius", &m_ui.reblurSettings.diffuseSettings.blurRadius, 0.f, 64.f);
        ImGui::SliderFloat("Adaptive Radius Scale", &m_ui.reblurSettings.diffuseSettings.maxAdaptiveRadiusScale, 0.f, 10.f);
        ImGui::Checkbox("Anti-Firefly", &m_ui.reblurSettings.diffuseSettings.antifirefly);
        ImGui::Checkbox("Pre-pass", &m_ui.reblurSettings.diffuseSettings.usePrePass);
        if (m_showAdvancedDenoisingSettings)
        {
            ImGui::SliderFloat("History Clamping Box Scale", &m_ui.reblurSettings.diffuseSettings.historyClampingColorBoxSigmaScale, 0.f, 3.f);
            ImGui::SliderFloat("Stabilization Amount", &m_ui.reblurSettings.diffuseSettings.stabilizationStrength, 0.f, 1.f);
            ImGui::SliderFloat("Normal Weight Strictness", &m_ui.reblurSettings.diffuseSettings.normalWeightStrictness, 0.f, 1.f);
        }
        ImGui::Separator();

        ImGui::Checkbox("Enable Intensity Anti-Lag", &m_ui.reblurSettings.diffuseSettings.antilagIntensitySettings.enable);
        if (m_showAdvancedDenoisingSettings)
        {
            ImGui::SliderFloat("Intensity Threshold Min", &m_ui.reblurSettings.diffuseSettings.antilagIntensitySettings.thresholdMin, 0.f, 1.f, "%.3f");
            ImGui::SliderFloat("Intensity Threshold Max", &m_ui.reblurSettings.diffuseSettings.antilagIntensitySettings.thresholdMax, 0.f, 1.f, "%.3f");
            ImGui::SliderFloat("Intensity Sigma Scale", &m_ui.reblurSettings.diffuseSettings.antilagIntensitySettings.sigmaScale, 0.f, 2.f);
            ImGui::SliderFloat("Intensity Sensitivity To Darkness", &m_ui.reblurSettings.diffuseSettings.antilagIntensitySettings.sensitivityToDarkness, 0.f, 1.f);
        }
        m_ui.reblurSettings.specularSettings.antilagIntensitySettings = m_ui.reblurSettings.diffuseSettings.antilagIntensitySettings;

        ImGui::Checkbox("Enable Hit Distance Anti-Lag", &m_ui.reblurSettings.diffuseSettings.antilagHitDistanceSettings.enable);
        if (m_showAdvancedDenoisingSettings)
        {
            ImGui::SliderFloat("Hit Distance Threshold Min", &m_ui.reblurSettings.diffuseSettings.antilagHitDistanceSettings.thresholdMin, 0.f, 1.f, "%.3f");
            ImGui::SliderFloat("Hit Distance Threshold Max", &m_ui.reblurSettings.diffuseSettings.antilagHitDistanceSettings.thresholdMax, 0.f, 1.f, "%.3f");
            ImGui::SliderFloat("Hit Distance Sigma Scale", &m_ui.reblurSettings.diffuseSettings.antilagHitDistanceSettings.sigmaScale, 0.f, 2.f);
            ImGui::SliderFloat("Hit Distance Sensitivity To Darkness", &m_ui.reblurSettings.diffuseSettings.antilagHitDistanceSettings.sensitivityToDarkness, 0.f, 1.f);
        }
        m_ui.reblurSettings.specularSettings.antilagHitDistanceSettings = m_ui.reblurSettings.diffuseSettings.antilagHitDistanceSettings;

        m_ui.reblurSettings.specularSettings.maxAccumulatedFrameNum = m_ui.reblurSettings.diffuseSettings.maxAccumulatedFrameNum;
        m_ui.reblurSettings.specularSettings.maxFastAccumulatedFrameNum = m_ui.reblurSettings.diffuseSettings.maxFastAccumulatedFrameNum;
        m_ui.reblurSettings.specularSettings.blurRadius = m_ui.reblurSettings.diffuseSettings.blurRadius;
        m_ui.reblurSettings.specularSettings.maxAdaptiveRadiusScale = m_ui.reblurSettings.diffuseSettings.maxAdaptiveRadiusScale;
        m_ui.reblurSettings.specularSettings.historyClampingColorBoxSigmaScale = m_ui.reblurSettings.diffuseSettings.historyClampingColorBoxSigmaScale;
        m_ui.reblurSettings.specularSettings.stabilizationStrength = m_ui.reblurSettings.diffuseSettings.stabilizationStrength;
        m_ui.reblurSettings.specularSettings.normalWeightStrictness = m_ui.reblurSettings.diffuseSettings.normalWeightStrictness;
        m_ui.reblurSettings.specularSettings.antifirefly = m_ui.reblurSettings.diffuseSettings.antifirefly;
        m_ui.reblurSettings.specularSettings.usePrePass = m_ui.reblurSettings.diffuseSettings.usePrePass;

        if (m_showAdvancedDenoisingSettings)
        {
            ImGui::Separator();
            ImGui::SliderFloat("Diffuse distance Normalization", &m_ui.reblurSettings.diffuseSettings.hitDistanceParameters.A, 0.1f, 10.f, "%.2f");
            ImGui::SliderFloat("Specular distance Normalization", &m_ui.reblurSettings.specularSettings.hitDistanceParameters.A, 0.1f, 10.f, "%.2f");
        }
    }
}
#endif

void UserInterface::CopySelectedLight() const
{
    Json::Value root(Json::objectValue);

    m_SelectedLight->Store(root);

    Json::StreamWriterBuilder builder;
    builder.settings_["precision"] = 4;
    auto* writer = builder.newStreamWriter();

    std::stringstream ss;
    writer->write(root, &ss);

    glfwSetClipboardString(GetDeviceManager()->GetWindow(), ss.str().c_str());
}

void UserInterface::CopyCamera() const
{
    dm::float3 cameraPos = m_ui.camera->GetPosition();
    dm::float3 cameraDir = m_ui.camera->GetDir();

    std::stringstream text;
    text.precision(4);
    text << "\"position\": [" << cameraPos.x << ", " << cameraPos.y << ", " << cameraPos.z << "], ";
    text << "\"direction\": [" << cameraDir.x << ", " << cameraDir.y << ", " << cameraDir.z << "]";

    glfwSetClipboardString(GetDeviceManager()->GetWindow(), text.str().c_str());
}

static std::string getEnvironmentMapName(SampleScene& scene, const int index)
{
    if (index < 0)
        return "None";
    const auto& environmentMapPath = scene.GetEnvironmentMaps()[index];

    if (environmentMapPath.empty())
        return "Procedural";

    return std::filesystem::path(environmentMapPath).stem().generic_string();
}

void UserInterface::SceneSettingsWindow()
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
    
    ImGui::Checkbox("##enableAnimations", &m_ui.enableAnimations);
    ImGui::SameLine();
    ImGui::PushItemWidth(89.f);
    ImGui::SliderFloat("Animation Speed", &m_ui.animationSpeed, 0.f, 2.f);
    ImGui::PopItemWidth();
    
    m_ui.resetAccumulation |= ImGui::Checkbox("Alpha-Tested Geometry", &m_ui.gbufferSettings.enableAlphaTestedGeometry);
    m_ui.resetAccumulation |= ImGui::Checkbox("Transparent Geometry", &m_ui.gbufferSettings.enableTransparentGeometry);

    const auto& environmentMaps = m_ui.scene->GetEnvironmentMaps();

    const std::string selectedEnvironmentMap = getEnvironmentMapName(*m_ui.scene, m_ui.environmentMapIndex);

    ImGui::PushItemWidth(120.f);
    if (ImGui::BeginCombo("Environment", selectedEnvironmentMap.c_str()))
    {
        for (int index = -1; index < int(environmentMaps.size()); index++)
        {
            bool selected = (index == m_ui.environmentMapIndex);
            ImGui::Selectable(getEnvironmentMapName(*m_ui.scene, index).c_str(), &selected);
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


    dm::float3 cameraPos = m_ui.camera->GetPosition();
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

    ImGui::Separator();
    if (m_ui.selectedMaterial)
    {
        ImGui::Text("%s", m_ui.selectedMaterial->name.c_str());

        ImGui::PushItemWidth(200.f);
        bool materialChanged = donut::app::MaterialEditor(m_ui.selectedMaterial.get(), false);
        ImGui::PopItemWidth();

        if (materialChanged)
            m_ui.selectedMaterial->dirty = true;
    }
    else
        ImGui::Text("Use RMB to select materials");
    
    ImGui::Separator();
    if (ImGui::BeginCombo("Select Light", m_SelectedLight ? m_SelectedLight->GetName().c_str() : "(None)"))
    {
        for (const auto& light : m_ui.scene->GetSceneGraph()->GetLights())
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

                for (auto profile : m_ui.iesProfiles)
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
                spotLight.SetPosition(dm::double3(m_ui.camera->GetPosition()));
                spotLight.SetDirection(dm::double3(m_ui.camera->GetDir()));
            }
            ImGui::SameLine();
            if (ImGui::Button("Camera to Light"))
            {
                m_ui.camera->LookAt(dm::float3(spotLight.GetPosition()), dm::float3(spotLight.GetPosition() + spotLight.GetDirection()));
            }
            break;
        }
        case LightType_Point:
        {
            engine::PointLight& pointLight = static_cast<engine::PointLight&>(*m_SelectedLight);
            app::LightEditor_Point(pointLight);
            if (ImGui::Button("Place Here"))
            {
                pointLight.SetPosition(dm::double3(m_ui.camera->GetPosition()));
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
                cylinderLight.SetPosition(dm::double3(m_ui.camera->GetPosition()));
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
                diskLight.SetPosition(dm::double3(m_ui.camera->GetPosition()));
                diskLight.SetDirection(dm::double3(m_ui.camera->GetDir()));
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
                rectLight.SetPosition(dm::double3(m_ui.camera->GetPosition()));
                rectLight.SetDirection(dm::double3(m_ui.camera->GetDir()));
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

    ImGui::SetNextWindowPos(ImVec2(10.f, float(height) - 10.f), 0, ImVec2(0.f, 1.f));
    if (ImGui::Begin("Rendering", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::PushItemWidth(100.f);
        GeneralSettingsWindow();
        ImGui::PopItemWidth();
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(10.f, 10.f), 0, ImVec2(0.f, 0.f));
    if (ImGui::Begin("Sampling", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::PushItemWidth(100.f);
        SamplingSettingsWindow();
        ImGui::PopItemWidth();
    }
    ImGui::End();

#ifdef WITH_NRD
    ImGui::SetNextWindowPos(ImVec2(float(width) - 10.f, 10.f), 0, ImVec2(1.f, 0.f));
    if (ImGui::Begin("Denoising", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::PushItemWidth(100.f);
        DenoiserSettingsWindow();
        ImGui::PopItemWidth();
    }
    ImGui::End();
#endif

    ImGui::SetNextWindowPos(ImVec2(float(width) - 10.f, float(height) - 10.f), 0, ImVec2(1.f, 1.f));
    if (ImGui::Begin("Performance", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::PushItemWidth(100.f);
        PerformanceWindow();
        ImGui::PopItemWidth();
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(345.f, 10.f), 0, ImVec2(0.f, 0.f));
    if (ImGui::Begin("Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::PushItemWidth(100.f);
        SceneSettingsWindow();
        ImGui::PopItemWidth();
    }
    ImGui::End();
}
