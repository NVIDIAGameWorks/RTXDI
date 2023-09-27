/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#include "GlassPass.h"
#include "RenderTargets.h"
#include "SampleScene.h"

#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>
#include <nvrhi/utils.h>

#include "Profiler.h"

using namespace donut::math;
#include "../shaders/ShaderParameters.h"

using namespace donut::engine;


GlassPass::GlassPass(
    nvrhi::IDevice* device,
    std::shared_ptr<donut::engine::ShaderFactory> shaderFactory,
    std::shared_ptr<donut::engine::CommonRenderPasses> commonPasses,
    std::shared_ptr<donut::engine::Scene> scene,
    std::shared_ptr<Profiler> profiler,
    nvrhi::IBindingLayout* bindlessLayout)
    : m_Device(device)
    , m_ShaderFactory(shaderFactory)
    , m_CommonPasses(commonPasses)
    , m_Scene(scene)
    , m_BindlessLayout(bindlessLayout)
    , m_Profiler(profiler)
{
    m_ConstantBuffer = m_Device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(GlassConstants), "GlassConstants", 16));

    nvrhi::BindingLayoutDesc globalBindingLayoutDesc;
    globalBindingLayoutDesc.visibility = nvrhi::ShaderType::Compute | nvrhi::ShaderType::AllRayTracing;
    globalBindingLayoutDesc.bindings = {
        nvrhi::BindingLayoutItem::RayTracingAccelStruct(0),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3),
        nvrhi::BindingLayoutItem::Texture_SRV(4),
        nvrhi::BindingLayoutItem::Sampler(0),
        nvrhi::BindingLayoutItem::Sampler(1),
        nvrhi::BindingLayoutItem::Texture_UAV(0),
        nvrhi::BindingLayoutItem::TypedBuffer_UAV(1),
        nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
        nvrhi::BindingLayoutItem::PushConstants(1, sizeof(PerPassConstants)),
    };

    m_BindingLayout = m_Device->createBindingLayout(globalBindingLayoutDesc);
}

void GlassPass::CreatePipeline(bool useRayQuery)
{
    m_Pass.Init(m_Device, *m_ShaderFactory, "app/GlassPass.hlsl", {}, useRayQuery, 16, m_BindingLayout, nullptr, m_BindlessLayout);
}

void GlassPass::CreateBindingSet(
    nvrhi::rt::IAccelStruct* topLevelAS,
    nvrhi::rt::IAccelStruct* prevTopLevelAS,
    const RenderTargets& renderTargets)
{
    for (int currentFrame = 0; currentFrame <= 1; currentFrame++)
    {
        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings = {
            nvrhi::BindingSetItem::RayTracingAccelStruct(0, currentFrame ? topLevelAS : prevTopLevelAS),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(1, m_Scene->GetInstanceBuffer()),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(2, m_Scene->GetGeometryBuffer()),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(3, m_Scene->GetMaterialBuffer()),
            nvrhi::BindingSetItem::Texture_SRV(4, renderTargets.GBufferEmissive),
            nvrhi::BindingSetItem::Sampler(0, m_CommonPasses->m_LinearWrapSampler),
            nvrhi::BindingSetItem::Sampler(1, m_CommonPasses->m_LinearWrapSampler),
            nvrhi::BindingSetItem::Texture_UAV(0, renderTargets.HdrColor),
            nvrhi::BindingSetItem::TypedBuffer_UAV(1, m_Profiler->GetRayCountBuffer()),
            nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
            nvrhi::BindingSetItem::PushConstants(1, sizeof(PerPassConstants)),
        };

        const nvrhi::BindingSetHandle bindingSet = m_Device->createBindingSet(bindingSetDesc, m_BindingLayout);

        if (currentFrame)
            m_BindingSet = bindingSet;
        else
            m_PrevBindingSet = bindingSet;
    }
}

void GlassPass::Render(
    nvrhi::ICommandList* commandList, 
    const donut::engine::IView& view,
    const EnvironmentLight& environmentLight,
    float normalMapScale,
    bool enableMaterialReadback,
    dm::int2 materialReadbackPosition)
{
    commandList->beginMarker("Glass");

    GlassConstants constants = {};
    view.FillPlanarViewConstants(constants.view);
    constants.enableEnvironmentMap = (environmentLight.textureIndex >= 0);
    constants.environmentMapTextureIndex = (environmentLight.textureIndex >= 0) ? environmentLight.textureIndex : 0;
    constants.environmentScale = environmentLight.radianceScale.x;
    constants.environmentRotation = environmentLight.rotation;
    constants.normalMapScale = normalMapScale;
    constants.materialReadbackBufferIndex = ProfilerSection::MaterialReadback * 2;
    constants.materialReadbackPosition = enableMaterialReadback ? materialReadbackPosition : int2(-1, -1);
    commandList->writeBuffer(m_ConstantBuffer, &constants, sizeof(constants));

    PerPassConstants pushConstants{};
    pushConstants.rayCountBufferIndex = ProfilerSection::Glass;

    m_Pass.Execute(commandList, view.GetViewExtent().width(), view.GetViewExtent().height(), 
        m_BindingSet, nullptr, m_Scene->GetDescriptorTable(), &pushConstants, sizeof(pushConstants));

    commandList->endMarker();
}

void GlassPass::NextFrame()
{
    std::swap(m_BindingSet, m_PrevBindingSet);
}
