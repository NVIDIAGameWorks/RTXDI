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

#include "RtxdiApplicationBridge.hlsli"

#include <rtxdi/ResamplingFunctions.hlsli>

#ifdef WITH_NRD
#define NRD_HEADER_ONLY
#include <NRD.hlsli>
#endif

#include "ShadingHelpers.hlsli"

static const float c_MaxIndirectRadiance = 10;

#if USE_RAY_QUERY
[numthreads(RTXDI_SCREEN_SPACE_GROUP_SIZE, RTXDI_SCREEN_SPACE_GROUP_SIZE, 1)]
void main(uint2 GlobalIndex : SV_DispatchThreadID)
#else
[shader("raygeneration")]
void RayGen()
#endif
{
#if !USE_RAY_QUERY
    uint2 GlobalIndex = DispatchRaysIndex().xy;
#endif
    uint2 pixelPosition = RTXDI_ReservoirToPixelPos(GlobalIndex, g_Const.runtimeParams);

    RAB_Surface surface = RAB_GetGBufferSurface(pixelPosition, false);

    if (!RAB_IsSurfaceValid(surface))
        return;

    RAB_RandomSamplerState rng = RAB_InitRandomSampler(GlobalIndex, 5);
    
    float3 tangent, bitangent;
    branchlessONB(surface.normal, tangent, bitangent);

    float distance = max(1, 0.1 * length(surface.worldPos - g_Const.view.cameraDirectionOrPosition.xyz));

    RayDesc ray;
    ray.TMin = 0.001f * distance;
    ray.TMax = 1000;

    float2 Rand;
    Rand.x = RAB_GetNextRandom(rng);
    Rand.y = RAB_GetNextRandom(rng);

    float3 V = normalize(g_Const.view.cameraDirectionOrPosition.xyz - surface.worldPos);

    float3 specularDirection;
    float3 specular_BRDF_over_PDF;
    {
        float3 Ve = float3(dot(V, tangent), dot(V, bitangent), dot(V, surface.normal));
        float3 He = sampleGGX_VNDF(Ve, surface.roughness, Rand);
        float3 H = normalize(He.x * tangent + He.y * bitangent + He.z * surface.normal);
        specularDirection = reflect(-V, H);

        float HoV = saturate(dot(H, V));
        float NoV = saturate(dot(surface.normal, V));
        float3 F = Schlick_Fresnel(surface.specularF0, HoV);
        float G1 = (NoV > 0) ? G1_Smith(surface.roughness, NoV) : 0;
        specular_BRDF_over_PDF = F * G1;
    }

    float3 diffuseDirection;
    float diffuse_BRDF_over_PDF;
    {
        float solidAnglePdf;
        float3 localDirection = sampleCosHemisphere(Rand, solidAnglePdf);
        diffuseDirection = tangent * localDirection.x + bitangent * localDirection.y + surface.normal * localDirection.z;
        diffuse_BRDF_over_PDF = 1.0;
    }

    float specular_PDF = saturate(calcLuminance(specular_BRDF_over_PDF) /
        calcLuminance(specular_BRDF_over_PDF + diffuse_BRDF_over_PDF * surface.diffuseAlbedo));

    bool isSpecularRay = RAB_GetNextRandom(rng) < specular_PDF;

    float3 BRDF_over_PDF;
    if (isSpecularRay)
    {
        ray.Direction = specularDirection;
        BRDF_over_PDF = specular_BRDF_over_PDF / specular_PDF;
    }
    else
    {
        ray.Direction = diffuseDirection;
        BRDF_over_PDF = diffuse_BRDF_over_PDF / (1.0 - specular_PDF);
    }

    if (dot(surface.geoNormal, ray.Direction) <= 0.0)
    {
        BRDF_over_PDF = 0.0;
        ray.TMax = 0;
    }

    ray.Origin = surface.worldPos;

    float3 radiance = 0;
    
    RayPayload payload = (RayPayload)0;
    payload.instanceID = ~0u;
    payload.throughput = 1.0;

    uint instanceMask = INSTANCE_MASK_OPAQUE;
    
    if (g_Const.enableAlphaTestedGeometry)
        instanceMask |= INSTANCE_MASK_ALPHA_TESTED;

    if (g_Const.enableTransparentGeometry)
        instanceMask |= INSTANCE_MASK_TRANSPARENT;

#if USE_RAY_QUERY
    RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> rayQuery;
    
    rayQuery.TraceRayInline(SceneBVH, RAY_FLAG_NONE, instanceMask, ray);

    while (rayQuery.Proceed())
    {
        if (rayQuery.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            if (considerTransparentMaterial(
                rayQuery.CandidateInstanceID(),
                rayQuery.CandidateGeometryIndex(),
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
        payload.geometryIndex = rayQuery.CommittedGeometryIndex();
        payload.primitiveIndex = rayQuery.CommittedPrimitiveIndex();
        payload.barycentrics = rayQuery.CommittedTriangleBarycentrics();
        payload.committedRayT = rayQuery.CommittedRayT();
    }
#else
    TraceRay(SceneBVH, RAY_FLAG_NONE, instanceMask, 0, 0, 0, ray, payload);
#endif

    if (g_PerPassConstants.rayCountBufferIndex >= 0)
    {
        InterlockedAdd(u_RayCountBuffer[RAY_COUNT_TRACED(g_PerPassConstants.rayCountBufferIndex)], 1);
    }

    const RTXDI_ResamplingRuntimeParameters params = g_Const.runtimeParams;
    uint gbufferIndex = RTXDI_ReservoirPositionToPointer(params, GlobalIndex, 0);
    bool secondarySurfaceStored = false;

    if (payload.instanceID != ~0u)
    {
        if (g_PerPassConstants.rayCountBufferIndex >= 0)
        {
            InterlockedAdd(u_RayCountBuffer[RAY_COUNT_HITS(g_PerPassConstants.rayCountBufferIndex)], 1);
        }

        GeometrySample gs = getGeometryFromHit(
            payload.instanceID,
            payload.geometryIndex,
            payload.primitiveIndex,
            payload.barycentrics,
            GeomAttr_Normal | GeomAttr_TexCoord | GeomAttr_Position,
            t_InstanceData, t_GeometryData, t_MaterialConstants);
        
        MaterialSample ms = sampleGeometryMaterial(gs, 0, 0, 0,
            MatAttr_BaseColor | MatAttr_Emissive | MatAttr_MetalRough, s_MaterialSampler);

        radiance += ms.emissiveColor;

        RAB_Surface secondarySurface;
        secondarySurface.worldPos = ray.Origin + ray.Direction * payload.committedRayT;
        secondarySurface.viewDepth = 1.0; // don't care
        secondarySurface.normal = (dot(gs.geometryNormal, ray.Direction) < 0) ? gs.geometryNormal : -gs.geometryNormal;
        secondarySurface.geoNormal = secondarySurface.normal;
        secondarySurface.diffuseAlbedo = ms.diffuseAlbedo;
        secondarySurface.specularF0 = ms.specularF0;
        secondarySurface.roughness = ms.roughness;
        secondarySurface.diffuseProbability = getSurfaceDiffuseProbability(secondarySurface);

        if (g_Const.enableBrdfIndirect)
        {
            SecondarySurface secondarySurface;
            secondarySurface.worldPos = ray.Origin + ray.Direction * payload.committedRayT;
            secondarySurface.normal = ndirToOctUnorm32((dot(gs.geometryNormal, ray.Direction) < 0) ? gs.geometryNormal : -gs.geometryNormal);
            secondarySurface.throughput = Pack_R16G16B16A16_FLOAT(float4(payload.throughput * BRDF_over_PDF, isSpecularRay));
            secondarySurface.diffuseAlbedo = Pack_R11G11B10_UFLOAT(ms.diffuseAlbedo);
            secondarySurface.specularAndRoughness = Pack_R8G8B8A8_Gamma_UFLOAT(float4(ms.specularF0, ms.roughness));
        
            u_SecondaryGBuffer[gbufferIndex] = secondarySurface;
            secondarySurfaceStored = true;
        }
    }
    else if (g_Const.enableEnvironmentMap && isSpecularRay && calcLuminance(BRDF_over_PDF) > 0.0)
    {
        float3 environmentRadiance = GetEnvironmentRadiance(ray.Direction);

        radiance += environmentRadiance;
    }

    if (g_Const.enableBrdfIndirect && !secondarySurfaceStored)
    {
        SecondarySurface secondarySurface = (SecondarySurface)0;
        u_SecondaryGBuffer[gbufferIndex] = secondarySurface;
    }


    radiance *= payload.throughput;

    float3 diffuse = isSpecularRay ? 0.0 : radiance * BRDF_over_PDF;
    float3 specular = isSpecularRay ? radiance * BRDF_over_PDF : 0.0;
    float diffuseHitT = payload.committedRayT;
    float specularHitT = payload.committedRayT;

    specular = DemodulateSpecular(surface.specularF0, specular);


    StoreShadingOutput(GlobalIndex, pixelPosition, 
        surface.viewDepth, surface.roughness, diffuse, specular, payload.committedRayT, !g_Const.enableBrdfAdditiveBlend, !g_Const.enableBrdfIndirect);
}
