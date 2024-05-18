/***************************************************************************
 # Copyright (c) 2021-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#if WITH_DLSS

#include "DLSS.h"
#include "RenderTargets.h"
#include <donut/engine/ShaderFactory.h>

using namespace donut;

DLSS::DLSS(nvrhi::IDevice* device, donut::engine::ShaderFactory& shaderFactory)
    : m_Device(device)
{
    m_ExposureShader = shaderFactory.CreateShader("app/DlssExposure.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);

    auto layoutDesc = nvrhi::BindingLayoutDesc()
        .setVisibility(nvrhi::ShaderType::Compute)
        .addItem(nvrhi::BindingLayoutItem::TypedBuffer_SRV(0))
        .addItem(nvrhi::BindingLayoutItem::Texture_UAV(0))
        .addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(float)));

    m_ExposureBindingLayout = device->createBindingLayout(layoutDesc);

    auto pipelineDesc = nvrhi::ComputePipelineDesc()
        .addBindingLayout(m_ExposureBindingLayout)
        .setComputeShader(m_ExposureShader);

    m_ExposurePipeline = device->createComputePipeline(pipelineDesc);

    auto textureDesc = nvrhi::TextureDesc()
        .setWidth(1)
        .setHeight(1)
        .setFormat(nvrhi::Format::R32_FLOAT)
        .setDebugName("DLSS Exposure Texture")
        .setInitialState(nvrhi::ResourceStates::UnorderedAccess)
        .setKeepInitialState(true)
        .setDimension(nvrhi::TextureDimension::Texture2D)
        .setIsUAV(true);

    m_ExposureTexture = device->createTexture(textureDesc);

    m_FeatureCommandList = device->createCommandList();
}

void DLSS::ComputeExposure(nvrhi::ICommandList* commandList, nvrhi::IBuffer* toneMapperExposureBuffer, float exposureScale)
{
    if (m_ExposureSourceBuffer != toneMapperExposureBuffer)
    {
        m_ExposureSourceBuffer = nullptr;
        m_ExposureBindingSet = nullptr;
    }

    if (!m_ExposureBindingSet)
    {
        auto setDesc = nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::TypedBuffer_SRV(0, toneMapperExposureBuffer))
            .addItem(nvrhi::BindingSetItem::Texture_UAV(0, m_ExposureTexture))
            .addItem(nvrhi::BindingSetItem::PushConstants(0, sizeof(float)));

        m_ExposureBindingSet = m_Device->createBindingSet(setDesc, m_ExposureBindingLayout);
    }

    auto state = nvrhi::ComputeState()
        .setPipeline(m_ExposurePipeline)
        .addBindingSet(m_ExposureBindingSet);

    commandList->setComputeState(state);
    commandList->setPushConstants(&exposureScale, sizeof(float));
    commandList->dispatch(1);
}

#endif
