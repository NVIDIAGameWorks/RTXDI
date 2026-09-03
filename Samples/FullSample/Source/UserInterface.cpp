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
#include "Scene/Lights.h"
#include "Scene/SampleScene.h"

#include <donut/engine/IesProfile.h>
#include <donut/app/Camera.h>
#include <donut/app/UserInterfaceUtils.h>
#include <donut/core/json.h>

#include "SharedShaderInclude/ShaderDebug/ShaderDebugPrintShared.h"
#include "SharedShaderInclude/ShaderDebug/PTPathViz/PTPathSetRecord.h"

using namespace donut;

uint32_t LocalLightSamplingUIData::GetSelectedLocalLightSampleCount()
{
    switch (localLightSamplingMode)
    {
    default:
    case ReSTIRDI_LocalLightSamplingMode::Uniform:
        return numLocalLightUniformSamples;
    case ReSTIRDI_LocalLightSamplingMode::Power_RIS:
        return numLocalLightPowerRISSamples;
    case ReSTIRDI_LocalLightSamplingMode::ReGIR_RIS:
        return numLocalLightReGIRRISSamples;
    }
}

UIData::UIData()
{
    taaParams.newFrameWeight = 0.04f;
    taaParams.maxRadiance = 200.f;
    taaParams.clampingFactor = 1.3f;

    restirDI.resamplingMode = rtxdi::ReSTIRDI_ResamplingMode::TemporalAndSpatial;
    restirDI.initialSamplingParams = rtxdi::GetDefaultReSTIRDIInitialSamplingParams();
    restirDI.temporalResamplingParams = rtxdi::GetDefaultReSTIRDITemporalResamplingParams();
    restirDI.boilingFilter = rtxdi::GetDefaultReSTIRDIBoilingFilterParams();
    restirDI.spatialResamplingParams = rtxdi::GetDefaultReSTIRDISpatialResamplingParams();
    restirDI.spatioTemporalResamplingParams = rtxdi::GetDefaultReSTIRDISpatioTemporalResamplingParams();
    restirDI.shadingParams = rtxdi::GetDefaultReSTIRDIShadingParams();

    restirGI.resamplingMode = rtxdi::ReSTIRGI_ResamplingMode::TemporalAndSpatial;
    restirGI.temporalResamplingParams = rtxdi::GetDefaultReSTIRGITemporalResamplingParams();
    restirGI.boilingFilter = rtxdi::GetDefaultReSTIRGIBoilingFilterParams();
    restirGI.spatialResamplingParams = rtxdi::GetDefaultReSTIRGISpatialResamplingParams();
    restirGI.finalShadingParams = rtxdi::GetDefaultReSTIRGIFinalShadingParams();

    restirPT.resamplingMode = rtxdi::ReSTIRPT_ResamplingMode::TemporalAndSpatial;
    restirPT.initialSampling = rtxdi::GetDefaultReSTIRPTInitialSamplingParams();
    restirPT.decorrelation = rtxdi::GetDefaultReSTIRPTDecorrelationParams();
    restirPT.temporalResampling = rtxdi::GetDefaultReSTIRPTTemporalResamplingParams();
    restirPT.hybridShift = rtxdi::GetDefaultReSTIRPTHybridShiftParams();
    restirPT.reconnection = rtxdi::GetDefaultReSTIRPTReconnectionParameters();
    restirPT.boilingFilter = rtxdi::GetDefaultReSTIRPTBoilingFilterParams();
    restirPT.spatialResampling = rtxdi::GetDefaultReSTIRPTSpatialResamplingParams();
    restirPT.hybridMirrorInitial = true;

    restirPT.neeLocalLightSampling.localLightSamplingMode = ReSTIRDI_LocalLightSamplingMode::Uniform;

    debugOutputSettings.ptPathViz.paths = {};
    debugOutputSettings.ptPathViz.paths.initial.color = float3(1.0f, 1.0f, 1.0f); // white
    debugOutputSettings.ptPathViz.paths.initial.enabled = 1;
    debugOutputSettings.ptPathViz.paths.temporalRetrace.color = float3(0.0f, 0.0f, 0.0f);
    debugOutputSettings.ptPathViz.paths.temporalRetrace.enabled = 0;
    debugOutputSettings.ptPathViz.paths.temporalShift.color = float3(0.0f, 0.0f, 1.0f);
    debugOutputSettings.ptPathViz.paths.temporalShift.enabled = 0;
    debugOutputSettings.ptPathViz.paths.temporalInverseShift.color = float3(0.15f, 0.15f, 0.9f);
    debugOutputSettings.ptPathViz.paths.temporalInverseShift.enabled = 0;
    debugOutputSettings.ptPathViz.paths.spatialRetrace.color = float3(0.333f, 0.333f, 0.333f);
    debugOutputSettings.ptPathViz.paths.spatialRetrace.enabled = 0;
    debugOutputSettings.ptPathViz.paths.spatialShift.color = float3(1.0f, 0.0f, 0.0f);
    debugOutputSettings.ptPathViz.paths.spatialShift.enabled = 0;
    debugOutputSettings.ptPathViz.paths.spatialInverseShift.color = float3(0.9f, 0.15f, 0.15f);
    debugOutputSettings.ptPathViz.paths.spatialInverseShift.enabled = 0;
    debugOutputSettings.ptPathViz.paths.normal.color = float3(0.0f, 1.0f, 0.0f); // green
    debugOutputSettings.ptPathViz.paths.normal.enabled = 1;
    debugOutputSettings.ptPathViz.paths.nee.color = float3(1.0f, 0.0f, 1.0f); // purple
    debugOutputSettings.ptPathViz.paths.nee.enabled = 1;

    ApplyPreset();
    ApplyReSTIRPTPreset();

#ifdef WITH_NRD
    SetDefaultDenoiserSettings();
#endif
}

void UIData::ApplyPreset()
{
    bool enableCheckerboardSampling = (restirDIStaticParams.CheckerboardSamplingMode != rtxdi::CheckerboardMode::Off);

    switch (preset)
    {
    case QualityPreset::Fast:
        enableCheckerboardSampling = true;
        restirDI.resamplingMode = rtxdi::ReSTIRDI_ResamplingMode::TemporalAndSpatial;
        restirDI.initialSamplingParams.localLightSamplingMode = ReSTIRDI_LocalLightSamplingMode::Power_RIS;
        restirDI.neeLocalLightSampling.numLocalLightUniformSamples = 4;
        restirDI.neeLocalLightSampling.numLocalLightPowerRISSamples = 4;
        restirDI.neeLocalLightSampling.numLocalLightReGIRRISSamples = 4;
        restirDI.initialSamplingParams.numLocalLightSamples = restirDI.neeLocalLightSampling.numLocalLightPowerRISSamples;
        restirDI.initialSamplingParams.numBrdfSamples = 0;
        restirDI.initialSamplingParams.numInfiniteLightSamples = 1;
        restirDI.temporalResamplingParams.enableVisibilityShortcut = true;
        restirDI.boilingFilter.enableBoilingFilter = true;
        restirDI.boilingFilter.boilingFilterStrength = 0.2f;
        restirDI.temporalResamplingParams.biasCorrectionMode = ReSTIRDI_TemporalBiasCorrectionMode::Off;
        restirDI.spatialResamplingParams.biasCorrectionMode = ReSTIRDI_SpatialBiasCorrectionMode::Off;
        restirDI.spatialResamplingParams.numSamples = 1;
        restirDI.spatialResamplingParams.numDisocclusionBoostSamples = 2;
        restirDI.shadingParams.reuseFinalVisibility = true;
        lightingSettings.brdfptParams.enableSecondaryResampling = false;
        lightingSettings.enableGradients = false;
        break;

    case QualityPreset::Medium:
        enableCheckerboardSampling = false;
        restirDI.resamplingMode = rtxdi::ReSTIRDI_ResamplingMode::TemporalAndSpatial;
        restirDI.initialSamplingParams.localLightSamplingMode = ReSTIRDI_LocalLightSamplingMode::ReGIR_RIS;
        restirDI.neeLocalLightSampling.numLocalLightUniformSamples = 8;
        restirDI.neeLocalLightSampling.numLocalLightPowerRISSamples = 8;
        restirDI.neeLocalLightSampling.numLocalLightReGIRRISSamples = 8;
        restirDI.initialSamplingParams.numLocalLightSamples = restirDI.neeLocalLightSampling.numLocalLightReGIRRISSamples;
        restirDI.initialSamplingParams.numBrdfSamples = 1;
        restirDI.initialSamplingParams.numInfiniteLightSamples = 1;
        restirDI.temporalResamplingParams.enableVisibilityShortcut = true;
        restirDI.boilingFilter.enableBoilingFilter = true;
        restirDI.boilingFilter.boilingFilterStrength = 0.2f;
        restirDI.temporalResamplingParams.biasCorrectionMode = ReSTIRDI_TemporalBiasCorrectionMode::Raytraced;
        restirDI.spatialResamplingParams.biasCorrectionMode = ReSTIRDI_SpatialBiasCorrectionMode::Basic;
        restirDI.spatialResamplingParams.numSamples = 1;
        restirDI.spatialResamplingParams.numDisocclusionBoostSamples = 8;
        restirDI.shadingParams.reuseFinalVisibility = true;
        lightingSettings.brdfptParams.enableSecondaryResampling = true;
        lightingSettings.brdfptParams.secondarySurfaceReSTIRDIParams.spatialResamplingParams.samplingRadius = 1.f;
        lightingSettings.brdfptParams.secondarySurfaceReSTIRDIParams.spatialResamplingParams.numSamples = 1;
        lightingSettings.brdfptParams.secondarySurfaceReSTIRDIParams.spatialResamplingParams.biasCorrectionMode = ReSTIRDI_SpatialBiasCorrectionMode::Basic;
        lightingSettings.enableGradients = true;
        break;

    case QualityPreset::Unbiased:
        enableCheckerboardSampling = false;
        restirDI.resamplingMode = rtxdi::ReSTIRDI_ResamplingMode::TemporalAndSpatial;
        restirDI.initialSamplingParams.localLightSamplingMode = ReSTIRDI_LocalLightSamplingMode::Uniform;
        restirDI.neeLocalLightSampling.numLocalLightUniformSamples = 8;
        restirDI.neeLocalLightSampling.numLocalLightPowerRISSamples = 8;
        restirDI.neeLocalLightSampling.numLocalLightReGIRRISSamples = 16;
        restirDI.initialSamplingParams.numLocalLightSamples = restirDI.neeLocalLightSampling.numLocalLightUniformSamples;
        restirDI.initialSamplingParams.numBrdfSamples = 1;
        restirDI.initialSamplingParams.numInfiniteLightSamples = 1;
        restirDI.temporalResamplingParams.enableVisibilityShortcut = false;
        restirDI.boilingFilter.enableBoilingFilter = false;
        restirDI.boilingFilter.boilingFilterStrength = 0.0f;
        restirDI.temporalResamplingParams.biasCorrectionMode = ReSTIRDI_TemporalBiasCorrectionMode::Raytraced;
        restirDI.spatialResamplingParams.biasCorrectionMode = ReSTIRDI_SpatialBiasCorrectionMode::Raytraced;
        restirDI.spatialResamplingParams.numSamples = 1;
        restirDI.spatialResamplingParams.numDisocclusionBoostSamples = 8;
        restirDI.shadingParams.reuseFinalVisibility = false;
        lightingSettings.brdfptParams.enableSecondaryResampling = true;
        lightingSettings.brdfptParams.secondarySurfaceReSTIRDIParams.spatialResamplingParams.samplingRadius = 1.f;
        lightingSettings.brdfptParams.secondarySurfaceReSTIRDIParams.spatialResamplingParams.numSamples = 1;
        lightingSettings.brdfptParams.secondarySurfaceReSTIRDIParams.spatialResamplingParams.biasCorrectionMode = ReSTIRDI_SpatialBiasCorrectionMode::Raytraced;
        lightingSettings.enableGradients = true;
        break;

    case QualityPreset::Ultra:
        enableCheckerboardSampling = false;
        restirDI.resamplingMode = rtxdi::ReSTIRDI_ResamplingMode::TemporalAndSpatial;
        restirDI.initialSamplingParams.localLightSamplingMode = ReSTIRDI_LocalLightSamplingMode::ReGIR_RIS;
        restirDI.neeLocalLightSampling.numLocalLightUniformSamples = 16;
        restirDI.neeLocalLightSampling.numLocalLightPowerRISSamples = 16;
        restirDI.neeLocalLightSampling.numLocalLightReGIRRISSamples = 16;
        restirDI.initialSamplingParams.numLocalLightSamples = restirDI.neeLocalLightSampling.numLocalLightReGIRRISSamples;
        restirDI.initialSamplingParams.localLightSamplingMode = ReSTIRDI_LocalLightSamplingMode::ReGIR_RIS;
        restirDI.initialSamplingParams.numBrdfSamples = 1;
        restirDI.initialSamplingParams.numInfiniteLightSamples = 1;
        restirDI.temporalResamplingParams.enableVisibilityShortcut = false;
        restirDI.boilingFilter.enableBoilingFilter = false;
        restirDI.boilingFilter.boilingFilterStrength = 0.0f;
        restirDI.temporalResamplingParams.biasCorrectionMode = ReSTIRDI_TemporalBiasCorrectionMode::Raytraced;
        restirDI.spatialResamplingParams.biasCorrectionMode = ReSTIRDI_SpatialBiasCorrectionMode::Raytraced;
        restirDI.spatialResamplingParams.numSamples = 4;
        restirDI.spatialResamplingParams.numDisocclusionBoostSamples = 16;
        restirDI.shadingParams.reuseFinalVisibility = false;
        lightingSettings.brdfptParams.enableSecondaryResampling = true;
        lightingSettings.brdfptParams.secondarySurfaceReSTIRDIParams.spatialResamplingParams.samplingRadius = 4.f;
        lightingSettings.brdfptParams.secondarySurfaceReSTIRDIParams.spatialResamplingParams.numSamples = 2;
        lightingSettings.brdfptParams.secondarySurfaceReSTIRDIParams.spatialResamplingParams.biasCorrectionMode = ReSTIRDI_SpatialBiasCorrectionMode::Raytraced;
        lightingSettings.enableGradients = true;
        break;

    case QualityPreset::Reference:
        enableCheckerboardSampling = false;
        restirDI.resamplingMode = rtxdi::ReSTIRDI_ResamplingMode::None;
        restirDI.initialSamplingParams.localLightSamplingMode = ReSTIRDI_LocalLightSamplingMode::Uniform;
        restirDI.neeLocalLightSampling.numLocalLightUniformSamples = 16;
        restirDI.neeLocalLightSampling.numLocalLightPowerRISSamples = 16;
        restirDI.neeLocalLightSampling.numLocalLightReGIRRISSamples = 0;
        restirDI.initialSamplingParams.numLocalLightSamples = restirDI.neeLocalLightSampling.numLocalLightUniformSamples;
        restirDI.initialSamplingParams.numBrdfSamples = 1;
        restirDI.initialSamplingParams.numInfiniteLightSamples = 1;
        restirDI.boilingFilter.enableBoilingFilter = false;
        restirDI.boilingFilter.boilingFilterStrength = 0.0f;
        lightingSettings.brdfptParams.enableSecondaryResampling = false;
        lightingSettings.enableGradients = false;
        break;

    case QualityPreset::Custom:
    default:;
    }

    rtxdi::CheckerboardMode newCheckerboardMode = enableCheckerboardSampling ? rtxdi::CheckerboardMode::Black : rtxdi::CheckerboardMode::Off;
    if (newCheckerboardMode != restirDIStaticParams.CheckerboardSamplingMode)
    {
        restirDIStaticParams.CheckerboardSamplingMode = newCheckerboardMode;
        resetISContext = true;
    }
}

