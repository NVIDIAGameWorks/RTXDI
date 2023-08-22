/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#pragma pack_matrix(row_major)

#include <donut/shaders/brdf.hlsli>
#include "ShaderParameters.h"
#include "SceneGeometry.hlsli"

ConstantBuffer<GlassConstants> g_Const : register(b0);
VK_PUSH_CONSTANT ConstantBuffer<PerPassConstants> g_PerPassConstants : register(b1);

RWTexture2D<float4> u_CompositedColor : register(u0);
RWBuffer<uint> u_RayCountBuffer : register(u1);

RaytracingAccelerationStructure SceneBVH : register(t0);
StructuredBuffer<InstanceData> t_InstanceData : register(t1);
StructuredBuffer<GeometryData> t_GeometryData : register(t2);
StructuredBuffer<MaterialConstants> t_MaterialConstants : register(t3);
Texture2D<float4> t_Emissive : register(t4);

SamplerState s_MaterialSampler : register(s0);
SamplerState s_EnvironmentSampler : register(s1);

RayDesc setupPrimaryRay(uint2 pixelPosition, PlanarViewConstants view, float TMax)
{
    float2 uv = (float2(pixelPosition) + 0.5) * view.viewportSizeInv;
    float4 clipPos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.5, 1);
    float4 worldPos = mul(clipPos, view.matClipToWorld);
    worldPos.xyz /= worldPos.w;

    RayDesc ray;
    ray.Origin = view.cameraDirectionOrPosition.xyz;
    ray.Direction = normalize(worldPos.xyz - ray.Origin);
    ray.TMin = 0;
    ray.TMax = TMax;
    return ray;
}

struct RayPayload
{
    float committedRayT;
    uint instanceID;
    uint geometryIndex;
    uint primitiveIndex;
    float2 barycentrics;
};

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
}
#endif

void tracePrimaryRay(inout RayPayload payload, RayDesc ray)
{
#if USE_RAY_QUERY
    RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES > rayQuery;

    rayQuery.TraceRayInline(SceneBVH, RAY_FLAG_NONE, INSTANCE_MASK_TRANSPARENT, ray);

    rayQuery.Proceed();

    if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        payload.instanceID = rayQuery.CommittedInstanceID();
        payload.primitiveIndex = rayQuery.CommittedPrimitiveIndex();
        payload.barycentrics = rayQuery.CommittedTriangleBarycentrics();
        payload.committedRayT = rayQuery.CommittedRayT();
    }
#else
    TraceRay(SceneBVH, RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES, INSTANCE_MASK_TRANSPARENT, 0, 0, 0, ray, payload);
#endif
    
    REPORT_RAY(payload.instanceID != ~0u);
}

float3 getSecondaryRadiance(float3 surfacePosition, float3 reflectedDirection)
{
    RayDesc ray;
    ray.Origin = surfacePosition;
    ray.Direction = reflectedDirection;
    ray.TMin = 0.001;
    ray.TMax = 1000;

    RayPayload payload = (RayPayload)0;
    payload.instanceID = ~0u;

#if USE_RAY_QUERY
    RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_FORCE_OPAQUE> rayQuery;

    rayQuery.TraceRayInline(SceneBVH, RAY_FLAG_NONE, INSTANCE_MASK_OPAQUE, ray);

    rayQuery.Proceed();

    if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        payload.instanceID = rayQuery.CommittedInstanceID();
        payload.primitiveIndex = rayQuery.CommittedPrimitiveIndex();
        payload.barycentrics = rayQuery.CommittedTriangleBarycentrics();
        payload.committedRayT = rayQuery.CommittedRayT();
    }
#else
    TraceRay(SceneBVH, RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_FORCE_OPAQUE, INSTANCE_MASK_OPAQUE, 0, 0, 0, ray, payload);
#endif

    REPORT_RAY(payload.instanceID != ~0u);

    if (payload.instanceID == ~0u)
    {
        if (g_Const.enableEnvironmentMap)
        {
            Texture2D environmentLatLongMap = t_BindlessTextures[g_Const.environmentMapTextureIndex];
            float2 uv = directionToEquirectUV(reflectedDirection);
            uv.x -= g_Const.environmentRotation;
            float3 environmentColor = environmentLatLongMap.SampleLevel(s_EnvironmentSampler, uv, 0).rgb;
            environmentColor *= g_Const.environmentScale;
            return environmentColor;
        }
        else
            return 0;
    }

    GeometrySample gs = getGeometryFromHit(
        payload.instanceID,
        payload.geometryIndex,
        payload.primitiveIndex,
        payload.barycentrics,
        GeomAttr_TexCoord | GeomAttr_Normal, 
        t_InstanceData, t_GeometryData, t_MaterialConstants);
    
    MaterialSample ms = sampleGeometryMaterial(gs, 0, 0, 0,
        MatAttr_BaseColor | MatAttr_Emissive, s_MaterialSampler);

    float3 ambient = float3(0.05, 0.045, 0.03); // very basic ambient, works OK for this sample

    return ms.emissiveColor + ms.diffuseAlbedo * ambient;
}

