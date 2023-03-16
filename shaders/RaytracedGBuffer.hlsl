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

#define ENABLE_METAL_ROUGH_RECONSTRUCTION 1

#include "ShaderParameters.h"
#include "SceneGeometry.hlsli"
#include "GBufferHelpers.hlsli"

ConstantBuffer<GBufferConstants> g_Const : register(b0);
VK_PUSH_CONSTANT ConstantBuffer<PerPassConstants> g_PerPassConstants : register(b1);

RWTexture2D<float> u_ViewDepth : register(u0);
RWTexture2D<uint> u_DiffuseAlbedo : register(u1);
RWTexture2D<uint> u_SpecularRough : register(u2);
RWTexture2D<uint> u_Normals : register(u3);
RWTexture2D<uint> u_GeoNormals : register(u4);
RWTexture2D<float4> u_Emissive : register(u5);
RWTexture2D<float4> u_MotionVectors : register(u6);
RWTexture2D<float> u_DeviceDepth : register(u7);
RWBuffer<uint> u_RayCountBuffer : register(u8);

RaytracingAccelerationStructure SceneBVH : register(t0);
StructuredBuffer<InstanceData> t_InstanceData : register(t1);
StructuredBuffer<GeometryData> t_GeometryData : register(t2);
StructuredBuffer<MaterialConstants> t_MaterialConstants : register(t3);

SamplerState s_MaterialSampler : register(s0);


void shadeSurface(
    uint2 pixelPosition, 
    uint instanceIndex,
    uint geometryIndex,
    uint primitiveIndex, 
    float2 rayBarycentrics, 
    float3 viewDirection, 
    float maxGlassHitT)
{
    GeometrySample gs = getGeometryFromHit(instanceIndex, geometryIndex, primitiveIndex, rayBarycentrics, 
        GeomAttr_All, t_InstanceData, t_GeometryData, t_MaterialConstants);
    
    RayDesc ray_0 = setupPrimaryRay(pixelPosition, g_Const.view);
    RayDesc ray_x = setupPrimaryRay(pixelPosition + uint2(1, 0), g_Const.view);
    RayDesc ray_y = setupPrimaryRay(pixelPosition + uint2(0, 1), g_Const.view);
    float3 worldSpacePositions[3];
    worldSpacePositions[0] = mul(gs.instance.transform, float4(gs.vertexPositions[0], 1.0)).xyz;
    worldSpacePositions[1] = mul(gs.instance.transform, float4(gs.vertexPositions[1], 1.0)).xyz;
    worldSpacePositions[2] = mul(gs.instance.transform, float4(gs.vertexPositions[2], 1.0)).xyz;
    float3 bary_0 = computeRayIntersectionBarycentrics(worldSpacePositions, ray_0.Origin, ray_0.Direction);
    float3 bary_x = computeRayIntersectionBarycentrics(worldSpacePositions, ray_x.Origin, ray_x.Direction);
    float3 bary_y = computeRayIntersectionBarycentrics(worldSpacePositions, ray_y.Origin, ray_y.Direction);
    float2 texcoord_0 = interpolate(gs.vertexTexcoords, bary_0);
    float2 texcoord_x = interpolate(gs.vertexTexcoords, bary_x);
    float2 texcoord_y = interpolate(gs.vertexTexcoords, bary_y);
    float2 texGrad_x = texcoord_x - texcoord_0;
    float2 texGrad_y = texcoord_y - texcoord_0;

    texGrad_x *= g_Const.textureGradientScale;
    texGrad_y *= g_Const.textureGradientScale;

    if (dot(gs.geometryNormal, viewDirection) > 0)
        gs.geometryNormal = -gs.geometryNormal;

    MaterialSample ms = sampleGeometryMaterial(gs, texGrad_x, texGrad_y, -1, MatAttr_All, 
        s_MaterialSampler, g_Const.normalMapScale);

    ms.shadingNormal = getBentNormal(gs.flatNormal, ms.shadingNormal, viewDirection);

    if (g_Const.roughnessOverride >= 0)
        ms.roughness = g_Const.roughnessOverride;

    if (g_Const.metalnessOverride >= 0)
    {
        ms.metalness = g_Const.metalnessOverride;
        getReflectivity(ms.metalness, ms.baseColor, ms.diffuseAlbedo, ms.specularF0);
    }

    float clipDepth = 0;
    float viewDepth = 0;
    float3 motion = getMotionVector(g_Const.view, g_Const.viewPrev, 
        gs.instance, gs.objectSpacePosition, gs.prevObjectSpacePosition, clipDepth, viewDepth);

    u_ViewDepth[pixelPosition] = viewDepth;
    u_DeviceDepth[pixelPosition] = clipDepth;
    u_DiffuseAlbedo[pixelPosition] = Pack_R11G11B10_UFLOAT(ms.diffuseAlbedo);
    u_SpecularRough[pixelPosition] = Pack_R8G8B8A8_Gamma_UFLOAT(float4(ms.specularF0, ms.roughness));
    u_Normals[pixelPosition] = ndirToOctUnorm32(ms.shadingNormal);
    u_GeoNormals[pixelPosition] = ndirToOctUnorm32(gs.flatNormal);
    u_Emissive[pixelPosition] = float4(ms.emissiveColor, maxGlassHitT);
    u_MotionVectors[pixelPosition] = float4(motion, 0);
    
    if (all(g_Const.materialReadbackPosition == int2(pixelPosition)))
    {
        u_RayCountBuffer[g_Const.materialReadbackBufferIndex] = gs.geometry.materialIndex + 1;
    }
}

