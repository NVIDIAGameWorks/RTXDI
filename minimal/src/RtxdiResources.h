/***************************************************************************
 # Copyright (c) 2020-2022, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#pragma once

#include <nvrhi/nvrhi.h>

namespace rtxdi
{
    class ReSTIRDIContext;
}

class RtxdiResources
{
private:
    bool m_NeighborOffsetsInitialized = false;
    uint32_t m_MaxEmissiveMeshes = 0;
    uint32_t m_MaxEmissiveTriangles = 0;
    uint32_t m_MaxGeometryInstances = 0;

public:
    nvrhi::BufferHandle TaskBuffer;
    nvrhi::BufferHandle LightDataBuffer;
    nvrhi::BufferHandle NeighborOffsetsBuffer;
    nvrhi::BufferHandle LightReservoirBuffer;
    nvrhi::BufferHandle GeometryInstanceToLightBuffer;

    RtxdiResources(
        nvrhi::IDevice* device, 
        const rtxdi::ReSTIRDIContext& context,
        uint32_t maxEmissiveMeshes,
        uint32_t maxEmissiveTriangles,
        uint32_t maxMeshInstances);

    void InitializeNeighborOffsets(nvrhi::ICommandList* commandList, uint32_t neighborOffsetCount);

    uint32_t GetMaxEmissiveMeshes() const { return m_MaxEmissiveMeshes; }
    uint32_t GetMaxEmissiveTriangles() const { return m_MaxEmissiveTriangles; }
    uint32_t GetMaxGeometryInstances() const { return m_MaxGeometryInstances; }
};