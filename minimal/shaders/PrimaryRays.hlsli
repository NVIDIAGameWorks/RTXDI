/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

struct PrimarySurfaceOutput
{
    RAB_Surface surface;
    float3 motionVector;
    float3 emissiveColor;
};

PrimarySurfaceOutput TracePrimaryRay(int2 pixelPosition)
{
    RayDesc ray = setupPrimaryRay(pixelPosition, g_Const.view);
    
    uint instanceMask = INSTANCE_MASK_OPAQUE;
    uint rayFlags = RAY_FLAG_CULL_NON_OPAQUE;
    
    RayQuery<RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> rayQuery;

    rayQuery.TraceRayInline(SceneBVH, rayFlags, instanceMask, ray);

    rayQuery.Proceed();


    PrimarySurfaceOutput result;
    result.surface = RAB_EmptySurface();
    result.motionVector = 0;
    result.emissiveColor = 0;

    if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        GeometrySample gs = getGeometryFromHit(
            rayQuery.CommittedInstanceID(),
            rayQuery.CommittedGeometryIndex(),
            rayQuery.CommittedPrimitiveIndex(),
            rayQuery.CommittedTriangleBarycentrics(), 
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
            s_MaterialSampler, 1.0);

        ms.shadingNormal = getBentNormal(gs.flatNormal, ms.shadingNormal, ray.Direction);

        result.motionVector = getMotionVector(g_Const.view, g_Const.prevView, 
            gs.instance, gs.objectSpacePosition, gs.prevObjectSpacePosition, result.surface.viewDepth);
        
        result.surface.worldPos = mul(gs.instance.transform, float4(gs.objectSpacePosition, 1.0)).xyz;
        result.surface.normal = ms.shadingNormal;
        result.surface.geoNormal = gs.flatNormal;
        result.surface.diffuseAlbedo = ms.diffuseAlbedo;
        result.surface.specularF0 = ms.specularF0;
        result.surface.roughness = ms.roughness;
        result.surface.viewDir = -ray.Direction;
        result.surface.diffuseProbability = getSurfaceDiffuseProbability(result.surface);
        result.emissiveColor = ms.emissiveColor;
    }

    return result;
}