int evaluateNonOpaqueMaterials(uint instanceID, uint geometryIndex, uint primitiveIndex, float2 rayBarycentrics)
{
    GeometrySample gs = getGeometryFromHit(instanceID, geometryIndex, primitiveIndex, rayBarycentrics, 
        GeomAttr_TexCoord, t_InstanceData, t_GeometryData, t_MaterialConstants);
    
    MaterialSample ms = sampleGeometryMaterial(gs, 0, 0, 0, MatAttr_BaseColor | MatAttr_Transmission, 
        s_MaterialSampler, g_Const.normalMapScale);

    bool alphaMask = ms.opacity >= gs.material.alphaCutoff;

    if (gs.material.domain == MaterialDomain_AlphaTested && alphaMask)
        return MaterialDomain_Opaque;

    if (gs.material.domain == MaterialDomain_AlphaBlended && ms.opacity >= 0.5)
        return MaterialDomain_Opaque; // no support for blending
    
    if (gs.material.domain == MaterialDomain_Transmissive ||
        (gs.material.domain == MaterialDomain_TransmissiveAlphaTested && alphaMask) ||
        gs.material.domain == MaterialDomain_TransmissiveAlphaBlended)
    {
        float throughput = ms.transmission;

        if ((gs.material.flags & MaterialFlags_UseSpecularGlossModel) == 0)
            throughput *= (1.0 - ms.metalness) * max(ms.baseColor.r, max(ms.baseColor.g, ms.baseColor.b));

        if (gs.material.domain == MaterialDomain_TransmissiveAlphaBlended)
            throughput *= (1.0 - ms.opacity);

        if (throughput == 0)
            return MaterialDomain_Opaque;
    }

    return gs.material.domain;
}

struct RayPayload
{
    float minGlassRayT;
    float committedRayT;
    uint instanceID;
    uint geometryIndex;
    uint primitiveIndex;
    float2 barycentrics;
};

bool anyHitLogic(inout RayPayload payload, uint instanceID, uint geometryIndex, uint primitiveIndex, float2 rayBarycentrics, float rayT)
{
    int evaluatedMaterialDomain = evaluateNonOpaqueMaterials(instanceID, geometryIndex, primitiveIndex, rayBarycentrics);

    if (evaluatedMaterialDomain == MaterialDomain_Transmissive || 
        evaluatedMaterialDomain == MaterialDomain_TransmissiveAlphaTested || 
        evaluatedMaterialDomain == MaterialDomain_TransmissiveAlphaBlended)
    {
        payload.minGlassRayT = min(payload.minGlassRayT, rayT);
    }
    else if(evaluatedMaterialDomain == MaterialDomain_Opaque)
    {
        return true;
    }

    return false;
}

