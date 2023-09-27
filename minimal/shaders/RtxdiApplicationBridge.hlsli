/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
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

#include "TriangleLight.hlsli"

static const float kMinRoughness = 0.05f;

// A surface with enough information to evaluate BRDFs
struct RAB_Surface
{
    float3 worldPos;
    float3 viewDir;
    float viewDepth;
    float3 normal;
    float3 geoNormal;
    float3 diffuseAlbedo;
    float3 specularF0;
    float roughness;
    float diffuseProbability;
};

typedef RandomSamplerState RAB_RandomSamplerState;

float getSurfaceDiffuseProbability(RAB_Surface surface)
{
    float diffuseWeight = calcLuminance(surface.diffuseAlbedo);
    float specularWeight = calcLuminance(Schlick_Fresnel(surface.specularF0, dot(surface.viewDir, surface.normal)));
    float sumWeights = diffuseWeight + specularWeight;
    return sumWeights < 1e-7f ? 1.f : (diffuseWeight / sumWeights);
}

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

void RAB_GetLightDirDistance(RAB_Surface surface, RAB_LightSample lightSample,
    out float3 o_lightDir,
    out float o_lightDistance)
{
    float3 toLight = lightSample.position - surface.worldPos;
    o_lightDistance = length(toLight);
    o_lightDir = toLight / o_lightDistance;
}

bool RAB_IsAnalyticLightSample(RAB_LightSample lightSample)
{
    return false;
}

float RAB_LightSampleSolidAnglePdf(RAB_LightSample lightSample)
{
    return lightSample.solidAnglePdf;
}


float2 RAB_GetEnvironmentMapRandXYFromDir(float3 worldDir)
{
    return 0;
}

float RAB_EvaluateEnvironmentMapSamplingPdf(float3 L)
{
    // No Environment sampling
    return 0;
}

