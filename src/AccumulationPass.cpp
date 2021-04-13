/***************************************************************************
 # Copyright (c) 2020-2021, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#include "AccumulationPass.h"
#include "RenderTargets.h"

#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>
#include <donut/core/log.h>
#include <nvrhi/utils.h>

using namespace donut::math;
#include "../shaders/ShaderParameters.h"

using namespace donut::engine;


AccumulationPass::AccumulationPass(
    nvrhi::IDevice* device, 
    std::shared_ptr<ShaderFactory> shaderFactory)
    : m_Device(device)
    , m_ShaderFactory(shaderFactory)
{
    nvrhi::BindingLayoutDesc bindingLayoutDesc;
    bindingLayoutDesc.visibility = nvrhi::ShaderType::Compute;
    bindingLayoutDesc.bindings = {
        nvrhi::BindingLayoutItem::Texture_SRV(0),
        nvrhi::BindingLayoutItem::Texture_UAV(0),
        nvrhi::BindingLayoutItem::PushConstants(0, sizeof(AccumulationConstants))
    };

    m_BindingLayout = m_Device->createBindingLayout(bindingLayoutDesc);
}

void AccumulationPass::CreatePipeline()
{
    donut::log::info("Initializing AccumulationPass...");

    m_ComputeShader = m_ShaderFactory->CreateShader("app/AccumulationPass.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);

    nvrhi::ComputePipelineDesc pipelineDesc;
    pipelineDesc.bindingLayouts = { m_BindingLayout };
    pipelineDesc.CS = m_ComputeShader;
    m_ComputePipeline = m_Device->createComputePipeline(pipelineDesc);
}

void AccumulationPass::CreateBindingSet(const RenderTargets& renderTargets)
{
    nvrhi::BindingSetDesc bindingSetDesc;

    bindingSetDesc.bindings = {
        nvrhi::BindingSetItem::Texture_SRV(0, renderTargets.HdrColor),
        nvrhi::BindingSetItem::Texture_UAV(0, renderTargets.AccumulatedColor),
        nvrhi::BindingSetItem::PushConstants(0, sizeof(AccumulationConstants))
    };

    m_BindingSet = m_Device->createBindingSet(bindingSetDesc, m_BindingLayout);
}

void AccumulationPass::Render(
    nvrhi::ICommandList* commandList, 
    const donut::engine::IView& view,
    float accumulationWeight)
{
    commandList->beginMarker("Accumulation");

    AccumulationConstants constants = {};
    constants.blendFactor = accumulationWeight;

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