void UIData::ApplyReSTIRPTPreset()
{
    // Base defaults depend on the active denoiser (DLSS-RR vs NRD); the quality-preset switch below
    // only tweaks orthogonal fields (sample counts, radius, bounce budget, etc.).
    const bool dlssRREnabled = (denoiserMode == DenoiserMode::DLSS_RR);

    // Reset everything to defaults, then apply preset-specific optimizations
    lightingSettings.ptParameters = GetDefaultPTParameters();
    restirPT.initialSampling = rtxdi::GetDefaultReSTIRPTInitialSamplingParams();
    restirPT.decorrelation = rtxdi::GetDefaultReSTIRPTDecorrelationParams(dlssRREnabled);
    restirPT.reconnection = rtxdi::GetDefaultReSTIRPTReconnectionParameters();
    restirPT.temporalResampling = rtxdi::GetDefaultReSTIRPTTemporalResamplingParams(dlssRREnabled);
    restirPT.hybridShift = rtxdi::GetDefaultReSTIRPTHybridShiftParams();
    restirPT.boilingFilter = rtxdi::GetDefaultReSTIRPTBoilingFilterParams(dlssRREnabled);
    restirPT.spatialResampling = rtxdi::GetDefaultReSTIRPTSpatialResamplingParams(dlssRREnabled);
    restirPT.hybridMirrorInitial = true;

    // Always enable MIS mode, even for fast mode
    lightingSettings.ptParameters.lightSamplingMode = PTInitialSamplingLightSamplingMode::Mis;

    // Always revert to these, since they regard correctness, not performance.
    lightingSettings.ptParameters.sampleEnvMapOnSecondaryMiss = false;
    lightingSettings.ptParameters.sampleEmissivesOnSecondaryHit = false;
    lightingSettings.ptParameters.copyReSTIRDISimilarityThresholds = true;

    switch (restirPtQualityPreset)
    {
    case ReSTIRPTQualityPreset::Fast:
        restirPT.resamplingMode = rtxdi::ReSTIRPT_ResamplingMode::Temporal;
        lightingSettings.ptParameters.minimumPathThroughput = 0.1f;
        lightingSettings.ptParameters.enableRussianRoulette = true;
        lightingSettings.ptParameters.russianRouletteContinueChance = 0.8f;
        lightingSettings.ptParameters.enableSecondaryDISpatialResampling = false;
        lightingSettings.ptParameters.extraMirrorBounceBudget = 1;
        restirPT.neeLocalLightSampling.localLightSamplingMode = ReSTIRDI_LocalLightSamplingMode::Uniform;
        restirPT.neeLocalLightSampling.numLocalLightUniformSamples = 2;
        restirPT.neeLocalLightSampling.numLocalLightPowerRISSamples = 2;
        restirPT.neeLocalLightSampling.numLocalLightReGIRRISSamples = 2;
        restirPT.initialSampling.maxBounceDepth = 3;
        restirPT.initialSampling.maxRcVertexLength = restirPT.initialSampling.maxBounceDepth + 1;
        restirPT.initialSampling.numInitialSamples = 1;
        restirPT.spatialResampling.numDisocclusionBoostSamples = 2;
        restirPT.spatialResampling.samplingRadius = 32;
        restirPT.spatialResampling.numSpatialSamples = 1;
        break;
    case ReSTIRPTQualityPreset::Medium:
        restirPT.resamplingMode = rtxdi::ReSTIRPT_ResamplingMode::TemporalAndSpatial;
        lightingSettings.ptParameters.minimumPathThroughput = 0.05f;
        lightingSettings.ptParameters.enableRussianRoulette = true;
        lightingSettings.ptParameters.russianRouletteContinueChance = 0.8f;
        lightingSettings.ptParameters.enableSecondaryDISpatialResampling = false;
        lightingSettings.ptParameters.extraMirrorBounceBudget = 4;
        restirPT.neeLocalLightSampling.localLightSamplingMode = ReSTIRDI_LocalLightSamplingMode::Power_RIS;
        restirPT.neeLocalLightSampling.numLocalLightUniformSamples = 4;
        restirPT.neeLocalLightSampling.numLocalLightPowerRISSamples = 4;
        restirPT.neeLocalLightSampling.numLocalLightReGIRRISSamples = 4;
        restirPT.initialSampling.maxBounceDepth = 3;
        restirPT.initialSampling.maxRcVertexLength = restirPT.initialSampling.maxBounceDepth + 1;
        restirPT.initialSampling.numInitialSamples = 1;
        restirPT.spatialResampling.numDisocclusionBoostSamples = 4;
        restirPT.spatialResampling.samplingRadius = 32;
        restirPT.spatialResampling.numSpatialSamples = 1;
        break;
    case ReSTIRPTQualityPreset::Ultra:
        restirPT.resamplingMode = rtxdi::ReSTIRPT_ResamplingMode::TemporalAndSpatial;
        lightingSettings.ptParameters.minimumPathThroughput = 0.02f;
        lightingSettings.ptParameters.enableRussianRoulette = false;
        lightingSettings.ptParameters.russianRouletteContinueChance = 0.8f;
        lightingSettings.ptParameters.enableSecondaryDISpatialResampling = false;
        lightingSettings.ptParameters.extraMirrorBounceBudget = 16;
        restirPT.neeLocalLightSampling.localLightSamplingMode = ReSTIRDI_LocalLightSamplingMode::Power_RIS;
        restirPT.neeLocalLightSampling.numLocalLightUniformSamples = 6;
        restirPT.neeLocalLightSampling.numLocalLightPowerRISSamples = 6;
        restirPT.neeLocalLightSampling.numLocalLightReGIRRISSamples = 6;
        restirPT.initialSampling.maxBounceDepth = 4;
        restirPT.initialSampling.maxRcVertexLength = restirPT.initialSampling.maxBounceDepth + 1;
        restirPT.initialSampling.numInitialSamples = 1;
        restirPT.spatialResampling.numDisocclusionBoostSamples = 8;
        restirPT.spatialResampling.samplingRadius = 32;
        restirPT.spatialResampling.numSpatialSamples = 1;
        restirPT.spatialResampling.enableSpatialHeuristicMode = 1;
        break;
    case ReSTIRPTQualityPreset::Custom:
        // Don't change anything
        break;
    }

}

void UIData::ApplyDLSSRRPreset()
{
    ApplyDenoiserCompatDefaults(/* dlssRREnabled = */ true);
}

void UIData::ApplyNonDLSSRRPreset()
{
    ApplyDenoiserCompatDefaults(/* dlssRREnabled = */ false);
}

// Applies just the denoiser-dependent fields (independent of the quality preset), sourced from the
// library defaults so the DLSS-RR / NRD profiles live in one place (GetDefaultReSTIRPT*Params).
// The quality preset owns the orthogonal spatial fields (sample counts, radius, boost), so those are
// left untouched here.
void UIData::ApplyDenoiserCompatDefaults(bool dlssRREnabled)
{
    const RTXDI_PTTemporalResamplingParameters temporalDefaults = rtxdi::GetDefaultReSTIRPTTemporalResamplingParams(dlssRREnabled);
    const RTXDI_PTSpatialResamplingParameters spatialDefaults = rtxdi::GetDefaultReSTIRPTSpatialResamplingParams(dlssRREnabled);

    restirPT.temporalResampling.maxHistoryLength = temporalDefaults.maxHistoryLength;
    restirPT.temporalResampling.enableAgeBasedRejection = temporalDefaults.enableAgeBasedRejection;
    restirPT.temporalResampling.duplicationBasedHistoryReduction = temporalDefaults.duplicationBasedHistoryReduction;

    restirPT.spatialResampling.duplicationBasedHistoryReduction = spatialDefaults.duplicationBasedHistoryReduction;
    restirPT.spatialResampling.maxTemporalHistory = temporalDefaults.maxHistoryLength;

    restirPT.decorrelation = rtxdi::GetDefaultReSTIRPTDecorrelationParams(dlssRREnabled);
    restirPT.boilingFilter = rtxdi::GetDefaultReSTIRPTBoilingFilterParams(dlssRREnabled);
}

#ifdef WITH_NRD
void UIData::SetDefaultDenoiserSettings()
{
    reblurSettings = nrd::ReblurSettings();
    reblurSettings.enableAntiFirefly = true;
    reblurSettings.hitDistanceReconstructionMode = nrd::HitDistanceReconstructionMode::AREA_3X3; // this is better than nothing, but risky because a sample in 3x3 area is not guaranteed in the sample

    relaxSettings = nrd::RelaxSettings();
    relaxSettings.diffusePhiLuminance = 1.0f;
    relaxSettings.spatialVarianceEstimationHistoryThreshold = 1;
    relaxSettings.enableAntiFirefly = true;
    reblurSettings.hitDistanceReconstructionMode = nrd::HitDistanceReconstructionMode::AREA_3X3; // this is better than nothing, but risky because a sample in 3x3 area is not guaranteed in the sample
}
#endif


