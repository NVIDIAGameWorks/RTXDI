/***************************************************************************
 # Copyright (c) 2020-2021, NVIDIA CORPORATION.  All rights reserved.
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
    uint32_t maxEmissiveTriangles,
    uint32_t maxPrimitiveLights,
    uint32_t environmentMapWidth,
    uint32_t environmentMapHeight)
    : m_MaxEmissiveMeshes(maxEmissiveMeshes)
    , m_MaxEmissiveTriangles(maxEmissiveTriangles)
    , m_MaxPrimitiveLights(maxPrimitiveLights)
{
    nvrhi::BufferDesc taskBufferDesc;
    taskBufferDesc.byteSize = sizeof(PrepareLightsTask) * (maxEmissiveMeshes + maxPrimitiveLights);
    taskBufferDesc.structStride = sizeof(PrepareLightsTask);
    taskBufferDesc.initialState = nvrhi::ResourceStates::ShaderResource;
    taskBufferDesc.keepInitialState = true;
    taskBufferDesc.debugName = "TaskBuffer";
    taskBufferDesc.canHaveUAVs = true;
    TaskBuffer = device->createBuffer(taskBufferDesc);


    nvrhi::BufferDesc primitiveLightBufferDesc;
    primitiveLightBufferDesc.byteSize = sizeof(PolymorphicLightInfo) * maxPrimitiveLights;
    primitiveLightBufferDesc.structStride = sizeof(PolymorphicLightInfo);
    primitiveLightBufferDesc.initialState = nvrhi::ResourceStates::ShaderResource;
    primitiveLightBufferDesc.keepInitialState = true;
    primitiveLightBufferDesc.debugName = "PrimitiveLightBuffer";
    PrimitiveLightBuffer = device->createBuffer(primitiveLightBufferDesc);


    nvrhi::BufferDesc risBufferDesc;
    risBufferDesc.byteSize = sizeof(uint32_t) * 2 * std::max(context.GetRisBufferElementCount(), 1u); // RG32_UINT per element
    risBufferDesc.format = nvrhi::Format::RG32_UINT;
    risBufferDesc.canHaveTypedViews = true;
    risBufferDesc.initialState = nvrhi::ResourceStates::ShaderResource;
    risBufferDesc.keepInitialState = true;
    risBufferDesc.debugName = "RisBuffer";
    risBufferDesc.canHaveUAVs = true;
    RisBuffer = device->createBuffer(risBufferDesc);


    risBufferDesc.byteSize = sizeof(uint32_t) * 8 * std::max(context.GetRisBufferElementCount(), 1u); // RGBA32_UINT x 2 per element
    risBufferDesc.format = nvrhi::Format::RGBA32_UINT;
    risBufferDesc.debugName = "RisLightDataBuffer";
    RisLightDataBuffer = device->createBuffer(risBufferDesc);


    uint32_t maxLocalLights = maxEmissiveTriangles + maxPrimitiveLights;
    uint32_t lightBufferElements = maxLocalLights * 2;

    nvrhi::BufferDesc lightBufferDesc;
    lightBufferDesc.byteSize = sizeof(PolymorphicLightInfo) * lightBufferElements;
    lightBufferDesc.structStride = sizeof(PolymorphicLightInfo);
    lightBufferDesc.initialState = nvrhi::ResourceStates::ShaderResource;
    lightBufferDesc.keepInitialState = true;
    lightBufferDesc.debugName = "LightDataBuffer";
    lightBufferDesc.canHaveUAVs = true;
    LightDataBuffer = device->createBuffer(lightBufferDesc);


    nvrhi::BufferDesc lightIndexMappingBufferDesc;
    lightIndexMappingBufferDesc.byteSize = sizeof(uint32_t) * lightBufferElements;
    lightIndexMappingBufferDesc.format = nvrhi::Format::R32_UINT;
    lightIndexMappingBufferDesc.canHaveTypedViews = true;
    lightIndexMappingBufferDesc.initialState = nvrhi::ResourceStates::ShaderResource;
    lightIndexMappingBufferDesc.keepInitialState = true;
    lightIndexMappingBufferDesc.debugName = "LightIndexMappingBuffer";
    lightIndexMappingBufferDesc.canHaveUAVs = true;
    LightIndexMappingBuffer = device->createBuffer(lightIndexMappingBufferDesc);
    

    nvrhi::BufferDesc neighborOffsetBufferDesc;
    neighborOffsetBufferDesc.byteSize = context.GetParameters().NeighborOffsetCount * 2;
    neighborOffsetBufferDesc.format = nvrhi::Format::RG8_SNORM;
    neighborOffsetBufferDesc.canHaveTypedViews = true;
    neighborOffsetBufferDesc.debugName = "NeighborOffsets";
    neighborOffsetBufferDesc.initialState = nvrhi::ResourceStates::ShaderResource;
    neighborOffsetBufferDesc.keepInitialState = true;
    NeighborOffsetsBuffer = device->createBuffer(neighborOffsetBufferDesc);


    nvrhi::BufferDesc lightReservoirBufferDesc;
    lightReservoirBufferDesc.byteSize = sizeof(RTXDI_PackedReservoir) * context.GetReservoirBufferElementCount() * 2; // Use 2 reservoir buffers for our pipeline
    lightReservoirBufferDesc.structStride = sizeof(RTXDI_PackedReservoir);
    lightReservoirBufferDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
    lightReservoirBufferDesc.keepInitialState = true;
    lightReservoirBufferDesc.debugName = "LightReservoirBuffer";
    lightReservoirBufferDesc.canHaveUAVs = true;
    LightReservoirBuffer = device->createBuffer(lightReservoirBufferDesc);


    nvrhi::TextureDesc environmentPdfDesc;
    environmentPdfDesc.width = environmentMapWidth;
    environmentPdfDesc.height = environmentMapHeight;
    environmentPdfDesc.mipLevels = uint32_t(ceilf(::log2f(float(std::max(environmentPdfDesc.width, environmentPdfDesc.height))))); // Stop at 2x1 or 2x2
    environmentPdfDesc.isUAV = true;
    environmentPdfDesc.debugName = "EnvironmentPdf";
    environmentPdfDesc.initialState = nvrhi::ResourceStates::ShaderResource;
    environmentPdfDesc.keepInitialState = true;
    environmentPdfDesc.format = nvrhi::Format::R16_FLOAT;
    EnvironmentPdfTexture = device->createTexture(environmentPdfDesc);

    nvrhi::TextureDesc localLightPdfDesc;
    rtxdi::ComputePdfTextureSize(maxLocalLights, localLightPdfDesc.width, localLightPdfDesc.height, localLightPdfDesc.mipLevels);
    assert(localLightPdfDesc.width * localLightPdfDesc.height >= maxLocalLights);
    localLightPdfDesc.isUAV = true;
    localLightPdfDesc.debugName = "LocalLightPdf";
    localLightPdfDesc.initialState = nvrhi::ResourceStates::ShaderResource;
    localLightPdfDesc.keepInitialState = true;
    localLightPdfDesc.format = nvrhi::Format::R32_FLOAT; // Use FP32 here to allow a wide range of flux values, esp. when downsampled.
    LocalLightPdfTexture = device->createTexture(localLightPdfDesc);
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