#if USE_RAY_QUERY
[numthreads(16, 16, 1)]
void main(uint2 pixelPosition : SV_DispatchThreadID)
#else
[shader("raygeneration")]
void RayGen()
#endif
{
#if !USE_RAY_QUERY
    uint2 pixelPosition = DispatchRaysIndex().xy;
#endif

    float maxGlassHitT = t_Emissive[pixelPosition].a;
    if(maxGlassHitT <= 0)
        return;

    float3 throughput = 1.0;
    float3 overlay = 0.0;

    RayDesc ray = setupPrimaryRay(pixelPosition, g_Const.view, maxGlassHitT + 0.01);

    for (uint surfaceIndex = 0; surfaceIndex < 8; surfaceIndex++)
    {
        RayPayload payload = (RayPayload)0;
        payload.instanceID = ~0u;

        tracePrimaryRay(payload, ray);

        if (payload.instanceID == ~0u)
            break;

        float3 surfacePosition = ray.Origin + ray.Direction * payload.committedRayT;

        ray.Origin = surfacePosition + ray.Direction * 0.001; // for the next ray
        ray.TMax -= payload.committedRayT;

        GeometrySample gs = getGeometryFromHit(
            payload.instanceID,
            payload.geometryIndex,
            payload.primitiveIndex,
            payload.barycentrics,
            GeomAttr_TexCoord | GeomAttr_Normal | GeomAttr_Tangents,
            t_InstanceData, t_GeometryData, t_MaterialConstants);

        MaterialSample ms = sampleGeometryMaterial(gs, 0, 0, 0, MatAttr_BaseColor | MatAttr_Normal | MatAttr_Transmission | MatAttr_Emissive,
            s_MaterialSampler, g_Const.normalMapScale);

        if (surfaceIndex == 0 && all(g_Const.materialReadbackPosition == int2(pixelPosition)))
        {
            u_RayCountBuffer[g_Const.materialReadbackBufferIndex] = gs.geometry.materialIndex + 1;
        }

        bool alphaMask = ms.opacity >= gs.material.alphaCutoff;

        if (gs.material.domain == MaterialDomain_Transmissive ||
            (gs.material.domain == MaterialDomain_TransmissiveAlphaTested && alphaMask) ||
            gs.material.domain == MaterialDomain_TransmissiveAlphaBlended)
        {
            float3 surfaceNormal = ms.shadingNormal;

            if (dot(surfaceNormal, ray.Direction) > 0)
                surfaceNormal = -surfaceNormal;

            // TODO: use the surface params provided by donut
            float3 F0 = lerp(0.04, ms.baseColor, ms.metalness);

            float3 fresnel = Schlick_Fresnel(F0, abs(dot(surfaceNormal, ray.Direction)));

            float3 reflectedDirection = reflect(ray.Direction, surfaceNormal);

            float3 secondaryRadiance = getSecondaryRadiance(surfacePosition, reflectedDirection);

            float3 contribution = secondaryRadiance * fresnel + ms.emissiveColor;

            float3 thisSurfaceThroughput = ms.transmission;

            if ((gs.material.flags & MaterialFlags_UseSpecularGlossModel) == 0)
                thisSurfaceThroughput *= (1.0 - ms.metalness) * ms.baseColor;

            if (gs.material.domain == MaterialDomain_TransmissiveAlphaBlended)
            {
                contribution *= ms.opacity;
                thisSurfaceThroughput *= (1.0 - ms.opacity);
            }

            thisSurfaceThroughput *= (1.0 - fresnel) * gs.material.baseOrDiffuseColor.rgb;

            if (all(thisSurfaceThroughput == 0)) // this is a transparent-turned-opaque surface, it should be in the G-buffer
                break;

            overlay += contribution * throughput;
            throughput *= thisSurfaceThroughput;

            if (calcLuminance(throughput) < 0.01)
                break;
        }
    }

    if (any(throughput < 1.0) || any(overlay > 0.0))
    {
        float4 previousColor = u_CompositedColor[pixelPosition];

        float3 newColor = previousColor.rgb * throughput + overlay;

        u_CompositedColor[pixelPosition] = float4(newColor.rgb, previousColor.a);
    }
}
