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
#include "rtxdi/ReSTIRDIParameters.h"

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

class RenderPass
{
private:
    nvrhi::DeviceHandle m_Device;

    nvrhi::ShaderHandle m_ComputeShader;
    nvrhi::ComputePipelineHandle m_ComputePipeline;

    nvrhi::BindingLayoutHandle m_BindingLayout;
    nvrhi::BindingLayoutHandle m_BindlessLayout;
    nvrhi::BindingSetHandle m_BindingSet;
    nvrhi::BindingSetHandle m_PrevBindingSet;
    nvrhi::BufferHandle m_ConstantBuffer;
    nvrhi::BufferHandle m_LightReservoirBuffer;
    
    std::shared_ptr<donut::engine::ShaderFactory> m_ShaderFactory;
    std::shared_ptr<donut::engine::CommonRenderPasses> m_CommonPasses;
    std::shared_ptr<donut::engine::Scene> m_Scene;
    
public:
    struct Settings
    {
        bool unbiasedMode = false;
        bool enableResampling = true;

        uint32_t numInitialSamples = 8;
        uint32_t numSpatialSamples = 1;
        uint32_t numInitialBRDFSamples = 1;
        float brdfCutoff = 0.f;
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
        const donut::engine::IView& previousView,
        const Settings& localSettings,
        const RTXDI_LightBufferParameters& lightBufferParams);

    void NextFrame();
};
