/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
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

#include <donut/engine/Scene.h>
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
    std::shared_ptr<donut::engine::Scene> scene,
    std::shared_ptr<Profiler> profiler,
    nvrhi::IBindingLayout* bindlessLayout)
    : m_Device(device)
    , m_BindlessLayout(bindlessLayout)
    , m_ShaderFactory(std::move(shaderFactory))
    , m_CommonPasses(std::move(commonPasses))
    , m_Scene(std::move(scene))
    , m_Profiler(std::move(profiler))
{
    m_ConstantBuffer = m_Device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(GBufferConstants), "GBufferPassConstants", 16));

    nvrhi::BindingLayoutDesc globalBindingLayoutDesc;
    globalBindingLayoutDesc.visibility = nvrhi::ShaderType::Compute | nvrhi::ShaderType::AllRayTracing;
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
    m_Pass.Init(m_Device, *m_ShaderFactory, "app/RaytracedGBuffer.hlsl", {}, useRayQuery, 16, m_BindingLayout, nullptr, m_BindlessLayout);
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
            nvrhi::BindingSetItem::Texture_UAV(1, currentFrame ? renderTargets.GBufferDiffuseAlbedo : renderTargets.PrevGBufferDiffuseAlbedo),
            nvrhi::BindingSetItem::Texture_UAV(2, currentFrame ? renderTargets.GBufferSpecularRough : renderTargets.PrevGBufferSpecularRough),
            nvrhi::BindingSetItem::Texture_UAV(3, currentFrame ? renderTargets.GBufferNormals : renderTargets.PrevGBufferNormals),
            nvrhi::BindingSetItem::Texture_UAV(4, currentFrame ? renderTargets.GBufferGeoNormals : renderTargets.PrevGBufferGeoNormals),
            nvrhi::BindingSetItem::Texture_UAV(5, renderTargets.GBufferEmissive),
            nvrhi::BindingSetItem::Texture_UAV(6, renderTargets.MotionVectors),
            nvrhi::BindingSetItem::Texture_UAV(7, renderTargets.DeviceDepthUAV),
            nvrhi::BindingSetItem::TypedBuffer_UAV(8, m_Profiler->GetRayCountBuffer()),

            nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
            nvrhi::BindingSetItem::PushConstants(1, sizeof(PerPassConstants)),
            nvrhi::BindingSetItem::RayTracingAccelStruct(0, currentFrame ? topLevelAS : prevTopLevelAS),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(1, m_Scene->GetInstanceBuffer()),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(2, m_Scene->GetGeometryBuffer()),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(3, m_Scene->GetMaterialBuffer()),
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
    constants.textureLodBias = settings.textureLodBias;
    constants.textureGradientScale = powf(2.f, settings.textureLodBias);
    commandList->writeBuffer(m_ConstantBuffer, &constants, sizeof(constants));

    PerPassConstants pushConstants{};
    pushConstants.rayCountBufferIndex = ProfilerSection::GBufferFill;

    m_Pass.Execute(
        commandList, 
        view.GetViewExtent().width(), 
        view.GetViewExtent().height(), 
        m_BindingSet,
        nullptr,
        m_Scene->GetDescriptorTable(),
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
    std::shared_ptr<donut::engine::Scene> scene,
    std::shared_ptr<Profiler> profiler,
    nvrhi::IBindingLayout* bindlessLayout)
    : m_Device(device)
    , m_BindlessLayout(bindlessLayout)
    , m_ShaderFactory(std::move(shaderFactory))
    , m_CommonPasses(std::move(commonPasses))
    , m_Scene(std::move(scene))
    , m_Profiler(std::move(profiler))
{
    m_ConstantBuffer = m_Device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(GBufferConstants), "GBufferPassConstants", 16));

    nvrhi::BindingLayoutDesc globalBindingLayoutDesc;
    globalBindingLayoutDesc.visibility = nvrhi::ShaderType::Vertex | nvrhi::ShaderType::Pixel;
    globalBindingLayoutDesc.bindings = {
        nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
        nvrhi::BindingLayoutItem::PushConstants(1, sizeof(uint2)),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2),
        nvrhi::BindingLayoutItem::Sampler(0),
        nvrhi::BindingLayoutItem::TypedBuffer_UAV(0)
    };

    m_BindingLayout = m_Device->createBindingLayout(globalBindingLayoutDesc);

}

