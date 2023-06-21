/***************************************************************************
 # Copyright (c) 2020-2022, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#include "RenderPass.h"
#include "RenderTargets.h"
#include "RtxdiResources.h"

#include <donut/engine/Scene.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>
#include <donut/core/log.h>
#include <nvrhi/utils.h>
#include <rtxdi/ReSTIRDI.h>

using namespace donut::math;
#include "../shaders/ShaderParameters.h"

using namespace donut::engine;

RenderPass::RenderPass(
    nvrhi::IDevice* device, 
    std::shared_ptr<ShaderFactory> shaderFactory,
    std::shared_ptr<donut::engine::CommonRenderPasses> commonPasses,
    std::shared_ptr<donut::engine::Scene> scene,
    nvrhi::IBindingLayout* bindlessLayout
)
    : m_Device(device)
    , m_BindlessLayout(bindlessLayout)
    , m_ShaderFactory(std::move(shaderFactory))
    , m_CommonPasses(std::move(commonPasses))
    , m_Scene(std::move(scene))
{
    // The binding layout descriptor must match the binding set descriptor defined in CreateBindingSet(...) below

    nvrhi::BindingLayoutDesc globalBindingLayoutDesc;
    globalBindingLayoutDesc.visibility = nvrhi::ShaderType::Compute | nvrhi::ShaderType::AllRayTracing;
    globalBindingLayoutDesc.bindings = {
        nvrhi::BindingLayoutItem::Texture_SRV(0),
        nvrhi::BindingLayoutItem::Texture_SRV(1),
        nvrhi::BindingLayoutItem::Texture_SRV(2),
        nvrhi::BindingLayoutItem::Texture_SRV(3),
        nvrhi::BindingLayoutItem::Texture_SRV(4),

        nvrhi::BindingLayoutItem::RayTracingAccelStruct(30),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(32),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(33),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(34),

        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(20),
        nvrhi::BindingLayoutItem::TypedBuffer_SRV(21),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(22),

        nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0),
        nvrhi::BindingLayoutItem::Texture_UAV(1),
        nvrhi::BindingLayoutItem::Texture_UAV(2),
        nvrhi::BindingLayoutItem::Texture_UAV(3),
        nvrhi::BindingLayoutItem::Texture_UAV(4),
        nvrhi::BindingLayoutItem::Texture_UAV(5),
        nvrhi::BindingLayoutItem::Texture_UAV(6),
        
        nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
        nvrhi::BindingLayoutItem::Sampler(0),
        nvrhi::BindingLayoutItem::Sampler(1),
    };

    m_BindingLayout = m_Device->createBindingLayout(globalBindingLayoutDesc);

    m_ConstantBuffer = m_Device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(ResamplingConstants), "ResamplingConstants", 16));
}

void RenderPass::CreateBindingSet(
    nvrhi::rt::IAccelStruct* topLevelAS,
    const RenderTargets& renderTargets,
    const RtxdiResources& resources)
{
    assert(&renderTargets);
    assert(&resources);

    for (int currentFrame = 0; currentFrame <= 1; currentFrame++)
    {
        // This list must match the binding declarations in RtxdiApplicationBridge.hlsli

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings = {
            nvrhi::BindingSetItem::Texture_SRV(0, currentFrame ? renderTargets.PrevDepth : renderTargets.Depth),
            nvrhi::BindingSetItem::Texture_SRV(1, currentFrame ? renderTargets.PrevGBufferNormals : renderTargets.GBufferNormals),
            nvrhi::BindingSetItem::Texture_SRV(2, currentFrame ? renderTargets.PrevGBufferGeoNormals : renderTargets.GBufferGeoNormals),
            nvrhi::BindingSetItem::Texture_SRV(3, currentFrame ? renderTargets.PrevGBufferDiffuseAlbedo : renderTargets.GBufferDiffuseAlbedo),
            nvrhi::BindingSetItem::Texture_SRV(4, currentFrame ? renderTargets.PrevGBufferSpecularRough : renderTargets.GBufferSpecularRough),
            
            nvrhi::BindingSetItem::RayTracingAccelStruct(30, topLevelAS),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(32, m_Scene->GetInstanceBuffer()),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(33, m_Scene->GetGeometryBuffer()),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(34, m_Scene->GetMaterialBuffer()),

            nvrhi::BindingSetItem::StructuredBuffer_SRV(20, resources.LightDataBuffer),
            nvrhi::BindingSetItem::TypedBuffer_SRV(21, resources.NeighborOffsetsBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(22, resources.GeometryInstanceToLightBuffer),

            nvrhi::BindingSetItem::StructuredBuffer_UAV(0, resources.LightReservoirBuffer),
            nvrhi::BindingSetItem::Texture_UAV(1, renderTargets.HdrColor),
            nvrhi::BindingSetItem::Texture_UAV(2, currentFrame ? renderTargets.Depth : renderTargets.PrevDepth),
            nvrhi::BindingSetItem::Texture_UAV(3, currentFrame ? renderTargets.GBufferNormals : renderTargets.PrevGBufferNormals),
            nvrhi::BindingSetItem::Texture_UAV(4, currentFrame ? renderTargets.GBufferGeoNormals : renderTargets.PrevGBufferGeoNormals),
            nvrhi::BindingSetItem::Texture_UAV(5, currentFrame ? renderTargets.GBufferDiffuseAlbedo : renderTargets.PrevGBufferDiffuseAlbedo),
            nvrhi::BindingSetItem::Texture_UAV(6, currentFrame ? renderTargets.GBufferSpecularRough : renderTargets.PrevGBufferSpecularRough),
            
            nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
            nvrhi::BindingSetItem::Sampler(0, m_CommonPasses->m_LinearWrapSampler),
            nvrhi::BindingSetItem::Sampler(1, m_CommonPasses->m_LinearWrapSampler)
        };

        const nvrhi::BindingSetHandle bindingSet = m_Device->createBindingSet(bindingSetDesc, m_BindingLayout);

        if (currentFrame)
            m_BindingSet = bindingSet;
        else
            m_PrevBindingSet = bindingSet;
    }
    
    m_LightReservoirBuffer = resources.LightReservoirBuffer;
}

void RenderPass::CreatePipeline()
{
    m_ComputeShader = m_ShaderFactory->CreateShader("app/Render.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);

    nvrhi::ComputePipelineDesc pipelineDesc;
    pipelineDesc.bindingLayouts = { m_BindingLayout, m_BindlessLayout };
    pipelineDesc.CS = m_ComputeShader;
    m_ComputePipeline = m_Device->createComputePipeline(pipelineDesc);
}

void RenderPass::Render(
    nvrhi::ICommandList* commandList,
    rtxdi::ReSTIRDIContext& context,
    const donut::engine::IView& view,
    const donut::engine::IView& previousView,
    const Settings& localSettings,
    const RTXDI_LightBufferParameters& lightBufferParams)
{
    ResamplingConstants constants = {};
    constants.frameIndex = context.getFrameIndex();
    view.FillPlanarViewConstants(constants.view);
    previousView.FillPlanarViewConstants(constants.prevView);

    constants.enableResampling = localSettings.enableResampling;
    constants.unbiasedMode = localSettings.unbiasedMode;
    constants.numInitialSamples = localSettings.numInitialSamples;
    constants.numInitialBRDFSamples = localSettings.numInitialBRDFSamples;
    constants.numSpatialSamples = localSettings.numSpatialSamples;
    constants.restirDIReservoirBufferParams = context.getReservoirBufferParameters();
    constants.lightBufferParams = lightBufferParams;
    constants.runtimeParams.neighborOffsetMask = context.getStaticParameters().NeighborOffsetCount - 1;
    constants.runtimeParams.activeCheckerboardField = 0;

    constants.inputBufferIndex = !(context.getFrameIndex() & 1);
    constants.outputBufferIndex = context.getFrameIndex() & 1;
    
    commandList->writeBuffer(m_ConstantBuffer, &constants, sizeof(constants));

    commandList->beginMarker("Render");

    nvrhi::ComputeState state;
    state.bindings = { m_BindingSet, m_Scene->GetDescriptorTable() };
    state.pipeline = m_ComputePipeline;
    commandList->setComputeState(state);

    commandList->dispatch(
        dm::div_ceil(view.GetViewExtent().width(), RTXDI_SCREEN_SPACE_GROUP_SIZE),
        dm::div_ceil(view.GetViewExtent().height(), RTXDI_SCREEN_SPACE_GROUP_SIZE));

    commandList->endMarker();
}

void RenderPass::NextFrame()
{
    std::swap(m_BindingSet, m_PrevBindingSet);
}
