/***************************************************************************
 # Copyright (c) 2021, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#ifndef RTXGI_HELPERS_HLSLI
#define RTXGI_HELPERS_HLSLI

#include "DDGIShaderConfig.h"

#define HLSL
#include <ddgi/Irradiance.hlsl>

float3 GetIrradianceFromDDGI(
    float3 surfacePosition,
    float3 surfaceNormal,
    float3 cameraPosition,
    uint numRtxgiVolumes,
    StructuredBuffer<DDGIVolumeDescGPUPacked> Volumes,
    StructuredBuffer<DDGIVolumeResourceIndices> VolumeResourceIndices,
    SamplerState Sampler)
{   
    float remainingBlendWeight = 1.0;
    float3 blendedIrradiance = 0;

    for (uint volumeIndex = 0; volumeIndex < numRtxgiVolumes; volumeIndex++)
    {
        DDGIVolumeResourceIndices resourceIndices = VolumeResourceIndices[volumeIndex];
        DDGIVolumeDescGPU DDGIVolume = UnpackDDGIVolumeDescGPU(Volumes[volumeIndex]);

        DDGIVolumeResources resources;
        resources.probeIrradiance = t_BindlessTextures[resourceIndices.irradianceTextureSRV];
        resources.probeDistance = t_BindlessTextures[resourceIndices.distanceTextureSRV];
        resources.probeData = t_BindlessTextures[resourceIndices.probeDataTextureSRV];
        resources.bilinearSampler = Sampler;

        float volumeBlendWeight = DDGIGetVolumeBlendWeight(surfacePosition, DDGIVolume);
        volumeBlendWeight = saturate(volumeBlendWeight);

        if (volumeBlendWeight > 0)
        {
            float3 viewDirection = normalize(surfacePosition - cameraPosition);
            float3 surfaceBias = DDGIGetSurfaceBias(surfaceNormal, viewDirection, DDGIVolume);

            float3 irradiance = DDGIGetVolumeIrradiance(
                surfacePosition,
                surfaceBias,
                surfaceNormal,
                DDGIVolume,
                resources);

            blendedIrradiance += irradiance * volumeBlendWeight * remainingBlendWeight;
            remainingBlendWeight *= 1.0 - volumeBlendWeight;

            if (remainingBlendWeight == 0.0)
                break;
        }
    }

    return blendedIrradiance;
}


#endif // RTXGI_HELPERS_HLSLI