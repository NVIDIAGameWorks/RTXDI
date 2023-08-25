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
class EnvironmentLight;

class GlassPass
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

    GlassPass(
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
        const EnvironmentLight& environmentLight,
        float normalMapScale,
        bool enableMaterialReadback,
        dm::int2 materialReadbackPosition);

    void NextFrame();
};
