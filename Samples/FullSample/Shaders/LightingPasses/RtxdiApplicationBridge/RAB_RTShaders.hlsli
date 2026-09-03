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

#ifndef RAB_RT_SHADERS_HLSLI
#define RAB_RT_SHADERS_HLSLI

bool ConsiderTransparentMaterial(uint instanceIndex, uint geometryIndex, uint triangleIndex, float2 rayBarycentrics, inout float3 throughput)
{
    GeometrySample gs = getGeometryFromHit(
        instanceIndex,
        geometryIndex,
        triangleIndex,
        rayBarycentrics,
        GeomAttr_TexCoord,
        t_InstanceData, t_GeometryData, t_MaterialConstants);
    
    MaterialSample ms = sampleGeometryMaterial(gs, 0, 0, 0,
        MatAttr_BaseColor | MatAttr_Transmission, s_MaterialSampler);

    bool alphaMask = ms.opacity >= gs.material.alphaCutoff;

    if (gs.material.domain == MaterialDomain_AlphaTested)
        return alphaMask;

    if (gs.material.domain == MaterialDomain_AlphaBlended)
    {
        throughput *= (1.0 - ms.opacity);
        return false;
    }

    if (gs.material.domain == MaterialDomain_Transmissive || 
        (gs.material.domain == MaterialDomain_TransmissiveAlphaTested && alphaMask) || 
        gs.material.domain == MaterialDomain_TransmissiveAlphaBlended)
    {
        throughput *= ms.transmission;

        if (ms.hasMetalRoughParams)
            throughput *= (1.0 - ms.metalness) * ms.baseColor;

        if (gs.material.domain == MaterialDomain_TransmissiveAlphaBlended)
            throughput *= (1.0 - ms.opacity);

        return all(throughput == 0);
    }

    return false;
}

#if !USE_RAY_QUERY
struct RayAttributes 
{
    float2 uv;
};

[shader("miss")]
void Miss(inout RAB_RayPayload payload : SV_RayPayload)
{
}

[shader("closesthit")]
void ClosestHit(inout RAB_RayPayload payload : SV_RayPayload, in RayAttributes attrib : SV_IntersectionAttributes)
{
    payload.committedRayT = RayTCurrent();
    payload.instanceID = InstanceID();
    payload.geometryIndex = GeometryIndex();
    payload.primitiveIndex = PrimitiveIndex();
    payload.frontFace = HitKind() == HIT_KIND_TRIANGLE_FRONT_FACE;
    payload.barycentrics = attrib.uv;
}

[shader("anyhit")]
void AnyHit(inout RAB_RayPayload payload : SV_RayPayload, in RayAttributes attrib : SV_IntersectionAttributes)
{
    if (!ConsiderTransparentMaterial(InstanceID(), GeometryIndex(), PrimitiveIndex(), attrib.uv, payload.throughput))
        IgnoreHit();
}
#endif

#endif // RAB_RT_SHADERS_HLSLI