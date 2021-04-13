/***************************************************************************
 # Copyright (c) 2020-2021, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#include "GBufferPass.h"
#include "RenderTargets.h"
#include "Profiler.h"
#include "SampleScene.h"

#include <donut/engine/BindlessScene.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>
#include <donut/core/log.h>
#include <nvrhi/utils.h>

#include <utility>

using namespace donut::math;
#include "../shaders/ShaderParameters.h"

using namespace donut::engine;


RaytracedGBufferPass::RaytracedGBufferPass(
    nvrhi::IDevice* device,
    std::shared_ptr<donut::engine::ShaderFactory> shaderFactory,
    std::shared_ptr<donut::engine::CommonRenderPasses> commonPasses,
    std::shared_ptr<donut::engine::BindlessScene> bindlessScene,
    std::shared_ptr<Profiler> profiler,
    nvrhi::IBindingLayout* bindlessLayout)
    : m_Device(device)
    , m_BindlessLayout(bindlessLayout)
    , m_ShaderFactory(std::move(shaderFactory))
    , m_CommonPasses(std::move(commonPasses))
    , m_BindlessScene(std::move(bindlessScene))
    , m_Profiler(std::move(profiler))
{
    m_ConstantBuffer = m_Device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(GBufferConstants), "GBufferPassConstants", 16));

    nvrhi::BindingLayoutDesc globalBindingLayoutDesc;
    globalBindingLayoutDesc.visibility = nvrhi::ShaderType::Compute;
    globalBindingLayoutDesc.bindings = {
        nvrhi::BindingLayoutItem::Texture_UAV(0),
        nvrhi::BindingLayoutItem::Texture_UAV(1),
        nvrhi::BindingLayoutItem::Texture_UAV(2),
        nvrhi::BindingLayoutItem::Texture_UAV(3),
        nvrhi::BindingLayoutItem::Texture_UAV(4),
        nvrhi::BindingLayoutItem::Texture_UAV(5),
        nvrhi::BindingLayoutItem::Texture_UAV(6),
        nvrhi::BindingLayoutItem::Texture_UAV(7),
        nvrhi::BindingLayoutItem::TypedBuffer_UAV(8),

        nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
        nvrhi::BindingLayoutItem::PushConstants(1, sizeof(PerPassConstants)),
        nvrhi::BindingLayoutItem::RayTracingAccelStruct(0),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3),
        nvrhi::BindingLayoutItem::Sampler(0)
    };

    m_BindingLayout = m_Device->createBindingLayout(globalBindingLayoutDesc);
}

void RaytracedGBufferPass::CreatePipeline(bool useRayQuery)
{
    m_Pass.Init(m_Device, *m_ShaderFactory, "app/RaytracedGBuffer.hlsl", {}, useRayQuery, 16, m_BindingLayout, m_BindlessLayout);
}

void RaytracedGBufferPass::CreateBindingSet(
    nvrhi::rt::IAccelStruct* topLevelAS,
    nvrhi::rt::IAccelStruct* prevTopLevelAS,
    const RenderTargets& renderTargets)
{
    for (int currentFrame = 0; currentFrame <= 1; currentFrame++)
    {
        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings = {
            nvrhi::BindingSetItem::Texture_UAV(0, currentFrame ? renderTargets.Depth : renderTargets.PrevDepth),
            nvrhi::BindingSetItem::Texture_UAV(1, currentFrame ? renderTargets.GBufferBaseColor : renderTargets.PrevGBufferBaseColor),
            nvrhi::BindingSetItem::Texture_UAV(2, currentFrame ? renderTargets.GBufferMetalRough : renderTargets.PrevGBufferMetalRough),
            nvrhi::BindingSetItem::Texture_UAV(3, currentFrame ? renderTargets.GBufferNormals : renderTargets.PrevGBufferNormals),
            nvrhi::BindingSetItem::Texture_UAV(4, currentFrame ? renderTargets.GBufferGeoNormals : renderTargets.PrevGBufferGeoNormals),
            nvrhi::BindingSetItem::Texture_UAV(5, renderTargets.GBufferEmissive),
            nvrhi::BindingSetItem::Texture_UAV(6, renderTargets.MotionVectors),
            nvrhi::BindingSetItem::Texture_UAV(7, renderTargets.NormalRoughness),
            nvrhi::BindingSetItem::TypedBuffer_UAV(8, m_Profiler->GetRayCountBuffer()),

            nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
            nvrhi::BindingSetItem::PushConstants(1, sizeof(PerPassConstants)),
            nvrhi::BindingSetItem::RayTracingAccelStruct(0, currentFrame ? topLevelAS : prevTopLevelAS),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(1, m_BindlessScene->GetInstanceBuffer()),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(2, m_BindlessScene->GetGeometryBuffer()),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(3, m_BindlessScene->GetMaterialBuffer()),
            nvrhi::BindingSetItem::Sampler(0, m_CommonPasses->m_AnisotropicWrapSampler)
        };

        const nvrhi::BindingSetHandle bindingSet = m_Device->createBindingSet(bindingSetDesc, m_BindingLayout);

        if (currentFrame)
            m_BindingSet = bindingSet;
        else
            m_PrevBindingSet = bindingSet;
    }
}

void RaytracedGBufferPass::Render(
    nvrhi::ICommandList* commandList, 
    const donut::engine::IView& view,
    const donut::engine::IView& viewPrev,
    const GBufferSettings& settings)
{
    commandList->beginMarker("GBufferFill");

    GBufferConstants constants;
    view.FillPlanarViewConstants(constants.view);
    viewPrev.FillPlanarViewConstants(constants.viewPrev);
    constants.roughnessOverride = (settings.enableRoughnessOverride) ? settings.roughnessOverride : -1.f;
    constants.metalnessOverride = (settings.enableMetalnessOverride) ? settings.metalnessOverride : -1.f;
    constants.normalMapScale = settings.normalMapScale;
    constants.enableAlphaTestedGeometry = settings.enableAlphaTestedGeometry;
    constants.enableTransparentGeometry = settings.enableTransparentGeometry;
    constants.materialReadbackBufferIndex = ProfilerSection::MaterialReadback * 2;
    constants.materialReadbackPosition = (settings.enableMaterialReadback) ? settings.materialReadbackPosition : int2(-1, -1);
    commandList->writeBuffer(m_ConstantBuffer, &constants, sizeof(constants));

    PerPassConstants pushConstants{};
    pushConstants.rayCountBufferIndex = ProfilerSection::GBufferFill;

    m_Pass.Execute(
        commandList, 
        view.GetViewExtent().width(), 
        view.GetViewExtent().height(), 
        m_BindingSet, 
        m_BindlessScene->GetDescriptorTable(),
        &pushConstants,
        sizeof(pushConstants));

    commandList->endMarker();
}

void RaytracedGBufferPass::NextFrame()
{
    std::swap(m_BindingSet, m_PrevBindingSet);
}

RasterizedGBufferPass::RasterizedGBufferPass(
    nvrhi::IDevice* device,
    std::shared_ptr<donut::engine::ShaderFactory> shaderFactory,
    std::shared_ptr<donut::engine::CommonRenderPasses> commonPasses,
    std::shared_ptr<donut::engine::BindlessScene> bindlessScene,
    std::shared_ptr<Profiler> profiler,
    nvrhi::IBindingLayout* bindlessLayout)
    : m_Device(device)
    , m_BindlessLayout(bindlessLayout)
    , m_ShaderFactory(std::move(shaderFactory))
    , m_CommonPasses(std::move(commonPasses))
    , m_BindlessScene(std::move(bindlessScene))
    , m_Profiler(std::move(profiler))
{
    m_ConstantBuffer = m_Device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(GBufferConstants), "GBufferPassConstants", 16));

    nvrhi::BindingLayoutDesc globalBindingLayoutDesc;
    globalBindingLayoutDesc.visibility = nvrhi::ShaderType::Vertex | nvrhi::ShaderType::Pixel;
    globalBindingLayoutDesc.bindings = {
        nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
        nvrhi::BindingLayoutItem::PushConstants(1, sizeof(uint32_t)),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2),
        nvrhi::BindingLayoutItem::Sampler(0)
    };

    m_BindingLayout = m_Device->createBindingLayout(globalBindingLayoutDesc);

}

void RasterizedGBufferPass::CreateBindingSet()
{
    nvrhi::BindingSetDesc bindingSetDesc;

    bindingSetDesc.bindings = {
        nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
        nvrhi::BindingSetItem::PushConstants(1, sizeof(uint32_t)),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(0, m_BindlessScene->GetInstanceBuffer()),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(1, m_BindlessScene->GetGeometryBuffer()),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(2, m_BindlessScene->GetMaterialBuffer()),
        nvrhi::BindingSetItem::Sampler(0, m_CommonPasses->m_AnisotropicWrapSampler)
    };

    m_BindingSet = m_Device->createBindingSet(bindingSetDesc, m_BindingLayout);
}

void RasterizedGBufferPass::CreatePipeline(const RenderTargets& renderTargets)
{
    donut::log::info("Initializing RasterizedGBufferPass...");

    std::vector<ShaderMacro> macros = { { "ALPHA_TESTED", "0"} };

    nvrhi::GraphicsPipelineDesc pipelineDesc;

    pipelineDesc.bindingLayouts = { m_BindingLayout, m_BindlessLayout };
    pipelineDesc.VS = m_ShaderFactory->CreateShader("app/RasterizedGBuffer.hlsl", "vs_main", nullptr, nvrhi::ShaderType::Vertex);
    pipelineDesc.PS = m_ShaderFactory->CreateShader("app/RasterizedGBuffer.hlsl", "ps_main", &macros, nvrhi::ShaderType::Pixel);
    pipelineDesc.primType = nvrhi::PrimitiveType::TRIANGLE_LIST;
    pipelineDesc.renderState.rasterState.frontCounterClockwise = true;
    pipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterState::CULL_BACK;
    pipelineDesc.renderState.depthStencilState.depthEnable = true;
    pipelineDesc.renderState.depthStencilState.depthFunc = nvrhi::DepthStencilState::COMPARISON_GREATER;

    auto* framebuffer = renderTargets.GBufferFramebuffer->GetFramebuffer(nvrhi::AllSubresources);

    m_OpaquePipeline = m_Device->createGraphicsPipeline(pipelineDesc, framebuffer);

    macros[0].definition = "1"; // ALPHA_TESTED
    pipelineDesc.PS = m_ShaderFactory->CreateShader("app/RasterizedGBuffer.hlsl", "ps_main", &macros, nvrhi::ShaderType::Pixel);
    pipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterState::CULL_NONE;

    m_AlphaTestedPipeline = m_Device->createGraphicsPipeline(pipelineDesc, framebuffer);
}

void RasterizedGBufferPass::Render(
    nvrhi::ICommandList* commandList,
    const donut::engine::IView& view,
    const donut::engine::IView& viewPrev,
    const RenderTargets& renderTargets,
    const SampleScene& scene,
    const GBufferSettings& settings)
{
    commandList->beginMarker("GBufferFill");

    commandList->clearDepthStencilTexture(renderTargets.DeviceDepth, nvrhi::AllSubresources, true, 0.f, false, 0);
    commandList->clearTextureFloat(renderTargets.Depth, nvrhi::AllSubresources, nvrhi::Color(0.f));


    GBufferConstants constants;
    view.FillPlanarViewConstants(constants.view);
    viewPrev.FillPlanarViewConstants(constants.viewPrev);
    constants.roughnessOverride = (settings.enableRoughnessOverride) ? settings.roughnessOverride : -1.f;
    constants.metalnessOverride = (settings.enableMetalnessOverride) ? settings.metalnessOverride : -1.f;
    constants.normalMapScale = settings.normalMapScale;
    commandList->writeBuffer(m_ConstantBuffer, &constants, sizeof(constants));

    const auto& instances = scene.GetMeshInstances();

    const auto viewFrustum = view.GetViewFrustum();

    for (int alphaTested = 0; alphaTested <= 1; alphaTested++)
    {
        if (alphaTested && !settings.enableAlphaTestedGeometry)
            break;

        nvrhi::GraphicsState state;
        state.pipeline = alphaTested ? m_AlphaTestedPipeline : m_OpaquePipeline;
        state.bindings = { m_BindingSet, m_BindlessScene->GetDescriptorTable() };
        state.framebuffer = renderTargets.GBufferFramebuffer->GetFramebuffer(nvrhi::AllSubresources);
        state.viewport = view.GetViewportState();
        commandList->setGraphicsState(state);

        nvrhi::DrawArguments args{};
        args.instanceCount = 1;

        for (const auto* instance : instances)
        {
            const auto materialDomain = instance->mesh->material->domain;
            
            if ((materialDomain == MD_OPAQUE) == alphaTested)
                continue;

            if (!viewFrustum.intersectsWith(instance->transformedBounds))
                continue;

            commandList->setPushConstants(&instance->globalInstanceIndex, sizeof(uint32_t));

            args.vertexCount = instance->mesh->numIndices;
            commandList->draw(args);
        }
    }

    commandList->endMarker();
}
