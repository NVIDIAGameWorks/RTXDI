/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#ifndef RAB_BUFFER_HLSLI
#define RAB_BUFFER_HLSLI

#include <SharedShaderInclude/ShaderParameters.h>
#include <SharedShaderInclude/ShaderDebug/ShaderDebugPrintShared.h>
#include <SharedShaderInclude/ShaderDebug/PTPathViz/PTPathVertexRecord.h>
#include <SharedShaderInclude/ShaderDebug/PTPathViz/PTPathSetRecord.h>

// G-buffer resources
Texture2D<float> t_GBufferDepth : register(t0);
Texture2D<uint> t_GBufferNormals : register(t1);
Texture2D<uint> t_GBufferGeoNormals : register(t2);
Texture2D<uint> t_GBufferDiffuseAlbedo : register(t3);
Texture2D<uint> t_GBufferSpecularRough : register(t4);
Texture2D<float> t_PrevGBufferDepth : register(t5);
Texture2D<uint> t_PrevGBufferNormals : register(t6);
Texture2D<uint> t_PrevGBufferGeoNormals : register(t7);
Texture2D<uint> t_PrevGBufferDiffuseAlbedo : register(t8);
Texture2D<uint> t_PrevGBufferSpecularRough : register(t9);
Texture2D<float2> t_PrevRestirLuminance : register(t10);
Texture2D<float4> t_MotionVectors : register(t11);
Texture2D<float4> t_DenoiserNormalRoughness : register(t12);
Texture2D<float4> t_PrevNeighborSelectionGBuffer : register(t13);
Texture2D<float> t_PrevSmoothedPTDuplicationMap : register(t14);

// Scene resources
RaytracingAccelerationStructure SceneBVH : register(t30);
RaytracingAccelerationStructure PrevSceneBVH : register(t31);
StructuredBuffer<InstanceData> t_InstanceData : register(t32);
StructuredBuffer<GeometryData> t_GeometryData : register(t33);
StructuredBuffer<MaterialConstants> t_MaterialConstants : register(t34);

// RTXDI resources
StructuredBuffer<PolymorphicLightInfo> t_LightDataBuffer : register(t20);
Buffer<float2> t_NeighborOffsets : register(t21);
Buffer<uint> t_LightIndexMappingBuffer : register(t22);
Texture2D t_EnvironmentPdfTexture : register(t23);
Texture2D t_LocalLightPdfTexture : register(t24);
StructuredBuffer<uint> t_GeometryInstanceToLight : register(t25);

// Screen-sized UAVs
RWStructuredBuffer<RTXDI_PackedDIReservoir> u_LightReservoirs : register(u0);
RWTexture2D<float4> u_DiffuseLighting : register(u1);
RWTexture2D<float4> u_SpecularLighting : register(u2);
RWTexture2D<int2> u_TemporalSamplePositions : register(u3);
RWTexture2DArray<float4> u_Gradients : register(u4);
RWTexture2D<float2> u_RestirLuminance : register(u5);
RWStructuredBuffer<RTXDI_PackedGIReservoir> u_GIReservoirs : register(u6);
RWStructuredBuffer<RTXDI_PackedPTReservoir> u_PTReservoirs : register(u7);
RWByteAddressBuffer u_SpatialNeighborSelection : register(u8);

// PSR UAVs
RWTexture2D<float> u_PSRDepth : register(u20);
RWTexture2D<float4> u_PSRNormalRoughness : register(u21);
RWTexture2D<float4> u_PSRMotionVectors : register(u22);
RWTexture2D<float> u_PSRHitT : register(u23);
RWTexture2D<uint> u_PSRDiffuseAlbedo : register(u24);
RWTexture2D<uint> u_PSRSpecularF0 : register(u25);
RWTexture2D<uint> u_PSRLightDir : register(u26);
RWTexture2D<uint> u_PTSampleIDTexture : register(u27);
RWTexture2D<float2> u_PTDuplicationMap : register(u28);
RWTexture2D<float4> u_NeighborSelectionGBuffer : register(u29);

