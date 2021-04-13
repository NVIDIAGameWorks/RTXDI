/***************************************************************************
 # Copyright (c) 2020-2021, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

/*
This header file is the bridge between the RTXDI resampling functions
and the application resources and parts of shader functionality.

The RTXDI SDK provides the resampling logic, and the application provides
other necessary aspects:
    - Material BRDF evaluation;
    - Ray tracing and transparent/alpha-tested material processing;
    - Light sampling functions and emission profiles.

The structures and functions that are necessary for SDK operation
start with the RAB_ prefix (for RTXDI-Application Bridge).

All structures defined here are opaque for the SDK, meaning that
it makes no assumptions about their contents, they are just passed
between the bridge functions.
*/

#ifndef RTXDI_APPLICATION_BRIDGE_HLSLI
#define RTXDI_APPLICATION_BRIDGE_HLSLI

#include <donut/shaders/brdf.hlsli>
#include <donut/shaders/bindless.h>

#include "ShaderParameters.h"
#include "SceneGeometry.hlsli"

// G-buffer resources
Texture2D<float> t_GBufferDepth : register(t0);
Texture2D<uint> t_GBufferNormals : register(t1);
Texture2D<uint> t_GBufferGeoNormals : register(t2);
Texture2D<uint> t_GBufferBaseColor : register(t3);
Texture2D<uint> t_GBufferMetalRough : register(t4);
Texture2D<float> t_PrevGBufferDepth : register(t5);
Texture2D<uint> t_PrevGBufferNormals : register(t6);
Texture2D<uint> t_PrevGBufferGeoNormals : register(t7);
Texture2D<uint> t_PrevGBufferBaseColor : register(t8);
Texture2D<uint> t_PrevGBufferMetalRough : register(t9);
Texture2D<float4> t_MotionVectors : register(t10);

// Scene resources
RaytracingAccelerationStructure SceneBVH : register(t11);
RaytracingAccelerationStructure PrevSceneBVH : register(t12);
StructuredBuffer<InstanceData> t_InstanceData : register(t13);
StructuredBuffer<GeometryData> t_GeometryData : register(t14);
StructuredBuffer<MaterialConstants> t_MaterialConstants : register(t15);

// RTXDI resources
StructuredBuffer<PolymorphicLightInfo> t_LightDataBuffer : register(t20);
Buffer<float2> t_NeighborOffsets : register(t21);
Buffer<uint> t_LightIndexMappingBuffer : register(t22);
Texture2D<float> t_EnvironmentPdfTexture : register(t23);
Texture2D<float> t_LocalLightPdfTexture : register(t24);

// Screen-sized UAVs
RWStructuredBuffer<RTXDI_PackedReservoir> u_LightReservoirs : register(u0);
RWTexture2D<float4> u_DiffuseLighting : register(u1);
RWTexture2D<float4> u_SpecularLighting : register(u2);

// RTXDI UAVs
RWBuffer<uint2> u_RisBuffer : register(u10);
RWBuffer<uint4> u_RisLightDataBuffer : register(u11);
RWBuffer<uint> u_RayCountBuffer : register(u12);

// Other
ConstantBuffer<ResamplingConstants> g_Const : register(b0);
VK_PUSH_CONSTANT ConstantBuffer<PerPassConstants> g_PerPassConstants : register(b1);
SamplerState s_MaterialSampler : register(s0);
SamplerState s_EnvironmentSampler : register(s1);

#define IES_SAMPLER s_EnvironmentSampler

#include "PolymorphicLight.hlsli"

struct RAB_Surface
{
    float3 worldPos;
    float viewDepth;
    float3 normal;
    float3 geoNormal;
    float3 baseColor;
    float metalness;
    float roughness;
};

struct RAB_LightSample
{
    float3 position;
    float3 normal;
    float3 radiance;
    float solidAnglePdf;
    PolymorphicLightType lightType;
};

typedef PolymorphicLightInfo RAB_LightInfo;
typedef RandomSamplerState RAB_RandomSamplerState;

