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
#include <Rtxdi/DI/ReSTIRDI.h>

#include <algorithm>
#include <utility>

using namespace donut::math;
#include "../Shaders/ShaderParameters.h"

using namespace donut::engine;


PrepareLightsPass::PrepareLightsPass(
    nvrhi::IDevice* device, 
    std::shared_ptr<ShaderFactory> shaderFactory, 
    std::shared_ptr<CommonRenderPasses> commonPasses,
    std::shared_ptr<donut::engine::Scene> scene,
    nvrhi::IBindingLayout* bindlessLayout)
    : m_device(device)
    , m_bindlessLayout(bindlessLayout)
    , m_shaderFactory(std::move(shaderFactory))
    , m_commonPasses(std::move(commonPasses))
    , m_scene(std::move(scene))
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

    m_bindingLayout = m_device->createBindingLayout(bindingLayoutDesc);
}

void PrepareLightsPass::CreatePipeline()
{
    donut::log::debug("Initializing PrepareLightsPass...");

    m_computeShader = m_shaderFactory->CreateShader("app/PrepareLights.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);

    nvrhi::ComputePipelineDesc pipelineDesc;
    pipelineDesc.bindingLayouts = { m_bindingLayout, m_bindlessLayout };
    pipelineDesc.CS = m_computeShader;
    m_computePipeline = m_device->createComputePipeline(pipelineDesc);
}

void PrepareLightsPass::CreateBindingSet(RtxdiResources& resources)
{
    nvrhi::BindingSetDesc bindingSetDesc;
    bindingSetDesc.bindings = {
        nvrhi::BindingSetItem::PushConstants(0, sizeof(PrepareLightsConstants)),
        nvrhi::BindingSetItem::StructuredBuffer_UAV(0, resources.LightDataBuffer),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(0, resources.TaskBuffer),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(2, m_scene->GetInstanceBuffer()),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(3, m_scene->GetGeometryBuffer()),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(4, m_scene->GetMaterialBuffer()),
        nvrhi::BindingSetItem::Sampler(0, m_commonPasses->m_AnisotropicWrapSampler)
    };

    m_bindingSet = m_device->createBindingSet(bindingSetDesc, m_bindingLayout);
    m_taskBuffer = resources.TaskBuffer;
    m_geometryInstanceToLightBuffer = resources.GeometryInstanceToLightBuffer;
}

void PrepareLightsPass::CountLightsInScene(uint32_t& numEmissiveMeshes, uint32_t& numEmissiveTriangles)
{
    numEmissiveMeshes = 0;
    numEmissiveTriangles = 0;

    const auto& instances = m_scene->GetSceneGraph()->GetMeshInstances();
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
    std::vector<uint32_t> geometryInstanceToLight(m_scene->GetSceneGraph()->GetGeometryInstancesCount(), RTXDI_INVALID_LIGHT_INDEX);

    const auto& instances = m_scene->GetSceneGraph()->GetMeshInstances();
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
    
    commandList->writeBuffer(m_geometryInstanceToLightBuffer, geometryInstanceToLight.data(), geometryInstanceToLight.size() * sizeof(uint32_t));

    outLightBufferParams.localLightBufferRegion.firstLightIndex = 0;
    outLightBufferParams.localLightBufferRegion.numLights = lightBufferOffset;
    outLightBufferParams.infiniteLightBufferRegion.firstLightIndex = 0;
    outLightBufferParams.infiniteLightBufferRegion.numLights = 0;
    outLightBufferParams.environmentLightParams.lightIndex = RTXDI_INVALID_LIGHT_INDEX;
    outLightBufferParams.environmentLightParams.lightPresent = false;
    
    commandList->writeBuffer(m_taskBuffer, tasks.data(), tasks.size() * sizeof(PrepareLightsTask));
    
    nvrhi::ComputeState state;
    state.pipeline = m_computePipeline;
    state.bindings = { m_bindingSet, m_scene->GetDescriptorTable() };
    commandList->setComputeState(state);

    PrepareLightsConstants constants;
    constants.numTasks = uint32_t(tasks.size());
    commandList->setPushConstants(&constants, sizeof(constants));

    commandList->dispatch(dm::div_ceil(lightBufferOffset, 256));

    commandList->endMarker();
    return outLightBufferParams;
}
