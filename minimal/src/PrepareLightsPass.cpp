/***************************************************************************
 # Copyright (c) 2020-2022, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#include "PrepareLightsPass.h"
#include "RtxdiResources.h"
#include "SampleScene.h"

#include <donut/engine/ShaderFactory.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/core/log.h>
#include <rtxdi/ReSTIRDI.h>

#include <algorithm>
#include <utility>

using namespace donut::math;
#include "../shaders/ShaderParameters.h"

using namespace donut::engine;


PrepareLightsPass::PrepareLightsPass(
    nvrhi::IDevice* device, 
    std::shared_ptr<ShaderFactory> shaderFactory, 
    std::shared_ptr<CommonRenderPasses> commonPasses,
    std::shared_ptr<donut::engine::Scene> scene,
    nvrhi::IBindingLayout* bindlessLayout)
    : m_Device(device)
    , m_BindlessLayout(bindlessLayout)
    , m_ShaderFactory(std::move(shaderFactory))
    , m_CommonPasses(std::move(commonPasses))
    , m_Scene(std::move(scene))
{
    nvrhi::BindingLayoutDesc bindingLayoutDesc;
    bindingLayoutDesc.visibility = nvrhi::ShaderType::Compute;
    bindingLayoutDesc.bindings = {
        nvrhi::BindingLayoutItem::PushConstants(0, sizeof(PrepareLightsConstants)),
        nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(4),
        nvrhi::BindingLayoutItem::Sampler(0)
    };

    m_BindingLayout = m_Device->createBindingLayout(bindingLayoutDesc);
}

void PrepareLightsPass::CreatePipeline()
{
    donut::log::debug("Initializing PrepareLightsPass...");

    m_ComputeShader = m_ShaderFactory->CreateShader("app/PrepareLights.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);

    nvrhi::ComputePipelineDesc pipelineDesc;
    pipelineDesc.bindingLayouts = { m_BindingLayout, m_BindlessLayout };
    pipelineDesc.CS = m_ComputeShader;
    m_ComputePipeline = m_Device->createComputePipeline(pipelineDesc);
}

void PrepareLightsPass::CreateBindingSet(RtxdiResources& resources)
{
    nvrhi::BindingSetDesc bindingSetDesc;
    bindingSetDesc.bindings = {
        nvrhi::BindingSetItem::PushConstants(0, sizeof(PrepareLightsConstants)),
        nvrhi::BindingSetItem::StructuredBuffer_UAV(0, resources.LightDataBuffer),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(0, resources.TaskBuffer),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(2, m_Scene->GetInstanceBuffer()),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(3, m_Scene->GetGeometryBuffer()),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(4, m_Scene->GetMaterialBuffer()),
        nvrhi::BindingSetItem::Sampler(0, m_CommonPasses->m_AnisotropicWrapSampler)
    };

    m_BindingSet = m_Device->createBindingSet(bindingSetDesc, m_BindingLayout);
    m_TaskBuffer = resources.TaskBuffer;
    m_GeometryInstanceToLightBuffer = resources.GeometryInstanceToLightBuffer;
}

void PrepareLightsPass::CountLightsInScene(uint32_t& numEmissiveMeshes, uint32_t& numEmissiveTriangles)
{
    numEmissiveMeshes = 0;
    numEmissiveTriangles = 0;

    const auto& instances = m_Scene->GetSceneGraph()->GetMeshInstances();
    for (const auto& instance : instances)
    {
        for (const auto& geometry : instance->GetMesh()->geometries)
        {
            if (any(geometry->material->emissiveColor != 0.f))
            {
                numEmissiveMeshes += 1;
                numEmissiveTriangles += geometry->numIndices / 3;
            }
        }
    }
}

RTXDI_LightBufferParameters PrepareLightsPass::Process(nvrhi::ICommandList* commandList)
{
    RTXDI_LightBufferParameters outLightBufferParams;
    commandList->beginMarker("PrepareLights");

    std::vector<PrepareLightsTask> tasks;
    uint32_t lightBufferOffset = 0;
    std::vector<uint32_t> geometryInstanceToLight(m_Scene->GetSceneGraph()->GetGeometryInstancesCount(), RTXDI_INVALID_LIGHT_INDEX);

    const auto& instances = m_Scene->GetSceneGraph()->GetMeshInstances();
    for (const auto& instance : instances)
    {
        const auto& mesh = instance->GetMesh();
        
        assert(instance->GetGeometryInstanceIndex() < geometryInstanceToLight.size());
        uint32_t firstGeometryInstanceIndex = instance->GetGeometryInstanceIndex();
        
        for (size_t geometryIndex = 0; geometryIndex < mesh->geometries.size(); ++geometryIndex)
        {
            const auto& geometry = mesh->geometries[geometryIndex];

            if (!any(geometry->material->emissiveColor != 0.f) || geometry->material->emissiveIntensity <= 0.f)
                continue;

            geometryInstanceToLight[firstGeometryInstanceIndex + geometryIndex] = lightBufferOffset;

            assert(geometryIndex < 0xfff);

            PrepareLightsTask task{};
            task.instanceIndex = instance->GetInstanceIndex();
            task.geometryIndex = (uint32_t)geometryIndex;
            task.lightBufferOffset = lightBufferOffset;
            task.triangleCount = geometry->numIndices / 3;
            
            lightBufferOffset += task.triangleCount;

            tasks.push_back(task);
        }
    }
    
    commandList->writeBuffer(m_GeometryInstanceToLightBuffer, geometryInstanceToLight.data(), geometryInstanceToLight.size() * sizeof(uint32_t));

    outLightBufferParams.localLightBufferRegion.firstLightIndex = 0;
    outLightBufferParams.localLightBufferRegion.numLights = lightBufferOffset;
    outLightBufferParams.infiniteLightBufferRegion.firstLightIndex = 0;
    outLightBufferParams.infiniteLightBufferRegion.numLights = 0;
    outLightBufferParams.environmentLightParams.lightIndex = RTXDI_INVALID_LIGHT_INDEX;
    outLightBufferParams.environmentLightParams.lightPresent = false;
    
    commandList->writeBuffer(m_TaskBuffer, tasks.data(), tasks.size() * sizeof(PrepareLightsTask));
    
    nvrhi::ComputeState state;
    state.pipeline = m_ComputePipeline;
    state.bindings = { m_BindingSet, m_Scene->GetDescriptorTable() };
    commandList->setComputeState(state);

    PrepareLightsConstants constants;
    constants.numTasks = uint32_t(tasks.size());
    commandList->setPushConstants(&constants, sizeof(constants));

    commandList->dispatch(dm::div_ceil(lightBufferOffset, 256));

    commandList->endMarker();
    return outLightBufferParams;
}
