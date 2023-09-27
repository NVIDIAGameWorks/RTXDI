/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#include "PackedDataVizPass.h"

#include "../RenderTargets.h"
#include "../SampleScene.h"
#include "../UserInterface.h"

#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>
#include <donut/engine/Scene.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/core/log.h>
#include <nvrhi/utils.h>

#include <utility>

using namespace donut::math;
using namespace donut::engine;

PackedDataVizPass::PackedDataVizPass(
    nvrhi::IDevice* device,
    std::shared_ptr<donut::engine::ShaderFactory> shaderFactory,
    std::shared_ptr<donut::engine::Scene> scene,
    nvrhi::IBindingLayout* bindlessLayout) :
    m_Device(device)
    , m_BindlessLayout(bindlessLayout)
    , m_ShaderFactory(std::move(shaderFactory))
    , m_Scene(std::move(scene))
{
    nvrhi::BindingLayoutDesc bindingLayoutDesc;
    bindingLayoutDesc.visibility = nvrhi::ShaderType::Compute;
    bindingLayoutDesc.bindings = {
        nvrhi::BindingLayoutItem::Texture_SRV(0),
        nvrhi::BindingLayoutItem::Texture_UAV(0),
    };

    m_BindingLayout = m_Device->createBindingLayout(bindingLayoutDesc);
}

void PackedDataVizPass::CreatePipeline(const std::string& shaderPath)
{
    std::string debugMsg = "Initializing PackedDataVizPass with " + shaderPath + "...";
    donut::log::debug(debugMsg.c_str());
    gpuPerfMarker = "Packed Data Viz Pass:" + shaderPath;

    m_ComputeShader = m_ShaderFactory->CreateShader(shaderPath.c_str(), "main", nullptr, nvrhi::ShaderType::Compute);

    nvrhi::ComputePipelineDesc pipelineDesc;
    pipelineDesc.bindingLayouts = { m_BindingLayout, m_BindlessLayout };
    pipelineDesc.CS = m_ComputeShader;
    m_ComputePipeline = m_Device->createComputePipeline(pipelineDesc);
}

void PackedDataVizPass::CreateBindingSet(nvrhi::TextureHandle src, nvrhi::TextureHandle prevSrc, nvrhi::TextureHandle dst)
{
    nvrhi::BindingSetDesc bindingSetDesc;

    bindingSetDesc.bindings = {
        // GBuffer bindings --- CAUTION: these items are addressed below to swap the even and odd frames
        nvrhi::BindingSetItem::Texture_SRV(0, src),
        nvrhi::BindingSetItem::Texture_UAV(0, dst)
    };

    m_BindingSetEven = m_Device->createBindingSet(bindingSetDesc, m_BindingLayout);

    bindingSetDesc.bindings[0].resourceHandle = prevSrc;

    m_BindingSetOdd = m_Device->createBindingSet(bindingSetDesc, m_BindingLayout);
}

void PackedDataVizPass::Render(
    nvrhi::ICommandList* commandList,
    const donut::engine::IView& view)
{
    commandList->beginMarker(gpuPerfMarker.c_str());

    nvrhi::ComputeState state;
    state.bindings = { m_BindingSetEven, m_Scene->GetDescriptorTable() };
    state.pipeline = m_ComputePipeline;
    commandList->setComputeState(state);

    commandList->dispatch(
        dm::div_ceil(view.GetViewExtent().width(), 16),
        dm::div_ceil(view.GetViewExtent().height(), 16),
        1);

    commandList->endMarker();
}

void PackedDataVizPass::NextFrame()
{
    std::swap(m_BindingSetEven, m_BindingSetOdd);
}