struct RayPayload
{
    float3 throughput;
    float committedRayT;
    uint instanceID;
    uint primitiveIndex;
    float2 barycentrics;
};

RayDesc setupVisibilityRay(RAB_Surface surface, RAB_LightSample lightSample)
{
    float3 L = lightSample.position - surface.worldPos;

    RayDesc ray;
    ray.TMin = 0.001f;
    ray.TMax = length(L) - 0.001f;
    ray.Direction = normalize(L);
    ray.Origin = surface.worldPos;

    return ray;
}

bool considerTransparentMaterial(uint instanceIndex, uint triangleIndex, float2 rayBarycentrics, inout float3 throughput)
{
    GeometrySample gs = getGeometryFromHit(
        instanceIndex,
        triangleIndex,
        rayBarycentrics,
        GeomAttr_TexCoord,
        t_InstanceData, t_GeometryData, t_MaterialConstants);

    int materialType = (gs.material.flags & MaterialFlags_MaterialType_Mask) >> MaterialFlags_MaterialType_Shift;

    if (gs.material.diffuseTextureIndex < 0)
    {
        throughput *= (1.0 - gs.material.opacity) * gs.material.diffuseColor;
        return (materialType == MaterialType_AlphaTested);
    }

    MaterialSample ms = sampleGeometryMaterial(gs, 0, 0, 0,
        MatAttr_BaseColor, s_MaterialSampler);

    if (materialType == MaterialType_AlphaTested)
        return (ms.opacity > 0.5);

    if (materialType == MaterialType_Transparent)
    {
        throughput *= (1.0 - ms.opacity) * gs.material.diffuseColor;
        return false;
    }

    return false;
}

#if !USE_RAY_QUERY
struct RayAttributes 
{
    float2 uv;
};

[shader("miss")]
void Miss(inout RayPayload payload : SV_RayPayload)
{
}

[shader("closesthit")]
void ClosestHit(inout RayPayload payload : SV_RayPayload, in RayAttributes attrib : SV_IntersectionAttributes)
{
    payload.committedRayT = RayTCurrent();
    payload.instanceID = InstanceID();
    payload.primitiveIndex = PrimitiveIndex();
    payload.barycentrics = attrib.uv;
}

[shader("anyhit")]
void AnyHit(inout RayPayload payload : SV_RayPayload, in RayAttributes attrib : SV_IntersectionAttributes)
{
    if (!considerTransparentMaterial(InstanceID(), PrimitiveIndex(), attrib.uv, payload.throughput))
        IgnoreHit();
}
#endif

// Traces a cheap visibility ray that returns approximate, conservative visibility
// between the surface and the light sample. Conservative means if unsure, assume the light is visible.
// Significant differences between this conservative visibility and the final one will result in more noise.
// When usePreviousFrameScene is true, use the previous frame BVH/TLAS, if it's available, otherwise use the current one.
// This function is used in the temporal and spatial resampling functions for ray traced bias correction.
bool RAB_GetConservativeVisibility(RAB_Surface surface, RAB_LightSample lightSample, bool usePreviousFrameScene)
{
    RayDesc ray = setupVisibilityRay(surface, lightSample);

    bool usePreviousTLAS = usePreviousFrameScene && g_Const.enablePreviousTLAS;

#if USE_RAY_QUERY
    RayQuery<RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> rayQuery;

    if(usePreviousTLAS)
        rayQuery.TraceRayInline(PrevSceneBVH, RAY_FLAG_NONE, INSTANCE_MASK_OPAQUE, ray);
    else
        rayQuery.TraceRayInline(SceneBVH, RAY_FLAG_NONE, INSTANCE_MASK_OPAQUE, ray);

    rayQuery.Proceed();

    bool visible = (rayQuery.CommittedStatus() == COMMITTED_NOTHING);
#else
    RayPayload payload = (RayPayload)0;
    payload.instanceID = ~0u;
    payload.throughput = 1.0;

    if(usePreviousTLAS)
        TraceRay(PrevSceneBVH, RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, INSTANCE_MASK_OPAQUE, 0, 0, 0, ray, payload);
    else
        TraceRay(SceneBVH, RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, INSTANCE_MASK_OPAQUE, 0, 0, 0, ray, payload);

    bool visible = (payload.instanceID == ~0u);
#endif

    REPORT_RAY(!visible);

    return visible;
}

