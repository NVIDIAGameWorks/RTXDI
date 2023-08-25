/***************************************************************************
 # Copyright (c) 2021-2023, NVIDIA CORPORATION.  All rights reserved.
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
    float4 clipPos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, (1.0 / 256.0), 1);
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
    float3 prevObjectSpacePosition,
    out float o_viewDepth)
{
    float3 worldSpacePosition = mul(instance.transform, float4(objectSpacePosition, 1.0)).xyz;
    float3 prevWorldSpacePosition = mul(instance.prevTransform, float4(prevObjectSpacePosition, 1.0)).xyz;

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
    motion.xy = prevWindowPos.xy - windowPos.xy;
    motion.xy += (view.pixelOffset - viewPrev.pixelOffset);
    motion.z = prevClipPos.w - clipPos.w;
    return motion;
}

float3 viewDepthToWorldPos(
    PlanarViewConstants view,
    int2 pixelPosition,
    float viewDepth)
{
    float2 uv = (float2(pixelPosition) + 0.5) * view.viewportSizeInv;
    float4 clipPos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.5, 1);
    float4 viewPos = mul(clipPos, view.matClipToView);
    viewPos.xy /= viewPos.z;
    viewPos.zw = 1.0;
    viewPos.xyz *= viewDepth;
    return mul(viewPos, view.matViewToWorld).xyz;
}
