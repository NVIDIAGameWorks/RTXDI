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

#include "ShaderParameters.h"
#include "SceneGeometry.hlsli"
#include "GBufferHelpers.hlsli"

ConstantBuffer<GBufferConstants> g_Const : register(b0);
VK_PUSH_CONSTANT ConstantBuffer<PerPassConstants> g_PerPassConstants : register(b1);

RWTexture2D<float> u_Depth : register(u0);
RWTexture2D<uint> u_BaseColor : register(u1);
RWTexture2D<uint> u_MetalRough : register(u2);
RWTexture2D<uint> u_Normals : register(u3);
RWTexture2D<uint> u_GeoNormals : register(u4);
RWTexture2D<float4> u_Emissive : register(u5);
RWTexture2D<float4> u_MotionVectors : register(u6);
RWTexture2D<float4> u_NormalRoughness : register(u7);
RWBuffer<uint> u_RayCountBuffer : register(u8);

RaytracingAccelerationStructure SceneBVH : register(t0);
StructuredBuffer<InstanceData> t_InstanceData : register(t1);
StructuredBuffer<GeometryData> t_GeometryData : register(t2);
StructuredBuffer<MaterialConstants> t_MaterialConstants : register(t3);

SamplerState s_MaterialSampler : register(s0);


void shadeSurface(
    uint2 pixelPosition, 
    uint instanceIndex, 
    uint primitiveIndex, 
    float2 rayBarycentrics, 
    float3 viewDirection, 
    float maxGlassHitT)
{
    GeometrySample gs = getGeometryFromHit(instanceIndex, primitiveIndex, rayBarycentrics, 
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

    MaterialSample ms = sampleGeometryMaterial(gs, texGrad_x, texGrad_y, -1, MatAttr_All, 
        s_MaterialSampler, g_Const.normalMapScale);

    ms.shadingNormal = getBentNormal(gs.flatNormal, ms.shadingNormal, viewDirection);

    if (g_Const.roughnessOverride >= 0)
        ms.roughness = g_Const.roughnessOverride;

    if (g_Const.metalnessOverride >= 0)
        ms.metalness = g_Const.metalnessOverride;

    float viewDepth = 0;
    float3 motion = getMotionVector(g_Const.view, g_Const.viewPrev, 
        gs.instance, gs.objectSpacePosition, viewDepth);

    u_Depth[pixelPosition] = viewDepth;
    u_BaseColor[pixelPosition] = Pack_R8G8B8_UFLOAT(ms.baseColor);
    u_MetalRough[pixelPosition] = Pack_R16G16_UFLOAT(float2(ms.metalness, ms.roughness));
    u_Normals[pixelPosition] = ndirToOctUnorm32(ms.shadingNormal);
    u_GeoNormals[pixelPosition] = ndirToOctUnorm32(gs.flatNormal);
    u_Emissive[pixelPosition] = float4(ms.emissive, maxGlassHitT);
    u_MotionVectors[pixelPosition] = float4(motion, 0);
    u_NormalRoughness[pixelPosition] = float4(ms.shadingNormal * 0.5 + 0.5, ms.roughness);

    if (all(g_Const.materialReadbackPosition == int2(pixelPosition)))
    {
        u_RayCountBuffer[g_Const.materialReadbackBufferIndex] = gs.geometry.materialIndex;
    }
}

static const int MaterialType_Glass = 1234; // Some value outside of the original enum

int evaluateNonOpaqueMaterials(uint instanceID, uint primitiveIndex, float2 rayBarycentrics)
{
    GeometrySample gs = getGeometryFromHit(instanceID, primitiveIndex, rayBarycentrics, 
        GeomAttr_TexCoord, t_InstanceData, t_GeometryData, t_MaterialConstants);
    
    int materialType = (gs.material.flags & MaterialFlags_MaterialType_Mask) >> MaterialFlags_MaterialType_Shift;

    if (materialType == MaterialType_Opaque)
        return MaterialType_Opaque;

    if (gs.material.diffuseTextureIndex < 0 && materialType == MaterialType_AlphaTested)
        return MaterialType_Opaque;

    MaterialSample ms = sampleGeometryMaterial(gs, 0, 0, 0, MatAttr_BaseColor, 
        s_MaterialSampler, g_Const.normalMapScale);

    if (ms.roughness <= 0.1 && materialType == MaterialType_Transparent)
        return MaterialType_Glass;
    
    return (ms.opacity > 0.5) ? MaterialType_Opaque : MaterialType_Transparent;
}

struct RayPayload
{
    float minGlassRayT;
    float committedRayT;
    uint instanceID;
    uint primitiveIndex;
    float2 barycentrics;
};

bool anyHitLogic(inout RayPayload payload, uint instanceID, uint primitiveIndex, float2 rayBarycentrics, float rayT)
{
    int evaluatedMaterialType = evaluateNonOpaqueMaterials(instanceID, primitiveIndex, rayBarycentrics);

    if (evaluatedMaterialType == MaterialType_Glass)
    {
        payload.minGlassRayT = min(payload.minGlassRayT, rayT);
    }
    else if(evaluatedMaterialType == MaterialType_Opaque)
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
    payload.primitiveIndex = PrimitiveIndex();
    payload.barycentrics = attrib.uv;
}

[shader("anyhit")]
void AnyHit(inout RayPayload payload : SV_RayPayload, in Attributes attrib : SV_IntersectionAttributes)
{
    if (!anyHitLogic(payload, InstanceID(), PrimitiveIndex(), attrib.uv, RayTCurrent()))
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
            payload.primitiveIndex,
            payload.barycentrics,
            ray.Direction,
            maxGlassHitT);

        return;
    }

    u_Depth[pixelPosition] = 0;
    u_BaseColor[pixelPosition] = 0;
    u_MetalRough[pixelPosition] = 0;
    u_Normals[pixelPosition] = 0;
    u_GeoNormals[pixelPosition] = 0;
    u_Emissive[pixelPosition] = float4(0, 0, 0, maxGlassHitT);
    u_MotionVectors[pixelPosition] = 0;
    u_NormalRoughness[pixelPosition] = 0;
}