// Traces an expensive visibility ray that considers all alpha tested  and transparent geometry along the way.
// Only used for final shading.
// Not a required bridge function.
float3 GetFinalVisibility(RAB_Surface surface, RAB_LightSample lightSample)
{
    RayDesc ray = setupVisibilityRay(surface, lightSample);

    uint instanceMask = INSTANCE_MASK_OPAQUE;
    uint rayFlags = RAY_FLAG_NONE;
    
    if (g_Const.enableAlphaTestedGeometry)
        instanceMask |= INSTANCE_MASK_ALPHA_TESTED;

    if (g_Const.enableTransparentGeometry)
        instanceMask |= INSTANCE_MASK_TRANSPARENT;

    if (!g_Const.enableTransparentGeometry && !g_Const.enableAlphaTestedGeometry)
        rayFlags |= RAY_FLAG_CULL_NON_OPAQUE;

    RayPayload payload = (RayPayload)0;
    payload.instanceID = ~0u;
    payload.throughput = 1.0;

#if USE_RAY_QUERY
    RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> rayQuery;

    rayQuery.TraceRayInline(SceneBVH, rayFlags, instanceMask, ray);

    while (rayQuery.Proceed())
    {
        if (rayQuery.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            if (considerTransparentMaterial(
                rayQuery.CandidateInstanceID(),
                rayQuery.CandidatePrimitiveIndex(), 
                rayQuery.CandidateTriangleBarycentrics(),
                payload.throughput))
            {
                rayQuery.CommitNonOpaqueTriangleHit();
            }
        }
    }

    if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        payload.instanceID = rayQuery.CommittedInstanceID();
        payload.primitiveIndex = rayQuery.CommittedPrimitiveIndex();
        payload.barycentrics = rayQuery.CommittedTriangleBarycentrics();
        payload.committedRayT = rayQuery.CommittedRayT();
    }
#else
    TraceRay(SceneBVH, rayFlags, instanceMask, 0, 0, 0, ray, payload);
#endif

    REPORT_RAY(payload.instanceID != ~0u);

    if(payload.instanceID == ~0u)
        return payload.throughput.rgb;
    else
        return 0;
}

RAB_Surface GetGBufferSurface(
    int2 pixelPosition, 
    PlanarViewConstants view, 
    Texture2D<float> depthTexture, 
    Texture2D<uint> normalsTexture, 
    Texture2D<uint> geoNormalsTexture, 
    Texture2D<uint> baseColorTexture, 
    Texture2D<uint> metalRoughTexture)
{
    RAB_Surface surface = (RAB_Surface)0;
    surface.viewDepth = depthTexture[pixelPosition];

    if(surface.viewDepth == 0)
        return surface;

    surface.normal = octToNdirUnorm32(normalsTexture[pixelPosition]);
    surface.geoNormal = octToNdirUnorm32(geoNormalsTexture[pixelPosition]);
    surface.baseColor = Unpack_R8G8B8_UFLOAT(baseColorTexture[pixelPosition]);
    float2 metalRough = Unpack_R16G16_UFLOAT(metalRoughTexture[pixelPosition]);
    surface.metalness = metalRough.x;
    surface.roughness = metalRough.y;

    float2 uv = (float2(pixelPosition) + 0.5) * view.viewportSizeInv;
    float4 clipPos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.5, 1);
    float4 viewPos = mul(clipPos, view.matClipToView);
    viewPos.xy /= viewPos.z;
    viewPos.zw = 1.0;
    viewPos.xyz *= surface.viewDepth;
    surface.worldPos = mul(viewPos, view.matViewToWorld).xyz;

    return surface;
}