// RTXDI UAVs
RWBuffer<uint2> u_RisBuffer : register(u10);
RWBuffer<uint4> u_RisLightDataBuffer : register(u11);
RWBuffer<uint> u_RayCountBuffer : register(u12);
RWStructuredBuffer<SecondaryGBufferData> u_SecondaryGBuffer : register(u13);
RWTexture2D<float> u_SmoothedPTDuplicationMap : register(u19);
RWTexture2D<float> u_PTDecorrelationFactor : register(u30); // for debug viz

// Constant buffer
ConstantBuffer<ResamplingConstants> g_Const : register(b0);
VK_PUSH_CONSTANT ConstantBuffer<PerPassConstants> g_PerPassConstants : register(b1);

// Debug Print
ConstantBuffer<ShaderPrintCBData> g_debugPrintCB : register(b2);
RWByteAddressBuffer u_DebugPrintBuffer : register(u16);
#define RTXDI_SHADER_DEBUG_PRINT_CB g_debugPrintCB
#define RTXDI_SHADER_DEBUG_PRINT_OUTPUT_BUFFER u_DebugPrintBuffer

// PT Path Viz
RWStructuredBuffer<PTPathVertexRecord> u_debugPathRecord : register(u14);
RWStructuredBuffer<PTPathSet> u_debugPathSet : register(u15);
#define DEBUG_PT_VERTEX_RECORD_BUFFER u_debugPathRecord
#define DEBUG_PT_PATH_SET_BUFFER u_debugPathSet

// Debug Lighting Buffers
RWTexture2D<float4> u_DirectLightingRaw : register(u17);
RWTexture2D<float4> u_IndirectLightingRaw : register(u18);

// Other
SamplerState s_MaterialSampler : register(s0);
SamplerState s_EnvironmentSampler : register(s1);

#define RTXDI_RIS_BUFFER u_RisBuffer
#define RTXDI_LIGHT_RESERVOIR_BUFFER u_LightReservoirs
#define RTXDI_NEIGHBOR_OFFSETS_BUFFER t_NeighborOffsets
#define RTXDI_GI_RESERVOIR_BUFFER u_GIReservoirs
#define RTXDI_PT_RESERVOIR_BUFFER u_PTReservoirs
#define RTXDI_SPATIAL_NEIGHBOR_SELECTION_BUFFER u_SpatialNeighborSelection
#define RTXDI_PREV_NEIGHBOR_SELECTION_GBUFFER t_PrevNeighborSelectionGBuffer
#define RTXDI_PT_SAMPLE_ID_TEXTURE u_PTSampleIDTexture
#define RTXDI_PT_DUPLICATION_MAP u_PTDuplicationMap
#define RTXDI_PT_SMOOTHED_DUPLICATION_MAP u_SmoothedPTDuplicationMap
#define RTXDI_PT_PREV_SMOOTHED_DUPLICATION_MAP t_PrevSmoothedPTDuplicationMap
#define RTXDI_PT_DEBUG u_debug

#define IES_SAMPLER s_EnvironmentSampler

// Translates the light index from the current frame to the previous frame (if currentToPrevious = true)
// or from the previous frame to the current frame (if currentToPrevious = false).
// Returns the new index, or a negative number if the light does not exist in the other frame.
int RAB_TranslateLightIndex(uint lightIndex, bool currentToPrevious)
{
    // In this implementation, the mapping buffer contains both forward and reverse mappings,
    // stored at different offsets, so we don't care about the currentToPrevious parameter.
    uint mappedIndexPlusOne = t_LightIndexMappingBuffer[lightIndex];

    // The mappings are stored offset by 1 to differentiate between valid and invalid mappings.
    // The buffer is cleared with zeros which indicate an invalid mapping.
    // Subtract that one to make this function return expected values.
    return int(mappedIndexPlusOne) - 1;
}

// Duplication map: count of pixels sharing the same sample ID in a neighborhood (max 255 after RG8_UNORM compression).
// Used for duplication-based temporal history reduction (MCap). prevPixelPos is in pixel space.
uint RAB_GetDuplicationMapCount(int2 prevPixelPos)
{
    int2 dim = int2(g_Const.view.viewportSize);
    if (any(prevPixelPos < 0) || any(prevPixelPos >= dim))
        return 0u;
    return uint(round(saturate(u_PTDuplicationMap[prevPixelPos].x) * 255.0));
}

#endif // RAB_BUFFER_HLSLI
