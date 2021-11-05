/*
* Copyright (c) 2019-2021, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma pack_matrix(row_major)

#include "DDGIShaderConfig.h"

// -------------------------------------------------------------------------------------------

// Disable specular MIS on direct lighting of the surfaces found by probe rays,
// because there are no BRDF rays shot from these surfaces.
#define RAB_ENABLE_SPECULAR_MIS 0

#define HLSL
#include <ddgi/Irradiance.hlsl>

#include "../LightingPasses/RtxdiApplicationBridge.hlsli"
#include <rtxdi/ResamplingFunctions.hlsli>

#ifdef WITH_NRD
#undef WITH_NRD
#endif
#include "../LightingPasses/ShadingHelpers.hlsli"

StructuredBuffer<DDGIVolumeDescGPUPacked> t_DDGIVolumes : register(t40 VK_DESCRIPTOR_SET(2));
StructuredBuffer<DDGIVolumeResourceIndices> t_DDGIVolumeResourceIndices : register(t41 VK_DESCRIPTOR_SET(2));

SamplerState s_ProbeSampler : register(s40 VK_DESCRIPTOR_SET(2));

static const float c_MaxIndirectRadiance = 10;

#if USE_RAY_QUERY
[numthreads(16, 16, 1)]
void main(uint2 DispatchIndex : SV_DispatchThreadID)
#else
[shader("raygeneration")]
void RayGen()
#endif
{
#if !USE_RAY_QUERY
    uint2 DispatchIndex = DispatchRaysIndex().xy;
#endif

    int rayIndex = DispatchIndex.x;                    // index of the current probe ray
    int probeIndex = DispatchIndex.y;                  // index of current probe

    RAB_RandomSamplerState rng = RAB_InitRandomSampler(DispatchIndex, 6);

    // Get the DDGIVolume's index
    uint volumeIndex = g_PerPassConstants.rtxgiVolumeIndex;

    // Get the DDGIVolume's constants
    DDGIVolumeResourceIndices resourceIndices = t_DDGIVolumeResourceIndices[volumeIndex];
    DDGIVolumeDescGPU DDGIVolume = UnpackDDGIVolumeDescGPU(t_DDGIVolumes[volumeIndex]);

    // Fill the volume's resource pointers
    DDGIVolumeResources resources;
    resources.probeIrradiance = t_BindlessTextures[resourceIndices.irradianceTextureSRV];
    resources.probeDistance = t_BindlessTextures[resourceIndices.distanceTextureSRV];
    resources.probeData = t_BindlessTextures[resourceIndices.probeDataTextureSRV];
    resources.bilinearSampler = s_ProbeSampler;

    RWTexture2D<float4> rayDataTexture = u_BindlessTexturesRW[resourceIndices.rayDataTextureUAV];

    // Get the probe's grid coordinates
    int3 probeCoords = DDGIGetProbeCoords(probeIndex, DDGIVolume);

    // Adjust the probe index for the scroll offsets
    probeIndex = DDGIGetScrollingProbeIndex(probeCoords, DDGIVolume);

    int2 rayStoragePosition = int2(rayIndex, probeIndex);

    // Get the probe's state if classification is enabled
    // Early out: do not shoot rays when the probe is inactive *unless* it is one of the "fixed" rays used by probe classification
    float probeState = RTXGI_DDGI_PROBE_STATE_ACTIVE;
    if (DDGIVolume.probeClassificationEnabled)
    {
        // Get the probe's texel coordinates in the Probe Data texture
        int2 probeDataCoords = DDGIGetProbeDataTexelCoords(probeIndex, DDGIVolume);

        // Get the probe's classification state
        probeState = resources.probeData.Load(int3(probeDataCoords, 0)).w;

        // Early out if the probe is inactive and the ray is not one of the fixed rays
        if (probeState == RTXGI_DDGI_PROBE_STATE_INACTIVE && rayIndex >= RTXGI_DDGI_NUM_FIXED_RAYS) return;
    }

    // Get the probe's world position
    // Note: world positions are computed from probe coordinates *not* adjusted for infinite scrolling
    float3 probeWorldPosition = DDGIGetProbeWorldPosition(probeCoords, DDGIVolume, resources.probeData);

    // Get a ray direction for this probe
    float3 probeRayDirection = DDGIGetProbeRayDirection(rayIndex, DDGIVolume);

    // Setup the probe ray
    RayDesc ray;
    ray.Origin = probeWorldPosition;
    ray.Direction = probeRayDirection;
    ray.TMin = 0.f;
    ray.TMax = DDGIVolume.probeMaxRayDistance;

    // Trace the Probe Ray
    RayPayload payload = (RayPayload)0;
    payload.instanceID = ~0u;
    payload.throughput = 1.0;

    uint instanceMask = INSTANCE_MASK_OPAQUE;
    
#if USE_RAY_QUERY
    RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_FORCE_OPAQUE> rayQuery;

    rayQuery.TraceRayInline(SceneBVH, 0, instanceMask, ray);

    rayQuery.Proceed();

    if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        payload.instanceID = rayQuery.CommittedInstanceID();
        payload.geometryIndex = rayQuery.CommittedGeometryIndex();
        payload.primitiveIndex = rayQuery.CommittedPrimitiveIndex();
        payload.barycentrics = rayQuery.CommittedTriangleBarycentrics();
        payload.committedRayT = rayQuery.CommittedRayT();
        payload.frontFace = rayQuery.CommittedTriangleFrontFace();
    }
#else
    TraceRay(SceneBVH, RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_FORCE_OPAQUE, instanceMask, 0, 0, 0, ray, payload);
#endif

    if (g_PerPassConstants.rayCountBufferIndex >= 0)
    {
        InterlockedAdd(u_RayCountBuffer[RAY_COUNT_TRACED(g_PerPassConstants.rayCountBufferIndex)], 1);
    }

    // The ray missed. Set hit distance to a large value and exit early.
    if (payload.instanceID == ~0u)
    {
        float3 environmentRadiance = GetEnvironmentRadiance(ray.Direction);

        DDGIStoreProbeRayMiss(rayDataTexture, rayStoragePosition, DDGIVolume, environmentRadiance);
        return;
    }

    if (g_PerPassConstants.rayCountBufferIndex >= 0)
    {
        InterlockedAdd(u_RayCountBuffer[RAY_COUNT_HITS(g_PerPassConstants.rayCountBufferIndex)], 1);
    }

    // Hit a surface backface.
    if (!payload.frontFace)
    {
        // Make hit distance negative to mark a backface hit for blending, probe relocation, and probe classification.
        DDGIStoreProbeRayBackfaceHit(rayDataTexture, rayStoragePosition, DDGIVolume, payload.committedRayT);
        return;
    }

    // Early out: a "fixed" ray hit a front facing surface. Fixed rays are not blended since their direction
    // is not random and they would bias the irradiance estimate. Don't perform lighting for these rays.
    if (DDGIVolume.probeClassificationEnabled && rayIndex < RTXGI_DDGI_NUM_FIXED_RAYS)
    {
        // Store the ray front face hit distance (only)
        DDGIStoreProbeRayFrontfaceHit(rayDataTexture, rayStoragePosition, DDGIVolume, payload.committedRayT);
        return;
    }

    GeometrySample gs = getGeometryFromHit(
        payload.instanceID,
        payload.geometryIndex,
        payload.primitiveIndex,
        payload.barycentrics,
        GeomAttr_Normal | GeomAttr_TexCoord | GeomAttr_Position,
        t_InstanceData, t_GeometryData, t_MaterialConstants);
    
    MaterialSample ms = sampleGeometryMaterial(gs, 0, 0, 0,
        MatAttr_BaseColor | MatAttr_MetalRough, s_MaterialSampler);


    RAB_Surface secondarySurface;
    secondarySurface.worldPos = ray.Origin + ray.Direction * payload.committedRayT;
    secondarySurface.viewDepth = 1.0; // don't care
    secondarySurface.normal = (dot(gs.geometryNormal, ray.Direction) < 0) ? gs.geometryNormal : -gs.geometryNormal;
    secondarySurface.geoNormal = secondarySurface.normal;
    secondarySurface.diffuseAlbedo = ms.diffuseAlbedo;
    secondarySurface.specularF0 = ms.specularF0;
    secondarySurface.roughness = ms.roughness;
    secondarySurface.viewPoint = ray.Origin;

    const RTXDI_ResamplingRuntimeParameters params = g_Const.runtimeParams;

    RAB_LightSample lightSample = (RAB_LightSample)0;
    RTXDI_Reservoir reservoir = RTXDI_SampleLightsForSurface(rng, rng, secondarySurface,
        g_Const.numIndirectRegirSamples, 
        g_Const.numIndirectLocalLightSamples, 
        g_Const.numIndirectInfiniteLightSamples, 
        g_Const.numIndirectEnvironmentSamples,
        params, u_RisBuffer, lightSample);

    float lightSampleScale = (lightSample.solidAnglePdf > 0) ? RTXDI_GetReservoirInvPdf(reservoir) / lightSample.solidAnglePdf : 0;

    // Firefly suppression
    float indirectLuminance = calcLuminance(lightSample.radiance) * lightSampleScale;
    if(indirectLuminance > c_MaxIndirectRadiance)
        lightSampleScale *= c_MaxIndirectRadiance / indirectLuminance;

    float3 indirectDiffuse = 0;
    float3 indirectSpecular = 0;
    float lightDistance = 0;
    ShadeSurfaceWithLightSample(reservoir, secondarySurface, lightSample, /* previousFrameTLAS = */ false,
        /* enableVisibilityReuse = */ false, indirectDiffuse, indirectSpecular, lightDistance);
    
    float3 directLighting = indirectDiffuse * ms.diffuseAlbedo + indirectSpecular;



    // Indirect Lighting (recursive)
    float3 irradiance = 0.f;
    float3 surfaceBias = DDGIGetSurfaceBias(gs.geometryNormal, ray.Direction, DDGIVolume);

    float3 worldPosition = ray.Origin + ray.Direction * payload.committedRayT;

    // Compute volume blending weight
    float volumeBlendWeight = DDGIGetVolumeBlendWeight(worldPosition, DDGIVolume);

    // Avoid evaluating irradiance when the surface is outside the volume
    if (volumeBlendWeight > 0)
    {
        // Get irradiance from the DDGIVolume
        irradiance = DDGIGetVolumeIrradiance(
            worldPosition,
            surfaceBias,
            gs.geometryNormal,
            DDGIVolume,
            resources);

        // Attenuate irradiance by the blend weight
        irradiance *= volumeBlendWeight;
    }

    // Perfectly diffuse reflectors don't exist in the real world. Limit the BRDF
    // albedo to a maximum value to account for the energy loss at each bounce.
    float maxAlbedo = 0.9f;

    // Store the final ray radiance and hit distance
    float3 radiance = directLighting + ((min(ms.diffuseAlbedo, maxAlbedo) / M_PI) * irradiance);
    DDGIStoreProbeRayFrontfaceHit(rayDataTexture, rayStoragePosition, DDGIVolume, radiance, payload.committedRayT);
}
