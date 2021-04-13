/***************************************************************************
 # Copyright (c) 2021, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

RayDesc setupPrimaryRay(uint2 pixelPosition, PlanarViewConstants view)
{
    float2 uv = (float2(pixelPosition) + 0.5) * view.viewportSizeInv;
    float4 clipPos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.5, 1);
    float4 worldPos = mul(clipPos, view.matClipToWorld);
    worldPos.xyz /= worldPos.w;

    RayDesc ray;
    ray.Origin = view.cameraDirectionOrPosition.xyz;
    ray.Direction = normalize(worldPos.xyz - ray.Origin);
    ray.TMin = 0;
    ray.TMax = 1000;
    return ray;
}

float3 getMotionVector(
    PlanarViewConstants view,
    PlanarViewConstants viewPrev,
    InstanceData instance,
    float3 objectSpacePosition,
    out float o_viewDepth)
{
    float3 worldSpacePosition = mul(instance.transform, float4(objectSpacePosition, 1.0)).xyz;
    float3 prevWorldSpacePosition = mul(instance.prevTransform, float4(objectSpacePosition, 1.0)).xyz;

    float4 clipPos = mul(float4(worldSpacePosition, 1.0), view.matWorldToClip);
    clipPos.xyz /= clipPos.w;
    float4 prevClipPos = mul(float4(prevWorldSpacePosition, 1.0), viewPrev.matWorldToClip);
    prevClipPos.xyz /= prevClipPos.w;

    o_viewDepth = clipPos.w;

    if (clipPos.w <= 0 || prevClipPos.w <= 0)
        return 0;

    float2 windowPos = clipPos.xy * view.clipToWindowScale + view.clipToWindowBias;
    float2 prevWindowPos = prevClipPos.xy * viewPrev.clipToWindowScale + viewPrev.clipToWindowBias;
    float3 motion;
    motion.xy = prevWindowPos.xy - windowPos.xy + (view.pixelOffset - viewPrev.pixelOffset);
    motion.z = prevClipPos.w - clipPos.w;
    return motion;
}

float2 getEnvironmentMotionVector(
    PlanarViewConstants view,
    PlanarViewConstants viewPrev,
    float2 windowPos)
{
    float4 clipPos;
    clipPos.xy = windowPos * view.windowToClipScale + view.windowToClipBias;
    clipPos.z = 0;
    clipPos.w = 1;

    float4 worldPos = mul(clipPos, view.matClipToWorld);
    float4 prevClipPos = mul(worldPos, viewPrev.matWorldToClip);

    prevClipPos.xyz /= prevClipPos.w;

    float2 prevWindowPos = prevClipPos.xy * viewPrev.clipToWindowScale + viewPrev.clipToWindowBias;

    float2 motion = prevWindowPos.xy - windowPos.xy;
    motion += view.pixelOffset - viewPrev.pixelOffset;

    return motion;
}

// Smart bent normal for ray tracing
// See appendix A.3 in https://arxiv.org/pdf/1705.01263.pdf
float3 getBentNormal(float3 geometryNormal, float3 shadingNormal, float3 viewDirection)
{
    // Flip the normal in case we're looking at the geometry from its back side
    if (dot(geometryNormal, viewDirection) > 0)
    {
        geometryNormal = -geometryNormal;
        shadingNormal = -shadingNormal;
    }

    // Specular reflection in shading normal
    float3 R = reflect(viewDirection, shadingNormal);
    float a = dot(geometryNormal, R);
    if (a < 0) // Perturb normal
    {
        float b = max(0.001, dot(shadingNormal, geometryNormal));
        return normalize(-viewDirection + normalize(R - shadingNormal * a / b));
    }

    return shadingNormal;
}

float3 computeRayIntersectionBarycentrics(float3 vertices[3], float3 rayOrigin, float3 rayDirection)
{
    float3 edge1 = vertices[1] - vertices[0];
    float3 edge2 = vertices[2] - vertices[0];

    float3 pvec = cross(rayDirection, edge2);

    float det = dot(edge1, pvec);
    float inv_det = 1.0f / det;

    float3 tvec = rayOrigin - vertices[0];

    float alpha = dot(tvec, pvec) * inv_det;

    float3 qvec = cross(tvec, edge1);

    float beta = dot(rayDirection, qvec) * inv_det;

    return float3(1.f - alpha - beta, alpha, beta);
}
