/***************************************************************************
 # Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

#ifndef TRIANGLE_LIGHT_HLSLI
#define TRIANGLE_LIGHT_HLSLI

#include "HelperFunctions.hlsli"
#include <rtxdi/RtxdiHelpers.hlsli>

struct RAB_LightSample
{
    float3 position;
    float3 normal;
    float3 radiance;
    float solidAnglePdf;
};

struct TriangleLight
{
    float3 base;
    float3 edge1;
    float3 edge2;
    float3 radiance;
    float3 normal;
    float surfaceArea;

    // Interface methods

    RAB_LightSample calcSample(in const float2 random, in const float3 viewerPosition)
    {
        RAB_LightSample result;

        float3 bary = sampleTriangle(random);
        result.position = base + edge1 * bary.y + edge2 * bary.z;
        result.normal = normal;

        result.solidAnglePdf = calcSolidAnglePdf(viewerPosition, result.position, result.normal);

        result.radiance = radiance;

        return result;   
    }

    float calcSolidAnglePdf(in const float3 viewerPosition,
                            in const float3 lightSamplePosition,
                            in const float3 lightSampleNormal)
    {
        float3 L = lightSamplePosition - viewerPosition;
        float Ldist = length(L);
        L /= Ldist;

        const float areaPdf = 1.0 / surfaceArea;
        const float sampleCosTheta = saturate(dot(L, -lightSampleNormal));

        return pdfAtoW(areaPdf, Ldist, sampleCosTheta);
    }

    // Helper methods

    static TriangleLight Create(in const RAB_LightInfo lightInfo)
    {
        TriangleLight triLight;

        triLight.edge1 = octToNdirUnorm32(lightInfo.direction1) * f16tof32(lightInfo.scalars);
        triLight.edge2 = octToNdirUnorm32(lightInfo.direction2) * f16tof32(lightInfo.scalars >> 16);
        triLight.base = lightInfo.center - (triLight.edge1 + triLight.edge2) / 3.0;
        triLight.radiance = Unpack_R16G16B16A16_FLOAT(lightInfo.radiance).rgb;

        float3 lightNormal = cross(triLight.edge1, triLight.edge2);
        float lightNormalLength = length(lightNormal);

        if(lightNormalLength > 0.0)
        {
            triLight.surfaceArea = 0.5 * lightNormalLength;
            triLight.normal = lightNormal / lightNormalLength;
        }
        else
        {
           triLight.surfaceArea = 0.0;
           triLight.normal = 0.0; 
        }

        return triLight;
    }

    RAB_LightInfo Store()
    {
        RAB_LightInfo lightInfo = (RAB_LightInfo)0;

        lightInfo.radiance = Pack_R16G16B16A16_FLOAT(float4(radiance, 0));
        lightInfo.center = base + (edge1 + edge2) / 3.0;
        lightInfo.direction1 = ndirToOctUnorm32(normalize(edge1));
        lightInfo.direction2 = ndirToOctUnorm32(normalize(edge2));
        lightInfo.scalars = f32tof16(length(edge1)) | (f32tof16(length(edge2)) << 16);
        
        return lightInfo;
    }
};

#endif // TRIANGLE_LIGHT_HLSLI