/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#ifndef RAB_VISIBILITY_TEST_HLSLI
#define RAB_VISIBILITY_TEST_HLSLI

#include "../../SceneGeometry.hlsli"
#include "RAB_LightSample.hlsli"

RayDesc setupVisibilityRay(RAB_Surface surface, float3 samplePosition, float offset = 0.001)
{
    float3 L = samplePosition - surface.worldPos;

    RayDesc ray;
    ray.TMin = offset;
    ray.TMax = max(offset, length(L) - offset * 2);
    ray.Direction = normalize(L);
    ray.Origin = surface.worldPos;

    return ray;
}

bool GetConservativeVisibility(RaytracingAccelerationStructure accelStruct, RAB_Surface surface, float3 samplePosition)
{
    RayDesc ray = setupVisibilityRay(surface, samplePosition);

#if USE_RAY_QUERY
    RayQuery<RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> rayQuery;

    rayQuery.TraceRayInline(accelStruct, RAY_FLAG_NONE, INSTANCE_MASK_OPAQUE, ray);

    rayQuery.Proceed();

    bool visible = (rayQuery.CommittedStatus() == COMMITTED_NOTHING);
#else
    RAB_RayPayload payload = (RAB_RayPayload)0;
    payload.instanceID = ~0u;
    payload.throughput = 1.0;

    TraceRay(accelStruct, RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, INSTANCE_MASK_OPAQUE, 0, 0, 0, ray, payload);

    bool visible = (payload.instanceID == ~0u);
#endif

    REPORT_RAY(!visible);

    return visible;
}

// Traces a cheap visibility ray that returns approximate, conservative visibility
// between the surface and the light sample. Conservative means if unsure, assume the light is visible.
// Significant differences between this conservative visibility and the final one will result in more noise.
// This function is used in the spatial resampling functions for ray traced bias correction.
bool RAB_GetConservativeVisibility(RAB_Surface surface, RAB_LightSample lightSample)
{
    return GetConservativeVisibility(SceneBVH, surface, lightSample.position);
}

// Same as RAB_GetConservativeVisibility but for temporal resampling.
// When the previous frame TLAS and BLAS are available, the implementation should use the previous position and the previous AS.
// When they are not available, use the current AS. That will result in transient bias.
bool RAB_GetTemporalConservativeVisibility(RAB_Surface curSurface, RAB_Surface prevSurface, RAB_LightSample lightSample)
{
    if (g_Const.enablePrevTLAS)
        return GetConservativeVisibility(PrevSceneBVH, prevSurface, lightSample.position);
    else
        return GetConservativeVisibility(SceneBVH, curSurface, lightSample.position);
}

// Traces an expensive visibility ray that considers all alpha tested  and transparent geometry along the way.
// Only used for final shading.
// Not a required bridge function.
float3 GetFinalVisibility(RaytracingAccelerationStructure accelStruct, RAB_Surface surface, float3 samplePosition)
{
    RayDesc ray = setupVisibilityRay(surface, samplePosition, 0.01);

    uint instanceMask = INSTANCE_MASK_OPAQUE;
    uint rayFlags = RAY_FLAG_NONE;
    
    if (g_Const.sceneConstants.enableAlphaTestedGeometry)
        instanceMask |= INSTANCE_MASK_ALPHA_TESTED;

    if (g_Const.sceneConstants.enableTransparentGeometry)
        instanceMask |= INSTANCE_MASK_TRANSPARENT;

    if (!g_Const.sceneConstants.enableTransparentGeometry && !g_Const.sceneConstants.enableAlphaTestedGeometry)
        rayFlags |= RAY_FLAG_CULL_NON_OPAQUE;

    RAB_RayPayload payload = (RAB_RayPayload)0;
    payload.instanceID = ~0u;
    payload.throughput = 1.0;

#if USE_RAY_QUERY
    RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> rayQuery;

    rayQuery.TraceRayInline(accelStruct, rayFlags, instanceMask, ray);

    while (rayQuery.Proceed())
    {
        if (rayQuery.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            if (ConsiderTransparentMaterial(
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
        payload.primitiveIndex = rayQuery.CommittedPrimitiveIndex();
        payload.geometryIndex = rayQuery.CommittedGeometryIndex();
        payload.barycentrics = rayQuery.CommittedTriangleBarycentrics();
        payload.committedRayT = rayQuery.CommittedRayT();
        payload.frontFace = rayQuery.CommittedTriangleFrontFace();
    }
#else
    TraceRay(accelStruct, rayFlags, instanceMask, 0, 0, 0, ray, payload);
#endif

    REPORT_RAY(payload.instanceID != ~0u);

    if(payload.instanceID == ~0u)
        return payload.throughput.rgb;
    else
        return 0;
}

// Traces a cheap visibility ray that returns approximate, conservative visibility
// between the surface and the light sample. Conservative means if unsure, assume the light is visible.
// Significant differences between this conservative visibility and the final one will result in more noise.
// This function is used in the spatial resampling functions for ray traced bias correction.
bool RAB_GetConservativeVisibility(RAB_Surface surface, float3 samplePosition)
{
    return GetConservativeVisibility(SceneBVH, surface, samplePosition);
}

// Same as RAB_GetConservativeVisibility but for temporal resampling.
// When the previous frame TLAS and BLAS are available, the implementation should use the previous position and the previous AS.
// When they are not available, use the current AS. That will result in transient bias.
bool RAB_GetTemporalConservativeVisibility(RAB_Surface curSurface, RAB_Surface prevSurface, float3 samplePosition)
{
    if (g_Const.enablePrevTLAS)
        return GetConservativeVisibility(PrevSceneBVH, prevSurface, samplePosition);
    else
        return GetConservativeVisibility(SceneBVH, curSurface, samplePosition);
}

#endif // RAB_VISIBILITY_TEST_HLSLI