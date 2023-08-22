/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#include "CompositingPass.h"
#include "RenderTargets.h"
#include "SampleScene.h"
#include "UserInterface.h"

#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>
#include <donut/engine/Scene.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/core/log.h>
#include <nvrhi/utils.h>

#include <utility>

using namespace donut::math;
#include "../shaders/ShaderParameters.h"

using namespace donut::engine;


CompositingPass::CompositingPass(
    nvrhi::IDevice* device, 
    std::shared_ptr<ShaderFactory> shaderFactory,
    std::shared_ptr<donut::engine::CommonRenderPasses> commonPasses,
    std::shared_ptr<donut::engine::Scene> scene,
    nvrhi::IBindingLayout* bindlessLayout)
    : m_Device(device)
    , m_BindlessLayout(bindlessLayout)
    , m_ShaderFactory(std::move(shaderFactory))
    , m_CommonPasses(std::move(commonPasses))
    , m_Scene(std::move(scene))
{
    m_ConstantBuffer = m_Device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(CompositingConstants), "CompositingConstants", 16));

    nvrhi::BindingLayoutDesc bindingLayoutDesc;
    bindingLayoutDesc.visibility = nvrhi::ShaderType::Compute;
    bindingLayoutDesc.bindings = {
        nvrhi::BindingLayoutItem::Texture_SRV(0),
        nvrhi::BindingLayoutItem::Texture_SRV(1),
        nvrhi::BindingLayoutItem::Texture_SRV(2),
        nvrhi::BindingLayoutItem::Texture_SRV(3),
        nvrhi::BindingLayoutItem::Texture_SRV(4),
        nvrhi::BindingLayoutItem::Texture_SRV(5),
        nvrhi::BindingLayoutItem::Texture_SRV(6),
        nvrhi::BindingLayoutItem::Texture_SRV(7),
        nvrhi::BindingLayoutItem::Texture_SRV(8),
        nvrhi::BindingLayoutItem::Texture_UAV(0),
        nvrhi::BindingLayoutItem::Texture_UAV(1),
        nvrhi::BindingLayoutItem::Sampler(0),
        nvrhi::BindingLayoutItem::VolatileConstantBuffer(0)
    };

    m_BindingLayout = m_Device->createBindingLayout(bindingLayoutDesc);
}

void CompositingPass::CreatePipeline()
{
    donut::log::debug("Initializing CompositingPass...");

    m_ComputeShader = m_ShaderFactory->CreateShader("app/CompositingPass.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);

    nvrhi::ComputePipelineDesc pipelineDesc;
    pipelineDesc.bindingLayouts = { m_BindingLayout, m_BindlessLayout };
    pipelineDesc.CS = m_ComputeShader;
    m_ComputePipeline = m_Device->createComputePipeline(pipelineDesc);
}

void CompositingPass::CreateBindingSet(const RenderTargets& renderTargets)
{
    nvrhi::BindingSetDesc bindingSetDesc;

    bindingSetDesc.bindings = {
        // GBuffer bindings --- CAUTION: these items are addressed below to swap the even and odd frames
        nvrhi::BindingSetItem::Texture_SRV(0, renderTargets.Depth),
        nvrhi::BindingSetItem::Texture_SRV(1, renderTargets.GBufferNormals),
        nvrhi::BindingSetItem::Texture_SRV(2, renderTargets.GBufferDiffuseAlbedo),
        nvrhi::BindingSetItem::Texture_SRV(3, renderTargets.GBufferSpecularRough),
        nvrhi::BindingSetItem::Texture_SRV(4, renderTargets.GBufferEmissive),

        nvrhi::BindingSetItem::Texture_SRV(5, renderTargets.DiffuseLighting),
        nvrhi::BindingSetItem::Texture_SRV(6, renderTargets.SpecularLighting),
        nvrhi::BindingSetItem::Texture_SRV(7, renderTargets.DenoisedDiffuseLighting),
        nvrhi::BindingSetItem::Texture_SRV(8, renderTargets.DenoisedSpecularLighting),
        nvrhi::BindingSetItem::Texture_UAV(0, renderTargets.HdrColor),
        nvrhi::BindingSetItem::Texture_UAV(1, renderTargets.MotionVectors),
        nvrhi::BindingSetItem::Sampler(0, m_CommonPasses->m_LinearWrapSampler),
        nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer)
    };

    m_BindingSetEven = m_Device->createBindingSet(bindingSetDesc, m_BindingLayout);

    bindingSetDesc.bindings[0].resourceHandle = renderTargets.PrevDepth;
    bindingSetDesc.bindings[1].resourceHandle = renderTargets.PrevGBufferNormals;
    bindingSetDesc.bindings[2].resourceHandle = renderTargets.PrevGBufferDiffuseAlbedo;
    bindingSetDesc.bindings[3].resourceHandle = renderTargets.PrevGBufferSpecularRough;

    m_BindingSetOdd = m_Device->createBindingSet(bindingSetDesc, m_BindingLayout);
}

void CompositingPass::Render(
    nvrhi::ICommandList* commandList, 
    const donut::engine::IView& view,
    const donut::engine::IView& viewPrev,
    const uint32_t denoiserMode,
    const bool checkerboard,
    const UIData& ui,
    const EnvironmentLight& environmentLight)
{
    commandList->beginMarker("Compositing");

    CompositingConstants constants = {};
    view.FillPlanarViewConstants(constants.view);
    viewPrev.FillPlanarViewConstants(constants.viewPrev);
    constants.enableTextures = ui.enableTextures;
    constants.denoiserMode = denoiserMode;
    constants.checkerboard = checkerboard;
    constants.enableEnvironmentMap = (environmentLight.textureIndex >= 0);
    constants.environmentMapTextureIndex = (environmentLight.textureIndex >= 0) ? environmentLight.textureIndex : 0;
    constants.environmentScale = environmentLight.radianceScale.x;
    constants.environmentRotation = environmentLight.rotation;
    constants.noiseMix = ui.noiseMix;
    constants.noiseClampLow = ui.noiseClampLow;
    constants.noiseClampHigh = ui.noiseClampHigh;
    commandList->writeBuffer(m_ConstantBuffer, &constants, sizeof(constants));

    nvrhi::ComputeState state;
    state.bindings = { m_BindingSetEven, m_Scene->GetDescriptorTable() };
    state.pipeline = m_ComputePipeline;
    commandList->setComputeState(state);

    commandList->dispatch(
        dm::div_ceil(view.GetViewExtent().width(), 8), 
        dm::div_ceil(view.GetViewExtent().height(), 8), 
        1);

    commandList->endMarker();
}

void CompositingPass::NextFrame()
{
    std::swap(m_BindingSetEven, m_BindingSetOdd);
}