void RasterizedGBufferPass::CreateBindingSet()
{
    nvrhi::BindingSetDesc bindingSetDesc;

    bindingSetDesc.bindings = {
        nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
        nvrhi::BindingSetItem::PushConstants(1, sizeof(uint2)),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(0, m_Scene->GetInstanceBuffer()),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(1, m_Scene->GetGeometryBuffer()),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(2, m_Scene->GetMaterialBuffer()),
        nvrhi::BindingSetItem::Sampler(0, m_CommonPasses->m_AnisotropicWrapSampler),
        nvrhi::BindingSetItem::TypedBuffer_UAV(0, m_Profiler->GetRayCountBuffer())
    };

    m_BindingSet = m_Device->createBindingSet(bindingSetDesc, m_BindingLayout);
}

void RasterizedGBufferPass::CreatePipeline(const RenderTargets& renderTargets)
{
    donut::log::debug("Initializing RasterizedGBufferPass...");

    std::vector<ShaderMacro> macros = { { "ALPHA_TESTED", "0"} };

    nvrhi::GraphicsPipelineDesc pipelineDesc;

    pipelineDesc.bindingLayouts = { m_BindingLayout, m_BindlessLayout };
    pipelineDesc.VS = m_ShaderFactory->CreateShader("app/RasterizedGBuffer.hlsl", "vs_main", nullptr, nvrhi::ShaderType::Vertex);
    pipelineDesc.PS = m_ShaderFactory->CreateShader("app/RasterizedGBuffer.hlsl", "ps_main", &macros, nvrhi::ShaderType::Pixel);
    pipelineDesc.primType = nvrhi::PrimitiveType::TriangleList;
    pipelineDesc.renderState.rasterState.frontCounterClockwise = true;
    pipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::Back;
    pipelineDesc.renderState.depthStencilState.depthTestEnable = true;
    pipelineDesc.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::Greater;

    auto* framebuffer = renderTargets.GBufferFramebuffer->GetFramebuffer(nvrhi::AllSubresources);

    m_OpaquePipeline = m_Device->createGraphicsPipeline(pipelineDesc, framebuffer);

    macros[0].definition = "1"; // ALPHA_TESTED
    pipelineDesc.PS = m_ShaderFactory->CreateShader("app/RasterizedGBuffer.hlsl", "ps_main", &macros, nvrhi::ShaderType::Pixel);
    pipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;

    m_AlphaTestedPipeline = m_Device->createGraphicsPipeline(pipelineDesc, framebuffer);
}

void RasterizedGBufferPass::Render(
    nvrhi::ICommandList* commandList,
    const donut::engine::IView& view,
    const donut::engine::IView& viewPrev,
    const RenderTargets& renderTargets,
    const GBufferSettings& settings)
{
    commandList->beginMarker("GBufferFill");

    commandList->clearDepthStencilTexture(renderTargets.DeviceDepth, nvrhi::AllSubresources, true, 0.f, false, 0);
    commandList->clearTextureFloat(renderTargets.Depth, nvrhi::AllSubresources, nvrhi::Color(BACKGROUND_DEPTH));


    GBufferConstants constants;
    view.FillPlanarViewConstants(constants.view);
    viewPrev.FillPlanarViewConstants(constants.viewPrev);
    constants.roughnessOverride = (settings.enableRoughnessOverride) ? settings.roughnessOverride : -1.f;
    constants.metalnessOverride = (settings.enableMetalnessOverride) ? settings.metalnessOverride : -1.f;
    constants.normalMapScale = settings.normalMapScale;
    constants.textureLodBias = settings.textureLodBias;
    constants.textureGradientScale = powf(2.f, settings.textureLodBias);
    constants.materialReadbackBufferIndex = ProfilerSection::MaterialReadback * 2;
    constants.materialReadbackPosition = (settings.enableMaterialReadback) ? settings.materialReadbackPosition : int2(-1, -1);
    commandList->writeBuffer(m_ConstantBuffer, &constants, sizeof(constants));

    nvrhi::IFramebuffer* framebuffer = renderTargets.GBufferFramebuffer->GetFramebuffer(nvrhi::AllSubresources);

    commandList->setEnableAutomaticBarriers(false);
    commandList->setResourceStatesForFramebuffer(framebuffer);
    commandList->commitBarriers();

    const auto& instances = m_Scene->GetSceneGraph()->GetMeshInstances();

    const auto viewFrustum = view.GetViewFrustum();

    for (int alphaTested = 0; alphaTested <= 1; alphaTested++)
    {
        if (alphaTested && !settings.enableAlphaTestedGeometry)
            break;

        nvrhi::GraphicsState state;
        state.pipeline = alphaTested ? m_AlphaTestedPipeline : m_OpaquePipeline;
        state.bindings = { m_BindingSet, m_Scene->GetDescriptorTable() };
        state.framebuffer = framebuffer;
        state.viewport = view.GetViewportState();
        commandList->setGraphicsState(state);

        nvrhi::DrawArguments args{};
        args.instanceCount = 1;

        for (const auto& instance : instances)
        {
            const auto& mesh = instance->GetMesh();
            const auto node = instance->GetNode();

            if (!node)
                continue;
            
            if (!viewFrustum.intersectsWith(node->GetGlobalBoundingBox()))
                continue;

            for (size_t geometryIndex = 0; geometryIndex < mesh->geometries.size(); geometryIndex++)
            {
                const auto& geometry = mesh->geometries[geometryIndex];
                const auto materialDomain = geometry->material->domain;

                if ((materialDomain == MaterialDomain::Opaque) == alphaTested)
                    continue;

                uint2 pushConstants = uint2(instance->GetInstanceIndex(), uint32_t(geometryIndex));

                commandList->setPushConstants(&pushConstants, sizeof(pushConstants));

                args.vertexCount = geometry->numIndices;
                commandList->draw(args);
            }
        }
    }

    commandList->setEnableAutomaticBarriers(true);

    commandList->endMarker();
}

