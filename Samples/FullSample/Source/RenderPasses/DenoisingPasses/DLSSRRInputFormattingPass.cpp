#include "DLSSRRInputFormattingPass.h"

#include <donut/core/log.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>

#include "RenderTargets.h"

using namespace donut::math;
#include "SharedShaderInclude/ShaderParameters.h"

DLSSRRInputFormattingPass::DLSSRRInputFormattingPass(
    nvrhi::IDevice* device,
    std::shared_ptr<donut::engine::ShaderFactory> shaderFactory) :
    m_device(device),
    m_shaderFactory(shaderFactory)
{
    nvrhi::BindingLayoutDesc bindingLayoutDesc;
    bindingLayoutDesc.visibility = nvrhi::ShaderType::Compute;
    bindingLayoutDesc.bindings = {
        nvrhi::BindingLayoutItem::PushConstants(0, sizeof(DLSSRRInputFormattingConstants)),
        nvrhi::BindingLayoutItem::Texture_SRV(0),
        nvrhi::BindingLayoutItem::Texture_SRV(1),
        nvrhi::BindingLayoutItem::Texture_UAV(0),
        nvrhi::BindingLayoutItem::Texture_UAV(1)
    };

    m_bindingLayout = m_device->createBindingLayout(bindingLayoutDesc);
}

void DLSSRRInputFormattingPass::CreatePipeline()
{
    donut::log::debug("Initializing FilterGradientsPass...");

    m_computeShader = m_shaderFactory->CreateShader("app/DenoisingPasses/DLSSRRInputFormattingPass.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);

    nvrhi::ComputePipelineDesc pipelineDesc;
    pipelineDesc.bindingLayouts = { m_bindingLayout };
    pipelineDesc.CS = m_computeShader;
    m_computePipeline = m_device->createComputePipeline(pipelineDesc);
}

void DLSSRRInputFormattingPass::CreateBindingSet(const RenderTargets& renderTargets)
{
    nvrhi::BindingSetDesc bindingSetDesc;

    bindingSetDesc.bindings = {
        nvrhi::BindingSetItem::PushConstants(0, sizeof(DLSSRRInputFormattingConstants)),
        nvrhi::BindingSetItem::Texture_SRV(0, renderTargets.PSRDiffuseAlbedo),
        nvrhi::BindingSetItem::Texture_SRV(1, renderTargets.PSRSpecularF0),
        nvrhi::BindingSetItem::Texture_UAV(0, renderTargets.PSRDiffuseAlbedo_RR),
        nvrhi::BindingSetItem::Texture_UAV(1, renderTargets.PSRSpecularF0_RR),
    };

    m_bindingSet = m_device->createBindingSet(bindingSetDesc, m_bindingLayout);
}

void DLSSRRInputFormattingPass::Render(nvrhi::ICommandList* commandList, const donut::engine::IView& view)
{
    commandList->beginMarker("DLSS-RR Input Formatting Pass");

    DLSSRRInputFormattingConstants constants = {};
    constants.viewportSize = dm::uint2(view.GetViewExtent().width(), view.GetViewExtent().height());

    nvrhi::ComputeState state;
    state.bindings = { m_bindingSet };
    state.pipeline = m_computePipeline;
    commandList->setComputeState(state);

    commandList->setPushConstants(&constants, sizeof(constants));

    commandList->dispatch(
        dm::div_ceil(view.GetViewExtent().width(), 8),
        dm::div_ceil(view.GetViewExtent().height(), 8),
        1);

    commandList->endMarker();
}