float RAB_EvaluateLocalLightSourcePdf(uint lightIndex)
{
    // Uniform pdf
    return 1.0 / g_Const.lightBufferParams.localLightBufferRegion.numLights;
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

// This function is called in the spatial resampling passes to make sure that 
// the samples actually land on the screen and not outside of its boundaries.
// It can clamp the position or reflect it about the nearest screen edge.
// The simplest implementation will just return the input pixelPosition.
int2 RAB_ClampSamplePositionIntoView(int2 pixelPosition, bool previousFrame)
{
    return clamp(pixelPosition, 0, int2(g_Const.view.viewportSize) - 1);
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
    surface.viewDir = normalize(g_Const.view.cameraDirectionOrPosition.xyz - surface.worldPos);
    surface.diffuseProbability = getSurfaceDiffuseProbability(surface);

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

// World space from surface to eye
float3 RAB_GetSurfaceViewDir(RAB_Surface surface)
{
    return surface.viewDir;
}

float3 RAB_GetSurfaceNormal(RAB_Surface surface)
{
    return surface.normal;
}

float RAB_GetSurfaceLinearDepth(RAB_Surface surface)
{
    return surface.viewDepth;
}

float3 worldToTangent(RAB_Surface surface, float3 w)
{
    // reconstruct tangent frame based off worldspace normal
    // this is ok for isotropic BRDFs
    // for anisotropic BRDFs, we need a user defined tangent
    float3 tangent;
    float3 bitangent;
    ConstructONB(surface.normal, tangent, bitangent);

    return float3(dot(bitangent, w), dot(tangent, w), dot(surface.normal, w));
}

float3 tangentToWorld(RAB_Surface surface, float3 h)
{
    // reconstruct tangent frame based off worldspace normal
    // this is ok for isotropic BRDFs
    // for anisotropic BRDFs, we need a user defined tangent
    float3 tangent;
    float3 bitangent;
    ConstructONB(surface.normal, tangent, bitangent);

    return bitangent * h.x + tangent * h.y + surface.normal * h.z;
}

RAB_RandomSamplerState RAB_InitRandomSampler(uint2 index, uint pass)
{
    return initRandomSampler(index, g_Const.frameIndex + pass * 13);
}

float RAB_GetNextRandom(inout RAB_RandomSamplerState rng)
{
    return sampleUniformRng(rng);
}

// Output an importanced sampled reflection direction from the BRDF given the view
// Return true if the returned direction is above the surface
bool RAB_GetSurfaceBrdfSample(RAB_Surface surface, inout RAB_RandomSamplerState rng, out float3 dir)
{
    float3 rand;
    rand.x = RAB_GetNextRandom(rng);
    rand.y = RAB_GetNextRandom(rng);
    rand.z = RAB_GetNextRandom(rng);
    if (rand.x < surface.diffuseProbability)
    {
        float pdf;
        float3 h = SampleCosHemisphere(rand.yz, pdf);
        dir = tangentToWorld(surface, h);
    }
    else
    {
        float3 h = ImportanceSampleGGX(rand.yz, max(surface.roughness, kMinRoughness));
        dir = reflect(-surface.viewDir, tangentToWorld(surface, h));
    }

    return dot(surface.normal, dir) > 0.f;
}

// Return PDF wrt solid angle for the BRDF in the given dir
float RAB_GetSurfaceBrdfPdf(RAB_Surface surface, float3 dir)
{
    float cosTheta = saturate(dot(surface.normal, dir));
    float diffusePdf = cosTheta / M_PI;
    float specularPdf = ImportanceSampleGGX_VNDF_PDF(max(surface.roughness, kMinRoughness), surface.normal, surface.viewDir, dir);
    float pdf = cosTheta > 0.f ? lerp(specularPdf, diffusePdf, surface.diffuseProbability) : 0.f;
    return pdf;
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


    float3 V = surface.viewDir;
    
    // Evaluate the BRDF
    float diffuse = Lambert(surface.normal, -L);
    float3 specular = GGX_times_NdotL(V, L, surface.normal, max(surface.roughness, kMinRoughness), surface.specularF0);

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

uint getLightIndex(uint instanceID, uint geometryIndex, uint primitiveIndex)
{
    uint lightIndex = RTXDI_InvalidLightIndex;
    InstanceData hitInstance = t_InstanceData[instanceID];
    uint geometryInstanceIndex = hitInstance.firstGeometryInstanceIndex + geometryIndex;
    lightIndex = t_GeometryInstanceToLight[geometryInstanceIndex];
    if (lightIndex != RTXDI_InvalidLightIndex)
      lightIndex += primitiveIndex;
    return lightIndex;
}


// Return true if anything was hit. If false, RTXDI will do environment map sampling
// o_lightIndex: If hit, must be a valid light index for RAB_LoadLightInfo, if no local light was hit, must be RTXDI_InvalidLightIndex
// randXY: The randXY that corresponds to the hit location and is the same used for RAB_SamplePolymorphicLight
bool RAB_TraceRayForLocalLight(float3 origin, float3 direction, float tMin, float tMax,
    out uint o_lightIndex, out float2 o_randXY)
{
    o_lightIndex = RTXDI_InvalidLightIndex;
    o_randXY = 0;

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = tMin;
    ray.TMax = tMax;

    RayQuery<RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> rayQuery;
    rayQuery.TraceRayInline(SceneBVH, RAY_FLAG_NONE, INSTANCE_MASK_OPAQUE, ray);
    rayQuery.Proceed();

    bool hitAnything = rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    if (hitAnything)
    {
        o_lightIndex = getLightIndex(rayQuery.CommittedInstanceID(), rayQuery.CommittedGeometryIndex(), rayQuery.CommittedPrimitiveIndex());
        if (o_lightIndex != RTXDI_InvalidLightIndex)
        {
            float2 hitUV = rayQuery.CommittedTriangleBarycentrics();
            o_randXY = randomFromBarycentric(hitUVToBarycentric(hitUV));
        }
    }

    return hitAnything;
}

#endif // RTXDI_APPLICATION_BRIDGE_HLSLI
