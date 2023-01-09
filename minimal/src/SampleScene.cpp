/***************************************************************************
 # Copyright (c) 2021-2022, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#include "SampleScene.h"
#include <donut/core/json.h>
#include <donut/core/vfs/VFS.h>
#include <nvrhi/utils.h>
#include <nvrhi/common/misc.h>

#include "donut/engine/TextureCache.h"


using namespace donut;
using namespace donut::math;

#include "../shaders/ShaderParameters.h"

inline uint64_t advanceHeapPtr(uint64_t& heapPtr, const nvrhi::MemoryRequirements& memReq)
{
    heapPtr = nvrhi::align(heapPtr, memReq.alignment);
    uint64_t current = heapPtr;

    heapPtr += memReq.size;

    return current;
}

void SampleScene::BuildMeshBLASes(nvrhi::IDevice* device)
{
    assert(device->queryFeatureSupport(nvrhi::Feature::VirtualResources));

    uint64_t heapSize = 0;

    for (const auto& mesh : GetSceneGraph()->GetMeshes())
    {
        if (mesh->buffers->hasAttribute(engine::VertexAttribute::JointWeights))
            continue;

        nvrhi::rt::AccelStructDesc blasDesc;
        blasDesc.isTopLevel = false;
        blasDesc.isVirtual = true;

        for (const auto& geometry : mesh->geometries)
        {
            nvrhi::rt::GeometryDesc geometryDesc;
            auto& triangles = geometryDesc.geometryData.triangles;
            triangles.indexBuffer = mesh->buffers->indexBuffer;
            triangles.indexOffset = (mesh->indexOffset + geometry->indexOffsetInMesh) * sizeof(uint32_t);
            triangles.indexFormat = nvrhi::Format::R32_UINT;
            triangles.indexCount = geometry->numIndices;
            triangles.vertexBuffer = mesh->buffers->vertexBuffer;
            triangles.vertexOffset = (mesh->vertexOffset + geometry->vertexOffsetInMesh) * sizeof(float3) + mesh->buffers->getVertexBufferRange(engine::VertexAttribute::Position).byteOffset;
            triangles.vertexFormat = nvrhi::Format::RGB32_FLOAT;
            triangles.vertexStride = sizeof(float3);
            triangles.vertexCount = geometry->numVertices;
            geometryDesc.geometryType = nvrhi::rt::GeometryType::Triangles;
            geometryDesc.flags = (geometry->material->domain == engine::MaterialDomain::Opaque)
                ? nvrhi::rt::GeometryFlags::Opaque
                : nvrhi::rt::GeometryFlags::None;
            blasDesc.bottomLevelGeometries.push_back(geometryDesc);
        }

        blasDesc.buildFlags = nvrhi::rt::AccelStructBuildFlags::PreferFastTrace;
        if (!mesh->skinPrototype)
        {
            // Only allow compaction on non-skinned, static meshes.
            blasDesc.buildFlags = blasDesc.buildFlags | nvrhi::rt::AccelStructBuildFlags::AllowCompaction;
        }

        blasDesc.trackLiveness = false;
        blasDesc.debugName = mesh->name;

        nvrhi::rt::AccelStructHandle as = device->createAccelStruct(blasDesc);
        
        advanceHeapPtr(heapSize, device->getAccelStructMemoryRequirements(as));
        
        mesh->accelStruct = as;
    }
    
    nvrhi::rt::AccelStructDesc tlasDesc;
    tlasDesc.isTopLevel = true;
    tlasDesc.isVirtual = true;
    tlasDesc.topLevelMaxInstances = GetSceneGraph()->GetMeshInstances().size();
    tlasDesc.debugName = "TopLevelAS";
    tlasDesc.buildFlags = nvrhi::rt::AccelStructBuildFlags::AllowUpdate;

    m_TopLevelAS = device->createAccelStruct(tlasDesc);

    advanceHeapPtr(heapSize, device->getAccelStructMemoryRequirements(m_TopLevelAS));
        
    tlasDesc.debugName = "PrevTopLevelAS";
    

    nvrhi::HeapDesc heapDecs;
    heapDecs.type = nvrhi::HeapType::DeviceLocal;
    heapDecs.capacity = heapSize;
    heapDecs.debugName = "AccelStructHeap";

    nvrhi::HeapHandle heap = device->createHeap(heapDecs);

    heapSize = 0;

    for (const auto& mesh : GetSceneGraph()->GetMeshes())
    {
        if (!mesh->accelStruct)
            continue;

        uint64_t heapOffset = advanceHeapPtr(heapSize, device->getAccelStructMemoryRequirements(mesh->accelStruct));

        device->bindAccelStructMemory(mesh->accelStruct, heap, heapOffset);
    }

    uint64_t heapOffset = advanceHeapPtr(heapSize, device->getAccelStructMemoryRequirements(m_TopLevelAS));

    device->bindAccelStructMemory(m_TopLevelAS, heap, heapOffset);


    nvrhi::CommandListParameters clparams;
    clparams.scratchChunkSize = clparams.scratchMaxMemory;

    nvrhi::CommandListHandle commandList = device->createCommandList(clparams);
    commandList->open();

    for (const auto& mesh : GetSceneGraph()->GetMeshes())
    {
        if (!mesh->accelStruct)
            continue;

        // Get the desc from the AS, restore the buffer pointers because they're erased by nvrhi
        nvrhi::rt::AccelStructDesc blasDesc = mesh->accelStruct->getDesc();
        for (auto& geometryDesc : blasDesc.bottomLevelGeometries)
        {
            geometryDesc.geometryData.triangles.indexBuffer = mesh->buffers->indexBuffer;
            geometryDesc.geometryData.triangles.vertexBuffer = mesh->buffers->vertexBuffer;
        }

        nvrhi::utils::BuildBottomLevelAccelStruct(commandList, mesh->accelStruct, blasDesc);
    }

    commandList->close();
    device->executeCommandList(commandList);

    device->waitForIdle();
    device->runGarbageCollection();
}

void SampleScene::BuildTopLevelAccelStruct(nvrhi::ICommandList* commandList)
{
    m_TlasInstances.resize(GetSceneGraph()->GetMeshInstances().size());

    nvrhi::rt::AccelStructBuildFlags buildFlags = m_CanUpdateTLAS
        ? nvrhi::rt::AccelStructBuildFlags::PerformUpdate
        : nvrhi::rt::AccelStructBuildFlags::None;
    
    uint32_t index = 0;

    for (const auto& instance : GetSceneGraph()->GetMeshInstances())
    {
        const auto& mesh = instance->GetMesh();
        
        if (!mesh->accelStruct)
            continue;

        nvrhi::rt::InstanceDesc& instanceDesc = m_TlasInstances[index++];

        instanceDesc.instanceMask = 0;
        engine::SceneContentFlags contentFlags = instance->GetContentFlags();

        if ((contentFlags & engine::SceneContentFlags::OpaqueMeshes) != 0)
            instanceDesc.instanceMask |= INSTANCE_MASK_OPAQUE;

        if ((contentFlags & engine::SceneContentFlags::AlphaTestedMeshes) != 0)
            instanceDesc.instanceMask |= INSTANCE_MASK_ALPHA_TESTED;

        if ((contentFlags & engine::SceneContentFlags::BlendedMeshes) != 0)
            instanceDesc.instanceMask |= INSTANCE_MASK_TRANSPARENT;

        for (const auto& geometry : mesh->geometries)
        {
            if (geometry->material->doubleSided)
                instanceDesc.flags = nvrhi::rt::InstanceFlags::TriangleCullDisable;
        }

        instanceDesc.bottomLevelAS = mesh->accelStruct;

        auto node = instance->GetNode();
        if (node)
            dm::affineToColumnMajor(node->GetLocalToWorldTransformFloat(), instanceDesc.transform);

        instanceDesc.instanceID = uint(instance->GetInstanceIndex());
    }

    commandList->buildTopLevelAccelStruct(m_TopLevelAS, m_TlasInstances.data(), m_TlasInstances.size(), buildFlags);
    m_CanUpdateTLAS = true;
}