// Reads the G-buffer, either the current one or the previous one, and returns a surface.
// If the provided pixel position is outside of the viewport bounds, the surface
// should indicate that it's invalid when RAB_IsSurfaceValid is called on it.
RAB_Surface RAB_GetGBufferSurface(int2 pixelPosition, bool previousFrame)
{
    if(previousFrame)
    {
        return GetGBufferSurface(
            pixelPosition, 
            g_Const.prevView, 
            t_PrevGBufferDepth, 
            t_PrevGBufferNormals, 
            t_PrevGBufferGeoNormals, 
            t_PrevGBufferBaseColor, 
            t_PrevGBufferMetalRough);
    }
    else
    {
        return GetGBufferSurface(
            pixelPosition, 
            g_Const.view, 
            t_GBufferDepth, 
            t_GBufferNormals, 
            t_GBufferGeoNormals, 
            t_GBufferBaseColor, 
            t_GBufferMetalRough);
    }
}

// Checks if the given surface is valid, see RAB_GetGBufferSurface.
bool RAB_IsSurfaceValid(RAB_Surface surface)
{
    return surface.viewDepth != 0.f;
}

// Returns the world position of the given surface
float3 RAB_GetSurfaceWorldPos(RAB_Surface surface)
{
    return surface.worldPos;
}

// Returns the world shading normal of the given surface
float3 RAB_GetSurfaceNormal(RAB_Surface surface)
{
    return surface.normal;
}

// Returns the linear depth of the given surface.
// It doesn't have to be linear depth in a strict sense (i.e. viewPos.z),
// and can be distance to the camera or primary path length instead.
// Just make sure that the motion vectors' .z component follows the same logic.
float RAB_GetSurfaceLinearDepth(RAB_Surface surface)
{
    return surface.viewDepth;
}

// Initialized the random sampler for a given pixel or tile index.
// The pass parameter is provided to help generate different RNG sequences
// for different resampling passes, which is important for image quality.
// In general, a high quality RNG is critical to get good results from ReSTIR.
// A table-based blue noise RNG dose not provide enough entropy, for example.
RAB_RandomSamplerState RAB_InitRandomSampler(uint2 index, uint pass)
{
    return initRandomSampler(index, g_Const.frameIndex + pass * 13);
}

// Draws a random number X from the sampler, so that (0 <= X < 1).
float RAB_GetNextRandom(inout RAB_RandomSamplerState rng)
{
    return sampleUniformRng(rng);
}

float EvaluateSpecularSampledLightingWeight(RAB_Surface surface, float3 L, float solidAnglePdf)
{
    if (!g_Const.enableBrdfMIS)
        return 1.0;

    float3 V = normalize(g_Const.view.cameraDirectionOrPosition.xyz - surface.worldPos);
    float ggxVndfPdf = ImportanceSampleGGX_VNDF_PDF(max(surface.roughness, 0.01), surface.normal, V, L);

    // Balance heuristic assuming one sample from each strategy: light sampling and BRDF sampling
    return saturate(solidAnglePdf / (solidAnglePdf + ggxVndfPdf));
}

// Computes the weight of the given light samples when the given surface is
// shaded using that light sample. Exact or approximate BRDF evaluation can be
// used to compute the weight. ReSTIR will converge to a correct lighting result
// even if all samples have a fixed weight of 1.0, but that will be very noisy.
// Scaling of the weights can be arbitrary, as long as it's consistent
// between all lights and surfaces.
float RAB_GetLightSampleTargetPdfForSurface(RAB_LightSample lightSample, RAB_Surface surface)
{
    if (lightSample.solidAnglePdf <= 0)
        return 0;

    float3 L = normalize(lightSample.position - surface.worldPos);

    if (dot(L, surface.geoNormal) <= 0)
        return 0;
    
    float3 V = normalize(g_Const.view.cameraDirectionOrPosition.xyz - surface.worldPos);
    float F0 = lerp(0.05, 1.0, surface.metalness);

    float d = Lambert(surface.normal, -L);
    float s = GGX(V, L, surface.normal, surface.roughness, F0);

    if (lightSample.lightType == PolymorphicLightType::kTriangle || 
        lightSample.lightType == PolymorphicLightType::kEnvironment)
    {
        // Only apply MIS to triangle and environment lights: other types have no geometric representation
        // and therefore cannot be hit by BRDF rays.
        s *= EvaluateSpecularSampledLightingWeight(surface, L, lightSample.solidAnglePdf);
    }

    float3 albedo, baseReflectivity;
    getReflectivity(surface.metalness, surface.baseColor, albedo, baseReflectivity);

    float3 reflectedRadiance = lightSample.radiance * (d * albedo + s * baseReflectivity);
    
    return calcLuminance(reflectedRadiance) / lightSample.solidAnglePdf;
}

