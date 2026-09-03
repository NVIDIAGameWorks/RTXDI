/***************************************************************************
 # Copyright (c) 2020-2022, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#pragma once

#include <donut/core/math/math.h>
#include <nvrhi/nvrhi.h>
#include <memory>
#include "Rtxdi/DI/ReSTIRDIParameters.h"

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
}

class RenderTargets;
class RtxdiResources;
class EnvironmentLight;
struct ResamplingConstants;

RTXDI_DIInitialSamplingParameters GetDefaultMinimalSampleInitialSamplingParams();
RTXDI_DISpatioTemporalResamplingParameters GetDefaultMinimalSpaioTemporalParams();

class RenderPass
{
public:
    struct Settings
    {
        bool unbiasedMode = false;
        bool enableResampling = true;

        RTXDI_DIInitialSamplingParameters initialSamplingParams = GetDefaultMinimalSampleInitialSamplingParams();
        RTXDI_DISpatioTemporalResamplingParameters spaioTemporalResamplingParams = GetDefaultMinimalSpaioTemporalParams();
    };

    RenderPass(
        nvrhi::IDevice* device,
        std::shared_ptr<donut::engine::ShaderFactory> shaderFactory,
        std::shared_ptr<donut::engine::CommonRenderPasses> commonPasses,
        std::shared_ptr<donut::engine::Scene> scene,
        nvrhi::IBindingLayout* bindlessLayout);

    void CreatePipeline();

    void CreateBindingSet(
        nvrhi::rt::IAccelStruct* topLevelAS,
        const RenderTargets& renderTargets,
        const RtxdiResources& resources);

    void Render(
        nvrhi::ICommandList* commandList,
        rtxdi::ReSTIRDIContext& context,
        const donut::engine::IView& view,
        const donut::engine::IView& prevView,
        const Settings& localSettings,
        const RTXDI_LightBufferParameters& lightBufferParams);

    void NextFrame();

private:
    nvrhi::DeviceHandle m_device;

    nvrhi::ShaderHandle m_computeShader;
    nvrhi::ComputePipelineHandle m_computePipeline;

    nvrhi::BindingLayoutHandle m_bindingLayout;
    nvrhi::BindingLayoutHandle m_bindlessLayout;
    nvrhi::BindingSetHandle m_bindingSet;
    nvrhi::BindingSetHandle m_prevBindingSet;
    nvrhi::BufferHandle m_constantBuffer;
    nvrhi::BufferHandle m_lightReservoirBuffer;

    std::shared_ptr<donut::engine::ShaderFactory> m_shaderFactory;
    std::shared_ptr<donut::engine::CommonRenderPasses> m_commonPasses;
    std::shared_ptr<donut::engine::Scene> m_scene;
};
