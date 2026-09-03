/*
 * SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#include "RtxdiResources.h"

#include <Rtxdi/DI/ReSTIRDI.h>
#include <Rtxdi/GI/ReSTIRGI.h>
#include <Rtxdi/LightSampling/RISBufferSegmentAllocator.h>
#include <Rtxdi/PT/ReSTIRPT.h>

#include <donut/core/math/math.h>
using namespace donut::math;

#include <donut/core/math/math.h>

using namespace dm;
#include "SharedShaderInclude/ShaderParameters.h"

RtxdiResources::RtxdiResources(
    nvrhi::IDevice* device, 
    const rtxdi::ReSTIRDIContext& context,
    const rtxdi::RISBufferSegmentAllocator& risBufferSegmentAllocator,
    uint32_t maxEmissiveMeshes,
    uint32_t maxEmissiveTriangles,
    uint32_t maxPrimitiveLights,
    uint32_t maxGeometryInstances,
    uint32_t environmentMapWidth,
    uint32_t environmentMapHeight)
    : m_maxEmissiveMeshes(maxEmissiveMeshes)
    , m_maxEmissiveTriangles(maxEmissiveTriangles)
    , m_maxPrimitiveLights(maxPrimitiveLights)
    , m_maxGeometryInstances(maxGeometryInstances)
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
    risBufferDesc.byteSize = sizeof(uint32_t) * 2 * std::max(risBufferSegmentAllocator.GetTotalSizeInElements(), 1u); // RG32_UINT per element
    risBufferDesc.format = nvrhi::Format::RG32_UINT;
    risBufferDesc.canHaveTypedViews = true;
    risBufferDesc.initialState = nvrhi::ResourceStates::ShaderResource;
    risBufferDesc.keepInitialState = true;
    risBufferDesc.debugName = "RisBuffer";
    risBufferDesc.canHaveUAVs = true;
    RisBuffer = device->createBuffer(risBufferDesc);


    risBufferDesc.byteSize = sizeof(uint32_t) * 8 * std::max(risBufferSegmentAllocator.GetTotalSizeInElements(), 1u); // RGBA32_UINT x 2 per element
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


    nvrhi::BufferDesc geometryInstanceToLightBufferDesc;
    geometryInstanceToLightBufferDesc.byteSize = sizeof(uint32_t) * maxGeometryInstances;
    geometryInstanceToLightBufferDesc.structStride = sizeof(uint32_t);
    geometryInstanceToLightBufferDesc.initialState = nvrhi::ResourceStates::ShaderResource;
    geometryInstanceToLightBufferDesc.keepInitialState = true;
    geometryInstanceToLightBufferDesc.debugName = "GeometryInstanceToLightBuffer";
    GeometryInstanceToLightBuffer = device->createBuffer(geometryInstanceToLightBufferDesc);


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
    neighborOffsetBufferDesc.byteSize = context.GetStaticParameters().NeighborOffsetCount * 2;
    neighborOffsetBufferDesc.format = nvrhi::Format::RG8_SNORM;
    neighborOffsetBufferDesc.canHaveTypedViews = true;
    neighborOffsetBufferDesc.debugName = "NeighborOffsets";
    neighborOffsetBufferDesc.initialState = nvrhi::ResourceStates::ShaderResource;
    neighborOffsetBufferDesc.keepInitialState = true;
    NeighborOffsetsBuffer = device->createBuffer(neighborOffsetBufferDesc);


    nvrhi::BufferDesc diReservoirBufferDesc;
    diReservoirBufferDesc.byteSize = sizeof(RTXDI_PackedDIReservoir) * context.GetReservoirBufferParameters().reservoirArrayPitch * rtxdi::c_NumReSTIRDIReservoirBuffers;
    diReservoirBufferDesc.structStride = sizeof(RTXDI_PackedDIReservoir);
    diReservoirBufferDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
    diReservoirBufferDesc.keepInitialState = true;
    diReservoirBufferDesc.debugName = "DIReservoirBuffer";
    diReservoirBufferDesc.canHaveUAVs = true;
    DIReservoirBuffer = device->createBuffer(diReservoirBufferDesc);


    nvrhi::BufferDesc secondaryGBufferDesc;
    secondaryGBufferDesc.byteSize = sizeof(SecondaryGBufferData) * context.GetReservoirBufferParameters().reservoirArrayPitch;
    secondaryGBufferDesc.structStride = sizeof(SecondaryGBufferData);
    secondaryGBufferDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
    secondaryGBufferDesc.keepInitialState = true;
    secondaryGBufferDesc.debugName = "SecondaryGBuffer";
    secondaryGBufferDesc.canHaveUAVs = true;
    SecondaryGBuffer = device->createBuffer(secondaryGBufferDesc);


    nvrhi::TextureDesc environmentPdfDesc;
    environmentPdfDesc.width = environmentMapWidth;
    environmentPdfDesc.height = environmentMapHeight;
    environmentPdfDesc.mipLevels = uint32_t(ceilf(::log2f(float(std::max(environmentPdfDesc.width, environmentPdfDesc.height)))) + 1); // full mip chain up to 1x1
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
    
    nvrhi::BufferDesc giReservoirBufferDesc;
    giReservoirBufferDesc.byteSize = sizeof(RTXDI_PackedGIReservoir) * context.GetReservoirBufferParameters().reservoirArrayPitch * rtxdi::c_NumReSTIRGIReservoirBuffers;
    giReservoirBufferDesc.structStride = sizeof(RTXDI_PackedGIReservoir);
    giReservoirBufferDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
    giReservoirBufferDesc.keepInitialState = true;
    giReservoirBufferDesc.debugName = "GIReservoirBuffer";
    giReservoirBufferDesc.canHaveUAVs = true;
    GIReservoirBuffer = device->createBuffer(giReservoirBufferDesc);

    nvrhi::BufferDesc ptReservoirBufferDesc;
    ptReservoirBufferDesc.byteSize = sizeof(RTXDI_PackedPTReservoir) * context.GetReservoirBufferParameters().reservoirArrayPitch * rtxdi::c_NumReSTIRPTReservoirBuffers;
    ptReservoirBufferDesc.structStride = sizeof(RTXDI_PackedPTReservoir);
    ptReservoirBufferDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
    ptReservoirBufferDesc.keepInitialState = true;
    ptReservoirBufferDesc.debugName = "PTReservoirBuffer";
    ptReservoirBufferDesc.canHaveUAVs = true;
    PTReservoirBuffer = device->createBuffer(ptReservoirBufferDesc);

    nvrhi::BufferDesc spatialNeighborSelectionDesc;
    spatialNeighborSelectionDesc.byteSize = context.GetReservoirBufferParameters().reservoirArrayPitch * SPATIAL_HEURISTIC_MAX_NEIGHBORS * sizeof(uint32_t);
    spatialNeighborSelectionDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
    spatialNeighborSelectionDesc.keepInitialState = true;
    spatialNeighborSelectionDesc.debugName = "SpatialNeighborSelectionBuffer";
    spatialNeighborSelectionDesc.canHaveRawViews = true;
    spatialNeighborSelectionDesc.canHaveUAVs = true;
    SpatialNeighborSelectionBuffer = device->createBuffer(spatialNeighborSelectionDesc);
}

void RtxdiResources::InitializeNeighborOffsets(nvrhi::ICommandList* commandList, uint32_t neighborOffsetCount)
{
    if (m_neighborOffsetsInitialized)
        return;

    std::vector<uint8_t> offsets;
    offsets.resize(neighborOffsetCount * 2);

    rtxdi::FillNeighborOffsetBuffer(offsets.data(), neighborOffsetCount);

    commandList->writeBuffer(NeighborOffsetsBuffer, offsets.data(), offsets.size());

    m_neighborOffsetsInitialized = true;
}

uint32_t RtxdiResources::GetMaxEmissiveMeshes() const
{
    return m_maxEmissiveMeshes;
}

uint32_t RtxdiResources::GetMaxEmissiveTriangles() const
{
    return m_maxEmissiveTriangles;
}

uint32_t RtxdiResources::GetMaxPrimitiveLights() const
{
    return m_maxPrimitiveLights;
}

uint32_t RtxdiResources::GetMaxGeometryInstances() const
{
    return m_maxGeometryInstances;
}
