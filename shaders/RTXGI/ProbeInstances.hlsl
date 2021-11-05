/***************************************************************************
 # Copyright (c) 2020-2021, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#pragma pack_matrix(row_major)

#include "DDGIShaderConfig.h"

#define HLSL
#include <ddgi/include/ProbeCommon.hlsl>

#include <donut/shaders/bindless.h>

#include "../ShaderParameters.h"
#include "../GBufferHelpers.hlsli"
#include "../SceneGeometry.hlsli"

struct GeometryInstance
{
    float3x4 transform;
    uint instanceIndexAndMask;
    uint instanceOffsetAndFlags;
    uint deviceAddressLow;
    uint deviceAddressHigh;
};

ConstantBuffer<ProbeDebugConstants> g_Const : register(b0);
StructuredBuffer<DDGIVolumeDescGPUPacked> t_DDGIVolumes : register(t0);
StructuredBuffer<DDGIVolumeResourceIndices> t_DDGIVolumeResourceIndices : register(t1);
RWStructuredBuffer<GeometryInstance> u_Instances : register(u0);

[numthreads(256, 1, 1)]
void main(uint globalIdx : SV_DispatchThreadID)
{
    DDGIVolumeResourceIndices resourceIndices = t_DDGIVolumeResourceIndices[g_Const.volumeIndex];
    DDGIVolumeDescGPU DDGIVolume = UnpackDDGIVolumeDescGPU(t_DDGIVolumes[g_Const.volumeIndex]);

    uint numProbes = DDGIVolume.probeCounts.x * DDGIVolume.probeCounts.y * DDGIVolume.probeCounts.z;
    if (globalIdx >= numProbes)
        return;

    int3 probeCoords = DDGIGetProbeCoords(globalIdx, DDGIVolume);

    Texture2D<float4> probeData = t_BindlessTextures[resourceIndices.probeDataTextureSRV];

    // Get the probe's world position
    float3 probeWorldPosition = DDGIGetProbeWorldPosition(probeCoords, DDGIVolume, probeData);

    float probeScale = min(min(DDGIVolume.probeSpacing.x, DDGIVolume.probeSpacing.y), DDGIVolume.probeSpacing.z) * 0.15;

    GeometryInstance instance;
    instance.transform = float3x4(
        probeScale, 0, 0, probeWorldPosition.x,
        0, probeScale, 0, probeWorldPosition.y,
        0, 0, probeScale, probeWorldPosition.z);
    instance.instanceIndexAndMask = (1 << 24) | globalIdx;
    instance.instanceOffsetAndFlags = 0;
    instance.deviceAddressLow = g_Const.blasDeviceAddressLow;
    instance.deviceAddressHigh = g_Const.blasDeviceAddressHigh;

    u_Instances[globalIdx] = instance;
}
