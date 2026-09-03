#ifndef RAB_BUFFER_HLSLI
#define RAB_BUFFER_HLSLI

#include "Rtxdi/RtxdiParameters.h"

// Previous G-buffer resources
Texture2D<float> t_PrevGBufferDepth : register(t0);
Texture2D<uint> t_PrevGBufferNormals : register(t1);
Texture2D<uint> t_PrevGBufferGeoNormals : register(t2);
Texture2D<uint> t_PrevGBufferDiffuseAlbedo : register(t3);
Texture2D<uint> t_PrevGBufferSpecularRough : register(t4);

// Scene resources
RaytracingAccelerationStructure SceneBVH : register(t30);
StructuredBuffer<InstanceData> t_InstanceData : register(t32);
StructuredBuffer<GeometryData> t_GeometryData : register(t33);
StructuredBuffer<MaterialConstants> t_MaterialConstants : register(t34);

// RTXDI resources
StructuredBuffer<RAB_LightInfo> t_LightDataBuffer : register(t20);
Buffer<float2> t_NeighborOffsets : register(t21);
StructuredBuffer<uint> t_GeometryInstanceToLight : register(t22);

// Screen-sized UAVs
RWStructuredBuffer<RTXDI_PackedDIReservoir> u_LightReservoirs : register(u0);
RWTexture2D<float4> u_ShadingOutput : register(u1);
RWTexture2D<float> u_GBufferDepth : register(u2);
RWTexture2D<uint> u_GBufferNormals : register(u3);
RWTexture2D<uint> u_GBufferGeoNormals : register(u4);
RWTexture2D<uint> u_GBufferDiffuseAlbedo : register(u5);
RWTexture2D<uint> u_GBufferSpecularRough : register(u6);

// Other
ConstantBuffer<ResamplingConstants> g_Const : register(b0);
SamplerState s_MaterialSampler : register(s0);

#define RTXDI_LIGHT_RESERVOIR_BUFFER u_LightReservoirs
#define RTXDI_NEIGHBOR_OFFSETS_BUFFER t_NeighborOffsets

// Translate the light index between the current and previous frame.
// Do nothing as our lights are static in this sample.
int RAB_TranslateLightIndex(uint lightIndex, bool currentToPrevious)
{
    return int(lightIndex);
}

// Load the packed light information from the buffer.
// Ignore the prevFrame parameter as our lights are static in this sample.
RAB_LightInfo RAB_LoadLightInfo(uint index, bool prevFrame)
{
    return t_LightDataBuffer[index];
}

#endif // RAB_BUFFER_HLSLI
