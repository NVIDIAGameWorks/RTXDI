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

#include "DDGIShaderConfig.h"

#define HLSL
#include <ddgi/include/ProbeCommon.hlsl>

#include <donut/shaders/bindless.h>

#include "../ShaderParameters.h"
#include "../GBufferHelpers.hlsli"
#include "../SceneGeometry.hlsli"

ConstantBuffer<ProbeDebugConstants> g_Const : register(b0);
RaytracingAccelerationStructure ProbeBVH : register(t0);
StructuredBuffer<DDGIVolumeDescGPUPacked> t_DDGIVolumes : register(t1);
StructuredBuffer<DDGIVolumeResourceIndices> t_DDGIVolumeResourceIndices : register(t2);
Texture2D<float> t_GBufferDepth : register(t3);
RWTexture2D<float4> u_CompositedColor : register(u0);
SamplerState s_ProbeSampler : register(s0);

[numthreads(16, 16, 1)]
void main(uint2 pixelPosition : SV_DispatchThreadID)
{
    float depth = t_GBufferDepth[pixelPosition];
    if (depth == 0)
        depth = 1000;

    float3 surfaceWorldPos = viewDepthToWorldPos(g_Const.view, pixelPosition, depth);

    RayDesc ray;
    ray.Origin = g_Const.view.cameraDirectionOrPosition.xyz;
    ray.Direction = normalize(surfaceWorldPos - ray.Origin);
    ray.TMin = 0;
    ray.TMax = length(surfaceWorldPos - ray.Origin);

    RayQuery<RAY_FLAG_NONE> rayQuery;

    rayQuery.TraceRayInline(ProbeBVH, RAY_FLAG_NONE, 0xFF, ray);

    while (rayQuery.Proceed())
    {
        if (rayQuery.CandidateType() == CANDIDATE_PROCEDURAL_PRIMITIVE)
        {
            float3 org = rayQuery.CandidateObjectRayOrigin();
            float3 dir = rayQuery.CandidateObjectRayDirection();

            // The primitive is a unit sphere (R = 1) centered at 0.
            // Compute the intersection of the ray and that sphere.

            // Quadratic equation: at^2 + 2bt + c = 0
            float a = dot(dir, dir);
            float b = dot(org, dir);
            float c = dot(org, org) - 1.0;
            float d = b * b - a * c;

            if (d >= 0)
            {
                float t = -(b + sqrt(d)) / a;
                if (ray.TMin <= t && t <= ray.TMax)
                {
                    rayQuery.CommitProceduralPrimitiveHit(t);
                }
            }
        }
    }

    if (rayQuery.CommittedStatus() != COMMITTED_PROCEDURAL_PRIMITIVE_HIT)
        return;

    uint probeIndex = rayQuery.CommittedInstanceID();

    DDGIVolumeResourceIndices resourceIndices = t_DDGIVolumeResourceIndices[g_Const.volumeIndex];
    DDGIVolumeDescGPU DDGIVolume = UnpackDDGIVolumeDescGPU(t_DDGIVolumes[g_Const.volumeIndex]);

    // Fill the volume's resource pointers
    Texture2D probeIrradianceTexture = t_BindlessTextures[resourceIndices.irradianceTextureSRV];
    Texture2D probeDataTexture = t_BindlessTextures[resourceIndices.probeDataTextureSRV];

    // Get the probe's grid coordinates
    int3 probeCoords = DDGIGetProbeCoords(probeIndex, DDGIVolume);

    // Get the probe's storage index
    probeIndex = DDGIGetScrollingProbeIndex(probeCoords, DDGIVolume);

    float3x4 probeToWorld = rayQuery.CommittedObjectToWorld3x4();
    float3 probeWorldPosition = float3(probeToWorld._m03, probeToWorld._m13, probeToWorld._m23);
    float3 hitPosition = ray.Origin + ray.Direction * rayQuery.CommittedRayT();
    float3 sampleDirection = normalize(hitPosition - probeWorldPosition);
    

    float2 coords = DDGIGetOctahedralCoordinates(sampleDirection);

    float2 uv = DDGIGetProbeUV(probeIndex, coords, DDGIVolume.probeNumIrradianceTexels, DDGIVolume);

    float3 color = probeIrradianceTexture.SampleLevel(s_ProbeSampler, uv, 0).rgb;
    {
        // Decode the tone curve
        float3 exponent = DDGIVolume.probeIrradianceEncodingGamma * 0.5f;
        color = pow(color, exponent);

        // Go back to linear irradiance
        color *= color;

        // Multiply by the area of the integration domain (2PI) to complete the irradiance estimate. Divide by PI to normalize for the display.
        color *= 2.f;
    }


    if (DDGIVolume.probeClassificationEnabled)
    {
        const float3 INACTIVE_COLOR = float3(0.f, 0.f, 1.f);      // Blue
        const float3 ACTIVE_COLOR = float3(0.f, 1.f, 0.f);        // Green

        // Adjust probe index for scroll offsets
        int storageProbeIndex = DDGIGetScrollingProbeIndex(probeCoords, DDGIVolume);

        uint2 probeStateTexcoord = DDGIGetProbeDataTexelCoords(storageProbeIndex, DDGIVolume);

        // Probe state border visualization
        float probeState = probeDataTexture[probeStateTexcoord].w;

        if (abs(dot(ray.Direction, sampleDirection)) < 0.35f)
        {
            if (probeState == RTXGI_DDGI_PROBE_STATE_ACTIVE)
            {
                color = ACTIVE_COLOR * 0.1;
            }
            else if (probeState == RTXGI_DDGI_PROBE_STATE_INACTIVE)
            {
                color = INACTIVE_COLOR * 0.1;
            }
        }
    }

    u_CompositedColor[pixelPosition] = float4(color, 0);
}
