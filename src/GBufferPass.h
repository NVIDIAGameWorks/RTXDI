/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#pragma once

#include "RayTracingPass.h"

#include <donut/core/math/math.h>
#include <nvrhi/nvrhi.h>
#include <memory>

namespace donut::engine
{
    class Scene;
    class CommonRenderPasses;
    class IView;
    class ShaderFactory;
}

class RenderTargets;
class Profiler;
class SampleScene;

typedef int ibool;

struct GBufferSettings
{
    float roughnessOverride = 0.5f;
    float metalnessOverride = 0.5f;
    bool enableRoughnessOverride = false;
    bool enableMetalnessOverride = false;
    float normalMapScale = 1.f;
    ibool enableAlphaTestedGeometry = true;
    ibool enableTransparentGeometry = true;
    float textureLodBias = -1.f;

    bool enableMaterialReadback = false;
    dm::int2 materialReadbackPosition = 0;
};

class RaytracedGBufferPass
{
private:
    nvrhi::DeviceHandle m_Device;

    RayTracingPass m_Pass;
    nvrhi::BindingLayoutHandle m_BindingLayout;
    nvrhi::BindingLayoutHandle m_BindlessLayout;
    nvrhi::BindingSetHandle m_BindingSet;
    nvrhi::BindingSetHandle m_PrevBindingSet;

    nvrhi::BufferHandle m_ConstantBuffer;

    std::shared_ptr<donut::engine::ShaderFactory> m_ShaderFactory;
    std::shared_ptr<donut::engine::CommonRenderPasses> m_CommonPasses;
    std::shared_ptr<donut::engine::Scene> m_Scene;
    std::shared_ptr<Profiler> m_Profiler;

public:

    RaytracedGBufferPass(
        nvrhi::IDevice* device,
        std::shared_ptr<donut::engine::ShaderFactory> shaderFactory,
        std::shared_ptr<donut::engine::CommonRenderPasses> commonPasses,
        std::shared_ptr<donut::engine::Scene> scene,
        std::shared_ptr<Profiler> profiler,
        nvrhi::IBindingLayout* bindlessLayout);
    
    void CreatePipeline(bool useRayQuery);

    void CreateBindingSet(
        nvrhi::rt::IAccelStruct* topLevelAS,
        nvrhi::rt::IAccelStruct* prevTopLevelAS,
        const RenderTargets& renderTargets);

    void Render(
        nvrhi::ICommandList* commandList,
        const donut::engine::IView& view,
        const donut::engine::IView& viewPrev,
        const GBufferSettings& settings);

    void NextFrame();
};

class RasterizedGBufferPass
{
private:
    nvrhi::DeviceHandle m_Device;

    nvrhi::GraphicsPipelineHandle m_OpaquePipeline;
    nvrhi::GraphicsPipelineHandle m_AlphaTestedPipeline;
    nvrhi::BindingLayoutHandle m_BindingLayout;
    nvrhi::BindingLayoutHandle m_BindlessLayout;
    nvrhi::BindingSetHandle m_BindingSet;

    nvrhi::BufferHandle m_ConstantBuffer;

    std::shared_ptr<donut::engine::ShaderFactory> m_ShaderFactory;
    std::shared_ptr<donut::engine::CommonRenderPasses> m_CommonPasses;
    std::shared_ptr<donut::engine::Scene> m_Scene;
    std::shared_ptr<Profiler> m_Profiler;
public:
    RasterizedGBufferPass(
        nvrhi::IDevice* device,
        std::shared_ptr<donut::engine::ShaderFactory> shaderFactory,
        std::shared_ptr<donut::engine::CommonRenderPasses> commonPasses,
        std::shared_ptr<donut::engine::Scene> scene,
        std::shared_ptr<Profiler> profiler,
        nvrhi::IBindingLayout* bindlessLayout);

    void CreatePipeline(const RenderTargets& renderTargets);

    void CreateBindingSet();

    void Render(
        nvrhi::ICommandList* commandList,
        const donut::engine::IView& view,
        const donut::engine::IView& viewPrev,
        const RenderTargets& renderTargets,
        const GBufferSettings& settings);
};

class PostprocessGBufferPass
{
private:
    nvrhi::DeviceHandle m_Device;

    nvrhi::ShaderHandle m_ComputeShader;
    nvrhi::ComputePipelineHandle m_ComputePipeline;
    nvrhi::BindingLayoutHandle m_BindingLayout;
    nvrhi::BindingLayoutHandle m_BindlessLayout;
    nvrhi::BindingSetHandle m_BindingSet;
    nvrhi::BindingSetHandle m_PrevBindingSet;
    
    std::shared_ptr<donut::engine::ShaderFactory> m_ShaderFactory;

public:

    PostprocessGBufferPass(
        nvrhi::IDevice* device,
        std::shared_ptr<donut::engine::ShaderFactory> shaderFactory);

    void CreatePipeline();

    void CreateBindingSet(
        const RenderTargets& renderTargets);

    void Render(
        nvrhi::ICommandList* commandList,
        const donut::engine::IView& view);

    void NextFrame();
};