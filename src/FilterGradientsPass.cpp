/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#include "FilterGradientsPass.h"
#include "RenderTargets.h"

#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>
#include <donut/core/log.h>
#include <nvrhi/utils.h>

using namespace donut::math;
#include "../shaders/ShaderParameters.h"

using namespace donut::engine;

static const int c_NumFilterPasses = 4;

FilterGradientsPass::FilterGradientsPass(
    nvrhi::IDevice* device,
    std::shared_ptr<ShaderFactory> shaderFactory)
    : m_Device(device)
    , m_ShaderFactory(shaderFactory)
{
    nvrhi::BindingLayoutDesc bindingLayoutDesc;
    bindingLayoutDesc.visibility = nvrhi::ShaderType::Compute;
    bindingLayoutDesc.bindings = {
        nvrhi::BindingLayoutItem::Texture_UAV(0),
        nvrhi::BindingLayoutItem::PushConstants(0, sizeof(FilterGradientsConstants))
    };

    m_BindingLayout = m_Device->createBindingLayout(bindingLayoutDesc);
}

void FilterGradientsPass::CreatePipeline()
{
    donut::log::debug("Initializing FilterGradientsPass...");

    m_ComputeShader = m_ShaderFactory->CreateShader("app/FilterGradientsPass.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);

    nvrhi::ComputePipelineDesc pipelineDesc;
    pipelineDesc.bindingLayouts = { m_BindingLayout };
    pipelineDesc.CS = m_ComputeShader;
    m_ComputePipeline = m_Device->createComputePipeline(pipelineDesc);
}

void FilterGradientsPass::CreateBindingSet(const RenderTargets& renderTargets)
{
    nvrhi::BindingSetDesc bindingSetDesc;

    bindingSetDesc.bindings = {
        nvrhi::BindingSetItem::Texture_UAV(0, renderTargets.Gradients),
        nvrhi::BindingSetItem::PushConstants(0, sizeof(FilterGradientsConstants))
    };

    m_BindingSet = m_Device->createBindingSet(bindingSetDesc, m_BindingLayout);

    m_GradientsTexture = renderTargets.Gradients;
}

void FilterGradientsPass::Render(
    nvrhi::ICommandList* commandList,
    const donut::engine::IView& view,
    bool checkerboard)
{
    commandList->beginMarker("Filter Gradients");

    FilterGradientsConstants constants = {};
    constants.viewportSize = dm::uint2(view.GetViewExtent().width(), view.GetViewExtent().height());
    if (checkerboard) constants.viewportSize.x /= 2;
    constants.checkerboard = checkerboard;
    
    nvrhi::ComputeState state;
    state.bindings = { m_BindingSet };
    state.pipeline = m_ComputePipeline;
    commandList->setComputeState(state);

    for (int passIndex = 0; passIndex < c_NumFilterPasses; passIndex++)
    {
        constants.passIndex = passIndex;
        commandList->setPushConstants(&constants, sizeof(constants));

        commandList->dispatch(
            dm::div_ceil(view.GetViewExtent().width(), 8),
            dm::div_ceil(view.GetViewExtent().height(), 8),
            1);

        nvrhi::utils::TextureUavBarrier(commandList, m_GradientsTexture);
        commandList->commitBarriers();
    }

    commandList->endMarker();
}

int FilterGradientsPass::GetOutputBufferIndex()
{
    return c_NumFilterPasses & 1;
}