PostprocessGBufferPass::PostprocessGBufferPass(nvrhi::IDevice* device, std::shared_ptr<donut::engine::ShaderFactory> shaderFactory)
    : m_Device(device)
    , m_ShaderFactory(std::move(shaderFactory))
{
    nvrhi::BindingLayoutDesc globalBindingLayoutDesc;
    globalBindingLayoutDesc.visibility = nvrhi::ShaderType::Compute;
    globalBindingLayoutDesc.bindings = {
        nvrhi::BindingLayoutItem::Texture_UAV(0),
        nvrhi::BindingLayoutItem::Texture_UAV(1),

        nvrhi::BindingLayoutItem::Texture_SRV(0),
        nvrhi::BindingLayoutItem::Texture_SRV(1)
    };

    m_BindingLayout = m_Device->createBindingLayout(globalBindingLayoutDesc);
}

void PostprocessGBufferPass::CreatePipeline()
{
    m_ComputeShader = m_ShaderFactory->CreateShader("app/PostprocessGBuffer.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);

    auto pipelineDesc = nvrhi::ComputePipelineDesc()
        .setComputeShader(m_ComputeShader)
        .addBindingLayout(m_BindingLayout);

    m_ComputePipeline = m_Device->createComputePipeline(pipelineDesc);
}

void PostprocessGBufferPass::CreateBindingSet(const RenderTargets& renderTargets)
{
    for (int currentFrame = 0; currentFrame <= 1; currentFrame++)
    {
        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings = {
            nvrhi::BindingSetItem::Texture_UAV(0, currentFrame ? renderTargets.GBufferSpecularRough : renderTargets.PrevGBufferSpecularRough),
            nvrhi::BindingSetItem::Texture_UAV(1, renderTargets.NormalRoughness),

            nvrhi::BindingSetItem::Texture_SRV(0, currentFrame ? renderTargets.GBufferNormals : renderTargets.PrevGBufferNormals),
            nvrhi::BindingSetItem::Texture_SRV(1, currentFrame ? renderTargets.Depth : renderTargets.PrevDepth)
        };

        const nvrhi::BindingSetHandle bindingSet = m_Device->createBindingSet(bindingSetDesc, m_BindingLayout);

        if (currentFrame)
            m_BindingSet = bindingSet;
        else
            m_PrevBindingSet = bindingSet;
    }
}

void PostprocessGBufferPass::Render(nvrhi::ICommandList* commandList, const donut::engine::IView& view)
{
    auto state = nvrhi::ComputeState()
        .setPipeline(m_ComputePipeline)
        .addBindingSet(m_BindingSet);

    commandList->setComputeState(state);
    commandList->dispatch(
        dm::div_ceil(view.GetViewExtent().width(), 16),
        dm::div_ceil(view.GetViewExtent().height(), 16));
}

void PostprocessGBufferPass::NextFrame()
{
    std::swap(m_BindingSet, m_PrevBindingSet);
}
