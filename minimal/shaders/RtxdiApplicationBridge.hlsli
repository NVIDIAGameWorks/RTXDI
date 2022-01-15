/***************************************************************************
 # Copyright (c) 2020-2022, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#ifndef RTXDI_APPLICATION_BRIDGE_HLSLI
#define RTXDI_APPLICATION_BRIDGE_HLSLI

// See RtxdiApplicationBridge.hlsli in the full sample app for more information.
// This is a minimal viable implementation.

#include <donut/shaders/brdf.hlsli>
#include <donut/shaders/bindless.h>

#include "ShaderParameters.h"
#include "SceneGeometry.hlsli"
#include "GBufferHelpers.hlsli"

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

// Screen-sized UAVs
RWStructuredBuffer<RTXDI_PackedReservoir> u_LightReservoirs : register(u0);
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

#include "TriangleLight.hlsli"

// A surface with enough information to evaluate BRDFs
struct RAB_Surface
{
    float3 worldPos;
    float viewDepth;
    float3 normal;
    float3 geoNormal;
    float3 diffuseAlbedo;
    float3 specularF0;
    float roughness;
};

typedef RandomSamplerState RAB_RandomSamplerState;

RAB_Surface RAB_EmptySurface()
{
    RAB_Surface surface = (RAB_Surface)0;
    surface.viewDepth = BACKGROUND_DEPTH;
    return surface;
}

RAB_LightInfo RAB_EmptyLightInfo()
{
    return (RAB_LightInfo)0;
}

RAB_LightSample RAB_EmptyLightSample()
{
    return (RAB_LightSample)0;
}

RayDesc setupVisibilityRay(RAB_Surface surface, RAB_LightSample lightSample, float offset = 0.001)
{
    float3 L = lightSample.position - surface.worldPos;

    RayDesc ray;
    ray.TMin = offset;
    ray.TMax = length(L) - offset;
    ray.Direction = normalize(L);
    ray.Origin = surface.worldPos;

    return ray;
}

// Tests the visibility between a surface and a light sample.
// Returns true if there is nothing between them.
bool RAB_GetConservativeVisibility(RAB_Surface surface, RAB_LightSample lightSample)
{
    RayDesc ray = setupVisibilityRay(surface, lightSample);

    RayQuery<RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> rayQuery;

    rayQuery.TraceRayInline(SceneBVH, RAY_FLAG_NONE, INSTANCE_MASK_OPAQUE, ray);

    rayQuery.Proceed();

    bool visible = (rayQuery.CommittedStatus() == COMMITTED_NOTHING);
    
    return visible;
}

// Tests the visibility between a surface and a light sample on the previous frame.
// Since the scene is static in this sample app, it's equivalent to RAB_GetConservativeVisibility.
bool RAB_GetTemporalConservativeVisibility(RAB_Surface currentSurface, RAB_Surface previousSurface,
    RAB_LightSample lightSample)
{
    return RAB_GetConservativeVisibility(currentSurface, lightSample);
}

// Load a sample from the previous G-buffer.
RAB_Surface RAB_GetGBufferSurface(int2 pixelPosition, bool previousFrame)
{
    RAB_Surface surface = RAB_EmptySurface();

    // We do not have access to the current G-buffer in this sample because it's using
    // a single render pass with a fused resampling kernel, so just return an invalid surface.
    // This should never happen though, as the fused kernel doesn't call RAB_GetGBufferSurface(..., false)
    if (!previousFrame)
        return surface;

    const PlanarViewConstants view = g_Const.prevView;

    if (any(pixelPosition >= view.viewportSize))
        return surface;

    surface.viewDepth = t_PrevGBufferDepth[pixelPosition];

    if(surface.viewDepth == BACKGROUND_DEPTH)
        return surface;

    surface.normal = octToNdirUnorm32(t_PrevGBufferNormals[pixelPosition]);
    surface.geoNormal = octToNdirUnorm32(t_PrevGBufferGeoNormals[pixelPosition]);
    surface.diffuseAlbedo = Unpack_R11G11B10_UFLOAT(t_PrevGBufferDiffuseAlbedo[pixelPosition]).rgb;
    float4 specularRough = Unpack_R8G8B8A8_Gamma_UFLOAT(t_PrevGBufferSpecularRough[pixelPosition]);
    surface.specularF0 = specularRough.rgb;
    surface.roughness = specularRough.a;
    surface.worldPos = viewDepthToWorldPos(view, pixelPosition, surface.viewDepth);

    return surface;
}

bool RAB_IsSurfaceValid(RAB_Surface surface)
{
    return surface.viewDepth != BACKGROUND_DEPTH;
}

float3 RAB_GetSurfaceWorldPos(RAB_Surface surface)
{
    return surface.worldPos;
}

float3 RAB_GetSurfaceNormal(RAB_Surface surface)
{
    return surface.normal;
}

float RAB_GetSurfaceLinearDepth(RAB_Surface surface)
{
    return surface.viewDepth;
}

RAB_RandomSamplerState RAB_InitRandomSampler(uint2 index, uint pass)
{
    return initRandomSampler(index, g_Const.frameIndex + pass * 13);
}

float RAB_GetNextRandom(inout RAB_RandomSamplerState rng)
{
    return sampleUniformRng(rng);
}

// Evaluate the surface BRDF and compute the weighted reflected radiance for the given light sample
float3 ShadeSurfaceWithLightSample(RAB_LightSample lightSample, RAB_Surface surface)
{
    // Ignore invalid light samples
    if (lightSample.solidAnglePdf <= 0)
        return 0;

    float3 L = normalize(lightSample.position - surface.worldPos);

    // Ignore light samples that are below the geometric surface (but above the normal mapped surface)
    if (dot(L, surface.geoNormal) <= 0)
        return 0;


    float3 V = normalize(g_Const.view.cameraDirectionOrPosition.xyz - surface.worldPos);
    
    // Evaluate the BRDF
    float diffuse = Lambert(surface.normal, -L);
    float3 specular = GGX_times_NdotL(V, L, surface.normal, surface.roughness, surface.specularF0);

    float3 reflectedRadiance = lightSample.radiance * (diffuse * surface.diffuseAlbedo + specular);

    return reflectedRadiance / lightSample.solidAnglePdf;
}

// Compute the target PDF (p-hat) for the given light sample relative to a surface
float RAB_GetLightSampleTargetPdfForSurface(RAB_LightSample lightSample, RAB_Surface surface)
{
    // Second-best implementation: the PDF is proportional to the reflected radiance.
    // The best implementation would be taking visibility into account,
    // but that would be prohibitively expensive.
    return calcLuminance(ShadeSurfaceWithLightSample(lightSample, surface));
}

// Compute the position on a triangle light given a pair of random numbers
RAB_LightSample RAB_SamplePolymorphicLight(RAB_LightInfo lightInfo, RAB_Surface surface, float2 uv)
{
    return TriangleLight::Create(lightInfo).calcSample(uv, surface.worldPos);
}

// Load the packed light information from the buffer.
// Ignore the previousFrame parameter as our lights are static in this sample.
RAB_LightInfo RAB_LoadLightInfo(uint index, bool previousFrame)
{
    return t_LightDataBuffer[index];
}

// Translate the light index between the current and previous frame.
// Do nothing as our lights are static in this sample.
int RAB_TranslateLightIndex(uint lightIndex, bool currentToPrevious)
{
    return int(lightIndex);
}

// Compare the materials of two surfaces to improve resampling quality.
// Just say that everything is similar for simplicity.
bool RAB_AreMaterialsSimilar(RAB_Surface a, RAB_Surface b)
{
    return true;
}

#endif // RTXDI_APPLICATION_BRIDGE_HLSLI