#if !USE_RAY_QUERY
struct Attributes 
{
    float2 uv;
};

[shader("miss")]
void Miss(inout RayPayload payload : SV_RayPayload)
{
}

[shader("closesthit")]
void ClosestHit(inout RayPayload payload : SV_RayPayload, in Attributes attrib : SV_IntersectionAttributes)
{
    payload.committedRayT = RayTCurrent();
    payload.instanceID = InstanceID();
    payload.geometryIndex = GeometryIndex();
    payload.primitiveIndex = PrimitiveIndex();
    payload.barycentrics = attrib.uv;
}

[shader("anyhit")]
void AnyHit(inout RayPayload payload : SV_RayPayload, in Attributes attrib : SV_IntersectionAttributes)
{
    if (!anyHitLogic(payload, InstanceID(), GeometryIndex(), PrimitiveIndex(), attrib.uv, RayTCurrent()))
        IgnoreHit();
}
#endif

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

    if (any(float2(pixelPosition) >= g_Const.view.viewportSize))
        return;

    RayDesc ray = setupPrimaryRay(pixelPosition, g_Const.view);
    
    uint instanceMask = INSTANCE_MASK_OPAQUE;
    uint rayFlags = RAY_FLAG_NONE;
    
    if (g_Const.enableAlphaTestedGeometry)
        instanceMask |= INSTANCE_MASK_ALPHA_TESTED;

    if (g_Const.enableTransparentGeometry)
        instanceMask |= INSTANCE_MASK_TRANSPARENT;

    if (!g_Const.enableTransparentGeometry && !g_Const.enableAlphaTestedGeometry)
        rayFlags |= RAY_FLAG_CULL_NON_OPAQUE;

    RayPayload payload;
    payload.minGlassRayT = ray.TMax + 1.0;
    payload.committedRayT = 0;
    payload.instanceID = ~0u;
    payload.primitiveIndex = 0;
    payload.barycentrics = 0;

#if USE_RAY_QUERY
    RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> rayQuery;

    rayQuery.TraceRayInline(SceneBVH, rayFlags, instanceMask, ray);

    while (rayQuery.Proceed())
    {
        if (rayQuery.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            if (anyHitLogic(payload, 
                rayQuery.CandidateInstanceID(),
                rayQuery.CandidateGeometryIndex(),
                rayQuery.CandidatePrimitiveIndex(),
                rayQuery.CandidateTriangleBarycentrics(),
                rayQuery.CandidateTriangleRayT()))
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
    TraceRay(SceneBVH, rayFlags, instanceMask, 0, 0, 0, ray, payload);
#endif

    REPORT_RAY(payload.instanceID != ~0u);

    const float hitT = payload.committedRayT;
    const bool hasGlass = payload.minGlassRayT < hitT;
    const float maxGlassHitT = hasGlass ? hitT : 0;

    if (payload.instanceID != ~0u)
    {
        shadeSurface(
            pixelPosition,
            payload.instanceID,
            payload.geometryIndex,
            payload.primitiveIndex,
            payload.barycentrics,
            ray.Direction,
            maxGlassHitT);

        return;
    }

    u_ViewDepth[pixelPosition] = BACKGROUND_DEPTH;
    u_DeviceDepth[pixelPosition] = 0;
    u_DiffuseAlbedo[pixelPosition] = 0;
    u_SpecularRough[pixelPosition] = 0;
    u_Normals[pixelPosition] = 0;
    u_GeoNormals[pixelPosition] = 0;
    u_Emissive[pixelPosition] = float4(0, 0, 0, maxGlassHitT);
    u_MotionVectors[pixelPosition] = 0;
}