UserInterface::UserInterface(app::DeviceManager* deviceManager, vfs::IFileSystem& rootFS, UIData& ui) :
    ImGui_Renderer(deviceManager),
    m_ui(ui),
    m_fontOpenSans(nullptr),
    m_showAdvancedSamplingSettings(false),
    m_showAdvancedDenoisingSettings(false)
{
    m_fontOpenSans = CreateFontFromFile(rootFS, "/media/fonts/OpenSans/OpenSans-Regular.ttf", 17.f);
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

bool LocalLightSamplingSelectorForReSTIRDI(LocalLightSamplingUIData& data)
{
    bool samplingSettingsChanged = false;

    int* initSamplingMode = (int*)&data.localLightSamplingMode;
    samplingSettingsChanged |= ImGui::RadioButton("Local Light Uniform Sampling", initSamplingMode, 0);
    ShowHelpMarker("Sample local lights uniformly");

    samplingSettingsChanged |= ImGui::SliderInt("Local Light Uniform Samples", (int*)&data.numLocalLightUniformSamples, 0, 32);
    ShowHelpMarker(
        "Number of samples drawn uniformly from the local light pool.");

    samplingSettingsChanged |= ImGui::RadioButton("Local Light Power RIS", initSamplingMode, 1);
    ShowHelpMarker("Sample local lights using power-based RIS");

    samplingSettingsChanged |= ImGui::SliderInt("Local Light Power RIS Samples", (int*)&data.numLocalLightPowerRISSamples, 0, 32);
    ShowHelpMarker(
        "Number of samples drawn from the local lights power-based RIS buffer.");

    samplingSettingsChanged |= ImGui::RadioButton("Local Light ReGIR RIS", initSamplingMode, 2);
    ShowHelpMarker("Sample local lights using ReGIR-based RIS");

    samplingSettingsChanged |= ImGui::SliderInt("Local Light ReGIR RIS Samples", (int*)&data.numLocalLightReGIRRISSamples, 0, 32);
    ShowHelpMarker(
        "Number of samples drawn from the local lights ReGIR-based RIS buffer");

    return samplingSettingsChanged;
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

constexpr uint32_t c_ColorRegularHeader = 0xffff8080;
constexpr uint32_t c_ColorAttentionHeader = 0xff80ffff;

static bool ImGui_ColoredTreeNodeEx(const char* text, uint32_t color, ImGuiTreeNodeFlags_ flags)
{
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    bool expanded = ImGui::TreeNodeEx(text, flags);
    ImGui::PopStyleColor();
    return expanded;
}

static bool ImGui_ColoredTreeNode(const char* text, uint32_t color)
{
    return ImGui_ColoredTreeNodeEx(text, color, ImGuiTreeNodeFlags_None);
}


void UserInterface::GeneralRenderingSettings()
{
    if (ImGui_ColoredTreeNodeEx("General Rendering", c_ColorRegularHeader, ImGuiTreeNodeFlags_None))
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
    if (ImGui_ColoredTreeNode("Static ReSTIR Context Settings", c_ColorRegularHeader))
    {
        ShowHelpMarker("Heavyweight settings (e.g. that dictate buffer sizes) that require recreating the context to change.");
        m_ui.resetAccumulation |= ImGui::Checkbox("Importance Sample Env. Map", &m_ui.environmentMapImportanceSampling);

        if (ImGui::TreeNode("RTXDI Context"))
        {
            if (ImGui::Button("Apply Settings"))
                m_ui.resetISContext = true;

            // TODO: Pull this out of ReSTIRDIContext and make it global to both ReSTIRDI and ReSTIRGI
            bool enableCheckerboardSampling = (m_ui.restirDIStaticParams.CheckerboardSamplingMode != rtxdi::CheckerboardMode::Off);
            ImGui::Checkbox("Checkerboard Rendering", &enableCheckerboardSampling);
            m_ui.restirDIStaticParams.CheckerboardSamplingMode = enableCheckerboardSampling ? rtxdi::CheckerboardMode::Black : rtxdi::CheckerboardMode::Off;

            ImGui::TreePop();
        }

        if (ImGui::TreeNode("ReGIR Context"))
        {
            if (ImGui::Button("Apply Settings"))
                m_ui.resetISContext = true;

            ImGui::DragInt("Lights per Cell", (int*)&m_ui.regirStaticParams.lightsPerCell, 1, 32, 8192);

            static const char* regirShapeOptions[] = { "Disabled", "Grid", "Onion" };
            const char* currentReGIRShapeOption = regirShapeOptions[static_cast<int>(m_ui.regirStaticParams.mode)];
            if (ImGui::BeginCombo("ReGIR Mode", currentReGIRShapeOption))
            {
                for (int i = 0; i < sizeof(regirShapeOptions) / sizeof(regirShapeOptions[0]); i++)
                {
                    int enumIndex = i;
                    bool is_selected = (enumIndex == static_cast<int>(m_ui.regirStaticParams.mode));
                    if (ImGui::Selectable(regirShapeOptions[i], is_selected))
                        *(int*)&m_ui.regirStaticParams.mode = enumIndex;
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            if (m_ui.regirStaticParams.mode == rtxdi::ReGIRMode::Grid)
            {
                ImGui::DragInt3("Grid Resolution", (int*)&m_ui.regirStaticParams.gridParameters.gridSize.x, 1, 1, 64);
            }
            else if (m_ui.regirStaticParams.mode == rtxdi::ReGIRMode::Onion)
            {
                ImGui::SliderInt("Onion Layers - Detail", (int*)&m_ui.regirStaticParams.onionParameters.onionDetailLayers, 0, 8);
                ImGui::SliderInt("Onion Layers - Coverage", (int*)&m_ui.regirStaticParams.onionParameters.onionCoverageLayers, 0, 20);
            }

            ImGui::Text("Total ReGIR Cells: %d", m_ui.regirLightSlotCount / m_ui.regirStaticParams.lightsPerCell);

            ImGui::TreePop();
        }

        ImGui::TreePop();
    }
    ImGui::Separator();

    if (ImGui_ColoredTreeNode("Direct Lighting", c_ColorAttentionHeader))
    {
        bool samplingSettingsChanged = false;

        m_ui.resetAccumulation |= ImGui::Combo("Direct Lighting Mode", (int*)&m_ui.lightingSettings.directLightingMode,
            "None\0"
            "BRDF\0"
            "ReSTIR\0"
        );
        switch (m_ui.lightingSettings.directLightingMode)
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
#if DONUT_WITH_DLSS
            // Re-apply the DLSS-RR preset so its overrides survive the quality preset.
            if (m_ui.autoApplyDLSSRRPreset && m_ui.denoiserMode == DenoiserMode::DLSS_RR)
                m_ui.ApplyDLSSRRPreset();
#endif
            m_ui.resetAccumulation = true;
        }

        ImGui::Checkbox("Show Advanced Settings", &m_showAdvancedSamplingSettings);

        bool isUsingReStir = m_ui.lightingSettings.directLightingMode == DirectLightingMode::ReStir;

        if (isUsingReStir)
        {
            ImGui::PushItemWidth(180.f);
            m_ui.resetAccumulation |= ImGui::Combo("Resampling Mode", (int*)&m_ui.restirDI.resamplingMode,
                "None\0"
                "Temporal\0"
                "Spatial\0"
                "Temporal + Spatial\0"
                "Fused Spatiotemporal\0");
            ImGui::PopItemWidth();
            ImGui::Separator();

            if (ImGui::TreeNode("ReGIR Presampling"))
            {
                ShowHelpMarker("Dynamic ReGIR Settings");
                static const char* regirPresamplingOptions[] = { "Uniform Sampling", "Power RIS" };
                const char* currentPresamplingOption = regirPresamplingOptions[static_cast<int>(m_ui.regirDynamicParameters.presamplingMode)];
                if (ImGui::BeginCombo("ReGIR RIS Presampling Mode", currentPresamplingOption))
                {
                    for (int i = 0; i < sizeof(regirPresamplingOptions) / sizeof(regirPresamplingOptions[0]); i++)
                    {
                        bool is_selected = (i == static_cast<int>(m_ui.regirDynamicParameters.presamplingMode));
                        if (ImGui::Selectable(regirPresamplingOptions[i], is_selected))
                            *(int*)&m_ui.regirDynamicParameters.presamplingMode = i;
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ShowHelpMarker(
                    "Presampling method the ReGIR algorithm uses to select lights");
                m_ui.resetAccumulation |= ImGui::SliderFloat("Cell Size", &m_ui.regirDynamicParameters.regirCellSize, 0.1f, 4.f);
                m_ui.resetAccumulation |= ImGui::SliderInt("Grid Build Samples", (int*)&m_ui.regirDynamicParameters.regirNumBuildSamples, 0, 32);
                m_ui.resetAccumulation |= ImGui::SliderFloat("Sampling Jitter", &m_ui.regirDynamicParameters.regirSamplingJitter, 0.0f, 2.f);

                ImGui::Checkbox("Freeze Position", &m_ui.freezeRegirPosition);
                ImGui::SameLine(0.f, 10.f);
                ImGui::Checkbox("Visualize Cells", (bool*)&m_ui.lightingSettings.visualizeRegirCells);

                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Initial Sampling"))
            {
                if (ImGui::TreeNodeEx("Local Light Sampling", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    samplingSettingsChanged |= LocalLightSamplingSelectorForReSTIRDI(m_ui.restirDI.neeLocalLightSampling);

                    static const char* regirFallbackOptions[] = { "Uniform Sampling", "Power RIS" };
                    const char* currentFallbackOption = regirFallbackOptions[static_cast<int>(m_ui.regirDynamicParameters.fallbackSamplingMode)];
                    if (ImGui::BeginCombo("ReGIR RIS Fallback Sampling Mode", currentFallbackOption))
                    {
                        for (int i = 0; i < sizeof(regirFallbackOptions) / sizeof(regirFallbackOptions[0]); i++)
                        {
                            bool is_selected = (i == static_cast<int>(m_ui.regirDynamicParameters.fallbackSamplingMode));
                            if (ImGui::Selectable(regirFallbackOptions[i], is_selected))
                                *(int*)&m_ui.regirDynamicParameters.fallbackSamplingMode = i;
                            if (is_selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ShowHelpMarker(
                        "Sampling method to fall back to for surfaces outside the ReGIR volume");

                    m_ui.resetAccumulation |= samplingSettingsChanged;

                    ImGui::TreePop();
                }


                samplingSettingsChanged |= ImGui::SliderInt("Initial BRDF Samples", (int*)&m_ui.restirDI.initialSamplingParams.numBrdfSamples, 0, 8);
                ShowHelpMarker(
                    "Number of rays traced from the surface using BRDF importance sampling to find mesh lights or environment map samples. Helps glossy reflections.");

                samplingSettingsChanged |= ImGui::SliderInt("Initial Infinite Light Samples", (int*)&m_ui.restirDI.initialSamplingParams.numInfiniteLightSamples, 0, 32);
                ShowHelpMarker(
                    "Number of samples drawn from the infinite light pool, i.e. the sun light when using "
                    "the procedural environment, and the environment map when it's not importance sampled.");

                samplingSettingsChanged |= ImGui::SliderInt("Initial Environment Samples", (int*)&m_ui.restirDI.initialSamplingParams.numEnvironmentSamples, 0, 32);
                ShowHelpMarker(
                    "Number of samples drawn from the environment map when it is importance sampled.");

                samplingSettingsChanged |= ImGui::Checkbox("Enable Initial Visibility", (bool*)&m_ui.restirDI.initialSamplingParams.enableInitialVisibility);

                samplingSettingsChanged |= ImGui::SliderFloat("BRDF Sample Cutoff", (float*)&m_ui.restirDI.initialSamplingParams.brdfCutoff, 0.0f, 0.1f);
                ShowHelpMarker(
                    "Determine how much to shorten BRDF rays. 0 to disable shortening");

                samplingSettingsChanged |= ImGui::SliderFloat("BRDF Ray Min T", (float*)&m_ui.restirDI.initialSamplingParams.brdfRayMinT, 0.001f, 1.0f);
                ShowHelpMarker(
                    "Minimum distance to shoot BRDF ray.");

                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Temporal Resampling"))
            {
                samplingSettingsChanged |= ImGui::Checkbox("Enable Previous Frame TLAS/BLAS", (bool*)&m_ui.lightingSettings.enablePrevTLAS);
                ShowHelpMarker(
                    "Use the previous frame TLAS for bias correction rays during temporal resampling and gradient computation. "
                    "Resutls in less biased results under motion and brighter, more complete gradients.");

                samplingSettingsChanged |= ImGui::Checkbox("Enable Permutation Sampling", (bool*)&m_ui.restirDI.temporalResamplingParams.enablePermutationSampling);
                ShowHelpMarker(
                    "Shuffle the pixels from the previous frame when resampling from them. This makes pixel colors less correlated "
                    "temporally and therefore better suited for temporal accumulation and denoising. Also results in a higher positive "
                    "bias when the Reuse Final Visibility setting is on, which somewhat counteracts the negative bias from spatial resampling.");

                if (m_ui.restirDI.resamplingMode != rtxdi::ReSTIRDI_ResamplingMode::FusedSpatiotemporal)
                {
                    static const char* temporalBiasCorrectionOptions[] = { "Off", "Basic", "Raytraced" };
                    const char* currentFallbackOption = temporalBiasCorrectionOptions[std::min(static_cast<int>(m_ui.restirDI.temporalResamplingParams.biasCorrectionMode), 2)];
                    if (ImGui::BeginCombo("Temporal Bias Correction", currentFallbackOption))
                    {
                        for (int i = 0; i < sizeof(temporalBiasCorrectionOptions) / sizeof(temporalBiasCorrectionOptions[0]); i++)
                        {
                            int enumIndex = i < 2 ? i : RTXDI_BIAS_CORRECTION_RAY_TRACED;
                            bool is_selected = (enumIndex == static_cast<int>(m_ui.restirDI.temporalResamplingParams.biasCorrectionMode));
                            if (ImGui::Selectable(temporalBiasCorrectionOptions[i], is_selected))
                                *(int*)&m_ui.restirDI.temporalResamplingParams.biasCorrectionMode = enumIndex;
                            if (is_selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ShowHelpMarker(
                        "Off = use the 1/M normalization.\n"
                        "Basic = use the MIS normalization but assume that every sample is visible.\n"
                        "Pairwise = pairwise MIS improves perf and specular quality (assumes every sample is visible).\n"
                        "Ray Traced = use the MIS normalization and verify visibility.");
                }

                if (m_showAdvancedSamplingSettings)
                {
                    samplingSettingsChanged |= ImGui::SliderFloat("Temporal Depth Threshold", &m_ui.restirDI.temporalResamplingParams.depthThreshold, 0.f, 1.f);
                    ShowHelpMarker("Higher values result in accepting temporal samples with depths more different from the current pixel.");

                    samplingSettingsChanged |= ImGui::SliderFloat("Temporal Normal Threshold", &m_ui.restirDI.temporalResamplingParams.normalThreshold, 0.f, 1.f);
                    ShowHelpMarker("Lower values result in accepting temporal samples with normals more different from the current pixel.");

                    ImGui::SliderFloat("Permutation Sampling Threshold", &m_ui.restirDI.temporalResamplingParams.permutationSamplingThreshold, 0.8f, 1.f);
                    ShowHelpMarker("Higher values result in disabling permutation sampling on less complex surfaces.");
                }
                samplingSettingsChanged |= ImGui::SliderInt("Max History Length", (int*)&m_ui.restirDI.temporalResamplingParams.maxHistoryLength, 1, 100);

                samplingSettingsChanged |= ImGui::Checkbox("##enableBoilingFilter", (bool*)&m_ui.restirDI.boilingFilter.enableBoilingFilter);
                ImGui::SameLine();
                ImGui::PushItemWidth(69.f);
                samplingSettingsChanged |= ImGui::SliderFloat("Boiling Filter", &m_ui.restirDI.boilingFilter.boilingFilterStrength, 0.f, 1.f);
                ImGui::PopItemWidth();
                ShowHelpMarker(
                    "The boiling filter analyzes the neighborhood of each pixel and discards the pixel's reservoir "
                    "if it has a significantly higher weight than the other pixels.");

                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Spatial Resampling"))
            {
                samplingSettingsChanged |= ImGui::Combo("Spatial Bias Correction", (int*)&m_ui.restirDI.spatialResamplingParams.biasCorrectionMode, "Off\0Basic\0Pairwise\0Ray Traced\0");
                ShowHelpMarker(
                    "Off = use the 1/M normalization.\n"
                    "Basic = use the MIS normalization but assume that every sample is visible.\n"
                    "Pairwise = pairwise MIS improves perf and specular quality (assumes every sample is visible).\n"
                    "Ray Traced = use the MIS normalization and verify visibility.");
                samplingSettingsChanged |= ImGui::SliderInt("Spatial Samples", (int*)&m_ui.restirDI.spatialResamplingParams.numSamples, 1, 32);

                if (m_ui.restirDI.resamplingMode == rtxdi::ReSTIRDI_ResamplingMode::TemporalAndSpatial || m_ui.restirDI.resamplingMode == rtxdi::ReSTIRDI_ResamplingMode::FusedSpatiotemporal)
                {
                    samplingSettingsChanged |= ImGui::SliderInt("Disocclusion Boost Samples", (int*)&m_ui.restirDI.spatialResamplingParams.numDisocclusionBoostSamples, 1, 32);
                    ShowHelpMarker(
                        "The number of spatial samples to take on surfaces which don't have sufficient accumulated history length. "
                        "More samples result in faster convergence in disoccluded regions but increase processing time.");
                }

                samplingSettingsChanged |= ImGui::SliderFloat("Spatial Sampling Radius", &m_ui.restirDI.spatialResamplingParams.samplingRadius, 1.f, 32.f);
                if (m_showAdvancedSamplingSettings && m_ui.restirDI.resamplingMode != rtxdi::ReSTIRDI_ResamplingMode::FusedSpatiotemporal)
                {
                    samplingSettingsChanged |= ImGui::SliderFloat("Spatial Depth Threshold", &m_ui.restirDI.spatialResamplingParams.depthThreshold, 0.f, 1.f);
                    ShowHelpMarker("Higher values result in accepting samples with depths more different from the center pixel.");

                    samplingSettingsChanged |= ImGui::SliderFloat("Spatial Normal Threshold", &m_ui.restirDI.spatialResamplingParams.normalThreshold, 0.f, 1.f);
                    ShowHelpMarker("Lower values result in accepting samples with normals more different from the center pixel.");

                    samplingSettingsChanged |= ImGui::Checkbox("Discount Naive Samples", (bool*)(&m_ui.restirDI.spatialResamplingParams.discountNaiveSamples));
                    ShowHelpMarker("Prevents samples which are from the current frame or have no reasonable temporal history merged being spread to neighbors.");
                }

                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Final Shading"))
            {
                samplingSettingsChanged |= ImGui::Checkbox("Enable Final Visibility", (bool*)&m_ui.restirDI.shadingParams.enableFinalVisibility);

                samplingSettingsChanged |= ImGui::Checkbox("Discard Invisible Samples", (bool*)&m_ui.restirDI.temporalResamplingParams.enableVisibilityShortcut);
                ShowHelpMarker(
                    "When a sample is determined to be occluded during final shading, its reservoir is discarded. "
                    "This can significantly reduce noise, but also introduce some bias near shadow boundaries beacuse the reservoirs' M values are kept. "
                    "Also, enabling this option speeds up temporal resampling with Ray Traced bias correction by skipping most of the bias correction rays.");

                samplingSettingsChanged |= ImGui::Checkbox("Reuse Final Visibility", (bool*)&m_ui.restirDI.shadingParams.reuseFinalVisibility);
                ShowHelpMarker(
                    "Store the fractional final visibility term in the reservoirs and reuse it later if the reservoir is not too old and has not "
                    "moved too far away from its original location. Enable the Advanced Settings option to control the thresholds.");

                if (m_ui.restirDI.shadingParams.reuseFinalVisibility && m_showAdvancedSamplingSettings)
                {
                    samplingSettingsChanged |= ImGui::SliderFloat("Final Visibility - Max Distance", &m_ui.restirDI.shadingParams.finalVisibilityMaxDistance, 0.f, 32.f);
                    samplingSettingsChanged |= ImGui::SliderInt("Final Visibility - Max Age", (int*)&m_ui.restirDI.shadingParams.finalVisibilityMaxAge, 0, 16);
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

    if (ImGui_ColoredTreeNodeEx("Indirect Lighting", c_ColorAttentionHeader, ImGuiTreeNodeFlags_DefaultOpen))
    {
        m_ui.resetAccumulation |= ImGui::Combo("Indirect Lighting Mode", (int*)&m_ui.indirectLightingMode,
            "None\0"
            "BRDF\0"
            "ReSTIR GI\0"
            "ReSTIR PT\0"
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
        case IndirectLightingMode::ReStirPT:
            ShowHelpMarker(
                "Run a path tracer starting from primary surfaces and resample results with ReSTIR PT.");
            break;
        default:;
        }

        bool isUsingIndirect = m_ui.indirectLightingMode != IndirectLightingMode::None;

        if (m_ui.indirectLightingMode == IndirectLightingMode::Brdf || m_ui.indirectLightingMode == IndirectLightingMode::ReStirGI)
            m_ui.resetAccumulation |= ImGui::SliderFloat("Min Secondary Roughness", &m_ui.lightingSettings.brdfptParams.materialOverrideParams.minSecondaryRoughness, 0.f, 1.f);

        if ((m_ui.indirectLightingMode == IndirectLightingMode::Brdf || m_ui.indirectLightingMode == IndirectLightingMode::ReStirGI) && ImGui::TreeNode("Secondary Surface Light Sampling"))
        {
            // TODO: Determine whether to have choice of sampling mode here and in ReSTIR DI.
            // Should probably have a single numLocalLightSamples in the struct and have the UI keep track of the 3 different values for each mode
            m_ui.resetAccumulation |= ImGui::SliderInt("Indirect Local Light Samples", (int*)&m_ui.lightingSettings.brdfptParams.secondarySurfaceReSTIRDIParams.initialSamplingParams.numLocalLightSamples, 0, 32);
            m_ui.resetAccumulation |= ImGui::SliderInt("Indirect Inifinite Light Samples", (int*)&m_ui.lightingSettings.brdfptParams.secondarySurfaceReSTIRDIParams.initialSamplingParams.numInfiniteLightSamples, 0, 32);
            m_ui.resetAccumulation |= ImGui::SliderInt("Indirect Environment Samples", (int*)&m_ui.lightingSettings.brdfptParams.secondarySurfaceReSTIRDIParams.initialSamplingParams.numEnvironmentSamples, 0, 32);

            ImGui::TreePop();
        }

        if ((m_ui.indirectLightingMode == IndirectLightingMode::Brdf || m_ui.indirectLightingMode == IndirectLightingMode::ReStirGI) && m_ui.lightingSettings.directLightingMode == DirectLightingMode::ReStir && ImGui::TreeNode("Reuse Primary Samples"))
        {
            m_ui.resetAccumulation |= ImGui::Checkbox("Reuse RTXDI samples for secondary surface", (bool*)&m_ui.lightingSettings.brdfptParams.enableSecondaryResampling);
            ShowHelpMarker(
                "When shading a secondary surface, try to find a matching surface in screen space and reuse its light reservoir. "
                "This feature uses the Spatial Resampling function and has similar controls.");

            m_ui.resetAccumulation |= ImGui::Combo("Secondary Bias Correction", (int*)&m_ui.lightingSettings.brdfptParams.secondarySurfaceReSTIRDIParams.spatialResamplingParams.biasCorrectionMode, "Off\0Basic\0Pairwise\0Ray Traced\0");
            m_ui.resetAccumulation |= ImGui::SliderInt("Secondary Samples", (int*)&m_ui.lightingSettings.brdfptParams.secondarySurfaceReSTIRDIParams.spatialResamplingParams.numSamples, 1, 4);
            m_ui.resetAccumulation |= ImGui::SliderFloat("Secondary Sampling Radius", &m_ui.lightingSettings.brdfptParams.secondarySurfaceReSTIRDIParams.spatialResamplingParams.samplingRadius, 0.f, 32.f);
            m_ui.resetAccumulation |= ImGui::SliderFloat("Secondary Depth Threshold", &m_ui.lightingSettings.brdfptParams.secondarySurfaceReSTIRDIParams.spatialResamplingParams.depthThreshold, 0.f, 1.f);
            m_ui.resetAccumulation |= ImGui::SliderFloat("Secondary Normal Threshold", &m_ui.lightingSettings.brdfptParams.secondarySurfaceReSTIRDIParams.spatialResamplingParams.normalThreshold, 0.f, 1.f);

            ImGui::TreePop();
        }

        if (m_ui.indirectLightingMode == IndirectLightingMode::ReStirGI)
        {
            ImGui::PushItemWidth(180.f);
            m_ui.resetAccumulation |= ImGui::Combo("Resampling Mode", (int*)&m_ui.restirGI.resamplingMode,
                "None\0"
                "Temporal\0"
                "Spatial\0"
                "Temporal + Spatial\0"
                "Fused Spatiotemporal\0");
            ImGui::PopItemWidth();
            ImGui::Separator();

            if ((m_ui.restirGI.resamplingMode == rtxdi::ReSTIRGI_ResamplingMode::Temporal ||
                m_ui.restirGI.resamplingMode == rtxdi::ReSTIRGI_ResamplingMode::TemporalAndSpatial ||
                m_ui.restirGI.resamplingMode == rtxdi::ReSTIRGI_ResamplingMode::FusedSpatiotemporal) &&
                ImGui::TreeNode("Temporal Resampling"))
            {
                m_ui.resetAccumulation |= ImGui::SliderFloat("Temporal Depth Threshold", &m_ui.restirGI.temporalResamplingParams.depthThreshold, 0.001f, 1.f);
                m_ui.resetAccumulation |= ImGui::SliderFloat("Temporal Normal Threshold", &m_ui.restirGI.temporalResamplingParams.normalThreshold, 0.001f, 1.f);

                m_ui.resetAccumulation |= ImGui::SliderInt("Max reservoir age", (int*)&m_ui.restirGI.temporalResamplingParams.maxReservoirAge, 1, 100);
                m_ui.resetAccumulation |= ImGui::SliderInt("Max history length", (int*)&m_ui.restirGI.temporalResamplingParams.maxHistoryLength, 1, 100);
                m_ui.resetAccumulation |= ImGui::Checkbox("Enable permutation sampling", (bool*)&m_ui.restirGI.temporalResamplingParams.enablePermutationSampling);
                m_ui.resetAccumulation |= ImGui::Checkbox("Enable fallback sampling", (bool*)&m_ui.restirGI.temporalResamplingParams.enableFallbackSampling);

                const char* biasCorrectionText = (m_ui.restirGI.resamplingMode == rtxdi::ReSTIRGI_ResamplingMode::FusedSpatiotemporal)
                    ? "Fused bias correction"
                    : "Temporal bias correction";
                static const char* temporalResamplingOptions[] = { "Off", "Basic MIS", "RayTraced" };
                static const std::map<RTXDI_GIBiasCorrectionMode, uint32_t> mode2index = { {RTXDI_GIBiasCorrectionMode::Off, 0},
                                                                                                    {RTXDI_GIBiasCorrectionMode::Basic, 1},
                                                                                                    {RTXDI_GIBiasCorrectionMode::Raytraced, 2} };
                static const std::array<RTXDI_GIBiasCorrectionMode, 3> index2mode = { RTXDI_GIBiasCorrectionMode::Off,
                                                                                               RTXDI_GIBiasCorrectionMode::Basic,
                                                                                               RTXDI_GIBiasCorrectionMode::Raytraced };
                const char* currentTemporalResamplingModeOption = temporalResamplingOptions[mode2index.at(m_ui.restirGI.temporalResamplingParams.biasCorrectionMode)];
                if (ImGui::BeginCombo(biasCorrectionText, currentTemporalResamplingModeOption))
                {
                    for (int i = 0; i < sizeof(temporalResamplingOptions) / sizeof(temporalResamplingOptions[0]); i++)
                    {
                        bool is_selected = (index2mode.at(i) == m_ui.restirGI.temporalResamplingParams.biasCorrectionMode);
                        if (ImGui::Selectable(temporalResamplingOptions[i], is_selected))
                        {
                            m_ui.resetAccumulation |= true;
                            m_ui.restirGI.temporalResamplingParams.biasCorrectionMode = index2mode.at(i);
                        }
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                m_ui.resetAccumulation |= ImGui::Checkbox("##enableGIBoilingFilter", (bool*)&m_ui.restirGI.boilingFilter.enableBoilingFilter);
                ImGui::SameLine();
                ImGui::PushItemWidth(69.f);
                m_ui.resetAccumulation |= ImGui::SliderFloat("Boiling Filter##GIBoilingFilter", &m_ui.restirGI.boilingFilter.boilingFilterStrength, 0.f, 1.f);
                ImGui::PopItemWidth();

                ImGui::TreePop();
            }

            if ((m_ui.restirGI.resamplingMode == rtxdi::ReSTIRGI_ResamplingMode::Spatial ||
                m_ui.restirGI.resamplingMode == rtxdi::ReSTIRGI_ResamplingMode::TemporalAndSpatial ||
                m_ui.restirGI.resamplingMode == rtxdi::ReSTIRGI_ResamplingMode::FusedSpatiotemporal) &&
                ImGui::TreeNode("Spatial Resampling"))
            {
                m_ui.resetAccumulation |= ImGui::SliderInt("Num spatial samples", (int*)&m_ui.restirGI.spatialResamplingParams.numSamples, 1, 7);
                m_ui.resetAccumulation |= ImGui::SliderFloat("Sampling Radius", (float*)&m_ui.restirGI.spatialResamplingParams.samplingRadius, 0.01f, 60.0f);
                m_ui.resetAccumulation |= ImGui::SliderFloat("Spatial Depth Threshold", &m_ui.restirGI.spatialResamplingParams.depthThreshold, 0.001f, 1.f);
                m_ui.resetAccumulation |= ImGui::SliderFloat("Spatial Normal Threshold", &m_ui.restirGI.spatialResamplingParams.normalThreshold, 0.001f, 1.f);

                if (m_ui.restirGI.resamplingMode != rtxdi::ReSTIRGI_ResamplingMode::FusedSpatiotemporal)
                {
                    static const char* spatialResamplingOptions[] = { "Off", "Basic MIS", "RayTraced" };
                    static const std::map<RTXDI_GIBiasCorrectionMode, uint32_t> mode2index = { {RTXDI_GIBiasCorrectionMode::Off, 0},
                                                                                                       {RTXDI_GIBiasCorrectionMode::Basic, 1},
                                                                                                       {RTXDI_GIBiasCorrectionMode::Raytraced, 2} };
                    static const std::array<RTXDI_GIBiasCorrectionMode, 3> index2mode = { RTXDI_GIBiasCorrectionMode::Off,
                                                                                                 RTXDI_GIBiasCorrectionMode::Basic,
                                                                                                 RTXDI_GIBiasCorrectionMode::Raytraced };
                    const char* currentSpatialResamplingModeOption = spatialResamplingOptions[mode2index.at(m_ui.restirGI.spatialResamplingParams.biasCorrectionMode)];
                    if (ImGui::BeginCombo("Spatial Bias Correction Mode", currentSpatialResamplingModeOption))
                    {
                        for (int i = 0; i < sizeof(spatialResamplingOptions) / sizeof(spatialResamplingOptions[0]); i++)
                        {
                            bool is_selected = (index2mode.at(i) == m_ui.restirGI.spatialResamplingParams.biasCorrectionMode);
                            if (ImGui::Selectable(spatialResamplingOptions[i], is_selected))
                            {
                                m_ui.resetAccumulation |= true;
                                m_ui.restirGI.spatialResamplingParams.biasCorrectionMode = index2mode.at(i);
                            }
                            if (is_selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                }

                ImGui::TreePop();
            }

            m_ui.resetAccumulation |= ImGui::Checkbox("Final visibility", (bool*)&m_ui.restirGI.finalShadingParams.enableFinalVisibility);
            m_ui.resetAccumulation |= ImGui::Checkbox("Final MIS", (bool*)&m_ui.restirGI.finalShadingParams.enableFinalMIS);
        }
        else if (m_ui.indirectLightingMode == IndirectLightingMode::ReStirPT)
        {
            bool samplingSettingsChanged = false;
            if (ImGui::Combo("Preset", (int*)&m_ui.restirPtQualityPreset, "(Custom)\0Fast\0Medium\0Ultra\0"))
            {
                m_ui.ApplyReSTIRPTPreset();
#if DONUT_WITH_DLSS
                // Re-apply the DLSS-RR preset so its overrides survive the PT preset.
                if (m_ui.autoApplyDLSSRRPreset && m_ui.denoiserMode == DenoiserMode::DLSS_RR)
                    m_ui.ApplyDLSSRRPreset();
#endif
                m_ui.resetAccumulation = true;
            }

            samplingSettingsChanged |= ImGui::Combo("Resampling Mode", (int*)&m_ui.restirPT.resamplingMode,
                "None\0"
                "Temporal\0"
                "Spatial\0"
                "Temporal + Spatial\0");

            if (ImGui::TreeNodeEx("Initial Sampling", ImGuiTreeNodeFlags_DefaultOpen))
            {
                samplingSettingsChanged |= ImGui::SliderInt("Initial sample count", (int*)&m_ui.restirPT.initialSampling.numInitialSamples, 1, 16);
                samplingSettingsChanged |= ImGui::SliderInt("Max bounce depth", (int*)&m_ui.restirPT.initialSampling.maxBounceDepth, 1, 16);
                samplingSettingsChanged |= ImGui::SliderInt("Extra mirror bounce budget", (int*)&m_ui.lightingSettings.ptParameters.extraMirrorBounceBudget, 0, 16);
                ShowHelpMarker("Number of additional mirror bounces allowed on top of the max bounce depth. Increases Max RC vertex length accordingly.");
                samplingSettingsChanged |= ImGui::SliderFloat("Minimum path throughput", (float*)&m_ui.lightingSettings.ptParameters.minimumPathThroughput, 0.0f, 0.2f);
                ShowHelpMarker("Minimum throughput for the path tracer to continue tracing rays.");
                if (ImGui::TreeNodeEx("Path tracing", ImGuiTreeNodeFlags_None))
                {
                    samplingSettingsChanged |= ImGui::Checkbox("Russian roulette", (bool*)&m_ui.lightingSettings.ptParameters.enableRussianRoulette);
                    samplingSettingsChanged |= ImGui::SliderFloat("Russian roulette continuation chance", (float*)&m_ui.lightingSettings.ptParameters.russianRouletteContinueChance, 0.0f, 1.0f);
                    samplingSettingsChanged |= ImGui::Checkbox("Sample env map on secondary miss", (bool*)&m_ui.lightingSettings.ptParameters.sampleEnvMapOnSecondaryMiss);
                    samplingSettingsChanged |= ImGui::Combo("Light Sampling Mode", (int*)&m_ui.lightingSettings.ptParameters.lightSamplingMode,
                        "Emissive Only\0"
                        "NEE Only\0"
                        "Emissive + NEE MIS\0");
                    ImGui::TreePop();
                    if (m_ui.lightingSettings.ptParameters.lightSamplingMode == PTInitialSamplingLightSamplingMode::EmissiveOnly ||
                        m_ui.lightingSettings.ptParameters.lightSamplingMode == PTInitialSamplingLightSamplingMode::Mis)
                    {
                        if (ImGui::TreeNodeEx("Emissives", ImGuiTreeNodeFlags_DefaultOpen))
                        {
                            samplingSettingsChanged |= ImGui::Checkbox("Sample emissives on secondary hit", (bool*)&m_ui.lightingSettings.ptParameters.sampleEmissivesOnSecondaryHit);
                            ImGui::TreePop();
                        }
                    }
                    if (m_ui.lightingSettings.ptParameters.lightSamplingMode == PTInitialSamplingLightSamplingMode::NeeOnly ||
                        m_ui.lightingSettings.ptParameters.lightSamplingMode == PTInitialSamplingLightSamplingMode::Mis)
                    {
                        if (ImGui::TreeNodeEx("NEE", ImGuiTreeNodeFlags_DefaultOpen))
                        {
                            ShowHelpMarker("How to sample direct lighting for secondary surfaces.");
                            ImGui::Text("Initial Sampling");
                            samplingSettingsChanged |= LocalLightSamplingSelectorForReSTIRDI(m_ui.restirPT.neeLocalLightSampling);
                            m_ui.lightingSettings.ptParameters.nee.initialSamplingParams.localLightSamplingMode = m_ui.restirPT.neeLocalLightSampling.localLightSamplingMode;
                            m_ui.lightingSettings.ptParameters.nee.initialSamplingParams.numLocalLightSamples = m_ui.restirPT.neeLocalLightSampling.GetSelectedLocalLightSampleCount();
                            samplingSettingsChanged |= ImGui::SliderInt("Infinite Light Samples", (int*)&m_ui.lightingSettings.ptParameters.nee.initialSamplingParams.numInfiniteLightSamples, 0, 8);
                            samplingSettingsChanged |= ImGui::SliderInt("EnvironmentLight Light Samples", (int*)&m_ui.lightingSettings.ptParameters.nee.initialSamplingParams.numEnvironmentSamples, 0, 8);
                            ImGui::Text("Spatial Resampling");
                            ShowHelpMarker("If a secondary surface is within the camera's view, resample from the corresponding ReSTIR DI reservoir.");
                            samplingSettingsChanged |= ImGui::Checkbox("Spatial Resampling: Enable", (bool*)&m_ui.lightingSettings.ptParameters.enableSecondaryDISpatialResampling);
                            ImGui::BeginDisabled(!m_ui.lightingSettings.ptParameters.enableSecondaryDISpatialResampling);
                            samplingSettingsChanged |= ImGui::SliderInt("Num Samples", (int*)&m_ui.lightingSettings.ptParameters.nee.spatialResamplingParams.numSamples, 1, 12);
                            samplingSettingsChanged |= ImGui::SliderFloat("Sampling Radius", (float*)&m_ui.lightingSettings.ptParameters.nee.spatialResamplingParams.samplingRadius, 1, 32);
                            ImGui::Text("Resampling Similarity Thresholds");
                            samplingSettingsChanged |= ImGui::Checkbox("Mirror primary ReSTIR DI settings", (bool*)&m_ui.lightingSettings.ptParameters.copyReSTIRDISimilarityThresholds);
                            ImGui::BeginDisabled(m_ui.lightingSettings.ptParameters.copyReSTIRDISimilarityThresholds);
                            samplingSettingsChanged |= ImGui::SliderFloat("Depth Threshold", &m_ui.lightingSettings.ptParameters.nee.spatialResamplingParams.depthThreshold, 0.001f, 1.f);
                            samplingSettingsChanged |= ImGui::SliderFloat("Normal Threshold", &m_ui.lightingSettings.ptParameters.nee.spatialResamplingParams.normalThreshold, 0.001f, 1.f);
                            ImGui::EndDisabled();
                            ImGui::EndDisabled();
                            ImGui::TreePop();
                        }
                    }
                }
                ImGui::TreePop();
            }
            if (ImGui::TreeNodeEx("Decorrelation", ImGuiTreeNodeFlags_None))
            {
                samplingSettingsChanged |= ImGui::Combo("Decorrelation mode", (int*)&m_ui.restirPT.decorrelation.decorrelationMode,
                    "None\0"
                    "Uniform\0"
                    "Stagnancy-based\0");
                ShowHelpMarker("How to compute the per-pixel decorrelation probability for final shading.\n"
                               "None: feature disabled, regardless of the slider.\n"
                               "Uniform: constant probability equal to the slider across all pixels.\n"
                               "Stagnancy-based: slider modulated by the temporally/spatially-smoothed reservoir Stagnancy signal, "
                               "so decorrelation only fires where samples have been reused heavily.");

                const bool decorrelationEnabled = m_ui.restirPT.decorrelation.decorrelationMode != RTXDI_PTDecorrelationMode::None;
                const bool stagnancyModeActive = m_ui.restirPT.decorrelation.decorrelationMode == RTXDI_PTDecorrelationMode::Stagnancy;

                ImGui::BeginDisabled(!decorrelationEnabled);
                samplingSettingsChanged |= ImGui::SliderFloat("Decorrelation factor", &m_ui.restirPT.decorrelation.decorrelationFactor, 0.0f, 1.0f);
                ShowHelpMarker("Maximum probability that final shading falls back to the unresampled initial-sampling reservoir "
                               "(bypassing temporal/spatial resampling). In Uniform mode this value is used directly across all "
                               "pixels; in Stagnancy mode it is further scaled by the smoothed duplication signal (0 where "
                               "reservoirs are fresh, ramping up to this slider value where reservoirs have been reused heavily).");
                ImGui::EndDisabled();

                ImGui::BeginDisabled(!stagnancyModeActive);
                samplingSettingsChanged |= ImGui::SliderFloat("Decorrelation stagnancy exponent", &m_ui.restirPT.decorrelation.decorrelationStagnancyExponent, 0.1f, 8.0f);
                ShowHelpMarker("Stagnancy mode only. Power exponent applied to the normalized smoothed Stagnancy (in [0, 1]) "
                               "before it scales the decorrelation probability. Values < 1 make decorrelation ramp up earlier "
                               "(more sensitive); values > 1 delay it (less sensitive). 1.0 is linear.");
                samplingSettingsChanged |= ImGui::SliderFloat("Decorrelation EMA factor", &m_ui.restirPT.decorrelation.decorrelationEmaFactor, 0.0f, 1.0f);
                ShowHelpMarker("Stagnancy mode only. Exponential-moving-average blend factor for the temporally-smoothed "
                               "duplication map. 1 fully uses the current frame's age/40 (no smoothing, more noisy); "
                               "smaller values keep more history (smoother, slower to react). 0 means never update "
                               "(not useful in practice).");
                samplingSettingsChanged |= ImGui::Checkbox("Decorrelation firefly replacement", (bool*)&m_ui.restirPT.decorrelation.fireflyReplacementFilterEnable);
                ShowHelpMarker("Stagnancy mode only. When enabled, the FinalShading pass runs an additional boiling filter "
                               "and forces full decorrelation on pixels whose contribution looks like an outlier within their "
                               "tile. Independent of the temporal-resampling boiling filter.");
                ImGui::BeginDisabled(!m_ui.restirPT.decorrelation.fireflyReplacementFilterEnable);
                samplingSettingsChanged |= ImGui::SliderFloat("Decorrelation firefly filter strength", &m_ui.restirPT.decorrelation.fireflyReplacementFilterStrength, 0.0f, 1.0f);
                ShowHelpMarker("Strength of the FinalShading boiling filter. Higher values reject more pixels as outliers.");
                ImGui::EndDisabled();
                samplingSettingsChanged |= ImGui::Checkbox("Firefly replacement bias reduction", (bool*)&m_ui.restirPT.decorrelation.fireflyReplacementBiasReduction);
                ShowHelpMarker("Stagnancy mode only. When the decorrelation path swaps in the preserved initial-sampling "
                               "reservoir, clamp its WeightSum to at most `multiplier bound * previous WeightSum` on pixels "
                               "whose duplication value indicates fresh reservoirs (age == 0). Reduces bias "
                               "from replacing resampled (filtered) firefly with higher value initial firefly.");
                ImGui::BeginDisabled(!m_ui.restirPT.decorrelation.fireflyReplacementBiasReduction);
                samplingSettingsChanged |= ImGui::SliderFloat("Firefly replacement multiplier bound", &m_ui.restirPT.decorrelation.fireflyReplacementMultiplyBound, 1.0f, 100.0f);
                ShowHelpMarker("Upper-bound multiplier used by the firefly-replacement bias-reduction clamp. The replacement "
                               "reservoir's WeightSum is limited to `multiplier bound * previous WeightSum`. Smaller values "
                               "reduces bias but can introduce more post-denoise correlation artifacts.");
                ImGui::EndDisabled();
                ImGui::EndDisabled();
                ImGui::TreePop();
            }
            if (ImGui::TreeNodeEx("Reconnection Parameters", ImGuiTreeNodeFlags_None))
            {
                int* reconnectionMode = (int*)&m_ui.restirPT.reconnection.reconnectionMode;
                samplingSettingsChanged |= ImGui::RadioButton("Fixed threshold", reconnectionMode, 0);
                ShowHelpMarker("Determine reconnectibility based on fixed roughness and distance parameters");

                samplingSettingsChanged |= ImGui::SliderFloat("Roughness threshold", (float*)&m_ui.restirPT.reconnection.roughnessThreshold, 0.0f, 1.0f);
                ShowHelpMarker("Minimum roughness for a surface to be considered rough, below which a surface is treated as specular");
                samplingSettingsChanged |= ImGui::SliderFloat("Distance threshold", (float*)&m_ui.restirPT.reconnection.distanceThreshold, 0.0f, 20.0f);
                ShowHelpMarker("Minimum distance between surfaces to be considered far enough away from one another.");

                samplingSettingsChanged |= ImGui::RadioButton("Footprint", reconnectionMode, 1);
                ShowHelpMarker("Determine reconnectibility based on dynamically calculated ray footprints and BRDF parameters");

                samplingSettingsChanged |= ImGui::SliderFloat("Pdf roughness threshold", (float*)&m_ui.restirPT.reconnection.minPdfRoughness, 0.0f, 1.0f);
                ShowHelpMarker("Brdf roughness threshold, below which a surface is treated as specular");
                samplingSettingsChanged |= ImGui::SliderFloat("Pdf roughness threshold sigma", (float*)&m_ui.restirPT.reconnection.minPdfRoughnessSigma, 0.0f, 10.0f);
                ShowHelpMarker("Sigma for gaussian noise applied around the pdf threshold.");
                samplingSettingsChanged |= ImGui::SliderFloat("Footprint threshold", (float*)&m_ui.restirPT.reconnection.minConnectionFootprint, 0.0f, 1.0f);
                ShowHelpMarker("Ray footprint threshold, a proxy for distance, below which a surface is treated as too close");
                samplingSettingsChanged |= ImGui::SliderFloat("Footprint threshold sigma", (float*)&m_ui.restirPT.reconnection.minConnectionFootprintSigma, 0.0f, 10.0f);
                ShowHelpMarker("Sigma for gaussian noise applied around the footprint parameter.");

                ImGui::TreePop();
            }
            if ((m_ui.restirPT.resamplingMode == rtxdi::ReSTIRPT_ResamplingMode::Temporal || m_ui.restirPT.resamplingMode == rtxdi::ReSTIRPT_ResamplingMode::Spatial || m_ui.restirPT.resamplingMode == rtxdi::ReSTIRPT_ResamplingMode::TemporalAndSpatial) &&
                ImGui::TreeNodeEx("Hybrid Shift", ImGuiTreeNodeFlags_None))
            {
                ImGui::Text("Random replay parameters");
                ImGui::Checkbox("Mirror initial sampling parameters", (bool*)&m_ui.restirPT.hybridMirrorInitial);
                ImGui::BeginDisabled(m_ui.restirPT.hybridMirrorInitial);
                samplingSettingsChanged |= ImGui::SliderInt("Max bounce depth", (int*)&m_ui.restirPT.hybridShift.maxBounceDepth, 1, 16);
                //samplingSettingsChanged |= ImGui::SliderInt("Max RC vertex length", (int*)&m_ui.restirPT.hybridShift.maxRcVertexLength, 1, 16);
                ImGui::EndDisabled();
                ImGui::TreePop();
            }
            if ((m_ui.restirPT.resamplingMode == rtxdi::ReSTIRPT_ResamplingMode::Temporal || m_ui.restirPT.resamplingMode == rtxdi::ReSTIRPT_ResamplingMode::TemporalAndSpatial)
                && ImGui::TreeNodeEx("Temporal Resampling", ImGuiTreeNodeFlags_None))
            {
                samplingSettingsChanged |= ImGui::Checkbox("Enable boiling filter", (bool*)&m_ui.restirPT.boilingFilter.enableBoilingFilter);
                samplingSettingsChanged |= ImGui::SliderFloat("Boiling filter strength", &m_ui.restirPT.boilingFilter.boilingFilterStrength, 0.0f, 1.0f);
                samplingSettingsChanged |= ImGui::Checkbox("Enable fallback sampling", (bool*)&m_ui.restirPT.temporalResampling.enableFallbackSampling);
                samplingSettingsChanged |= ImGui::Checkbox("Enable permutation sampling", (bool*)&m_ui.restirPT.temporalResampling.enablePermutationSampling);
                if (ImGui::Checkbox("Duplication based history reduction", (bool*)&m_ui.restirPT.temporalResampling.duplicationBasedHistoryReduction))
                {
                    samplingSettingsChanged = true;
                    m_ui.restirPT.spatialResampling.duplicationBasedHistoryReduction = m_ui.restirPT.temporalResampling.duplicationBasedHistoryReduction;
                }
                samplingSettingsChanged |= ImGui::SliderFloat("Depth threshold", &m_ui.restirPT.temporalResampling.depthThreshold, 0.0f, 1.0f);
                samplingSettingsChanged |= ImGui::SliderFloat("Normal threshold", &m_ui.restirPT.temporalResampling.normalThreshold, 0.0f, 1.0f);
                samplingSettingsChanged |= ImGui::SliderFloat("History reduction strength", &m_ui.restirPT.temporalResampling.historyReductionStrength, 0.0f, 1.0f);
                if (ImGui::SliderInt("Max history legnth", (int*)&m_ui.restirPT.temporalResampling.maxHistoryLength, 1, 64))
                {
                    samplingSettingsChanged = true;
                    m_ui.restirPT.spatialResampling.maxTemporalHistory = m_ui.restirPT.temporalResampling.maxHistoryLength;
                }
                // The reservoir shares a single "age" field for both age-based rejection and the duplication
                // map's temporal (stagnancy) channel. Whenever the duplication map is generated, age is
                // repurposed as stagnancy, so age-based rejection must be disabled to keep the two consistent.
                bool ageRejectionForcedOff = rtxdi::NeedsDuplicationMap(m_ui.restirPT.temporalResampling, m_ui.restirPT.decorrelation);
                bool ageRejectionEnabled = m_ui.restirPT.temporalResampling.enableAgeBasedRejection != 0 && !ageRejectionForcedOff;
                ImGui::BeginDisabled(ageRejectionForcedOff);
                if (ImGui::Checkbox("Enable age-based sample rejection", &ageRejectionEnabled) && !ageRejectionForcedOff)
                {
                    m_ui.restirPT.temporalResampling.enableAgeBasedRejection = ageRejectionEnabled;
                    samplingSettingsChanged = true;
                }
                ImGui::EndDisabled();
                ImGui::BeginDisabled(!ageRejectionEnabled);
                samplingSettingsChanged |= ImGui::SliderInt("Max reservoir age", (int*)&m_ui.restirPT.temporalResampling.maxReservoirAge, 1, 31);
                ImGui::EndDisabled();
                ImGui::TreePop();
            }
            if ((m_ui.restirPT.resamplingMode == rtxdi::ReSTIRPT_ResamplingMode::Spatial ||
                m_ui.restirPT.resamplingMode == rtxdi::ReSTIRPT_ResamplingMode::TemporalAndSpatial) &&
                ImGui::TreeNodeEx("Spatial Resampling", ImGuiTreeNodeFlags_None))
            {
                samplingSettingsChanged |= ImGui::Checkbox("Compatibility-guided neighbor selection", (bool*)&m_ui.restirPT.spatialResampling.enableSpatialHeuristicMode);
                samplingSettingsChanged |= ImGui::SliderInt("Num spatial samples", (int*)&m_ui.restirPT.spatialResampling.numSpatialSamples, 1, 32);
                samplingSettingsChanged |= ImGui::SliderInt("Num disocclusion boost samples", (int*)&m_ui.restirPT.spatialResampling.numDisocclusionBoostSamples, 1, 32);

                samplingSettingsChanged |= ImGui::SliderFloat("Sampling radius", (float*)&m_ui.restirPT.spatialResampling.samplingRadius, 0.0f, 100.0f);
                samplingSettingsChanged |= ImGui::SliderFloat("Normal rejection threshold", (float*)&m_ui.restirPT.spatialResampling.normalThreshold, 0.0f, 1.0f);
                samplingSettingsChanged |= ImGui::SliderFloat("Depth rejection threshold", (float*)&m_ui.restirPT.spatialResampling.depthThreshold, 0.0f, 1.0f);
                ImGui::TreePop();
            }

            if (samplingSettingsChanged)
            {
                m_ui.resetAccumulation = true;
                m_ui.restirPtQualityPreset = ReSTIRPTQualityPreset::Custom;
            }
        }
        ImGui::TreePop();
    }

    ImGui::Separator();
}

void UserInterface::PostProcessSettings()
{
    if (ImGui_ColoredTreeNode("Post-Processing", c_ColorRegularHeader))
    {
        AntiAliasingMode prevAAMode = m_ui.aaMode;
#if DONUT_WITH_DLSS
        // When DLSS-RR is the active denoiser, it performs the post-processing resolve itself, so the
        // AA options below don't apply. Disable them and tell the user where post-processing comes from.
        const bool postProcessingByDLSSRR = (m_ui.denoiserMode == DenoiserMode::DLSS_RR);
        ImGui::BeginDisabled(postProcessingByDLSSRR);
#endif
        ImGui::RadioButton("No AA", (int*)&m_ui.aaMode, (int)AntiAliasingMode::None);
        ImGui::SameLine();
        ImGui::RadioButton("Accumulation", (int*)&m_ui.aaMode, (int)AntiAliasingMode::Accumulation);
        ImGui::SameLine();
        ImGui::RadioButton("TAAU", (int*)&m_ui.aaMode, (int)AntiAliasingMode::TAA);
#if DONUT_WITH_DLSS
        if (m_ui.dlssAvailable)
        {
            ImGui::SameLine();
            ImGui::RadioButton("DLSS SR", (int*)&m_ui.aaMode, (int)AntiAliasingMode::DLSS_SR);
        }
        ImGui::EndDisabled();
        if (postProcessingByDLSSRR)
            ImGui::TextDisabled("Post processing provided by DLSS-RR");
#endif
        if (m_ui.aaMode != prevAAMode)
        {
            m_ui.resetAccumulation = true;
        }

        ImGui::PushItemWidth(50.f);
        m_ui.resetAccumulation |= ImGui::DragInt("Accum. Frame Limit", (int*)&m_ui.framesToAccumulate, 1.f, 0, 1024);
        ImGui::PopItemWidth();

        if (m_ui.aaMode == AntiAliasingMode::Accumulation)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("// %d frame(s)", m_ui.numAccumulatedFrames);
        }
#if DONUT_WITH_DLSS
        else if (m_ui.dlssAvailable && m_ui.aaMode == AntiAliasingMode::DLSS_SR)
        {
            ImGui::SliderFloat("DLSS Exposure Scale", &m_ui.dlssExposureScale, 0.125f, 16.f, "%.3f", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("DLSS Sharpness", &m_ui.dlssSharpness, 0.f, 1.f);
        }
#endif

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

        m_ui.resetAccumulation |= ImGui::Checkbox("Apply Textures in Compositing", (bool*)&m_ui.enableTextures);

        ImGui::Checkbox("Tone mapping", (bool*)&m_ui.enableToneMapping);
        ImGui::SameLine(160.f);
        ImGui::SliderFloat("Exposure bias", &m_ui.exposureBias, -4.f, 2.f);

        ImGui::Checkbox("Bloom", (bool*)&m_ui.enableBloom);

        ImGui::TreePop();
    }
    ImGui::Separator();
}

void UserInterface::DebugSettings()
{
    if (ImGui_ColoredTreeNodeEx("Debug", c_ColorRegularHeader, ImGuiTreeNodeFlags_None))
    {
        if (ImGui_ColoredTreeNodeEx("Shader Debug Print", c_ColorRegularHeader, ImGuiTreeNodeFlags_DefaultOpen))
        {
            bool shaderDebugCompileTimeEnabled = SHADER_DEBUG_PRINT_ENABLED;
            bool shaderDebugEnabledForD3D12 = m_ui.graphicsAPI == nvrhi::GraphicsAPI::D3D12;
            bool shaderDebugEnabled = shaderDebugCompileTimeEnabled && shaderDebugEnabledForD3D12;
            if (!shaderDebugEnabled)
            {
                if (!shaderDebugCompileTimeEnabled && shaderDebugEnabledForD3D12)
                    ShowHelpMarker("Enable shader debug print by setting SHADER_DEBUG_PRINT_ENABLED in ShaderDebugPrintShared.h to 1 and recompiling.");
                else if (shaderDebugCompileTimeEnabled && !shaderDebugEnabledForD3D12)
                    ShowHelpMarker("Shader debug print is not supported on Vulkan. Enable it by switching to D3D12.");
                else if (!shaderDebugCompileTimeEnabled && !shaderDebugEnabledForD3D12)
                    ShowHelpMarker("Shader debug print is not supported on Vulkan. Enable it by setting SHADER_DEBUG_PRINT_ENABLED in ShaderDebugPrintShared.h to 1 and recompiling, then running in D3D12 mode.");
                ImGui::BeginDisabled(!shaderDebugEnabled);
            }
            ImGui::Checkbox("Enable shader debug print", &m_ui.debugOutputSettings.enableShaderDebugPrint);
            ImGui::Checkbox("Shader debug print on click/always", &m_ui.debugOutputSettings.shaderDebugPrintOnClickOrAlways);
            if(!shaderDebugEnabled)
                ImGui::EndDisabled();
            ImGui::TreePop();
        }
        if (ImGui_ColoredTreeNodeEx("Path Tracing Visualization", c_ColorRegularHeader, ImGuiTreeNodeFlags_DefaultOpen))
        {
            bool pathVizEnabled = PT_PATH_VIZ_ENABLED;
            if (!pathVizEnabled)
            {
                ShowHelpMarker("Enable path visualization by setting PT_PATH_VIZ_ENABLED in PTPathSetRecord.h to 1 and recompiling.");
                ImGui::BeginDisabled(!pathVizEnabled);
            }
            // Selected debug pixel
            uint32_t mouseX = m_ui.lightingSettings.restirShaderDebugParams.mouseSelectedPixel.x;
            uint32_t mouseY = m_ui.lightingSettings.restirShaderDebugParams.mouseSelectedPixel.y;
            ImGui::Text("Selected debug pixel: (%d, %d)", mouseX, mouseY);
            ImGui::Checkbox("Enable Path Visualization", &m_ui.debugOutputSettings.ptPathViz.enabled);
            ImGui::Checkbox("Show Camera Vertex", &m_ui.debugOutputSettings.ptPathViz.cameraVertexEnabled);
            ImGui::Checkbox("Enable Depth Test", &m_ui.debugOutputSettings.ptPathViz.enableDepth);
            ImGui::Checkbox("Update on click/always", &m_ui.debugOutputSettings.ptPathViz.updateOnClickOrAlways);

            const uint retraceEnabled = RTXDI_DEBUG;
            const std::string retraceTooltip = "Enable temporal and spatial retrace by setting RTXDI_DEBUG to 1 and recompiling";

            ImGui::Checkbox("Initial Path      ", (bool*)&m_ui.debugOutputSettings.ptPathViz.paths.initial.enabled);              ImGui::SameLine(); ImGui::ColorEdit3("##Initial Path Color      ", &m_ui.debugOutputSettings.ptPathViz.paths.initial.color.x);
            ImGui::BeginDisabled(!retraceEnabled);
            ImGui::Checkbox("Temporal Retrace  ", (bool*)&m_ui.debugOutputSettings.ptPathViz.paths.temporalRetrace.enabled);      ImGui::SameLine(); ImGui::ColorEdit3("##Temporal Retrace Color  ", &m_ui.debugOutputSettings.ptPathViz.paths.temporalRetrace.color.x);
            ImGui::EndDisabled(); if (!retraceEnabled) ShowHelpMarker(retraceTooltip.c_str());
            ImGui::Checkbox("Temporal Shift    ", (bool*)&m_ui.debugOutputSettings.ptPathViz.paths.temporalShift.enabled);        ImGui::SameLine(); ImGui::ColorEdit3("##Temporal Shift Color    ", &m_ui.debugOutputSettings.ptPathViz.paths.temporalShift.color.x);
            ImGui::Checkbox("Temporal Inv Shift", (bool*)&m_ui.debugOutputSettings.ptPathViz.paths.temporalInverseShift.enabled); ImGui::SameLine(); ImGui::ColorEdit3("##Temporal Inv Shift Color", &m_ui.debugOutputSettings.ptPathViz.paths.temporalInverseShift.color.x);
            ImGui::BeginDisabled(!retraceEnabled);
            ImGui::Checkbox("Spatial Retrace   ", (bool*)&m_ui.debugOutputSettings.ptPathViz.paths.spatialRetrace.enabled);       ImGui::SameLine(); ImGui::ColorEdit3("## Retrace Color          ", &m_ui.debugOutputSettings.ptPathViz.paths.spatialRetrace.color.x);
            ImGui::EndDisabled(); if (!retraceEnabled) ShowHelpMarker(retraceTooltip.c_str());
            ImGui::Checkbox("Spatial Shift     ", (bool*)&m_ui.debugOutputSettings.ptPathViz.paths.spatialShift.enabled);         ImGui::SameLine(); ImGui::ColorEdit3("##Spatial Shift Color     ", &m_ui.debugOutputSettings.ptPathViz.paths.spatialShift.color.x);
            ImGui::Checkbox("Spatial Inv Shift ", (bool*)&m_ui.debugOutputSettings.ptPathViz.paths.spatialInverseShift.enabled);  ImGui::SameLine(); ImGui::ColorEdit3("##Spatial Inv Shift Color ", &m_ui.debugOutputSettings.ptPathViz.paths.spatialInverseShift.color.x);
            ImGui::Checkbox("Normals           ", (bool*)&m_ui.debugOutputSettings.ptPathViz.paths.normal.enabled);               ImGui::SameLine(); ImGui::ColorEdit3("##Normals Color           ", &m_ui.debugOutputSettings.ptPathViz.paths.normal.color.x);
            ImGui::Checkbox("NEE Lights        ", (bool*)&m_ui.debugOutputSettings.ptPathViz.paths.nee.enabled);                  ImGui::SameLine(); ImGui::ColorEdit3("##NEE Lights Color        ", &m_ui.debugOutputSettings.ptPathViz.paths.nee.color.x);
            if (!pathVizEnabled)
                ImGui::EndDisabled();
            ImGui::TreePop();
        }
        if (ImGui_ColoredTreeNodeEx("Intermediate Buffer Visualization", c_ColorRegularHeader, ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::PushItemWidth(150.f);

            ImGui::RadioButton("Off", (int*)&m_ui.debugOutputSettings.renderOutputMode, Off);

            ImGui::RadioButton("Visualization Overlay", (int*)&m_ui.debugOutputSettings.renderOutputMode, DebugRenderOutputMode::VisualizationOverlay);
            ShowHelpMarker(
                "For HDR signals, displays a horizontal cross-section of the specified channel.\n"
                "The cross-section is taken in the middle of the screen, at the yellow line.\n"
                "Horizontal lines show the values in log scale: the yellow line in the middle is 1.0,\n"
                "above it are 10, 100, etc., and below it are 0.1, 0.01, etc.\n"
                "The yellow \"fire\" at the bottom is shown where the displayed value is 0.\n"
                "For confidence, shows a heat map with blue at full confidence and red at zero.");
            if (m_ui.debugOutputSettings.renderOutputMode != DebugRenderOutputMode::VisualizationOverlay) ImGui::BeginDisabled();
            ImGui::Combo("Visualization Overlays", (int*)&m_ui.debugOutputSettings.visualizationOverlayMode,
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
            if (m_ui.debugOutputSettings.renderOutputMode != DebugRenderOutputMode::VisualizationOverlay) ImGui::EndDisabled();

            ImGui::RadioButton("Intermediate Texture Display", (int*)&m_ui.debugOutputSettings.renderOutputMode, DebugRenderOutputMode::TextureBlit);
            ShowHelpMarker("Display a texture other than the final color.\n"
                "Packed data will be unpacked before display.\n");
            if (m_ui.debugOutputSettings.renderOutputMode != DebugRenderOutputMode::TextureBlit) ImGui::BeginDisabled();
            ImGui::Combo("Debug Render Target", (int*)&m_ui.debugOutputSettings.textureBlitMode,
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
                "Direct Lighting Raw\0"
                "Indirect Lighting Raw\0"
                "PSRDepth\0"
                "PSRNormalRoughness\0"
                "PSRMotionVectors\0"
                "PSRHitT\0"
                "PSRDiffuseAlbedo\0"
                "PSRSpecularF0\0"
                "PT Duplication Map\0"
                "Smoothed PT Duplication Map\0"
                "PT Decorrelation Factor\0"
                "PT Sample ID\0"
                "Neighbor Selection GBuffer\0"
            );
            if (m_ui.debugOutputSettings.renderOutputMode != DebugRenderOutputMode::TextureBlit)
            {
                ImGui::EndDisabled();
            }

            m_ui.lightingSettings.restirShaderDebugParams.outputDebugDirectLighting = m_ui.debugOutputSettings.textureBlitMode == DebugTextureBlitMode::DirectLightingRaw;
            m_ui.lightingSettings.restirShaderDebugParams.outputDebugIndirectLighting = m_ui.debugOutputSettings.textureBlitMode == DebugTextureBlitMode::IndirectLightingRaw;
            m_ui.lightingSettings.restirShaderDebugParams.visualizePTDecorrelationFactor = m_ui.debugOutputSettings.textureBlitMode == DebugTextureBlitMode::PTDecorrelationFactor;

            ImGui::RadioButton("Reservoir Subfield Display", (int*)&m_ui.debugOutputSettings.renderOutputMode, DebugRenderOutputMode::ReservoirSubfield);
            ShowHelpMarker("Display a component of one of the ReSTIR reservoirs.\n");
            if (m_ui.debugOutputSettings.renderOutputMode != DebugRenderOutputMode::ReservoirSubfield) ImGui::BeginDisabled();
            ImGui::Combo("Reservoir", (int*)&m_ui.debugOutputSettings.reservoirSubfieldVizMode,
                "DI Reservoirs\0"
                "GI Reservoirs\0"
                "PT Reservoirs\0"
            );
            switch (m_ui.debugOutputSettings.reservoirSubfieldVizMode)
            {
            case ReservoirSubfieldVizMode::DIReservoir:
                ImGui::Combo("DI reservoir subfield", (int*)&m_ui.debugOutputSettings.diReservoirVizField,
                    "Light data\0"
                    "UV data\0"
                    "Target Pdf\0"
                    "M\0"
                    "Packed visibility\0"
                    "Spatial distance\0"
                    "Age\0"
                    "Canonical weight\0"
                );
                break;
                break;
            case ReservoirSubfieldVizMode::GIReservoir:
                ImGui::Combo("GI reservoir subfield", (int*)&m_ui.debugOutputSettings.giReservoirVizField,
                    "Position\0"
                    "Normal\0"
                    "Radiance\0"
                    "Weight sum\0"
                    "M\0"
                    "Age\0"
                );
                break;
            case ReservoirSubfieldVizMode::PTReservoir:
                ImGui::Combo("PT reservoir subfield", (int*)&m_ui.debugOutputSettings.ptReservoirVizField,
                    "Translated world position\0"
                    "Weight sum\0"
                    "World normal\0"
                    "M\0"
                    "Radiance\0"
                    "Age\0"
                    "RcWiPdf\0"
                    "Partial Jacobian\0"
                    "Rc vertex length\0"
                    "Path length\0"
                    "Random seed\0"
                    "Random index\0"
                    "Target function\0"
                );
                break;
            }
            if (m_ui.debugOutputSettings.renderOutputMode != DebugRenderOutputMode::ReservoirSubfield) ImGui::EndDisabled();

            ImGui::RadioButton("NRD Validation", (int*)&m_ui.debugOutputSettings.renderOutputMode, DebugRenderOutputMode::NrdValidation);

            m_ui.nrdDebugSettings.enableValidation = m_ui.debugOutputSettings.renderOutputMode == DebugRenderOutputMode::NrdValidation;
            if (!m_ui.nrdDebugSettings.enableValidation) ImGui::BeginDisabled();
            ImGui::Checkbox("Overlay on final image", &m_ui.nrdDebugSettings.overlayOnFinalImage);
            if (m_ui.debugOutputSettings.renderOutputMode != DebugRenderOutputMode::NrdValidation) ImGui::EndDisabled();

            ImGui::PopItemWidth();
            ImGui::TreePop();
        }
        ImGui::TreePop();
    }
}

#ifdef WITH_NRD
void UserInterface::DenoiserSettings()
{
    const nrd::LibraryDesc& nrdLibraryDesc = *nrd::GetLibraryDesc();

    char s[128];
    snprintf(s, sizeof(s) - 1, "Denoising (NRD v%u.%u.%u)", nrdLibraryDesc.versionMajor, nrdLibraryDesc.versionMinor, nrdLibraryDesc.versionBuild);

    std::string denoiserCombo;
    denoiserCombo += "Off";
    denoiserCombo.push_back(0);
#if WITH_NRD
    denoiserCombo += "NRD ReLAX\0";
    denoiserCombo.push_back(0);
    denoiserCombo += "NRD ReBLUR\0";
    denoiserCombo.push_back(0);
#endif
#if DONUT_WITH_DLSS
    if (m_ui.dlssRRSupported)
    {
        denoiserCombo += "DLSS-RR\0";
        denoiserCombo.push_back(0);
    }
#endif

    if (ImGui_ColoredTreeNode(s, c_ColorAttentionHeader))
    {
        if (ImGui::Combo("Denoiser mode", (int*)&m_ui.denoiserMode, denoiserCombo.c_str()))
        {
            m_ui.resetAccumulation = true;
#if DONUT_WITH_DLSS
            // Auto-apply the denoiser compatibility preset on mode change: DLSS-RR wants the
            // decorrelation / firefly-replacement settings, every other denoiser wants them off.
            if (m_ui.autoApplyDLSSRRPreset)
            {
                if (m_ui.denoiserMode == DenoiserMode::DLSS_RR)
                    m_ui.ApplyDLSSRRPreset();
                else if (m_ui.denoiserMode != DenoiserMode::NONE)
                    m_ui.ApplyNonDLSSRRPreset();
            }
#endif
        }

#if DONUT_WITH_DLSS
        if (m_ui.dlssRRSupported)
            ImGui::Checkbox("Auto-apply denoiser compatibility preset", &m_ui.autoApplyDLSSRRPreset);
#endif

#ifdef WITH_NRD
        if (m_ui.denoiserMode == DenoiserMode::NRD_RELAX || m_ui.denoiserMode == DenoiserMode::NRD_REBLUR)
        {
            ImGui::Checkbox("Advanced Settings", &m_showAdvancedDenoisingSettings);

            ImGui::Checkbox("Enable PSR", (bool*)&m_ui.lightingSettings.enableDenoiserPSR);
            ImGui::Checkbox("Use PSR motion vectors for ReSTIR PT", (bool*)&m_ui.lightingSettings.usePSRMvecForResampling);
            ImGui::Checkbox("Update PSR with Resampling", (bool*)&m_ui.lightingSettings.updatePSRwithResampling);

            if (ImGui::TreeNode("Debug"))
            {
                ImGui::SliderFloat("Splitscreen ratio", (float*)&m_ui.nrdDebugSettings.splitScreen, 0.0f, 1.0f);
                ImGui::TreePop();
            }

            bool useReLAX = false;
            if (m_ui.denoiserMode == DenoiserMode::NRD_RELAX)
            {
                m_ui.nrdDenoisingMethod = nrd::Denoiser::RELAX_DIFFUSE_SPECULAR;
                useReLAX = true;
            }
            else if(m_ui.denoiserMode == DenoiserMode::NRD_REBLUR)
                m_ui.nrdDenoisingMethod = nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR;

            ImGui::SameLine();
            if (ImGui::Button("Reset Settings"))
                m_ui.SetDefaultDenoiserSettings();

            ImGui::PushItemWidth(160.f);

            ImGui::SliderFloat("Accumulation time (sec)", &m_ui.accumulationTime, 0.0f, 1.0f, "%.2f");
            double fps = 1.0 / std::max(GetDeviceManager()->GetAverageFrameTimeSeconds(), 0.001);

            // Use "accumulation time" instead of "accumulated frames", since the former is FPS independent
            uint32_t maxHistoryLength = std::min(uint32_t(m_ui.accumulationTime * fps + 0.5), 40u);

            // Don't be very greedy, RESTIR regresses to "initial samples" in disocclusions (and some other cases)
            uint32_t maxFastHistoryLength = std::max(maxHistoryLength / 6u, 2u); // 6x faster "fast" history

            m_ui.relaxSettings.diffuseMaxAccumulatedFrameNum = maxHistoryLength;
            m_ui.relaxSettings.specularMaxAccumulatedFrameNum = maxHistoryLength;
            m_ui.relaxSettings.diffuseMaxFastAccumulatedFrameNum = maxFastHistoryLength;
            m_ui.relaxSettings.specularMaxFastAccumulatedFrameNum = maxFastHistoryLength;

            m_ui.reblurSettings.maxAccumulatedFrameNum = maxHistoryLength;
            m_ui.reblurSettings.maxFastAccumulatedFrameNum = maxFastHistoryLength;
            m_ui.reblurSettings.maxStabilizedFrameNum = maxHistoryLength;

            ImGui::Separator();
            ImGui::PushItemWidth(160.f);
            ImGui::BeginDisabled();
            ImGui::SliderInt("Max history length", (int32_t*)&maxHistoryLength, 0, 60);
            ImGui::SliderInt("Max fast history length", (int32_t*)&maxFastHistoryLength, 0, 60);
            ImGui::EndDisabled();

            if (m_showAdvancedDenoisingSettings)
            {
                if (useReLAX)
                {
                    ImGui::Checkbox("Anti-firefly", &m_ui.relaxSettings.enableAntiFirefly);
                    ImGui::SliderFloat2("Pre-pass radius (px)", &m_ui.relaxSettings.diffusePrepassBlurRadius, 0.0f, 50.0f, "%.1f");
                    ImGui::SliderInt("A-trous iterations", (int*)&m_ui.relaxSettings.atrousIterationNum, 2, 8);
                    ImGui::SliderFloat2("Diff-Spec luma weight", &m_ui.relaxSettings.diffusePhiLuminance, 0.0f, 10.0f, "%.1f");
                    ImGui::SliderFloat2("Min luma weight", &m_ui.relaxSettings.diffuseMinLuminanceWeight, 0.0f, 1.0f, "%.2f");
                    ImGui::SliderFloat("Depth threshold", &m_ui.relaxSettings.depthThreshold, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
                    ImGui::SliderFloat("Lobe fraction", &m_ui.relaxSettings.lobeAngleFraction, 0.0f, 1.0f, "%.2f");
                    ImGui::SliderFloat("Roughness fraction", &m_ui.relaxSettings.roughnessFraction, 0.0f, 1.0f, "%.2f");
                    ImGui::SliderFloat("Min hitT weight", &m_ui.relaxSettings.minHitDistanceWeight, 0.01f, 0.2f, "%.2f");
                    ImGui::SliderFloat("Spec variance boost", &m_ui.relaxSettings.specularVarianceBoost, 0.0f, 8.0f, "%.2f");
                    ImGui::SliderFloat("Clamping sigma scale", &m_ui.relaxSettings.fastHistoryClampingSigmaScale, 0.0f, 10.0f, "%.1f");
                    ImGui::SliderInt("History threshold", (int*)&m_ui.relaxSettings.spatialVarianceEstimationHistoryThreshold, 0, 10);
                    ImGui::Text("Luminance / Normal / Roughness:");
                    ImGui::SliderFloat3("Relaxation", &m_ui.relaxSettings.luminanceEdgeStoppingRelaxation, 0.0f, 1.0f, "%.2f");

                    ImGui::Text("HISTORY FIX:");
                    ImGui::SliderFloat("Normal weight power", &m_ui.relaxSettings.historyFixEdgeStoppingNormalPower, 0.0f, 128.0f, "%.1f");
                    ImGui::SliderInt("Frames", (int*)&m_ui.relaxSettings.historyFixFrameNum, 0, 5);
                    ImGui::SliderInt("Stride", (int*)&m_ui.relaxSettings.historyFixBasePixelStride, 1, 20);

                    ImGui::Text("ANTI-LAG:");
                    ImGui::SliderFloat("Acceleration amount", &m_ui.relaxSettings.antilagSettings.accelerationAmount, 0.0f, 1.0f, "%.2f");
                    ImGui::SliderFloat2("S/T sigma scales", &m_ui.relaxSettings.antilagSettings.spatialSigmaScale, 0.0f, 10.0f, "%.1f");
                    ImGui::SliderFloat("Reset amount", &m_ui.relaxSettings.antilagSettings.resetAmount, 0.0f, 1.0f, "%.2f");
                }
                else
                {
                    ImGui::Checkbox("Anti-firefly", &m_ui.reblurSettings.enableAntiFirefly);
                    ImGui::SliderFloat2("Pre-pass radius (px)", &m_ui.reblurSettings.diffusePrepassBlurRadius, 0.0f, 50.0f, "%.1f");
                    ImGui::SliderFloat("Min blur radius (px)", &m_ui.reblurSettings.minBlurRadius, 0.0f, 5.0f, "%.1f");
                    ImGui::SliderFloat("Max blur radius (px)", &m_ui.reblurSettings.maxBlurRadius, 0.0f, 30.0f, "%.1f");
                    ImGui::SliderFloat("Lobe fraction", &m_ui.reblurSettings.lobeAngleFraction, 0.0f, 1.0f, "%.2f");
                    ImGui::SliderFloat("Roughness fraction", &m_ui.reblurSettings.roughnessFraction, 0.0f, 1.0f, "%.2f");
                    ImGui::SliderFloat("Min hitT weight", &m_ui.reblurSettings.minHitDistanceWeight, 0.01f, 0.2f, "%.2f");
                    ImGui::SliderInt("History fix frames", (int*)&m_ui.reblurSettings.historyFixFrameNum, 0, 5);
                    ImGui::SliderInt("History fix stride", (int*)&m_ui.reblurSettings.historyFixBasePixelStride, 1, 20);

                    if (m_ui.reblurSettings.maxAccumulatedFrameNum && m_ui.reblurSettings.maxStabilizedFrameNum)
                    {
                        ImGui::Text("ANTI-LAG:");
                        ImGui::SliderFloat("Sigma scale", &m_ui.reblurSettings.antilagSettings.luminanceSigmaScale, 1.0f, 5.0f, "%.1f");
                        ImGui::SliderFloat("Sensitivity", &m_ui.reblurSettings.antilagSettings.luminanceSensitivity, 1.0f, 5.0f, "%.1f");
                    }
                }

                ImGui::PopItemWidth();
            }

            // Confidence
            ImGui::Separator();
            ImGui::Checkbox("Use Confidence Input", (bool*)&m_ui.lightingSettings.enableGradients);
            if (m_ui.lightingSettings.enableGradients && m_showAdvancedDenoisingSettings)
            {
                ImGui::SliderFloat("Gradient Sensitivity", &m_ui.lightingSettings.gradientSensitivity, 1.f, 20.f);
                ImGui::SliderFloat("Darkness Bias (EV)", &m_ui.lightingSettings.gradientLogDarknessBias, -16.f, -4.f);
                ImGui::SliderFloat("Confidence History Length", &m_ui.lightingSettings.confidenceHistoryLength, 0.f, 3.f);
            }
        }
#endif // WITH_NRD
        ImGui::TreePop();
    }

    ImGui::Separator();
}
#endif

void UserInterface::CopySelectedLight() const
{
    Json::Value root(Json::objectValue);

    m_selectedLight->Store(root);

    {
        auto n = m_selectedLight->GetNode();
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
        m_ui.lightingSettings.enableTransparentGeometry = m_ui.gbufferSettings.enableTransparentGeometry;
        m_ui.resetAccumulation |= ImGui::Checkbox("Transparent Geometry", (bool*)&m_ui.gbufferSettings.enableTransparentGeometry);
        m_ui.lightingSettings.enableAlphaTestedGeometry = m_ui.gbufferSettings.enableAlphaTestedGeometry;

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
                        m_ui.resetAccumulation = true;
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
        if (ImGui::BeginCombo("Select Light", m_selectedLight ? m_selectedLight->GetName().c_str() : "(None)"))
        {
            int unnamedLightCount = 1;
            for (const auto& light : m_ui.resources->scene->GetSceneGraph()->GetLights())
            {
                if (light->GetLightType() == LightType_Environment)
                    continue;

                bool selected = m_selectedLight == light;
                if (light->GetName().empty())
                    continue;

                ImGui::Selectable(light->GetName().c_str(), &selected);

                if (selected)
                {
                    m_selectedLight = light;
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (m_selectedLight)
        {
            ImGui::PushItemWidth(200.f);
            switch (m_selectedLight->GetLightType())
            {
            case LightType_Directional:
            {
                engine::DirectionalLight& dirLight = static_cast<engine::DirectionalLight&>(*m_selectedLight);
                if (app::LightEditor_Directional(dirLight))
                    m_ui.environmentMapDirty = 1;
                break;
            }
            case LightType_Spot:
            {
                SpotLightWithProfile& spotLight = static_cast<SpotLightWithProfile&>(*m_selectedLight);
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
                engine::PointLight& pointLight = static_cast<engine::PointLight&>(*m_selectedLight);
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
                CylinderLight& cylinderLight = static_cast<CylinderLight&>(*m_selectedLight);
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
                DiskLight& diskLight = static_cast<DiskLight&>(*m_selectedLight);
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
                RectLight& rectLight = static_cast<RectLight&>(*m_selectedLight);
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
        DebugSettings();
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

    m_ui.restirPT.initialSampling.maxRcVertexLength = m_ui.restirPT.initialSampling.maxBounceDepth + 1;
    if (m_ui.lightingSettings.ptParameters.copyReSTIRDISimilarityThresholds)
    {
        m_ui.lightingSettings.ptParameters.nee.spatialResamplingParams.depthThreshold = m_ui.restirDI.spatialResamplingParams.depthThreshold;
        m_ui.lightingSettings.ptParameters.nee.spatialResamplingParams.normalThreshold = m_ui.restirDI.spatialResamplingParams.normalThreshold;
    }
    if (m_ui.restirPT.hybridMirrorInitial)
    {
        m_ui.restirPT.hybridShift.maxBounceDepth = m_ui.restirPT.initialSampling.maxBounceDepth;
        m_ui.restirPT.hybridShift.maxRcVertexLength = m_ui.restirPT.initialSampling.maxRcVertexLength;
    }
}