// Computes the weight of the given light for arbitrary surfaces located inside 
// the specified volume. Used for world-space light grid construction.
float RAB_GetLightTargetPdfForVolume(RAB_LightInfo light, float3 volumeCenter, float volumeRadius)
{
    return PolymorphicLight::getWeightForVolume(light, volumeCenter, volumeRadius);
}

// Samples a polymorphic light relative to the given receiver surface.
// For most light types, the "uv" parameter is just a pair of uniform random numbers, originally
// produced by the RAB_GetNextRandom function and then stored in light reservoirs.
// For importance sampled environment lights, the "uv" parameter has the texture coordinates
// in the PDF texture, normalized to the (0..1) range.
RAB_LightSample RAB_SamplePolymorphicLight(RAB_LightInfo lightInfo, RAB_Surface surface, float2 uv)
{
    PolymorphicLightSample pls = PolymorphicLight::calcSample(lightInfo, uv, surface.worldPos);

    RAB_LightSample lightSample;
    lightSample.position = pls.position;
    lightSample.normal = pls.normal;
    lightSample.radiance = pls.radiance;
    lightSample.solidAnglePdf = pls.solidAnglePdf;
    lightSample.lightType = getLightType(lightInfo);
    return lightSample;
}

// Loads polymorphic light data from the global light buffer.
RAB_LightInfo RAB_LoadLightInfo(uint index, bool previousFrame)
{
    return t_LightDataBuffer[index];
}

// Loads triangle light data from a tile produced by the presampling pass.
RAB_LightInfo RAB_LoadCompactLightInfo(uint linearIndex)
{
    uint4 packedData1, packedData2;
    packedData1 = u_RisLightDataBuffer[linearIndex * 2 + 0];
    packedData2 = u_RisLightDataBuffer[linearIndex * 2 + 1];
    return unpackCompactLightInfo(packedData1, packedData2);
}

// Stores triangle light data into a tile.
// Returns true if this light can be stored in a tile (i.e. compacted).
// If it cannot, for example it's a shaped light, this function returns false and doesn't store.
// A basic implementation can ignore this feature and always return false, which is just slower.
bool RAB_StoreCompactLightInfo(uint linearIndex, RAB_LightInfo lightInfo)
{
    uint4 data1, data2;
    if (!packCompactLightInfo(lightInfo, data1, data2))
        return false;

    u_RisLightDataBuffer[linearIndex * 2 + 0] = data1;
    u_RisLightDataBuffer[linearIndex * 2 + 1] = data2;

    return true;
}

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

// Forward declare the SDK function that's used in RAB_AreMaterialsSimilar
bool RTXDI_CompareRelativeDifference(float reference, float candidate, float threshold);

// Compares the materials of two surfaces, returns true if the surfaces
// are similar enough that we can share the light reservoirs between them.
// If unsure, just return true.
bool RAB_AreMaterialsSimilar(RAB_Surface a, RAB_Surface b)
{
    const float roughnessThreshold = 0.5;
    const float metalnessThreshold = 0.5;
    const float colorThreshold = 0.5;

    if (!RTXDI_CompareRelativeDifference(a.roughness, b.roughness, roughnessThreshold))
        return false;

    if (!RTXDI_CompareRelativeDifference(a.metalness, b.metalness, metalnessThreshold))
        return false;

    if (!RTXDI_CompareRelativeDifference(calcLuminance(a.baseColor), calcLuminance(b.baseColor), colorThreshold))
        return false;

    return true;
}

#endif // RTXDI_APPLICATION_BRIDGE_HLSLI