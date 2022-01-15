/***************************************************************************
 # Copyright (c) 2020-2022, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#include "RtxdiResources.h"
#include <rtxdi/RTXDI.h>

#include <donut/core/math/math.h>

using namespace dm;
#include "../shaders/ShaderParameters.h"

RtxdiResources::RtxdiResources(
    nvrhi::IDevice* device, 
    const rtxdi::Context& context,
    uint32_t maxEmissiveMeshes,
    uint32_t maxEmissiveTriangles)
    : m_MaxEmissiveMeshes(maxEmissiveMeshes)
    , m_MaxEmissiveTriangles(maxEmissiveTriangles)
{
    nvrhi::BufferDesc taskBufferDesc;
    taskBufferDesc.byteSize = sizeof(PrepareLightsTask) * maxEmissiveMeshes;
    taskBufferDesc.structStride = sizeof(PrepareLightsTask);
    taskBufferDesc.initialState = nvrhi::ResourceStates::ShaderResource;
    taskBufferDesc.keepInitialState = true;
    taskBufferDesc.debugName = "TaskBuffer";
    taskBufferDesc.canHaveUAVs = true;
    TaskBuffer = device->createBuffer(taskBufferDesc);


    nvrhi::BufferDesc lightBufferDesc;
    lightBufferDesc.byteSize = sizeof(RAB_LightInfo) * maxEmissiveTriangles;
    lightBufferDesc.structStride = sizeof(RAB_LightInfo);
    lightBufferDesc.initialState = nvrhi::ResourceStates::ShaderResource;
    lightBufferDesc.keepInitialState = true;
    lightBufferDesc.debugName = "LightDataBuffer";
    lightBufferDesc.canHaveUAVs = true;
    LightDataBuffer = device->createBuffer(lightBufferDesc);

    
    nvrhi::BufferDesc neighborOffsetBufferDesc;
    neighborOffsetBufferDesc.byteSize = context.GetParameters().NeighborOffsetCount * 2;
    neighborOffsetBufferDesc.format = nvrhi::Format::RG8_SNORM;
    neighborOffsetBufferDesc.canHaveTypedViews = true;
    neighborOffsetBufferDesc.debugName = "NeighborOffsets";
    neighborOffsetBufferDesc.initialState = nvrhi::ResourceStates::ShaderResource;
    neighborOffsetBufferDesc.keepInitialState = true;
    NeighborOffsetsBuffer = device->createBuffer(neighborOffsetBufferDesc);


    nvrhi::BufferDesc lightReservoirBufferDesc;
    lightReservoirBufferDesc.byteSize = sizeof(RTXDI_PackedReservoir) * context.GetReservoirBufferElementCount() * c_NumReservoirBuffers;
    lightReservoirBufferDesc.structStride = sizeof(RTXDI_PackedReservoir);
    lightReservoirBufferDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
    lightReservoirBufferDesc.keepInitialState = true;
    lightReservoirBufferDesc.debugName = "LightReservoirBuffer";
    lightReservoirBufferDesc.canHaveUAVs = true;
    LightReservoirBuffer = device->createBuffer(lightReservoirBufferDesc);
}

void RtxdiResources::InitializeNeighborOffsets(nvrhi::ICommandList* commandList, const rtxdi::Context& context)
{
    if (m_NeighborOffsetsInitialized)
        return;

    std::vector<uint8_t> offsets;
    offsets.resize(context.GetParameters().NeighborOffsetCount* 2);

    context.FillNeighborOffsetBuffer(offsets.data());

    commandList->writeBuffer(NeighborOffsetsBuffer, offsets.data(), offsets.size());

    m_NeighborOffsetsInitialized = true;
}
