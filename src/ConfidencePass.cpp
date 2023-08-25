/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#include "ConfidencePass.h"
#include "FilterGradientsPass.h"
#include "RenderTargets.h"

#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>
#include <donut/core/log.h>
#include <nvrhi/utils.h>


using namespace donut::math;
#include "../shaders/ShaderParameters.h"

using namespace donut::engine;

ConfidencePass::ConfidencePass(
    nvrhi::IDevice* device, 
    std::shared_ptr<ShaderFactory> shaderFactory)
    : m_Device(device)
    , m_ShaderFactory(shaderFactory)
{
    nvrhi::BindingLayoutDesc bindingLayoutDesc;
    bindingLayoutDesc.visibility = nvrhi::ShaderType::Compute;
    bindingLayoutDesc.bindings = {
        nvrhi::BindingLayoutItem::Texture_SRV(0),
        nvrhi::BindingLayoutItem::Texture_SRV(1),
        nvrhi::BindingLayoutItem::Texture_SRV(2),
        nvrhi::BindingLayoutItem::Texture_SRV(3),
        nvrhi::BindingLayoutItem::Texture_UAV(0),
        nvrhi::BindingLayoutItem::Texture_UAV(1),
        nvrhi::BindingLayoutItem::Sampler(0),
        nvrhi::BindingLayoutItem::PushConstants(0, sizeof(ConfidenceConstants))
    };

    m_BindingLayout = m_Device->createBindingLayout(bindingLayoutDesc);

    auto samplerDesc = nvrhi::SamplerDesc()
        .setAllFilters(true)
        .setAllAddressModes(nvrhi::SamplerAddressMode::ClampToBorder)
        .setBorderColor(nvrhi::Color(0.f));

    m_Sampler = device->createSampler(samplerDesc);
}

void ConfidencePass::CreatePipeline()
{
    donut::log::debug("Initializing ConfidencePass...");

    m_ComputeShader = m_ShaderFactory->CreateShader("app/ConfidencePass.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);

    nvrhi::ComputePipelineDesc pipelineDesc;
    pipelineDesc.bindingLayouts = { m_BindingLayout };
    pipelineDesc.CS = m_ComputeShader;
    m_ComputePipeline = m_Device->createComputePipeline(pipelineDesc);
}

void ConfidencePass::CreateBindingSet(const RenderTargets& renderTargets)
{
    for (int currentFrame = 0; currentFrame <= 1; currentFrame++)
    {
        nvrhi::BindingSetDesc bindingSetDesc;

        bindingSetDesc.bindings = {
            nvrhi::BindingSetItem::Texture_SRV(0, renderTargets.Gradients),
            nvrhi::BindingSetItem::Texture_SRV(1, renderTargets.MotionVectors),
            nvrhi::BindingSetItem::Texture_SRV(2, currentFrame ? renderTargets.PrevDiffuseConfidence : renderTargets.DiffuseConfidence),
            nvrhi::BindingSetItem::Texture_SRV(3, currentFrame ? renderTargets.PrevSpecularConfidence : renderTargets.SpecularConfidence),
            nvrhi::BindingSetItem::Texture_UAV(0, currentFrame ? renderTargets.DiffuseConfidence : renderTargets.PrevDiffuseConfidence),
            nvrhi::BindingSetItem::Texture_UAV(1, currentFrame ? renderTargets.SpecularConfidence : renderTargets.PrevSpecularConfidence),
            nvrhi::BindingSetItem::Sampler(0, m_Sampler),
            nvrhi::BindingSetItem::PushConstants(0, sizeof(ConfidenceConstants))
        };

        nvrhi::BindingSetHandle bindingSet = m_Device->createBindingSet(bindingSetDesc, m_BindingLayout);
        if (currentFrame)
            m_BindingSet = bindingSet;
        else
            m_PrevBindingSet = bindingSet;
    }

    m_GradientsTexture = renderTargets.Gradients;
}

void ConfidencePass::Render(
    nvrhi::ICommandList* commandList, 
    const donut::engine::IView& view,
    float logDarknessBias,
    float sensitivity,
    float historyLength,
    bool checkerboard)
{
    commandList->beginMarker("Confidence");

    const auto& gradientsDesc = m_GradientsTexture->getDesc();

    ConfidenceConstants constants = {};
    constants.viewportSize = dm::uint2(view.GetViewExtent().width(), view.GetViewExtent().height());
    constants.invGradientTextureSize.x = 1.f / float(gradientsDesc.width);
    constants.invGradientTextureSize.y = 1.f / float(gradientsDesc.height);
    constants.darknessBias = ::exp2f(logDarknessBias);
    constants.sensitivity = sensitivity;
    constants.checkerboard = checkerboard;
    constants.blendFactor = 1.f / (historyLength + 1.f);
    constants.inputBufferIndex = FilterGradientsPass::GetOutputBufferIndex();

    nvrhi::ComputeState state;
    state.bindings = { m_BindingSet };
    state.pipeline = m_ComputePipeline;
    commandList->setComputeState(state);

    commandList->setPushConstants(&constants, sizeof(constants));
    
    commandList->dispatch(
        dm::div_ceil(view.GetViewExtent().width(), 8), 
        dm::div_ceil(view.GetViewExtent().height(), 8), 
        1);

    commandList->endMarker();
}

void ConfidencePass::NextFrame()
{
    std::swap(m_BindingSet, m_PrevBindingSet);
}
